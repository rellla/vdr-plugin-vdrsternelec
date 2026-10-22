/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * vdrsternelec.c: Plugin main file
 */

#include <getopt.h>
#include <vdr/menu.h>
#include <vdr/osd.h>
#include <vdr/plugin.h>
#include <vdr/recording.h>
#include <vdr/remote.h>
#include <vdr/timers.h>
#include "common.h"
#include "config.h"
#include "http.h"
#include "menu.h"
#include "updater.h"

#if defined(APIVERSNUM) && APIVERSNUM < 20400
#error "VDR-2.4.0 API version or greater is required!"
#endif

static const char *VERSION        = "0.5.0";
static const char *DESCRIPTION    = trNOOP("Update VDR*ELEC from release servers");
static const char *MAINMENUENTRY  = trNOOP("VDR*ELEC update");

#define IDLE_CHECK_INTERVAL 60 // seconds
#define TIMER_MARGIN        (15 * 60)

class cPluginVdrsternelec : public cPlugin {
private:
  const char *osRelease;
  time_t startTime;
  time_t lastIdleCheck;
  time_t lastRecordingCheck;
  bool SystemIdle(void);
public:
  cPluginVdrsternelec(void);
  virtual ~cPluginVdrsternelec() override;
  virtual const char *Version(void) override { return VERSION; }
  virtual const char *Description(void) override { return tr(DESCRIPTION); }
  virtual const char *CommandLineHelp(void) override;
  virtual bool ProcessArgs(int argc, char *argv[]) override;
  virtual bool Initialize(void) override;
  virtual bool Start(void) override;
  virtual void Stop(void) override;
  virtual void MainThreadHook(void) override;
  virtual cString Active(void) override;
  virtual const char *MainMenuEntry(void) override { return PluginConfig.HideMainMenuEntry ? NULL : tr(MAINMENUENTRY); }
  virtual cOsdObject *MainMenuAction(void) override;
  virtual cMenuSetupPage *SetupMenu(void) override;
  virtual bool SetupParse(const char *Name, const char *Value) override;
  virtual const char **SVDRPHelpPages(void) override;
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode) override;
  };

cPluginVdrsternelec::cPluginVdrsternelec(void)
{
  osRelease = DEFAULT_OS_RELEASE;
  startTime = 0;
  lastIdleCheck = 0;
  lastRecordingCheck = 0;
}

cPluginVdrsternelec::~cPluginVdrsternelec()
{
}

const char *cPluginVdrsternelec::CommandLineHelp(void)
{
  return "  -o FILE,  --os-release=FILE  read the system information from FILE\n"
         "                               (default: " DEFAULT_OS_RELEASE ")\n";
}

bool cPluginVdrsternelec::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
    { "os-release", required_argument, NULL, 'o' },
    { NULL,         no_argument,       NULL,  0  }
    };
  int c;
  while ((c = getopt_long(argc, argv, "o:", long_options, NULL)) != -1) {
        switch (c) {
          case 'o': osRelease = optarg;
                    break;
          default:  return false;
          }
        }
  return true;
}

bool cPluginVdrsternelec::Initialize(void)
{
  HttpGlobalInit();
  return true;
}

bool cPluginVdrsternelec::Start(void)
{
  startTime = time(NULL);
  cString servers = AddDirectory(ConfigDirectory(Name()), "servers.conf");
  ServerList.Load(servers);
  cString state = AddDirectory(ConfigDirectory(Name()), "state");
  Updater.Init(osRelease, state);
  Updater.Start();
  return true;
}

void cPluginVdrsternelec::Stop(void)
{
  Updater.Stop();
  HttpGlobalCleanup();
}

cString cPluginVdrsternelec::Active(void)
{
  cUpdater::eState s = Updater.State();
  if (s == cUpdater::usDownloading || s == cUpdater::usVerifying)
     return tr("VDR*ELEC update download is running");
  return NULL;
}

