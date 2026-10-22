/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * maintenance.c: Backups, restore, images for rollback and addons
 */

#include "maintenance.h"
#include "common.h"
#include "config.h"
#include "parsers.h"
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BACKUP_PREFIX "vdrsternelec-"
#define BACKUP_SUFFIX ".tar.gz"

// All absolute system paths are relative to PluginConfig.RootDir ("" = /),
// which is only changed for tests.
static std::string Root(const std::string &Path)
{
  return std::string(PluginConfig.RootDir) + Path;
}

// --- Commands ----------------------------------------------------------------------

int RunCommand(const std::vector<std::string> &Argv, std::string *Output, size_t MaxOutput)
{
  if (Argv.empty())
     return -1;
  int fds[2];
  if (pipe(fds) != 0)
     return -1;
  pid_t pid = fork();
  if (pid < 0) {
     close(fds[0]);
     close(fds[1]);
     return -1;
     }
  if (pid == 0) {
     // child: stdout/stderr into the pipe, close everything else
     dup2(fds[1], STDOUT_FILENO);
     dup2(fds[1], STDERR_FILENO);
     int devnull = open("/dev/null", O_RDONLY);
     if (devnull >= 0)
        dup2(devnull, STDIN_FILENO);
     for (int fd = getdtablesize() - 1; fd > STDERR_FILENO; fd--)
         close(fd);
     std::vector<char *> argv;
     for (size_t i = 0; i < Argv.size(); i++)
         argv.push_back(const_cast<char *>(Argv[i].c_str()));
     argv.push_back(NULL);
     execvp(argv[0], &argv[0]);
     _exit(127);
     }
  close(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR)) {
        if (n > 0 && out.size() < MaxOutput)
           out.append(buf, n);
        }
  close(fds[0]);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
  if (Output)
     *Output = out;
  if (!WIFEXITED(status))
     return -1;
  int rc = WEXITSTATUS(status);
  if (rc != 0)
     dsyslog_plg("command '%s' failed (%d): %s", Argv[0].c_str(), rc, Trim(out.substr(0, 500)).c_str());
  return rc;
}

static std::vector<std::string> SplitWords(const std::string &s)
{
  std::vector<std::string> v;
  size_t i = 0;
  while (i < s.size()) {
        while (i < s.size() && s[i] == ' ')
              i++;
        size_t j = s.find(' ', i);
        if (j == std::string::npos)
           j = s.size();
        if (j > i)
           v.push_back(s.substr(i, j - i));
        i = j;
        }
  return v;
}

// --- Backups -------------------------------------------------------------------------

static const char *ScopePaths(int Scope)
{
  return Scope == bsConfig ? "storage/.config" : "storage/.config/vdropt";
}

static std::string ShellQuote(const std::string &s)
{
  std::string r = "'";
  for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '\'')
         r += "'\\''";
      else
         r += s[i];
      }
  return r + "'";
}

static bool WriteTextFile(const std::string &FileName, const std::string &Content, int Mode = 0644)
{
  std::string tmp = FileName + ".new";
  FILE *f = fopen(tmp.c_str(), "w");
  if (!f)
     return false;
  bool ok = fputs(Content.c_str(), f) >= 0;
  ok = fflush(f) == 0 && fsync(fileno(f)) == 0 && ok;
  if (fclose(f) != 0)
     ok = false;
  chmod(tmp.c_str(), Mode);
  if (!ok || rename(tmp.c_str(), FileName.c_str()) != 0) {
     unlink(tmp.c_str());
     return false;
     }
  return true;
}

static std::map<std::string, std::string> ReadKeyValues(const std::string &FileName)
{
  std::map<std::string, std::string> kv;
  FILE *f = fopen(FileName.c_str(), "r");
  if (!f)
     return kv;
  char line[1024];
  while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (eq) {
           *eq = 0;
           kv[Trim(line)] = Trim(eq + 1);
           }
        }
  fclose(f);
  return kv;
}

