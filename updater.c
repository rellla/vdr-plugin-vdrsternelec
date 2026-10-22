/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * updater.c: Background thread checking for and downloading updates
 */

#include "updater.h"
#include "common.h"
#include "filesource.h"
#include "http.h"
#include "json.h"
#include "maintenance.h"
#include "sha256.h"
#include <algorithm>
#include <dirent.h>
#include <map>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vdr/i18n.h>
#include <vdr/skins.h>

#define FIRST_CHECK_DELAY   300  // seconds after startup
#define CHECK_RETRY_DELAY  3600  // retry after a failed automatic check
#define SPACE_MARGIN       (16LL * 1024 * 1024)

cUpdater Updater;

// --- cDownloadProgress ---------------------------------------------------------

class cDownloadProgress : public cHttpProgress {
private:
  cUpdater *updater;
  bool throttling;
  cTimeMs window;
  long long windowBytes;
public:
  cDownloadProgress(cUpdater *Updater) : updater(Updater), throttling(false), windowBytes(0) {}
  virtual bool Progress(long long Now, long long Total)
  {
    {
      cMutexLock MutexLock(&updater->mutex);
      updater->dlNow = Now;
      if (Total > 0)
         updater->dlTotal = Total;
    }
    // limit the download rate while recording (shared network, e.g. SAT>IP)
    long long limit = PluginConfig.ThrottleKBps * 1024LL;
    bool throttle = limit > 0 && updater->recording;
    if (throttle != throttling || window.Elapsed() > 30000) {
       throttling = throttle;
       window.Set();
       windowBytes = Now;
       }
    if (throttling) {
       // wait until the average rate in this window is within the limit
       for (;;) {
           long long allowed = limit * (long long)window.Elapsed() / 1000;
           long long excess = (Now - windowBytes) - allowed;
           if (excess <= 0 || updater->abortDownload || !updater->Running() || !updater->recording)
              break;
           cCondWait::SleepMs(int(std::min(excess * 1000 / limit + 1, 200LL)));
           }
       }
    return !updater->abortDownload && updater->Running();
  }
  };

// --- Helpers -----------------------------------------------------------------

static cFileSource *SourceForRelease(const cReleaseInfo &Release, std::string &Error);

static bool ReadFile(const char *FileName, std::string &Content)
{
  FILE *f = fopen(FileName, "r");
  if (!f)
     return false;
  char buf[4096];
  size_t n;
  Content.clear();
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && Content.size() < 65536)
        Content.append(buf, n);
  fclose(f);
  return true;
}

static bool IsSafeFileName(const std::string &Name)
{
  return !Name.empty() && Name[0] != '.' && Name.find('/') == std::string::npos && Name.find('\\') == std::string::npos;
}

// --- cUpdater ------------------------------------------------------------------

cUpdater::cUpdater(void)
:cThread("vdrsternelec updater")
{
  state = usIdle;
  lastCheck = 0;
  nextAutoCheck = 0;
  checkRequested = false;
  installRequested = false;
  dlNow = 0;
  dlTotal = -1;
  abortDownload = false;
  notifiedChanged = false;
  autoRebootBlocked = false;
  rebootRequested = false;
  backupRequested = false;
  postInstallAddons = false;
  deferredDownloadSet = false;
  recording = false;
}

cUpdater::~cUpdater()
{
  Stop();
}

void cUpdater::Init(const char *OsRelease, const char *StateFile)
{
  stateFile = StateFile;
  std::string content;
  cSysInfo info;
  if (ReadFile(OsRelease, content)) {
     ParseOsRelease(content, info);
     struct stat st;
     if (stat(OsRelease, &st) == 0)
        info.buildTime = st.st_mtime;
     }
  else
     esyslog_plg("can't read %s", OsRelease);
  {
    cMutexLock MutexLock(&mutex);
    sysInfo = info;
    notifiedName = PluginConfig.LastNotified;
    nextAutoCheck = time(NULL) + FIRST_CHECK_DELAY;
  }
  isyslog_plg("system: %s %s, arch %s%s%s", info.distName.c_str(), info.version.c_str(), info.arch.empty() ? "unknown" : info.arch.c_str(),
              *PluginConfig.Arch ? ", using " : "", PluginConfig.Arch);
  FindPendingUpdate();
  LoadState();
}

// The state file records the update downloaded by the plugin together with
// the system version at that time. After a reboot with a changed system
// version we know which file is installed, which makes the comparison with
// the releases on the servers reliable even if file names carry no version.

static std::map<std::string, std::string> ReadState(const std::string &FileName)
{
  std::map<std::string, std::string> kv;
  std::string content;
  if (ReadFile(FileName.c_str(), content)) {
     size_t pos = 0;
     while (pos < content.size()) {
           size_t nl = content.find('\n', pos);
           std::string line = content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
           pos = nl == std::string::npos ? content.size() : nl + 1;
           size_t eq = line.find('=');
           if (eq != std::string::npos)
              kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
           }
     }
  return kv;
}

static bool WriteState(const std::string &FileName, const std::map<std::string, std::string> &kv)
{
  std::string tmp = FileName + ".new";
  FILE *f = fopen(tmp.c_str(), "w");
  if (!f) {
     esyslog_plg("can't write %s: %m", tmp.c_str());
     return false;
     }
  for (std::map<std::string, std::string>::const_iterator it = kv.begin(); it != kv.end(); ++it)
      if (!it->second.empty())
         fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
  bool ok = fclose(f) == 0 && rename(tmp.c_str(), FileName.c_str()) == 0;
  if (!ok) {
     esyslog_plg("can't write %s: %m", FileName.c_str());
     unlink(tmp.c_str());
     }
  return ok;
}