bool cPluginVdrsternelec::SystemIdle(void)
{
  time_t now = time(NULL);
  time_t idle = PluginConfig.IdleMinutes * 60;
  if (now - startTime < idle)
     return false;
  if (now - cRemote::LastActivity() < idle)
     return false;
  if (cOsd::IsOpen())
     return false;
  if (cRecordControls::Active() || RecordingsHandler.Active())
     return false;
  {
    LOCK_TIMERS_READ;
    const cTimer *t = Timers->GetNextActiveTimer();
    if (t && t->StartTime() - now < TIMER_MARGIN)
       return false;
  }
  if (cPluginManager::Active(NULL))
     return false;
  return true;
}

void cPluginVdrsternelec::MainThreadHook(void)
{
  std::string name;
  if (Updater.GetNotified(name)) {
     strn0cpy(PluginConfig.LastNotified, name.c_str(), sizeof(PluginConfig.LastNotified));
     SetupStore("LastNotified", PluginConfig.LastNotified);
     }
  time_t now = time(NULL);
  if (now != lastRecordingCheck) {
     // for the download limit while recording
     lastRecordingCheck = now;
     Updater.SetRecording(cRecordControls::Active());
     }
  if (now - lastIdleCheck < IDLE_CHECK_INTERVAL)
     return;
  lastIdleCheck = now;
  if (PluginConfig.RebootMode == rmIdle && Updater.UpdatePending() && Updater.AutoRebootAllowed() && cUpdater::InWindow() && SystemIdle()) {
     std::string err;
     isyslog_plg("system is idle, rebooting to install the update");
     if (!Updater.Reboot(err))
        esyslog_plg("reboot failed: %s", err.c_str());
     }
}

cOsdObject *cPluginVdrsternelec::MainMenuAction(void)
{
  return new cMenuUpdateMain;
}

cMenuSetupPage *cPluginVdrsternelec::SetupMenu(void)
{
  return new cMenuSetupVdrsternelec;
}

bool cPluginVdrsternelec::SetupParse(const char *Name, const char *Value)
{
  return PluginConfig.SetupParse(Name, Value);
}

const char **cPluginVdrsternelec::SVDRPHelpPages(void)
{
  static const char *HelpPages[] = {
    "STAT\n"
    "    Show the installed version and the state of the updater.",
    "CHECK\n"
    "    Check all enabled servers for updates (runs in the background,\n"
    "    use STAT to see when it is finished).",
    "LIST [ NEW ]\n"
    "    List the releases found by the last check. With NEW, only releases\n"
    "    newer than the installed one are listed. Columns: number, mark\n"
    "    (* newer, = installed), server, channel, version, file name.",
    "INSTALL <number>\n"
    "    Download and verify the release with the given number (see LIST)\n"
    "    into the update directory. The update is installed on the next reboot.",
    "CANCEL\n"
    "    Cancel a running download.",
    "DISCARD\n"
    "    Remove a downloaded update that has not been installed yet.",
    "REBOOT\n"
    "    Reboot now to install a downloaded update (after the backup and the\n"
    "    installation of driver addons, if enabled).",
    "BACKUP\n"
    "    Create a backup of the configuration now.",
    "SERVERS\n"
    "    List the configured release servers.",
    NULL
    };
  return HelpPages;
}