bool CreateBackup(const std::string &Dir, int Scope, const std::string &Reason, const std::string &FromVersion, cBackupInfo &Info, std::string &Error)
{
  if (!MakeDirs(Dir.c_str(), true)) {
     Error = Dir + ": " + strerror(errno);
     return false;
     }
  char stamp[32];
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
  std::string file = Dir + "/" + BACKUP_PREFIX + stamp + BACKUP_SUFFIX;
  std::string part = file + ".part";
  std::string paths = ScopePaths(Scope);
  std::vector<std::string> argv;
  argv.push_back("tar");
  argv.push_back("-czf");
  argv.push_back(part);
  argv.push_back("-C");
  argv.push_back(Root("/"));
  std::vector<std::string> p = SplitWords(paths);
  argv.insert(argv.end(), p.begin(), p.end());
  isyslog_plg("creating backup %s (%s)", file.c_str(), paths.c_str());
  std::string out;
  if (RunCommand(argv, &out) != 0) {
     unlink(part.c_str());
     Error = "tar: " + Trim(out);
     esyslog_plg("backup failed: %s", Error.c_str());
     return false;
     }
  if (rename(part.c_str(), file.c_str()) != 0) {
     Error = file + ": " + strerror(errno);
     unlink(part.c_str());
     return false;
     }
  std::string info = "reason=" + Reason + "\nfromVersion=" + FromVersion + "\npaths=" + paths + "\ncreated=" + std::string(*cString::sprintf("%lld", (long long)now)) + "\n";
  WriteTextFile(file + ".info", info);
  struct stat st;
  Info = cBackupInfo();
  Info.file = file;
  Info.created = now;
  Info.reason = Reason;
  Info.fromVersion = FromVersion;
  Info.paths = paths;
  if (stat(file.c_str(), &st) == 0)
     Info.size = st.st_size;
  return true;
}

std::vector<cBackupInfo> ListBackups(const std::string &Dir)
{
  std::vector<cBackupInfo> v;
  DIR *d = opendir(Dir.c_str());
  if (!d)
     return v;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (name.compare(0, strlen(BACKUP_PREFIX), BACKUP_PREFIX) != 0 || name.size() <= strlen(BACKUP_SUFFIX) || name.compare(name.size() - strlen(BACKUP_SUFFIX), strlen(BACKUP_SUFFIX), BACKUP_SUFFIX) != 0)
           continue;
        cBackupInfo b;
        b.file = Dir + "/" + name;
        struct stat st;
        if (stat(b.file.c_str(), &st) != 0)
           continue;
        b.size = st.st_size;
        b.created = st.st_mtime;
        std::map<std::string, std::string> kv = ReadKeyValues(b.file + ".info");
        b.reason = kv["reason"];
        b.fromVersion = kv["fromVersion"];
        b.paths = kv["paths"].empty() ? ScopePaths(bsVdr) : kv["paths"];
        if (atoll(kv["created"].c_str()) > 0)
           b.created = atoll(kv["created"].c_str());
        v.push_back(b);
        }
  closedir(d);
  std::sort(v.begin(), v.end(), [](const cBackupInfo &a, const cBackupInfo &b) { return a.created != b.created ? a.created > b.created : a.file > b.file; });
  return v;
}

bool DeleteBackup(const cBackupInfo &Info)
{
  unlink((Info.file + ".info").c_str());
  return unlink(Info.file.c_str()) == 0;
}

void PruneBackups(const std::string &Dir, int Keep)
{
  std::vector<cBackupInfo> v = ListBackups(Dir);
  for (size_t i = Keep; i < v.size(); i++) {
      isyslog_plg("removing old backup %s", v[i].file.c_str());
      DeleteBackup(v[i]);
      }
}