void cUpdater::LoadState(void)
{
  if (stateFile.empty())
     return;
  std::map<std::string, std::string> kv = ReadState(stateFile);
  cMutexLock MutexLock(&mutex);
  if (!kv["result"].empty()) {
     time_t t = atol(kv["resultTime"].c_str());
     lastResult = *cString::sprintf(kv["result"] == "ok" ? tr("%s installed (%s)") : tr("%s NOT installed (%s)"), kv["resultLabel"].c_str(), *DateString(t));
     }
  if (kv["name"].empty())
     return;
  std::string pendingName = pendingFile.empty() ? "" : pendingFile.substr(pendingFile.rfind('/') + 1);
  if (!pendingName.empty() && kv["name"] == pendingName) {
     // restore what we knew about the downloaded release
     if (!kv["label"].empty())
        pendingRelease.label = kv["label"];
     pendingRelease.timestamp = atol(kv["timestamp"].c_str());
     pendingRelease.channel = kv["channel"];
     pendingRelease.server = kv["server"];
     }
  if (!pendingName.empty() && kv["rebootFor"] == pendingName) {
     esyslog_plg("system was rebooted for %s, but the update is still there - not rebooting automatically again", pendingName.c_str());
     autoRebootBlocked = true;
     startupMessage = tr("VDR*ELEC update was not processed during the reboot");
     }
  std::string version = sysInfo.version;
  std::string build = std::string(*cString::sprintf("%lld", (long long)sysInfo.buildTime));
  if (!kv["postVersion"].empty()) {
     // already applied earlier: valid as long as the system did not change
     if (kv["postVersion"] == version && kv["postBuild"] == build) {
        sysInfo.installedName = kv["name"];
        sysInfo.installedKey = kv["key"];
        }
     else {
        isyslog_plg("system changed since %s was installed, forgetting it", kv["name"].c_str());
        unlink(stateFile.c_str());
        }
     }
  else if (!kv["preVersion"].empty()) {
     bool pending = !pendingFile.empty() && pendingFile.substr(pendingFile.rfind('/') + 1) == kv["name"];
     if (pending)
        return; // not installed yet
     std::string label = kv["label"].empty() ? kv["name"] : kv["label"];
     if (kv["preVersion"] != version || kv["preBuild"] != build) {
        isyslog_plg("update %s has been installed", kv["name"].c_str());
        kv["postVersion"] = version;
        kv["postBuild"] = build;
        if (!kv["rebootFor"].empty()) {
           kv["result"] = "ok";
           kv["resultLabel"] = label;
           kv["resultTime"] = *cString::sprintf("%lld", (long long)time(NULL));
           lastResult = *cString::sprintf(tr("%s installed (%s)"), label.c_str(), *DateString(time(NULL)));
           startupMessage = *cString::sprintf(tr("VDR*ELEC update installed: %s"), label.c_str());
           // the other addons are installed now that the new version runs
           postInstallAddons = !kv["addons"].empty();
           }
        kv.erase("rebootFor");
        WriteState(stateFile, kv);
        sysInfo.installedName = kv["name"];
        sysInfo.installedKey = kv["key"];
        }
     else {
        // update file vanished without being installed: LibreELEC refused it
        std::map<std::string, std::string> r;
        if (!kv["rebootFor"].empty()) {
           esyslog_plg("update %s was NOT installed by the system", kv["name"].c_str());
           r["result"] = "failed";
           r["resultLabel"] = label;
           r["resultTime"] = *cString::sprintf("%lld", (long long)time(NULL));
           lastResult = *cString::sprintf(tr("%s NOT installed (%s)"), label.c_str(), *DateString(time(NULL)));
           startupMessage = *cString::sprintf(tr("VDR*ELEC update NOT installed: %s"), label.c_str());
           if (kv["driverAddons"] == "1")
              startupMessage += std::string(" - ") + tr("driver addons were already updated, see backups");
           }
        else if (!kv["result"].empty()) {
           r["result"] = kv["result"];
           r["resultLabel"] = kv["resultLabel"];
           r["resultTime"] = kv["resultTime"];
           }
        if (!kv["installedName"].empty()) {
           // the previously installed update is still installed
           r["name"] = kv["installedName"];
           r["key"] = kv["installedKey"];
           r["postVersion"] = version;
           r["postBuild"] = build;
           sysInfo.installedName = kv["installedName"];
           sysInfo.installedKey = kv["installedKey"];
           }
        // staged addons must not be installed
        std::string staging = std::string(PluginConfig.WorkDir) + "/addons";
        RunCommand(std::vector<std::string>{ "rm", "-rf", staging });
        if (r.empty())
           unlink(stateFile.c_str());
        else
           WriteState(stateFile, r);
        }
     }
  if (!sysInfo.installedName.empty())
     isyslog_plg("installed update: %s", sysInfo.installedName.c_str());
}

void cUpdater::SaveState(const cReleaseInfo &Release)
{
  if (stateFile.empty())
     return;
  std::map<std::string, std::string> old = ReadState(stateFile);
  std::map<std::string, std::string> kv;
  kv["result"] = old["result"];
  kv["resultLabel"] = old["resultLabel"];
  kv["resultTime"] = old["resultTime"];
  {
    cMutexLock MutexLock(&mutex);
    // what is installed now (restored if the new update is refused)
    kv["installedName"] = sysInfo.installedName;
    kv["installedKey"] = sysInfo.installedKey;
    kv["name"] = Release.name;
    kv["key"] = ReleaseKey(Release);
    kv["preVersion"] = sysInfo.version;
    kv["preBuild"] = std::string(*cString::sprintf("%lld", (long long)sysInfo.buildTime));
    kv["label"] = Release.label;
    kv["timestamp"] = std::string(*cString::sprintf("%lld", (long long)Release.timestamp));
    kv["channel"] = Release.channel;
    kv["server"] = Release.server;
  }
  WriteState(stateFile, kv);
}

void cUpdater::Stop(void)
{
  if (Running()) {
     abortDownload = true;
     Cancel(-1);
     {
       cMutexLock MutexLock(&mutex);
       condVar.Broadcast();
     }
     Cancel(10);
     }
}

std::string cUpdater::Arch(void)
{
  // mutex must be locked by the caller
  return *PluginConfig.Arch ? std::string(PluginConfig.Arch) : sysInfo.arch;
}