cString cPluginVdrsternelec::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  if (!strcasecmp(Command, "STAT")) {
     cSysInfo info = Updater.SysInfo();
     time_t last = Updater.LastCheck();
     std::string pending = Updater.PendingFile();
     std::string message = Updater.Message();
     time_t t;
     bool hasTime;
     std::string built = InstalledBuildDate(info, t, hasTime) ? FormatBuildDate(t, hasTime) : "unknown";
     cReleaseInfo p = Updater.PendingRelease();
     std::string pendingBuilt = p.name.empty() ? "-" : ReleaseBuildDate(p, t, hasTime) ? FormatBuildDate(t, hasTime) : "unknown";
     std::string result = Updater.LastResult();
     return cString::sprintf("Installed: %s\nBuild date: %s\nArchitecture: %s\nState: %s\nMessage: %s\nPending: %s\nPending build date: %s\nLast check: %s\nLast update: %s",
                             info.version.empty() ? "unknown" : info.version.c_str(),
                             built.c_str(),
                             info.arch.empty() ? "unknown" : info.arch.c_str(),
                             Updater.StateText().c_str(),
                             message.empty() ? "-" : message.c_str(),
                             pending.empty() ? "-" : pending.c_str(),
                             pendingBuilt.c_str(),
                             last ? *DayDateTime(last) : "never",
                             result.empty() ? "-" : result.c_str());
     }
  if (!strcasecmp(Command, "CHECK")) {
     Updater.RequestCheck();
     return "Check started";
     }
  if (!strcasecmp(Command, "LIST")) {
     bool onlyNew = Option && !strcasecmp(skipspace(Option), "NEW");
     if (Option && *skipspace(Option) && !onlyNew) {
        ReplyCode = 501;
        return "Invalid option";
        }
     if (!Updater.LastCheck()) {
        ReplyCode = 550;
        return "No releases known - run CHECK first";
        }
     std::vector<cReleaseInfo> releases = Updater.Releases();
     std::string out;
     for (size_t i = 0; i < releases.size(); i++) {
         eCompareResult c = Updater.Compare(releases[i]);
         if (onlyNew && c != crNewer)
            continue;
         if (!out.empty())
            out += "\n";
         out += *cString::sprintf("%zu %s %s | %s | %s | %s", i + 1, c == crNewer ? "*" : c == crSame ? "=" : "-",
                                  releases[i].server.c_str(), releases[i].channel.c_str(), releases[i].label.c_str(), releases[i].name.c_str());
         }
     if (out.empty()) {
        ReplyCode = 550;
        return onlyNew ? "No newer releases found" : "No releases found";
        }
     return out.c_str();
     }
  if (!strcasecmp(Command, "INSTALL")) {
     int n = Option ? atoi(Option) : 0;
     std::vector<cReleaseInfo> releases = Updater.Releases();
     if (n < 1 || n > int(releases.size())) {
        ReplyCode = 501;
        return "Invalid release number (see LIST)";
        }
     std::string err;
     if (!Updater.RequestInstall(releases[n - 1], err)) {
        ReplyCode = 550;
        return err.c_str();
        }
     return cString::sprintf("Download of %s started", releases[n - 1].name.c_str());
     }
  if (!strcasecmp(Command, "CANCEL")) {
     cUpdater::eState s = Updater.State();
     if (s != cUpdater::usDownloading && s != cUpdater::usVerifying) {
        ReplyCode = 550;
        return "No download running";
        }
     Updater.CancelDownload();
     return "Download cancelled";
     }
  if (!strcasecmp(Command, "DISCARD")) {
     std::string err;
     if (!Updater.DiscardPending(err)) {
        ReplyCode = 550;
        return err.c_str();
        }
     return "Update discarded";
     }
  if (!strcasecmp(Command, "REBOOT")) {
     std::string err;
     if (!Updater.Reboot(err)) {
        ReplyCode = 550;
        return err.c_str();
        }
     return "Preparing reboot (backup, addons), then rebooting";
     }
  if (!strcasecmp(Command, "BACKUP")) {
     std::string err;
     if (!Updater.RequestBackup(err)) {
        ReplyCode = 550;
        return err.c_str();
        }
     return "Backup started";
     }
  if (!strcasecmp(Command, "SERVERS")) {
     std::vector<cServerEntry> servers = ServerList.Get();
     if (servers.empty()) {
        ReplyCode = 550;
        return "No servers configured";
        }
     std::string out;
     for (size_t i = 0; i < servers.size(); i++) {
         if (i)
            out += "\n";
         cServerEntry e = servers[i];
         e.url = RedactUrl(e.url); // don't show passwords
         out += e.ToLine();
         }
     return out.c_str();
     }
  return NULL;
}

VDRPLUGINCREATOR(cPluginVdrsternelec); // Don't touch this!