bool StartRestore(const cBackupInfo &Backup, bool Reboot, const std::string &WorkDir, const std::string &KeepFile, std::string &Error)
{
  if (access(Backup.file.c_str(), R_OK) != 0) {
     Error = Backup.file + ": " + strerror(errno);
     return false;
     }
  std::vector<std::string> paths = SplitWords(Backup.paths);
  for (size_t i = 0; i < paths.size(); i++) {
      // only paths below storage/ are restored
      if (paths[i].compare(0, 8, "storage/") != 0 || paths[i].find("..") != std::string::npos) {
         Error = "invalid path in backup: " + paths[i];
         return false;
         }
      }
  if (!MakeDirs(WorkDir.c_str(), true)) {
     Error = WorkDir + ": " + strerror(errno);
     return false;
     }
  std::string root = Root("");
  std::string tmp = WorkDir + "/restore.tmp";
  std::string script = WorkDir + "/restore.sh";
  std::string s;
  s += "#!/bin/sh\n";
  s += "# generated by vdr-plugin-vdrsternelec: restore " + Backup.file + "\n";
  s += "exec > " + ShellQuote(WorkDir + "/restore.log") + " 2>&1\n";
  s += "set -x\n";
  s += std::string(PluginConfig.VdrStopCommand) + "\n";
  s += "sleep 1\n";
  s += "TMP=" + ShellQuote(tmp) + "\n";
  s += "rm -rf \"$TMP\" && mkdir -p \"$TMP\" || exit 1\n";
  if (!KeepFile.empty())
     s += "[ -f " + ShellQuote(KeepFile) + " ] && cp -p " + ShellQuote(KeepFile) + " \"$TMP/.keep\"\n";
  s += "if tar -xzf " + ShellQuote(Backup.file) + " -C \"$TMP\"; then\n";
  for (size_t i = 0; i < paths.size(); i++) {
      std::string dst = ShellQuote(root + "/" + paths[i]);
      std::string src = "\"$TMP/" + paths[i] + "\"";
      s += "  if [ -e " + src + " ]; then\n";
      s += "    rm -rf " + dst + ".vdrsternelec-old\n";
      s += "    [ -e " + dst + " ] && mv " + dst + " " + dst + ".vdrsternelec-old\n";
      s += "    if mv " + src + " " + dst + "; then rm -rf " + dst + ".vdrsternelec-old; else mv " + dst + ".vdrsternelec-old " + dst + "; fi\n";
      s += "  fi\n";
      }
  if (!KeepFile.empty())
     s += "  [ -f \"$TMP/.keep\" ] && mkdir -p \"$(dirname " + ShellQuote(KeepFile) + ")\" && cp -p \"$TMP/.keep\" " + ShellQuote(KeepFile) + "\n";
  s += "  echo restored\n";
  s += "else\n";
  s += "  echo restore failed\n";
  s += "fi\n";
  s += "rm -rf \"$TMP\"\n";
  s += "sync\n";
  if (Reboot)
     s += std::string(PluginConfig.RebootCommand) + "\n";
  else
     s += std::string(PluginConfig.VdrStartCommand) + "\n";
  if (!WriteTextFile(script, s, 0755)) {
     Error = script + ": " + strerror(errno);
     return false;
     }
  // run it outside of VDR's service, otherwise stopping VDR would stop it, too
  std::string cmd = *cString::sprintf(PluginConfig.DetachCommand, (long)time(NULL));
  std::vector<std::string> argv = SplitWords(cmd);
  argv.push_back("/bin/sh");
  argv.push_back(script);
  isyslog_plg("restoring %s (%s), running %s %s", Backup.file.c_str(), Backup.paths.c_str(), cmd.c_str(), script.c_str());
  std::string out;
  if (RunCommand(argv, &out) != 0) {
     Error = cmd + ": " + Trim(out);
     return false;
     }
  return true;
}

// --- Images for rollback ---------------------------------------------------------------

bool KeepImage(const std::string &File, const std::string &Sha256, const std::string &Dir, int Count, std::string &Error)
{
  if (Count <= 0)
     return true;
  if (!MakeDirs(Dir.c_str(), true)) {
     Error = Dir + ": " + strerror(errno);
     return false;
     }
  std::string name = File.substr(File.rfind('/') + 1);
  std::string dest = Dir + "/" + name;
  unlink(dest.c_str());
  if (link(File.c_str(), dest.c_str()) != 0) {
     // not on the same file system: copy
     std::vector<std::string> argv;
     argv.push_back("cp");
     argv.push_back("-p"); // keep the time (build date)
     argv.push_back(File);
     argv.push_back(dest + ".part");
     std::string out;
     if (RunCommand(argv, &out) != 0 || rename((dest + ".part").c_str(), dest.c_str()) != 0) {
        unlink((dest + ".part").c_str());
        Error = "can't keep " + name + ": " + Trim(out);
        return false;
        }
     }
  if (!Sha256.empty())
     WriteTextFile(dest + ".sha256", Sha256 + "  " + name + "\n");
  // keep the Count newest images
  std::vector<std::pair<time_t, std::string> > images;
  DIR *d = opendir(Dir.c_str());
  if (d) {
     struct dirent *e;
     while ((e = readdir(d)) != NULL) {
           std::string n = e->d_name;
           struct stat st;
           // linking/copying sets the change time, the modification time
           // (build date) is kept
           if (IsUpdateFile(n) && stat((Dir + "/" + n).c_str(), &st) == 0)
              images.push_back(std::make_pair(st.st_ctime, n));
           }
     closedir(d);
     }
  std::sort(images.begin(), images.end(), [](const std::pair<time_t, std::string> &a, const std::pair<time_t, std::string> &b) { return a.first > b.first; });
  for (size_t i = Count; i < images.size(); i++) {
      if (images[i].second == name)
         continue;
      isyslog_plg("removing old image %s", images[i].second.c_str());
      unlink((Dir + "/" + images[i].second).c_str());
      unlink((Dir + "/" + images[i].second + ".sha256").c_str());
      }
  isyslog_plg("kept %s for rollback", dest.c_str());
  return true;
}