cSysInfo cUpdater::SysInfo(void)
{
  cMutexLock MutexLock(&mutex);
  cSysInfo info = sysInfo;
  if (*PluginConfig.Arch)
     info.arch = PluginConfig.Arch;
  return info;
}

void cUpdater::SetState(eState State, const std::string &Message)
{
  cMutexLock MutexLock(&mutex);
  state = State;
  message = Message;
}

void cUpdater::FindPendingUpdate(void)
{
  std::string dir = PluginConfig.UpdateDir;
  DIR *d = opendir(dir.c_str());
  if (!d)
     return;
  std::string found;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (IsSafeFileName(name) && IsUpdateFile(name)) {
           found = name;
           break;
           }
        }
  closedir(d);
  if (!found.empty()) {
     cMutexLock MutexLock(&mutex);
     pendingFile = dir + "/" + found;
     pendingRelease = cReleaseInfo();
     pendingRelease.name = found;
     pendingRelease.label = LabelFromName(found, Arch());
     state = usReady;
     message = found;
     isyslog_plg("update %s is waiting to be installed", pendingFile.c_str());
     }
}

void cUpdater::RequestCheck(void)
{
  cMutexLock MutexLock(&mutex);
  checkRequested = true;
  condVar.Broadcast();
  if (!Running())
     Start();
}

bool cUpdater::RequestInstall(const cReleaseInfo &Release, std::string &Error)
{
  cMutexLock MutexLock(&mutex);
  if (state == usDownloading || state == usVerifying || installRequested) {
     Error = tr("A download is already running");
     return false;
     }
  if (!IsSafeFileName(Release.name) || !IsUpdateFile(Release.name)) {
     Error = tr("Invalid file name");
     return false;
     }
  // file:// and smb:// are only allowed for releases from directories/shares
  if (IsFileSourceUrl(Release.url) != Release.local || (!Release.sha256Url.empty() && (IsFileSourceUrl(Release.sha256Url) != Release.local || Release.sha256Url.compare(0, 6, Release.url.substr(0, 6)) != 0))) {
     Error = tr("Invalid URL");
     return false;
     }
  if (!Allowed(Release)) {
     Error = tr("Change of the major version not allowed (see setup)");
     return false;
     }
  installRelease = Release;
  installRequested = true;
  abortDownload = false;
  condVar.Broadcast();
  if (!Running())
     Start();
  return true;
}

void cUpdater::CancelDownload(void)
{
  cMutexLock MutexLock(&mutex);
  installRequested = false;
  abortDownload = true;
}

cUpdater::eState cUpdater::State(void)
{
  cMutexLock MutexLock(&mutex);
  return state;
}

std::string cUpdater::Message(void)
{
  cMutexLock MutexLock(&mutex);
  return message;
}

int cUpdater::ProgressPercent(void)
{
  cMutexLock MutexLock(&mutex);
  if (state != usDownloading || dlTotal <= 0)
     return -1;
  return int(dlNow * 100 / dlTotal);
}

bool cUpdater::Busy(void)
{
  cMutexLock MutexLock(&mutex);
  return state == usChecking || state == usDownloading || state == usVerifying || state == usPreparing || state == usBackup || checkRequested || installRequested || rebootRequested || backupRequested;
}

std::string cUpdater::StateText(void)
{
  cMutexLock MutexLock(&mutex);
  switch (state) {
    case usIdle:     return tr("Idle");
    case usChecking: return tr("Checking for updates...");
    case usDownloading: {
         if (dlTotal > 0)
            return cString::sprintf(current.local ? tr("Copying %d%% (%s of %s)") : tr("Downloading %d%% (%s of %s)"), int(dlNow * 100 / dlTotal), FormatSize(dlNow).c_str(), FormatSize(dlTotal).c_str()).operator const char*();
         return cString::sprintf(current.local ? tr("Copying (%s)") : tr("Downloading (%s)"), FormatSize(dlNow).c_str()).operator const char*();
         }
    case usVerifying: return tr("Verifying checksum...");
    case usReady:     return tr("Update ready - reboot to install");
    case usError:     return tr("Error");
    case usPreparing: return tr("Preparing reboot...");
    case usBackup:    return tr("Creating backup...");
    }
  return "";
}

time_t cUpdater::LastCheck(void)
{
  cMutexLock MutexLock(&mutex);
  return lastCheck;
}

eCompareResult cUpdater::Compare(const cReleaseInfo &Release)
{
  cMutexLock MutexLock(&mutex);
  return CompareWithInstalled(Release, sysInfo);
}

int cUpdater::InstalledMajor(void)
{
  // mutex must be locked by the caller
  int m = MajorVersion(sysInfo.versionId);
  return m >= 0 ? m : MajorVersion(sysInfo.version);
}

bool cUpdater::Allowed(const cReleaseInfo &Release)
{
  // mutex must be locked by the caller
  if (PluginConfig.AllowMajorUpgrade)
     return true;
  int installed = InstalledMajor();
  int major = MajorVersion(Release.label);
  return installed < 0 || major < 0 || major == installed;
}

bool cUpdater::MajorAllowed(const cReleaseInfo &Release)
{
  cMutexLock MutexLock(&mutex);
  return Allowed(Release);
}

static bool Visible(const cReleaseInfo &r, eCompareResult c)
{
  if (r.prerelease && !PluginConfig.ShowPrerelease)
     return false;
  if (c == crOlder && !PluginConfig.ShowOlder)
     return false;
  return true;
}

std::vector<cServerResult> cUpdater::Results(void)
{
  cMutexLock MutexLock(&mutex);
  std::vector<cServerResult> r = results;
  for (size_t i = 0; i < r.size(); i++) {
      std::vector<cReleaseInfo> v;
      for (size_t j = 0; j < r[i].releases.size(); j++) {
          if (Visible(r[i].releases[j], CompareWithInstalled(r[i].releases[j], sysInfo)) && Allowed(r[i].releases[j]))
             v.push_back(r[i].releases[j]);
          }
      r[i].releases.swap(v);
      }
  return r;
}