// --- Addons ----------------------------------------------------------------------------

std::string AddonTargetDir(const std::string &Id)
{
  // as installed by VDR*ELEC's install.sh (-c and -l), others are Kodi addons
  if (Id == "addon.cefbrowser")
     return Root("/storage/browser");
  if (Id == "channellogos")
     return Root("/storage");
  return Root("/storage/.kodi/addons");
}

bool AddonInstalled(const std::string &Id)
{
  struct stat st;
  return stat((AddonTargetDir(Id) + "/" + Id).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool IsDriverAddon(const std::string &Id)
{
  return Id.compare(0, 7, "driver.") == 0;
}

static bool SafeEntry(const std::string &Name, const std::string &Id)
{
  std::string n = Name;
  while (n.compare(0, 2, "./") == 0)
        n = n.substr(2);
  if (n.empty() || n == Id || n == Id + "/")
     return true;
  if (n.compare(0, Id.size() + 1, Id + "/") != 0)
     return false;
  return ("/" + n + "/").find("/../") == std::string::npos;
}

static bool ZipEntries(const std::string &File, std::vector<std::string> &Names, std::string &Error)
{
  // read the central directory (no ZIP64 needed for addon sizes)
  FILE *f = fopen(File.c_str(), "rb");
  if (!f) {
     Error = File + ": " + strerror(errno);
     return false;
     }
  bool ok = false;
  fseeko(f, 0, SEEK_END);
  off_t size = ftello(f);
  off_t tail = size < 66000 ? size : 66000;
  std::vector<unsigned char> buf(tail);
  fseeko(f, size - tail, SEEK_SET);
  if (tail >= 22 && fread(&buf[0], 1, tail, f) == size_t(tail)) {
     for (off_t i = tail - 22; i >= 0; i--) {
         if (buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 0x05 && buf[i + 3] == 0x06) {
            unsigned entries = buf[i + 10] | (buf[i + 11] << 8);
            unsigned long cdSize = buf[i + 12] | (buf[i + 13] << 8) | (buf[i + 14] << 16) | ((unsigned long)buf[i + 15] << 24);
            unsigned long cdOffset = buf[i + 16] | (buf[i + 17] << 8) | (buf[i + 18] << 16) | ((unsigned long)buf[i + 19] << 24);
            if (cdOffset + cdSize > (unsigned long)size || cdSize > 64 * 1024 * 1024)
               break;
            std::vector<unsigned char> cd(cdSize);
            fseeko(f, cdOffset, SEEK_SET);
            if (cdSize && fread(&cd[0], 1, cdSize, f) != cdSize)
               break;
            size_t p = 0;
            unsigned n = 0;
            for (; n < entries && p + 46 <= cd.size(); n++) {
                if (cd[p] != 0x50 || cd[p + 1] != 0x4b || cd[p + 2] != 0x01 || cd[p + 3] != 0x02)
                   break;
                unsigned nameLen = cd[p + 28] | (cd[p + 29] << 8);
                unsigned extraLen = cd[p + 30] | (cd[p + 31] << 8);
                unsigned commentLen = cd[p + 32] | (cd[p + 33] << 8);
                if (p + 46 + nameLen > cd.size())
                   break;
                Names.push_back(std::string((const char *)&cd[p + 46], nameLen));
                p += 46 + nameLen + extraLen + commentLen;
                }
            ok = n == entries;
            break;
            }
         }
     }
  fclose(f);
  if (!ok && Error.empty())
     Error = File + ": not a valid zip file";
  return ok;
}

static bool IsTarGz(const std::string &File)
{
  return File.size() > 7 && File.compare(File.size() - 7, 7, ".tar.gz") == 0;
}

bool CheckArchive(const std::string &File, const std::string &Id, std::string &Error)
{
  std::vector<std::string> names;
  if (IsTarGz(File)) {
     std::vector<std::string> argv;
     argv.push_back("tar");
     argv.push_back("-tzf");
     argv.push_back(File);
     std::string out;
     if (RunCommand(argv, &out, 256 * 1024 * 1024) != 0) {
        Error = File + ": not a valid archive";
        return false;
        }
     size_t pos = 0;
     while (pos < out.size()) {
           size_t nl = out.find('\n', pos);
           std::string l = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
           pos = nl == std::string::npos ? out.size() : nl + 1;
           if (!l.empty())
              names.push_back(l);
           }
     }
  else if (!ZipEntries(File, names, Error))
     return false;
  if (names.empty()) {
     Error = File + ": empty archive";
     return false;
     }
  for (size_t i = 0; i < names.size(); i++) {
      if (!SafeEntry(names[i], Id)) {
         Error = File + ": unexpected entry " + names[i];
         return false;
         }
      }
  return true;
}

static bool ServiceActive(const std::string &Service)
{
  std::vector<std::string> argv;
  argv.push_back(PluginConfig.ServiceCommand);
  argv.push_back("is-active");
  argv.push_back("--quiet");
  argv.push_back(Service);
  return RunCommand(argv) == 0;
}

static void ServiceControl(const std::string &Action, const std::string &Service)
{
  std::vector<std::string> argv;
  argv.push_back(PluginConfig.ServiceCommand);
  argv.push_back(Action);
  argv.push_back(Service);
  RunCommand(argv);
}

bool InstallAddon(const std::string &File, const std::string &Id, std::string &Error)
{
  if (!CheckArchive(File, Id, Error))
     return false;
  std::string target = AddonTargetDir(Id);
  if (!MakeDirs(target.c_str(), true)) {
     Error = target + ": " + strerror(errno);
     return false;
     }
  // the browser's files can't be replaced while it runs
  bool restart = false;
  if (Id == "addon.cefbrowser" && ServiceActive("cefbrowser.service")) {
     ServiceControl("stop", "cefbrowser.service");
     restart = true;
     }
  std::vector<std::string> argv;
  if (IsTarGz(File)) {
     argv.push_back("tar");
     argv.push_back("-xzf");
     argv.push_back(File);
     argv.push_back("-C");
     argv.push_back(target);
     }
  else {
     argv.push_back("unzip");
     argv.push_back("-o");
     argv.push_back("-q");
     argv.push_back(File);
     argv.push_back("-d");
     argv.push_back(target);
     }
  isyslog_plg("installing addon %s from %s into %s", Id.c_str(), File.c_str(), target.c_str());
  std::string out;
  bool ok = RunCommand(argv, &out) == 0;
  if (!ok)
     Error = argv[0] + ": " + Trim(out);
  sync();
  if (restart)
     ServiceControl("start", "cefbrowser.service");
  return ok;
}

// --- System functions (install.sh) ---------------------------------------------------

static const char *SystemActionArgs[saActions][2] = {
  { "-b", "vdr"  },
  { "-b", "kodi" },
  { "-w", NULL   },
  { "-T", NULL   },
  { "-i", NULL   },
  { "-C", NULL   },
  };

std::string SystemActionCommand(int Action)
{
  if (Action < 0 || Action >= saActions)
     return "";
  std::string s = "install.sh ";
  s += SystemActionArgs[Action][0];
  if (SystemActionArgs[Action][1])
     s += std::string(" ") + SystemActionArgs[Action][1];
  return s;
}

bool InstallScriptAvailable(void)
{
  return access(PluginConfig.InstallScript, X_OK) == 0;
}

static bool Exists(const std::string &Path)
{
  struct stat st;
  return stat(Root(Path).c_str(), &st) == 0;
}

std::string BootTarget(void)
{
  // install.sh appends START_PRG=..., the last one wins when sourcing the file
  FILE *f = fopen(Root("/storage/.profile").c_str(), "r");
  if (!f)
     return "";
  std::string target;
  char buf[512];
  while (fgets(buf, sizeof(buf), f)) {
        std::string line = Trim(buf);
        if (line.compare(0, 10, "START_PRG=") != 0)
           continue;
        std::string v = line.substr(10);
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v[v.size() - 1] == v[0])
           v = v.substr(1, v.size() - 2);
        target = v;
        }
  fclose(f);
  return target;
}

bool CefbrowserInstalled(void)
{
  return Exists("/storage/browser/addon.cefbrowser/bin/cefbrowser");
}

bool SystemActionDone(int Action)
{
  switch (Action) {
    case saBootVdr:      return BootTarget() == "vdr";
    case saBootKodi:     return BootTarget() == "kodi";
    case saWeb:          return Exists("/storage/.config/system.d/cefbrowser.service") && Exists("/storage/.config/system.d/remotetranscode.service");
    case saTriggerhappy: return Exists("/storage/.config/system.d/triggerhappy.service");
    case saSamples:      return Exists("/storage/.config/vdropt-sample");
    default: break;
    }
  return false;
}

std::string SystemActionLog(const std::string &WorkDir)
{
  std::string s;
  FILE *f = fopen((WorkDir + "/install.log").c_str(), "r");
  if (!f)
     return s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && s.size() < 65536)
        s.append(buf, n);
  fclose(f);
  return s;
}