std::vector<cReleaseInfo> cUpdater::Releases(void)
{
  std::vector<cServerResult> r = Results();
  std::vector<cReleaseInfo> v;
  for (size_t i = 0; i < r.size(); i++)
      v.insert(v.end(), r[i].releases.begin(), r[i].releases.end());
  return v;
}

bool cUpdater::UpdatePending(void)
{
  cMutexLock MutexLock(&mutex);
  return !pendingFile.empty() && state != usDownloading && state != usVerifying;
}

cReleaseInfo cUpdater::PendingRelease(void)
{
  cMutexLock MutexLock(&mutex);
  return pendingFile.empty() ? cReleaseInfo() : pendingRelease;
}

std::string cUpdater::PendingFile(void)
{
  cMutexLock MutexLock(&mutex);
  return pendingFile;
}

bool cUpdater::DiscardPending(std::string &Error)
{
  cMutexLock MutexLock(&mutex);
  if (pendingFile.empty()) {
     Error = tr("No update pending");
     return false;
     }
  if (state == usDownloading || state == usVerifying) {
     Error = tr("A download is already running");
     return false;
     }
  if (unlink(pendingFile.c_str()) != 0 && errno != ENOENT) {
     Error = cString::sprintf("%s: %s", pendingFile.c_str(), strerror(errno)).operator const char*();
     return false;
     }
  unlink((pendingFile + ".sha256").c_str());
  isyslog_plg("discarded pending update %s", pendingFile.c_str());
  pendingFile.clear();
  state = usIdle;
  message.clear();
  return true;
}

bool cUpdater::Reboot(std::string &Error)
{
  std::string pending = PendingFile();
  if (pending.empty()) {
     Error = tr("No update pending");
     return false;
     }
  if (access(pending.c_str(), R_OK) != 0) {
     Error = tr("Update file vanished");
     cMutexLock MutexLock(&mutex);
     pendingFile.clear();
     state = usIdle;
     return false;
     }
  cMutexLock MutexLock(&mutex);
  if (state == usDownloading || state == usVerifying || state == usPreparing || state == usBackup) {
     Error = tr("Please wait until the current action is finished");
     return false;
     }
  autoRebootBlocked = true; // one automatic attempt per update and boot
  rebootRequested = true;
  condVar.Broadcast();
  if (!Running())
     Start();
  return true;
}

bool cUpdater::DoBackup(const std::string &Reason, std::string &Error)
{
  std::string version;
  {
    cMutexLock MutexLock(&mutex);
    version = sysInfo.version;
  }
  cBackupInfo info;
  if (!CreateBackup(PluginConfig.BackupDir, PluginConfig.BackupScope, Reason, version, info, Error))
     return false;
  PruneBackups(PluginConfig.BackupDir, PluginConfig.BackupKeep);
  return true;
}

bool cUpdater::RequestBackup(std::string &Error)
{
  cMutexLock MutexLock(&mutex);
  if (state == usDownloading || state == usVerifying || state == usPreparing || state == usBackup || backupRequested) {
     Error = tr("Please wait until the current action is finished");
     return false;
     }
  backupRequested = true;
  condVar.Broadcast();
  if (!Running())
     Start();
  return true;
}

bool cUpdater::PrepareReboot(void)
{
  std::string pending = PendingFile();
  std::string name = pending.substr(pending.rfind('/') + 1);
  eState oldState = State();
  SetState(usPreparing, name);
  if (PluginConfig.BackupBeforeUpdate) {
     std::string err;
     cReleaseInfo p = PendingRelease();
     if (!DoBackup(p.label.empty() ? name : p.label, err)) {
        esyslog_plg("backup failed, not rebooting: %s", err.c_str());
        SetState(usError, std::string(tr("Backup failed")) + ": " + err);
        return false;
        }
     }
  // kernel driver addons have to match the new kernel
  InstallStagedAddons(true);
  isyslog_plg("rebooting to install %s (%s)", pending.c_str(), PluginConfig.RebootCommand);
  if (!stateFile.empty()) {
     std::map<std::string, std::string> kv = ReadState(stateFile);
     kv["rebootFor"] = name;
     if (kv["name"].empty())
        kv["name"] = name;
     WriteState(stateFile, kv);
     }
  sync();
  if (SystemExec(PluginConfig.RebootCommand, true) < 0) {
     esyslog_plg("reboot command failed");
     SetState(usError, tr("Reboot command failed"));
     return false;
     }
  SetState(oldState == usPreparing ? usReady : oldState, name);
  return true;
}

void cUpdater::DownloadAddons(const cReleaseInfo &Release)
{
  std::string staging = std::string(PluginConfig.WorkDir) + "/addons";
  RunCommand(std::vector<std::string>{ "rm", "-rf", staging });
  if (!PluginConfig.UpdateAddons || Release.addons.empty())
     return;
  std::string list;
  bool drivers = false;
  for (size_t i = 0; i < Release.addons.size() && Running(); i++) {
      const cAddonInfo &a = Release.addons[i];
      if (!AddonInstalled(a.id)) {
         dsyslog_plg("addon %s is not installed, skipped", a.id.c_str());
         continue;
         }
      if (!MakeDirs(staging.c_str(), true)) {
         esyslog_plg("can't create %s", staging.c_str());
         return;
         }
      // download like an update file (same checks), but into the staging area
      cReleaseInfo r;
      r.name = a.name;
      r.label = a.id + " " + a.version;
      r.url = a.url;
      r.sha256 = a.sha256;
      r.sha256Url = a.sha256Url;
      r.size = a.size;
      r.server = Release.server;
      r.local = Release.local;
      std::string err;
      cFileSource *src = Release.local ? SourceForRelease(r, err) : NULL;
      if (Release.local && !src) {
         esyslog_plg("addon %s: %s", a.id.c_str(), err.c_str());
         continue;
         }
      std::string file = staging + "/" + a.name;
      {
        cMutexLock MutexLock(&mutex);
        dlNow = 0;
        dlTotal = a.size;
        state = usDownloading;
        message = a.name;
      }
      bool ok = Fetch(r, src, file, err);
      delete src;
      unlink((file + ".sha256").c_str()); // verified, not needed any more
      if (!ok) {
         esyslog_plg("addon %s not downloaded: %s", a.id.c_str(), err.c_str());
         unlink(file.c_str());
         continue;
         }
      if (!CheckArchive(file, a.id, err)) {
         esyslog_plg("addon %s refused: %s", a.id.c_str(), err.c_str());
         unlink(file.c_str());
         continue;
         }
      isyslog_plg("addon %s %s downloaded", a.id.c_str(), a.version.c_str());
      list += (list.empty() ? "" : ",") + a.id + ":" + a.name;
      if (IsDriverAddon(a.id))
         drivers = true;
      }
  if (!stateFile.empty()) {
     std::map<std::string, std::string> kv = ReadState(stateFile);
     kv["addons"] = list;
     kv["driverAddons"] = drivers ? "1" : "";
     WriteState(stateFile, kv);
     }
}