int RunSystemAction(int Action, const std::string &WorkDir, std::string &Output, std::string &Error)
{
  Output.clear();
  if (Action < 0 || Action >= saActions) {
     Error = "invalid action";
     return -1;
     }
  if (!InstallScriptAvailable()) {
     Error = std::string(PluginConfig.InstallScript) + ": " + strerror(errno);
     return -1;
     }
  if (!MakeDirs(WorkDir.c_str(), true)) {
     Error = WorkDir + ": " + strerror(errno);
     return -1;
     }
  std::string log = WorkDir + "/install.log";
  std::string rcFile = WorkDir + "/install.rc";
  std::string script = WorkDir + "/install-action.sh";
  unlink(rcFile.c_str());
  // the exit code is passed through a file, so it doesn't depend on how the
  // unit is run
  std::string s = "#!/bin/sh\n";
  s += "# generated by vdr-plugin-vdrsternelec: " + SystemActionCommand(Action) + "\n";
  s += ShellQuote(PluginConfig.InstallScript) + " " + SystemActionArgs[Action][0];
  if (SystemActionArgs[Action][1])
     s += std::string(" ") + SystemActionArgs[Action][1];
  s += " > " + ShellQuote(log) + " 2>&1\n";
  s += "echo $? > " + ShellQuote(rcFile) + "\n";
  if (!WriteTextFile(script, s, 0755)) {
     Error = script + ": " + strerror(errno);
     return -1;
     }
  std::vector<std::string> argv;
  if (*PluginConfig.RunScriptCommand)
     argv = SplitWords(*cString::sprintf(PluginConfig.RunScriptCommand, (long)time(NULL)));
  argv.push_back("/bin/sh");
  argv.push_back(script);
  isyslog_plg("running %s", SystemActionCommand(Action).c_str());
  std::string out;
  int rc = RunCommand(argv, &out);
  Output = SystemActionLog(WorkDir);
  FILE *f = fopen(rcFile.c_str(), "r");
  int scriptRc = -1;
  if (f) {
     if (fscanf(f, "%d", &scriptRc) != 1)
        scriptRc = -1;
     fclose(f);
     }
  if (scriptRc < 0) {
     Error = argv[0] + ": " + (rc ? Trim(out) : std::string("no result"));
     if (Error.size() > 300)
        Error = Error.substr(0, 300);
     return -1;
     }
  isyslog_plg("%s finished with exit code %d", SystemActionCommand(Action).c_str(), scriptRc);
  return scriptRc;
}