void cUpdater::InstallStagedAddons(bool Drivers)
{
  if (stateFile.empty())
     return;
  std::map<std::string, std::string> kv = ReadState(stateFile);
  std::string list = kv["addons"];
  std::string rest;
  std::string staging = std::string(PluginConfig.WorkDir) + "/addons";
  size_t pos = 0;
  while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? list.size() : comma + 1;
        size_t colon = item.find(':');
        if (colon == std::string::npos)
           continue;
        std::string id = item.substr(0, colon);
        std::string name = item.substr(colon + 1);
        if (IsDriverAddon(id) != Drivers) {
           rest += (rest.empty() ? "" : ",") + item;
           continue;
           }
        std::string file = staging + "/" + name;
        if (name.find('/') != std::string::npos || access(file.c_str(), R_OK) != 0)
           continue;
        SetState(usPreparing, name);
        std::string err;
        if (InstallAddon(file, id, err))
           isyslog_plg("addon %s installed", id.c_str());
        else
           esyslog_plg("addon %s not installed: %s", id.c_str(), err.c_str());
        unlink(file.c_str());
        }
  kv["addons"] = rest;
  WriteState(stateFile, kv);
  if (rest.empty())
     rmdir(staging.c_str());
}

bool cUpdater::InWindow(void)
{
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  return InTimeWindow(PluginConfig.WindowStart, PluginConfig.WindowEnd, tm.tm_hour * 100 + tm.tm_min);
}

std::string cUpdater::LastResult(void)
{
  cMutexLock MutexLock(&mutex);
  return lastResult;
}

std::vector<cReleaseInfo> cUpdater::RollbackImages(void)
{
  std::string dir = std::string(PluginConfig.BackupDir) + "/images";
  std::vector<cReleaseInfo> v;
  std::vector<cLocalFile> files;
  DIR *d = opendir(dir.c_str());
  if (d) {
     struct dirent *e;
     while ((e = readdir(d)) != NULL) {
           struct stat st;
           if (e->d_name[0] != '.' && stat((dir + "/" + e->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
              cLocalFile f;
              f.name = e->d_name;
              f.size = st.st_size;
              f.mtime = st.st_mtime;
              files.push_back(f);
              }
           }
     closedir(d);
     }
  std::string arch;
  {
    cMutexLock MutexLock(&mutex);
    arch = Arch();
  }
  // all kept files are ours, so don't filter by device
  ParseLocalDirectory(files, PathToFileUrl(dir) + "/", arch, ".", tr("Rollback"), v);
  for (size_t i = 0; i < v.size(); i++)
      v[i].server = tr("Rollback");
  SortReleases(v);
  return v;
}

bool cUpdater::AutoRebootAllowed(void)
{
  cMutexLock MutexLock(&mutex);
  return !autoRebootBlocked;
}

bool cUpdater::GetNotified(std::string &Name)
{
  cMutexLock MutexLock(&mutex);
  if (!notifiedChanged)
     return false;
  notifiedChanged = false;
  Name = notifiedName;
  return true;
}

bool cUpdater::RemoveOtherUpdates(const std::string &Dir, const std::string &Keep)
{
  // LibreELEC only installs one update file, so make sure ours is the only one
  DIR *d = opendir(Dir.c_str());
  if (!d)
     return false;
  struct dirent *e;
  std::vector<std::string> remove;
  while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (name == Keep || name == Keep + ".part" || name == Keep + ".sha256")
           continue;
        bool isPart = name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0 && IsUpdateFile(name.substr(0, name.size() - 5));
        bool isSum = name.size() > 7 && name.compare(name.size() - 7, 7, ".sha256") == 0 && IsUpdateFile(name.substr(0, name.size() - 7));
        if (IsSafeFileName(name) && (IsUpdateFile(name) || isPart || isSum))
           remove.push_back(name);
        }
  closedir(d);
  for (size_t i = 0; i < remove.size(); i++) {
      std::string path = Dir + "/" + remove[i];
      isyslog_plg("removing old update file %s", path.c_str());
      unlink(path.c_str());
      }
  return true;
}

// --- Local directories and SMB shares ----------------------------------------------

static bool SourceLocation(const cServerEntry &Server, std::string &DirUrl, std::string &JsonUrl, cSmbUrl &Smb, std::string &Error)
{
  // DirUrl: URL of the directory (with trailing '/'), JsonUrl: URL of the
  // JSON file if the server entry points to one directly
  std::string url;
  JsonUrl.clear();
  if (Server.type == stDir) {
     std::string path = LocalPath(Server.url);
     if (path.empty()) {
        Error = tr("Invalid path (must start with /)");
        return false;
        }
     if (path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0) {
        JsonUrl = PathToFileUrl(path);
        DirUrl = UrlDirectory(JsonUrl);
        path = path.substr(0, path.rfind('/'));
        }
     else
        DirUrl = PathToFileUrl(path) + "/";
     std::string updateDir = LocalPath(PluginConfig.UpdateDir);
     char *a = realpath(path.empty() ? "/" : path.c_str(), NULL);
     char *b = realpath(updateDir.c_str(), NULL);
     bool same = a && b && strcmp(a, b) == 0;
     free(a);
     free(b);
     if (same) {
        Error = tr("The update directory can't be used as source");
        return false;
        }
     return true;
     }
  if (!ParseSmbUrl(Server.url, Smb)) {
     Error = tr("Invalid SMB address (use smb://server/share/path)");
     return false;
     }
  if (Smb.url.size() > 5 && Smb.url.compare(Smb.url.size() - 5, 5, ".json") == 0) {
     JsonUrl = Smb.url;
     DirUrl = UrlDirectory(JsonUrl);
     }
  else
     DirUrl = Smb.url + "/";
  return true;
}

static bool FetchFileSource(const cServerEntry &Server, const std::string &Arch, const std::string &Board, std::vector<cReleaseInfo> &Releases, std::string &Error, bool &FromJson)
{
  std::string dirUrl, jsonUrl;
  cSmbUrl smb;
  if (!SourceLocation(Server, dirUrl, jsonUrl, smb, Error))
     return false;
  cFileSource *src = NewFileSource(dirUrl, Server.type == stSmb ? &smb : NULL, Error);
  if (!src)
     return false;
  bool ok = false;
  std::vector<cLocalFile> files;
  if (jsonUrl.empty()) {
     if (src->List(dirUrl, files, Error)) {
        ok = true;
        for (size_t i = 0; i < files.size(); i++) {
            if (files[i].name == "releases.json") {
               jsonUrl = dirUrl + "releases.json";
               break;
               }
            }
        }
     }
  if (!jsonUrl.empty()) {
     std::string data;
     FromJson = true;
     dsyslog_plg("reading %s", RedactUrl(jsonUrl).c_str());
     ok = src->Read(jsonUrl, data, 32 * 1024 * 1024, Error) && ParseReleasesJson(data, jsonUrl, Arch, Server.channel, Board, Releases, Error, true);
     }
  else if (ok)
     ok = ParseLocalDirectory(files, dirUrl, Arch, Server.filter, Server.name, Releases);
  delete src;
  return ok;
}

static cFileSource *SourceForRelease(const cReleaseInfo &Release, std::string &Error)
{
  if (Release.url.compare(0, 6, "smb://") != 0)
     return NewFileSource(Release.url, NULL, Error);
  // credentials come from the server entry, and only for that server/share
  std::vector<cServerEntry> servers = ServerList.Get();
  for (size_t i = 0; i < servers.size(); i++) {
      cSmbUrl smb;
      if (servers[i].type != stSmb || servers[i].name != Release.server || !ParseSmbUrl(servers[i].url, smb))
         continue;
      std::string share = smb.url.substr(0, smb.url.find('/', smb.url.find('/', 6) + 1)) + "/";
      if (Release.url.compare(0, share.size(), share) != 0 || (!Release.sha256Url.empty() && Release.sha256Url.compare(0, share.size(), share) != 0)) {
         Error = tr("Invalid URL");
         return NULL;
         }
      return NewFileSource(Release.url, &smb, Error);
      }
  Error = tr("Server not found");
  return NULL;
}

void cUpdater::FetchServer(const cServerEntry &Server, cServerResult &Result)
{
  std::string arch, board;
  {
    cMutexLock MutexLock(&mutex);
    arch = Arch();
    board = PluginConfig.Board;
  }
  Result.server = Server;
  Result.releases.clear();
  Result.error.clear();
  if (arch.empty() && Server.filter.empty()) {
     Result.error = tr("Unknown architecture");
     return;
     }
  std::string data, err;
  bool ok = false;
  bool fromJson = Server.type == stJson;
  switch (Server.type) {
    case stJson: {
         std::string url = JsonUrl(Server.url);
         if (HttpGet(url, data, err))
            ok = ParseReleasesJson(data, url, arch, Server.channel, board, Result.releases, err);
         }
         break;
    case stGithub: {
         std::string url = GithubApiUrl(Server.url);
         if (url.empty()) {
            err = tr("Invalid GitHub repository (use owner/repo)");
            break;
            }
         if (HttpGet(url, data, err, 32 * 1024 * 1024, "application/vnd.github+json"))
            ok = ParseGithubReleases(data, arch, Server.channel, Server.filter, Result.releases, err);
         else if (!data.empty()) {
            // e.g. {"message": "API rate limit exceeded for ..."}
            cJsonValue v;
            std::string jerr;
            if (JsonParse(data, v, jerr) && !v.GetString("message").empty())
               err += ": " + v.GetString("message");
            }
         }
         break;
    case stDir:
    case stSmb:
         ok = FetchFileSource(Server, arch, board, Result.releases, err, fromJson);
         break;
    case stIndex: {
         if (HttpGet(Server.url, data, err))
            ok = ParseIndexHtml(data, Server.url, arch, Server.filter, Server.name, Result.releases);
         }
         break;
    default:
         err = "unknown server type";
    }
  if (!ok) {
     Result.error = err;
     Result.releases.clear();
     esyslog_plg("server '%s': %s", Server.name.c_str(), err.c_str());
     return;
     }
  // the "filter" also applies to releases.json entries
  if (fromJson && !Server.filter.empty()) {
     std::vector<cReleaseInfo> v;
     for (size_t i = 0; i < Result.releases.size(); i++)
         if (MatchesArch(Result.releases[i].name, arch, Server.filter))
            v.push_back(Result.releases[i]);
     Result.releases.swap(v);
     }
  for (size_t i = 0; i < Result.releases.size(); i++)
      Result.releases[i].server = Server.name;
  SortReleases(Result.releases);
  isyslog_plg("server '%s': %zu release(s) for %s", Server.name.c_str(), Result.releases.size(), arch.c_str());
}

void cUpdater::DoCheck(bool Automatic)
{
  SetState(usChecking);
  std::vector<cServerEntry> servers = ServerList.Get();
  std::vector<cServerResult> res;
  int errors = 0;
  int enabled = 0;
  for (size_t i = 0; i < servers.size() && Running(); i++) {
      if (!servers[i].enabled)
         continue;
      enabled++;
      cServerResult r;
      FetchServer(servers[i], r);
      if (!r.error.empty())
         errors++;
      res.push_back(r);
      }
  // find the newest release that is newer than the installed one
  cReleaseInfo best;
  bool found = false;
  {
    cMutexLock MutexLock(&mutex);
    results = res;
    lastCheck = time(NULL);
    for (size_t i = 0; i < res.size(); i++) {
        for (size_t j = 0; j < res[i].releases.size(); j++) {
            const cReleaseInfo &r = res[i].releases[j];
            if (r.prerelease && !PluginConfig.ShowPrerelease)
               continue;
            if (!Allowed(r))
               continue;
            if (CompareWithInstalled(r, sysInfo) != crNewer)
               continue;
            if (!found || ReleaseKey(r) > ReleaseKey(best)) {
               best = r;
               found = true;
               }
            break; // releases are sorted newest first
            }
        }
    if (!pendingFile.empty())
       state = usReady, message = pendingFile.substr(pendingFile.rfind('/') + 1);
    else if (enabled && errors == enabled)
       state = usError, message = res.empty() ? "" : res[0].error;
    else
       state = usIdle, message.clear();
  }
  if (enabled == 0)
     isyslog_plg("no servers enabled");
  if (!found) {
     dsyslog_plg("no newer release found");
     return;
     }
  isyslog_plg("newer release available: %s (%s)", best.name.c_str(), best.server.c_str());
  bool notify = false;
  {
    cMutexLock MutexLock(&mutex);
    if (Automatic && notifiedName != best.name) {
       notifiedName = best.name;
       notifiedChanged = true;
       notify = PluginConfig.Notify;
       }
  }
  if (notify)
     Skins.QueueMessage(mtInfo, cString::sprintf(tr("VDR*ELEC update available: %s"), best.label.c_str()));
  if (Automatic && PluginConfig.AutoDownload) {
     std::string pending = PendingFile();
     if (pending.empty() || pending.substr(pending.rfind('/') + 1) != best.name) {
        if (InWindow())
           DoInstall(best);
        else {
           isyslog_plg("download of %s deferred until the time window", best.name.c_str());
           cMutexLock MutexLock(&mutex);
           deferredDownload = best;
           deferredDownloadSet = true;
           }
        }
     }
}

bool cUpdater::DoInstall(const cReleaseInfo &Release)
{
  std::string dir = PluginConfig.UpdateDir;
  std::string finalName = dir + "/" + Release.name;
  std::string part = finalName + ".part";
  {
    cMutexLock MutexLock(&mutex);
    current = Release;
    dlNow = 0;
    dlTotal = Release.size;
    abortDownload = false;
    state = usDownloading;
    message = Release.name;
  }
  if (!MakeDirs(dir.c_str(), true)) {
     SetState(usError, cString::sprintf(tr("Can't create %s"), dir.c_str()).operator const char*());
     return false;
     }
  cFileSource *src = NULL;
  if (Release.local) {
     std::string err;
     src = SourceForRelease(Release, err);
     if (!src) {
        esyslog_plg("%s: %s", Release.name.c_str(), err.c_str());
        SetState(usError, err);
        return false;
        }
     }
  bool ok = DoInstall(Release, src, finalName, part);
  delete src;
  return ok;
}

bool cUpdater::Fetch(const cReleaseInfo &Release, cFileSource *Source, const std::string &FileName, std::string &Error)
{
  // download (or copy) Release into FileName and verify size and checksum
  std::string part = FileName + ".part";
  std::string dir = FileName.substr(0, FileName.rfind('/'));
  std::string sha = Release.sha256;
  if (sha.empty() && !Release.sha256Url.empty()) {
     std::string data, err;
     if (Source ? Source->Read(Release.sha256Url, data, 1024 * 1024, err) : HttpGet(Release.sha256Url, data, err, 64 * 1024))
        sha = ParseSha256File(data, Release.name);
     else
        esyslog_plg("can't get checksum from %s: %s", RedactUrl(Release.sha256Url).c_str(), err.c_str());
     }
  if (sha.empty()) {
     if (PluginConfig.RequireChecksum) {
        esyslog_plg("no checksum available for %s", Release.name.c_str());
        Error = tr("No checksum available");
        return false;
        }
     isyslog_plg("no checksum available for %s, installing without verification", Release.name.c_str());
     }
  // free space
  struct statvfs vfs;
  if (Release.size > 0 && statvfs(dir.c_str(), &vfs) == 0) {
     long long avail = (long long)vfs.f_bavail * vfs.f_frsize;
     // a pending update in the same directory will be replaced, so count it as free
     std::string pending = PendingFile();
     struct stat st;
     if (!pending.empty() && pending.compare(0, dir.size() + 1, dir + "/") == 0 && stat(pending.c_str(), &st) == 0)
        avail += st.st_size;
     if (avail < Release.size + SPACE_MARGIN) {
        esyslog_plg("not enough space in %s: %lld available, %lld needed", dir.c_str(), avail, Release.size + SPACE_MARGIN);
        Error = *cString::sprintf(tr("Not enough space in %s"), dir.c_str());
        return false;
        }
     }
  cDownloadProgress progress(this);
  std::string err;
  bool copied = false;
  unlink(part.c_str());
  if (Source && Release.url.compare(0, 8, "file:///") == 0 && link(LocalPath(Release.url).c_str(), part.c_str()) == 0) {
     // local file on the same file system (e.g. rollback): no need to copy
     isyslog_plg("linked %s to %s", LocalPath(Release.url).c_str(), part.c_str());
     copied = true;
     }
  else if (Source) {
     isyslog_plg("copying %s to %s", RedactUrl(Release.url).c_str(), part.c_str());
     copied = Source->Copy(Release.url, part, &progress, err);
     }
  else
     copied = HttpDownload(Release.url, part, &progress, err);
  if (!copied) {
     unlink(part.c_str());
     esyslog_plg("download of %s failed: %s", RedactUrl(Release.url).c_str(), err.c_str());
     Error = abortDownload ? std::string(tr("Download cancelled")) : err;
     return false;
     }
  struct stat st;
  if (stat(part.c_str(), &st) != 0 || (Release.size > 0 && st.st_size != Release.size)) {
     unlink(part.c_str());
     esyslog_plg("download of %s: size mismatch (%lld != %lld)", Release.name.c_str(), (long long)st.st_size, Release.size);
     Error = tr("Size mismatch");
     return false;
     }
  if (!sha.empty()) {
     eState old = State();
     SetState(usVerifying, Release.name);
     std::string actual = Sha256File(part.c_str(), &abortDownload);
     SetState(old, Release.name);
     if (actual != sha) {
        unlink(part.c_str());
        if (abortDownload) {
           Error = tr("Download cancelled");
           return false;
           }
        esyslog_plg("checksum mismatch for %s: expected %s, got %s", Release.name.c_str(), sha.c_str(), actual.c_str());
        Error = tr("Checksum mismatch");
        return false;
        }
     isyslog_plg("checksum of %s verified", Release.name.c_str());
     }
  if (rename(part.c_str(), FileName.c_str()) != 0) {
     esyslog_plg("can't rename %s: %m", part.c_str());
     unlink(part.c_str());
     Error = tr("Can't move update file");
     return false;
     }
  if (!sha.empty()) {
     FILE *f = fopen((FileName + ".sha256").c_str(), "w");
     if (f) {
        fprintf(f, "%s  %s\n", sha.c_str(), Release.name.c_str());
        fclose(f);
        }
     }
  return true;
}

bool cUpdater::DoInstall(const cReleaseInfo &Release, cFileSource *Source, const std::string &FinalName, const std::string &Part)
{
  std::string dir = PluginConfig.UpdateDir;
  std::string err;
  if (!Fetch(Release, Source, FinalName, err)) {
     SetState(abortDownload ? usIdle : usError, err);
     if (abortDownload)
        FindPendingUpdate();
     return false;
     }
  // only one update file may be in the update directory
  RemoveOtherUpdates(dir, Release.name);
  SaveState(Release);
  if (PluginConfig.KeepImages > 0) {
     std::string sha;
     std::string content;
     if (ReadFile((FinalName + ".sha256").c_str(), content))
        sha = ParseSha256File(content, Release.name);
     if (!KeepImage(FinalName, sha, std::string(PluginConfig.BackupDir) + "/images", PluginConfig.KeepImages + 1, err))
        esyslog_plg("%s", err.c_str());
     }
  DownloadAddons(Release);
  sync();
  {
    cMutexLock MutexLock(&mutex);
    pendingFile = FinalName;
    pendingRelease = Release;
    autoRebootBlocked = false;
    state = usReady;
    message = Release.name;
  }
  isyslog_plg("update %s downloaded to %s", Release.name.c_str(), FinalName.c_str());
  if (PluginConfig.RebootMode == rmIdle)
     Skins.QueueMessage(mtInfo, tr("VDR*ELEC update downloaded - will be installed when idle"));
  else
     Skins.QueueMessage(mtInfo, tr("VDR*ELEC update downloaded - reboot to install"));
  return true;
}

void cUpdater::Action(void)
{
  isyslog_plg("updater thread started");
  bool startup = true;
  cTimeMs started;
  while (Running()) {
        bool check = false;
        bool install = false;
        bool automatic = false;
        bool reboot = false;
        bool backup = false;
        bool addons = false;
        bool deferred = false;
        std::string message;
        cReleaseInfo release;
        {
          cMutexLock MutexLock(&mutex);
          if (startup && started.Elapsed() > 15000) {
             // let VDR come up before showing what happened during the reboot
             startup = false;
             message = startupMessage;
             addons = postInstallAddons;
             postInstallAddons = false;
             }
          bool idle = state != usDownloading && state != usVerifying && state != usPreparing && state != usBackup;
          if (deferredDownloadSet && idle && InWindow()) {
             deferredDownloadSet = false;
             deferred = true;
             release = deferredDownload;
             }
          if (!checkRequested && !installRequested && !rebootRequested && !backupRequested && !deferred && message.empty() && !addons) {
             time_t now = time(NULL);
             if (PluginConfig.AutoCheck && nextAutoCheck && now >= nextAutoCheck && idle)
                automatic = true;
             else
                condVar.TimedWait(mutex, 1000);
             }
          if (!Running())
             break;
          if (rebootRequested) {
             rebootRequested = false;
             reboot = true;
             }
          else if (backupRequested) {
             backupRequested = false;
             backup = true;
             }
          else if (installRequested) {
             installRequested = false;
             install = true;
             release = installRelease;
             }
          else if (deferred)
             install = true;
          else if (checkRequested || automatic) {
             checkRequested = false;
             check = true;
             }
        }
        if (!message.empty())
           Skins.QueueMessage(mtInfo, message.c_str());
        if (addons) {
           eState old = State();
           std::string msg = Message();
           InstallStagedAddons(false);
           SetState(old, msg);
           }
        if (reboot)
           PrepareReboot();
        else if (backup) {
           eState old = State();
           std::string msg = Message();
           SetState(usBackup);
           std::string err;
           if (DoBackup(tr("manual"), err)) {
              SetState(old, msg);
              Skins.QueueMessage(mtInfo, tr("Backup created"));
              }
           else
              SetState(usError, std::string(tr("Backup failed")) + ": " + err);
           }
        else if (install)
           DoInstall(release);
        else if (check) {
           DoCheck(automatic);
           cMutexLock MutexLock(&mutex);
           if (PluginConfig.AutoCheck) {
              bool failed = state == usError;
              nextAutoCheck = time(NULL) + (failed ? CHECK_RETRY_DELAY : PluginConfig.CheckInterval * 3600);
              }
           }
        }
  isyslog_plg("updater thread stopped");
}
