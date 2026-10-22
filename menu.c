/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * menu.c: OSD menus
 */

#include "menu.h"
#include "common.h"
#include "filesource.h"
#include "updater.h"
#include <vdr/interface.h>
#include <vdr/menu.h>
#include <vdr/recording.h>
#include <vdr/skins.h>

// characters allowed in URLs, channel names and regular expressions
static const char *UrlChars = " abcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%^|\\{}<>\"";

static std::string FormatDate(const cReleaseInfo &r)
{
  time_t t;
  bool hasTime;
  return ReleaseBuildDate(r, t, hasTime) ? FormatBuildDate(t, hasTime) : std::string();
}

static const char *CompareMark(eCompareResult c)
{
  switch (c) {
    case crNewer: return "*";
    case crSame:  return "=";
    default:      return " ";
    }
}

// --- cServerItem -------------------------------------------------------------------

class cServerItem : public cOsdItem {
private:
  std::string server;
public:
  cServerItem(const cServerResult &Result);
  const std::string &Server(void) const { return server; }
  };

cServerItem::cServerItem(const cServerResult &Result)
{
  server = Result.server.name;
  std::string status;
  if (!Result.error.empty())
     status = std::string(tr("Error")) + ": " + Result.error;
  else {
     int newer = 0;
     for (size_t i = 0; i < Result.releases.size(); i++)
         if (Updater.Compare(Result.releases[i]) == crNewer)
            newer++;
     if (newer)
        status = *cString::sprintf(tr("releases: %d, newer: %d"), int(Result.releases.size()), newer);
     else
        status = *cString::sprintf(tr("releases: %d"), int(Result.releases.size()));
     }
  SetText(cString::sprintf("%s\t%s", server.c_str(), status.c_str()));
}

// --- cMenuUpdateMain -----------------------------------------------------------------

cMenuUpdateMain::cMenuUpdateMain(void)
:cOsdMenu(tr("VDR*ELEC update"), 22)
{
  lastCheck = 0;
  lastRefresh = 0;
  Set();
  // start on "Check for updates"
  for (cOsdItem *item = First(); item; item = Next(item)) {
      if (item->Selectable()) {
         SetCurrent(item);
         break;
         }
      }
  SetHelp(tr("Button$Check"), NULL, NULL, tr("Button$Servers"));
  Display();
}

void cMenuUpdateMain::AddInfo(const char *Label, const std::string &Value)
{
  Add(new cOsdItem(cString::sprintf("%s:\t%s", Label, Value.c_str()), osUnknown, false));
}

void cMenuUpdateMain::Set(void)
{
  int current = Current();
  Clear();
  cSysInfo info = Updater.SysInfo();
  cUpdater::eState state = Updater.State();
  lastStateText = Updater.StateText();
  lastCheck = Updater.LastCheck();
  std::string message = Updater.Message();
  time_t t;
  bool hasTime;
  AddInfo(tr("Installed"), info.version.empty() ? tr("unknown") : info.version);
  AddInfo(tr("Build date"), InstalledBuildDate(info, t, hasTime) ? FormatBuildDate(t, hasTime) : std::string(tr("unknown")));
  AddInfo(tr("Device"), info.arch.empty() ? tr("unknown") : info.arch);
  AddInfo(tr("Status"), lastStateText);
  cReleaseInfo pending = Updater.PendingRelease();
  if (state == cUpdater::usReady && !pending.name.empty()) {
     AddInfo(tr("Update"), pending.label.empty() ? pending.name : pending.label);
     AddInfo(tr("Build date"), ReleaseBuildDate(pending, t, hasTime) ? FormatBuildDate(t, hasTime) : std::string(tr("unknown")));
     AddInfo(tr("File"), pending.name);
     }
  else if (!message.empty())
     AddInfo(state == cUpdater::usError ? tr("Error") : tr("File"), message);
  AddInfo(tr("Last check"), lastCheck ? std::string(*DayDateTime(lastCheck)) : std::string(tr("never")));
  std::string result = Updater.LastResult();
  if (!result.empty())
     AddInfo(tr("Last update"), result);
  Add(new cOsdItem("", osUnknown, false));
  Add(new cOsdItem(tr("Check for updates"), osUser1));
  if (state == cUpdater::usDownloading || state == cUpdater::usVerifying)
     Add(new cOsdItem(tr("Cancel download"), osUser4));
  if (Updater.UpdatePending()) {
     Add(new cOsdItem(tr("Reboot now and install update"), osUser2));
     Add(new cOsdItem(tr("Discard downloaded update"), osUser3));
     }
  Add(new cOsdItem(tr("Release servers..."), osUser5));
  Add(new cOsdItem(tr("Backups & rollback..."), osUser6));
  Add(new cOsdItem(tr("System functions..."), osUser7));
  std::vector<cServerResult> results = Updater.Results();
  if (!results.empty()) {
     Add(new cOsdItem(cString::sprintf("--- %s ---", tr("Available releases")), osUnknown, false));
     for (size_t i = 0; i < results.size(); i++)
         Add(new cServerItem(results[i]));
     }
  else if (lastCheck)
     Add(new cOsdItem(tr("No servers enabled"), osUnknown, false));
  if (current >= 0 && current < Count()) {
     cOsdItem *item = Get(current);
     if (item && item->Selectable())
        SetCurrent(item);
     }
  Display();
  lastRefresh = time(NULL);
}

eOSState cMenuUpdateMain::Check(void)
{
  Updater.RequestCheck();
  Set();
  return osContinue;
}

eOSState cMenuUpdateMain::Reboot(void)
{
  if (!Interface->Confirm(tr("Reboot now to install the update?")))
     return osContinue;
  if ((cRecordControls::Active() || RecordingsHandler.Active()) && !Interface->Confirm(tr("Recording - reboot anyway?")))
     return osContinue;
  std::string err;
  if (!Updater.Reboot(err)) {
     Skins.Message(mtError, err.c_str());
     Set();
     return osContinue;
     }
  Skins.Message(mtInfo, PluginConfig.BackupBeforeUpdate ? tr("Creating backup, then rebooting...") : tr("Rebooting..."));
  return osEnd;
}

eOSState cMenuUpdateMain::Discard(void)
{
  if (!Interface->Confirm(tr("Discard downloaded update?")))
     return osContinue;
  std::string err;
  if (!Updater.DiscardPending(err))
     Skins.Message(mtError, err.c_str());
  Set();
  return osContinue;
}

eOSState cMenuUpdateMain::CancelDownload(void)
{
  if (Interface->Confirm(tr("Cancel download?")))
     Updater.CancelDownload();
  Set();
  return osContinue;
}

eOSState cMenuUpdateMain::ProcessKey(eKeys Key)
{
  bool hadSubMenu = HasSubMenu();
  eOSState state = cOsdMenu::ProcessKey(Key);
  if (HasSubMenu())
     return state;
  if (hadSubMenu) {
     Set();
     return state;
     }
  switch (state) {
    case osUser1: return Check();
    case osUser2: return Reboot();
    case osUser3: return Discard();
    case osUser4: return CancelDownload();
    case osUser5: return AddSubMenu(new cMenuServers);
    case osUser6: return AddSubMenu(new cMenuBackups);
    case osUser7: return AddSubMenu(new cMenuSystem);
    default: break;
    }
  if (state == osUnknown) {
     switch (Key) {
       case kRed:  return Check();
       case kBlue: return AddSubMenu(new cMenuServers);
       case kOk: {
            cServerItem *item = dynamic_cast<cServerItem *>(Get(Current()));
            if (item)
               return AddSubMenu(new cMenuReleases(item->Server()));
            }
            break;
       default: break;
       }
     }
  if (Key == kNone && time(NULL) != lastRefresh) {
     if (Updater.StateText() != lastStateText || Updater.LastCheck() != lastCheck)
        Set();
     else
        lastRefresh = time(NULL);
     }
  return state;
}

// --- cReleaseItem --------------------------------------------------------------------

class cReleaseItem : public cOsdItem {
private:
  int index;
public:
  cReleaseItem(const cReleaseInfo &Release, int Index);
  int Index(void) const { return index; }
  };

cReleaseItem::cReleaseItem(const cReleaseInfo &Release, int Index)
{
  index = Index;
  std::string label = Release.label;
  if (Release.prerelease)
     label += std::string(" [") + tr("pre-release") + "]";
  SetText(cString::sprintf("%s\t%s\t%s", CompareMark(Updater.Compare(Release)), label.c_str(), FormatDate(Release).c_str()));
}

// --- cMenuReleases ---------------------------------------------------------------------

cMenuReleases::cMenuReleases(const std::string &Server)
:cOsdMenu(Server.c_str(), 2, 40)
{
  server = Server;
  Set();
  SetHelp(NULL, NULL, NULL, tr("Button$Info"));
}

void cMenuReleases::Set(void)
{
  Clear();
  releases.clear();
  std::vector<cServerResult> results = Updater.Results();
  for (size_t i = 0; i < results.size(); i++) {
      if (results[i].server.name != server)
         continue;
      if (!results[i].error.empty()) {
         Add(new cOsdItem(cString::sprintf("%s: %s", tr("Error"), results[i].error.c_str()), osUnknown, false));
         break;
         }
      releases = results[i].releases;
      break;
      }
  std::string channel;
  bool first = true;
  for (size_t i = 0; i < releases.size(); i++) {
      if (first || releases[i].channel != channel) {
         channel = releases[i].channel;
         first = false;
         Add(new cOsdItem(cString::sprintf("--- %s ---", channel.c_str()), osUnknown, false));
         }
      cReleaseItem *item = new cReleaseItem(releases[i], int(i));
      Add(item);
      if (Current() < 0)
         SetCurrent(item);
      }
  if (releases.empty() && !Count())
     Add(new cOsdItem(tr("No releases found for this device"), osUnknown, false));
  Display();
}

const cReleaseInfo *cMenuReleases::CurrentRelease(void)
{
  cReleaseItem *item = dynamic_cast<cReleaseItem *>(Get(Current()));
  if (item && item->Index() >= 0 && item->Index() < int(releases.size()))
     return &releases[item->Index()];
  return NULL;
}

eOSState cMenuReleases::Install(void)
{
  const cReleaseInfo *r = CurrentRelease();
  if (!r)
     return osContinue;
  cString question;
  switch (Updater.Compare(*r)) {
    case crOlder: question = cString::sprintf(tr("Downgrade to %s?"), r->label.c_str()); break;
    case crSame:  question = cString::sprintf(tr("Reinstall %s?"), r->label.c_str()); break;
    default:      question = cString::sprintf(tr("Download and install %s?"), r->label.c_str()); break;
    }
  if (!Interface->Confirm(question))
     return osContinue;
  std::string err;
  if (!Updater.RequestInstall(*r, err)) {
     Skins.Message(mtError, err.c_str());
     return osContinue;
     }
  Skins.Message(mtInfo, tr("Download started"));
  return osBack;
}

eOSState cMenuReleases::Info(void)
{
  const cReleaseInfo *r = CurrentRelease();
  if (!r)
     return osContinue;
  std::string checksum = !r->sha256.empty() ? r->sha256 : !r->sha256Url.empty() ? std::string(tr("from .sha256 file")) : std::string(tr("none"));
  const char *cmp = "";
  switch (Updater.Compare(*r)) {
    case crNewer:   cmp = tr("newer than installed"); break;
    case crSame:    cmp = tr("installed"); break;
    case crOlder:   cmp = tr("older than installed"); break;
    case crUnknown: cmp = tr("unknown"); break;
    }
  cString text = cString::sprintf("%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n\n%s:\n%s\n\n%s:\n%s",
     tr("Version"), r->label.c_str(),
     tr("Status"), cmp,
     tr("Server"), r->server.c_str(),
     tr("Channel"), r->channel.c_str(),
     tr("Build date"), FormatDate(*r).empty() ? tr("unknown") : FormatDate(*r).c_str(),
     tr("Size"), FormatSize(r->size).c_str(),
     tr("Pre-release"), r->prerelease ? tr("yes") : tr("no"),
     tr("Board"), r->board.empty() ? "-" : r->board.c_str(),
     tr("File"), r->name.c_str(),
     tr("Checksum (SHA-256)"), checksum.c_str());
  return AddSubMenu(new cMenuText(r->label.c_str(), text));
}

eOSState cMenuReleases::ProcessKey(eKeys Key)
{
  eOSState state = cOsdMenu::ProcessKey(Key);
  if (state == osUnknown && !HasSubMenu()) {
     switch (Key) {
       case kOk:   return Install();
       case kBlue: return Info();
       default: break;
       }
     }
  return state;
}

// --- cMenuServers ----------------------------------------------------------------------

class cServerListItem : public cOsdItem {
private:
  int index;
public:
  cServerListItem(const cServerEntry &Server, int Index)
  {
    index = Index;
    SetText(cString::sprintf("%s\t%s\t%s", Server.enabled ? "+" : "-", ServerTypeName(Server.type), Server.name.c_str()));
  }
  int Index(void) const { return index; }
  };

cMenuServers::cMenuServers(void)
:cOsdMenu(tr("Release servers"), 2, 8)
{
  servers = ServerList.Get();
  Set();
}

void cMenuServers::Set(void)
{
  int current = Current();
  Clear();
  for (size_t i = 0; i < servers.size(); i++)
      Add(new cServerListItem(servers[i], int(i)));
  if (servers.empty())
     Add(new cOsdItem(tr("No servers - press red to add one"), osUnknown, false));
  if (current >= 0 && current < Count())
     SetCurrent(Get(current));
  SetHelp(tr("Button$New"), servers.empty() ? NULL : tr("Button$On/Off"), servers.empty() ? NULL : tr("Button$Delete"), NULL);
  Display();
}

void cMenuServers::Save(void)
{
  ServerList.Set(servers);
  if (!ServerList.Save())
     Skins.Message(mtError, tr("Can't save server list"));
}

eOSState cMenuServers::ProcessKey(eKeys Key)
{
  bool hadSubMenu = HasSubMenu();
  eOSState state = cOsdMenu::ProcessKey(Key);
  if (HasSubMenu())
     return state;
  if (hadSubMenu) {
     // the edit menu may have changed the list
     Set();
     return state;
     }
  if (state == osUnknown) {
     cServerListItem *item = dynamic_cast<cServerListItem *>(Get(Current()));
     switch (Key) {
       case kRed:
            return AddSubMenu(new cMenuServerEdit(&servers, -1));
       case kOk:
            if (item)
               return AddSubMenu(new cMenuServerEdit(&servers, item->Index()));
            break;
       case kGreen:
            if (item) {
               servers[item->Index()].enabled = !servers[item->Index()].enabled;
               Save();
               Set();
               }
            return osContinue;
       case kYellow:
            if (item && Interface->Confirm(tr("Delete server?"))) {
               servers.erase(servers.begin() + item->Index());
               Save();
               Set();
               }
            return osContinue;
       default: break;
       }
     }
  return state;
}

// --- cMenuServerEdit -------------------------------------------------------------------

cMenuServerEdit::cMenuServerEdit(std::vector<cServerEntry> *Servers, int Index)
:cOsdMenu(Index < 0 ? tr("New server") : tr("Edit server"), 16)
{
  servers = Servers;
  index = Index;
  cServerEntry e;
  if (index >= 0 && index < int(servers->size()))
     e = (*servers)[index];
  else
     index = -1;
  type = e.type;
  enabled = e.enabled;
  strn0cpy(name, e.name.c_str(), sizeof(name));
  strn0cpy(url, e.url.c_str(), sizeof(url));
  strn0cpy(channel, e.channel.c_str(), sizeof(channel));
  strn0cpy(filter, e.filter.c_str(), sizeof(filter));
  typeNames[stJson] = tr("releases.json");
  typeNames[stGithub] = tr("GitHub");
  typeNames[stIndex] = tr("Directory listing");
  typeNames[stDir] = tr("Local/network directory");
  typeNames[stSmb] = tr("Samba share");
  Set();
}

void cMenuServerEdit::Set(void)
{
  Clear();
  Add(new cMenuEditStraItem(tr("Type"), &type, stTypes, typeNames));
  Add(new cMenuEditBoolItem(tr("Enabled"), &enabled));
  Add(new cMenuEditStrItem(tr("Name"), name, sizeof(name)));
  Add(new cMenuEditStrItem(type == stGithub ? tr("Repository") : type == stDir ? tr("Path") : type == stSmb ? tr("Address") : tr("URL"), url, sizeof(url), UrlChars));
  if (type != stIndex)
     Add(new cMenuEditStrItem(type == stGithub ? tr("Tag contains") : tr("Channel"), channel, sizeof(channel), UrlChars));
  Add(new cMenuEditStrItem(tr("Filter (regex)"), filter, sizeof(filter), UrlChars));
  Display();
}

eOSState cMenuServerEdit::ProcessKey(eKeys Key)
{
  int oldType = type;
  eOSState state = cOsdMenu::ProcessKey(Key);
  if (type != oldType) {
     int current = Current();
     Set();
     SetCurrent(Get(current));
     Display();
     }
  if (state == osUnknown && Key == kOk) {
     cServerEntry e;
     e.type = type;
     e.enabled = enabled;
     e.name = Trim(name);
     e.url = Trim(url);
     e.channel = Trim(channel);
     e.filter = Trim(filter);
     if (e.url.empty()) {
        Skins.Message(mtError, tr("URL must not be empty"));
        return osContinue;
        }
     if (e.type == stDir && LocalPath(e.url).empty()) {
        Skins.Message(mtError, tr("Invalid path (must start with /)"));
        return osContinue;
        }
     cSmbUrl smb;
     if (e.type == stSmb && !ParseSmbUrl(e.url, smb)) {
        Skins.Message(mtError, tr("Invalid SMB address (use smb://server/share/path)"));
        return osContinue;
        }
     if (e.type == stSmb && !HaveSmbSupport())
        Skins.Message(mtWarning, tr("SMB support not available in this build"));
     if (e.type == stGithub && GithubApiUrl(e.url).empty()) {
        Skins.Message(mtError, tr("Invalid GitHub repository (use owner/repo)"));
        return osContinue;
        }
     if (e.name.empty())
        e.name = e.url;
     for (size_t i = 0; i < servers->size(); i++) {
         if (int(i) != index && (*servers)[i].name == e.name) {
            Skins.Message(mtError, tr("A server with this name already exists"));
            return osContinue;
            }
         }
     if (index < 0)
        servers->push_back(e);
     else
        (*servers)[index] = e;
     ServerList.Set(*servers);
     if (!ServerList.Save())
        Skins.Message(mtError, tr("Can't save server list"));
     return osBack;
     }
  return state;
}

// --- cMenuBackups --------------------------------------------------------------------

class cIndexItem : public cOsdItem {
private:
  int index;
public:
  cIndexItem(const char *Text, int Index) : cOsdItem(Text), index(Index) {}
  int Index(void) const { return index; }
  };

#define IMAGE_INDEX_BASE 10000

cMenuBackups::cMenuBackups(void)
:cOsdMenu(tr("Backups & rollback"), 18, 22)
{
  lastRefresh = 0;
  Set();
  SetHelp(tr("Button$Backup"), NULL, tr("Button$Delete"), NULL);
}

void cMenuBackups::Set(void)
{
  int current = Current();
  Clear();
  backups = ListBackups(PluginConfig.BackupDir);
  images = Updater.RollbackImages();
  lastState = Updater.StateText();
  Add(new cOsdItem(cString::sprintf("%s:\t%s", tr("Status"), lastState.c_str()), osUnknown, false));
  Add(new cOsdItem(tr("Create backup now"), osUser1));
  Add(new cOsdItem(cString::sprintf("--- %s ---", tr("Backups (OK = restore)")), osUnknown, false));
  if (backups.empty())
     Add(new cOsdItem(tr("No backups"), osUnknown, false));
  for (size_t i = 0; i < backups.size(); i++) {
      const cBackupInfo &b = backups[i];
      std::string reason = b.reason.empty() ? tr("unknown") : b.reason;
      Add(new cIndexItem(cString::sprintf("%s\t%s\t%s", *DayDateTime(b.created), FormatSize(b.size).c_str(), reason.c_str()), int(i)));
      }
  Add(new cOsdItem(cString::sprintf("--- %s ---", tr("Rollback (OK = install)")), osUnknown, false));
  if (images.empty())
     Add(new cOsdItem(tr("No update files kept"), osUnknown, false));
  for (size_t i = 0; i < images.size(); i++) {
      const cReleaseInfo &r = images[i];
      time_t t;
      bool hasTime;
      std::string date = ReleaseBuildDate(r, t, hasTime) ? FormatBuildDate(t, hasTime) : "";
      eCompareResult c = Updater.Compare(r);
      std::string label = r.label + (c == crSame ? std::string(" (") + tr("installed") + ")" : "");
      Add(new cIndexItem(cString::sprintf("%s\t%s\t%s", date.c_str(), FormatSize(r.size).c_str(), label.c_str()), IMAGE_INDEX_BASE + int(i)));
      }
  if (current >= 0 && current < Count())
     SetCurrent(Get(current));
  Display();
  lastRefresh = time(NULL);
}

eOSState cMenuBackups::Restore(int Index)
{
  if (Index < 0 || Index >= int(backups.size()))
     return osContinue;
  if (Updater.Busy()) {
     Skins.Message(mtError, tr("Please wait until the current action is finished"));
     return osContinue;
     }
  const cBackupInfo &b = backups[Index];
  if (!Interface->Confirm(cString::sprintf(tr("Restore backup from %s? VDR will be restarted."), *DayDateTime(b.created))))
     return osContinue;
  if ((cRecordControls::Active() || RecordingsHandler.Active()) && !Interface->Confirm(tr("Recording - restart VDR anyway?")))
     return osContinue;
  std::string err;
  if (!StartRestore(b, false, PluginConfig.WorkDir, Updater.StateFile(), err)) {
     Skins.Message(mtError, err.c_str());
     return osContinue;
     }
  Skins.Message(mtInfo, tr("Restoring backup, VDR will be restarted..."));
  return osEnd;
}

eOSState cMenuBackups::Rollback(int Index)
{
  if (Index < 0 || Index >= int(images.size()))
     return osContinue;
  const cReleaseInfo &r = images[Index];
  if (Updater.Compare(r) == crSame) {
     Skins.Message(mtInfo, tr("This version is installed"));
     return osContinue;
     }
  if (!Interface->Confirm(cString::sprintf(tr("Install %s again?"), r.label.c_str())))
     return osContinue;
  std::string err;
  if (!Updater.RequestInstall(r, err)) {
     Skins.Message(mtError, err.c_str());
     return osContinue;
     }
  Skins.Message(mtInfo, tr("Update file is prepared - reboot to install. Restore a backup if needed."));
  return osContinue;
}

eOSState cMenuBackups::Delete(int Index)
{
  if (Index >= 0 && Index < int(backups.size())) {
     if (Interface->Confirm(tr("Delete backup?"))) {
        DeleteBackup(backups[Index]);
        Set();
        }
     }
  else if (Index >= IMAGE_INDEX_BASE && Index - IMAGE_INDEX_BASE < int(images.size())) {
     const cReleaseInfo &r = images[Index - IMAGE_INDEX_BASE];
     if (Interface->Confirm(tr("Delete kept update file?"))) {
        std::string path = LocalPath(r.url);
        unlink(path.c_str());
        unlink((path + ".sha256").c_str());
        Set();
        }
     }
  return osContinue;
}

eOSState cMenuBackups::ProcessKey(eKeys Key)
{
  eOSState state = cOsdMenu::ProcessKey(Key);
  if (HasSubMenu())
     return state;
  if (state == osUser1 || (state == osUnknown && Key == kRed)) {
     std::string err;
     if (!Updater.RequestBackup(err))
        Skins.Message(mtError, err.c_str());
     Set();
     return osContinue;
     }
  if (state == osUnknown) {
     cIndexItem *item = dynamic_cast<cIndexItem *>(Get(Current()));
     switch (Key) {
       case kOk:
            if (item)
               return item->Index() >= IMAGE_INDEX_BASE ? Rollback(item->Index() - IMAGE_INDEX_BASE) : Restore(item->Index());
            return osContinue;
       case kYellow:
            if (item)
               return Delete(item->Index());
            return osContinue;
       default: break;
       }
     }
  if (Key == kNone && time(NULL) - lastRefresh >= 2 && Updater.StateText() != lastState)
     Set(); // e.g. the backup is finished
  return state;
}

// --- cMenuSystem ----------------------------------------------------------------------

class cSystemItem : public cOsdItem {
private:
  int action;
public:
  cSystemItem(const char *Text, int Action) : cOsdItem(Text), action(Action) {}
  int Action(void) const { return action; }
  };

cMenuSystem::cMenuSystem(void)
:cOsdMenu(tr("System functions"), 20)
{
  lastRefresh = 0;
  Set();
  // start on the first function
  for (cOsdItem *item = First(); item; item = Next(item)) {
      if (dynamic_cast<cSystemItem *>(item)) {
         SetCurrent(item);
         break;
         }
      }
  Display();
}

void cMenuSystem::Set(void)
{
  int current = Current();
  Clear();
  lastState = Updater.StateText();
  lastResult = Updater.SystemResult();
  Add(new cOsdItem(cString::sprintf("%s:\t%s", tr("Status"), lastState.c_str()), osUnknown, false));
  if (!InstallScriptAvailable()) {
     Add(new cOsdItem(cString::sprintf("%s: %s", tr("install.sh not found"), PluginConfig.InstallScript), osUnknown, false));
     Display();
     return;
     }
  std::string boot = BootTarget();
  Add(new cOsdItem(cString::sprintf("%s:\t%s", tr("Start after boot"), boot == "vdr" ? "VDR" : boot == "kodi" ? "Kodi" : boot.empty() ? tr("not set") : boot.c_str()), osUnknown, false));
  Add(new cOsdItem("", osUnknown, false));
  struct { int action; const char *text; } items[] = {
    { saBootVdr,      trNOOP("Start VDR after boot") },
    { saBootKodi,     trNOOP("Start Kodi after boot") },
    { saWeb,          trNOOP("Install web components (cefbrowser, remotetranscode)") },
    { saTriggerhappy, trNOOP("Install triggerhappy (hotkey daemon)") },
    { saSamples,      trNOOP("Install sample configuration") },
    { saMerge,        trNOOP("Add missing sample files to configuration") },
    };
  for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
      int a = items[i].action;
      std::string text = tr(items[i].text);
      if (a == saWeb && !CefbrowserInstalled())
         text += std::string(" (") + tr("cefbrowser missing") + ")";
      else if (a != saBootVdr && a != saBootKodi && SystemActionDone(a))
         text += std::string(" (") + tr("installed") + ")";
      Add(new cSystemItem(text.c_str(), a));
      }
  Add(new cOsdItem("", osUnknown, false));
  Add(new cOsdItem(cString::sprintf("%s:", tr("Last function")), osUnknown, false));
  Add(new cOsdItem(lastResult.empty() ? tr("none") : lastResult.c_str(), osUnknown, false));
  if (!lastResult.empty() || !SystemActionLog(PluginConfig.WorkDir).empty())
     Add(new cOsdItem(tr("Show output of install.sh"), osUser1));
  if (current >= 0 && current < Count()) {
     cOsdItem *item = Get(current);
     if (item && item->Selectable())
        SetCurrent(item);
     }
  Display();
  lastRefresh = time(NULL);
}

eOSState cMenuSystem::Run(int Action)
{
  const char *question = NULL;
  switch (Action) {
    case saBootVdr:      question = tr("Start VDR after boot?"); break;
    case saBootKodi:     question = tr("Start Kodi after boot?"); break;
    case saWeb:          question = tr("Install web components?"); break;
    case saTriggerhappy: question = tr("Install and start triggerhappy?"); break;
    case saSamples:      question = tr("Extract sample configuration to vdropt-sample?"); break;
    case saMerge:        question = tr("Copy all missing sample files into vdropt (backup first)?"); break;
    default: return osContinue;
    }
  if (Action == saWeb && !CefbrowserInstalled()) {
     Skins.Message(mtError, tr("cefbrowser is not installed"));
     return osContinue;
     }
  if (!Interface->Confirm(question))
     return osContinue;
  if (Action == saMerge && !Interface->Confirm(tr("Files deleted from vdropt on purpose come back - continue?")))
     return osContinue;
  if ((Action == saSamples || Action == saMerge) && BootTarget() == "kodi" && !Interface->Confirm(tr("Boot target is Kodi - this may switch to Kodi. Continue?")))
     return osContinue;
  std::string err;
  if (!Updater.RequestSystemAction(Action, err))
     Skins.Message(mtError, err.c_str());
  Set();
  return osContinue;
}

eOSState cMenuSystem::ProcessKey(eKeys Key)
{
  bool hadSubMenu = HasSubMenu();
  eOSState state = cOsdMenu::ProcessKey(Key);
  if (HasSubMenu())
     return state;
  if (hadSubMenu) {
     Set();
     return state;
     }
  if (state == osUser1) {
     std::string log = SystemActionLog(PluginConfig.WorkDir);
     return AddSubMenu(new cMenuText(tr("Output of install.sh"), log.empty() ? tr("no output") : log.c_str()));
     }
  if (state == osUnknown && Key == kOk) {
     cSystemItem *item = dynamic_cast<cSystemItem *>(Get(Current()));
     if (item)
        return Run(item->Action());
     }
  if (Key == kNone && time(NULL) != lastRefresh) {
     if (Updater.StateText() != lastState || Updater.SystemResult() != lastResult)
        Set(); // e.g. install.sh finished
     else
        lastRefresh = time(NULL);
     }
  return state;
}

// --- cMenuSetupVdrsternelec --------------------------------------------------------------

cMenuSetupVdrsternelec::cMenuSetupVdrsternelec(void)
{
  config = PluginConfig;
  rebootModes[rmManual] = tr("manually");
  rebootModes[rmIdle] = tr("automatically when idle");
  backupScopes[bsVdr] = tr("VDR configuration");
  backupScopes[bsConfig] = tr("complete /storage/.config");
  Set();
}

void cMenuSetupVdrsternelec::AddSection(const char *Title)
{
  Add(new cOsdItem(cString::sprintf("--- %s ---", Title), osUnknown, false));
}

void cMenuSetupVdrsternelec::Set(void)
{
  int current = Current();
  Clear();
  // all functions are available here, so the main menu entry can be hidden
  Add(new cOsdItem(cString::sprintf("%s...", tr("VDR*ELEC update")), osUser1));
  Add(new cOsdItem(tr("Release servers..."), osUser2));
  Add(new cOsdItem(tr("Backups & rollback..."), osUser3));
  Add(new cOsdItem(tr("System functions..."), osUser4));
  AddSection(tr("General"));
  Add(new cMenuEditBoolItem(tr("Hide main menu entry"), &config.HideMainMenuEntry));
  AddSection(tr("Automatic"));
  // opt-in: without it the plugin only accesses the servers on user request
  Add(new cMenuEditBoolItem(tr("Check for updates automatically"), &config.AutoCheck));
  if (config.AutoCheck) {
     Add(new cMenuEditIntItem(cString::sprintf("  %s", tr("Check interval (h)")), &config.CheckInterval, 1, 24 * 7));
     Add(new cMenuEditBoolItem(cString::sprintf("  %s", tr("Notify about new releases")), &config.Notify));
     Add(new cMenuEditBoolItem(cString::sprintf("  %s", tr("Download new releases automatically")), &config.AutoDownload));
     }
  Add(new cMenuEditStraItem(tr("Reboot to install"), &config.RebootMode, rmModes, rebootModes));
  if (config.RebootMode == rmIdle)
     Add(new cMenuEditIntItem(cString::sprintf("  %s", tr("Idle time before reboot (min)")), &config.IdleMinutes, 1, 24 * 60));
  Add(new cMenuEditTimeItem(tr("Automatic actions from"), &config.WindowStart));
  Add(new cMenuEditTimeItem(tr("Automatic actions until"), &config.WindowEnd));
  Add(new cMenuEditIntItem(tr("Download limit while recording (KB/s)"), &config.ThrottleKBps, 0, 1000000, tr("none")));
  AddSection(tr("Safety"));
  Add(new cMenuEditBoolItem(tr("Require checksum"), &config.RequireChecksum));
  Add(new cMenuEditBoolItem(tr("Allow change of major version"), &config.AllowMajorUpgrade));
  Add(new cMenuEditBoolItem(tr("Backup before update"), &config.BackupBeforeUpdate));
  Add(new cMenuEditStraItem(tr("Backup contents"), &config.BackupScope, 2, backupScopes));
  Add(new cMenuEditIntItem(tr("Number of backups"), &config.BackupKeep, 1, 50));
  Add(new cMenuEditIntItem(tr("Older versions for rollback"), &config.KeepImages, 0, 5, tr("none")));
  Add(new cMenuEditBoolItem(tr("Update installed addons"), &config.UpdateAddons));
  AddSection(tr("Display"));
  Add(new cMenuEditBoolItem(tr("Show older releases"), &config.ShowOlder));
  Add(new cMenuEditBoolItem(tr("Show pre-releases"), &config.ShowPrerelease));
  AddSection(tr("Expert"));
  Add(new cMenuEditStrItem(tr("Update directory"), config.UpdateDir, sizeof(config.UpdateDir), UrlChars));
  Add(new cMenuEditStrItem(tr("Backup directory"), config.BackupDir, sizeof(config.BackupDir), UrlChars));
  Add(new cMenuEditStrItem(tr("Reboot command"), config.RebootCommand, sizeof(config.RebootCommand), UrlChars));
  Add(new cMenuEditStrItem(tr("U-Boot board"), config.Board, sizeof(config.Board), UrlChars));
  Add(new cMenuEditStrItem(tr("Architecture (empty = auto)"), config.Arch, sizeof(config.Arch), UrlChars));
  if (current >= 0 && current < Count())
     SetCurrent(Get(current));
}

eOSState cMenuSetupVdrsternelec::ProcessKey(eKeys Key)
{
  if (HasSubMenu()) {
     // keys unknown to a submenu must not store and close the setup page
     eOSState state = cOsdMenu::ProcessKey(Key);
     return state == osUnknown ? osContinue : state;
     }
  int oldAutoCheck = config.AutoCheck;
  int oldRebootMode = config.RebootMode;
  eOSState state = cMenuSetupPage::ProcessKey(Key);
  switch (state) {
    case osUser1: return AddSubMenu(new cMenuUpdateMain);
    case osUser2: return AddSubMenu(new cMenuServers);
    case osUser3: return AddSubMenu(new cMenuBackups);
    case osUser4: return AddSubMenu(new cMenuSystem);
    default: break;
    }
  if (config.AutoCheck != oldAutoCheck || config.RebootMode != oldRebootMode) {
     // show or hide the depending options
     Set();
     Display();
     }
  return state;
}

void cMenuSetupVdrsternelec::Store(void)
{
  // LastNotified is maintained by the updater, don't overwrite it
  strn0cpy(config.LastNotified, PluginConfig.LastNotified, sizeof(config.LastNotified));
  if (config.WindowStart == config.WindowEnd)
     config.WindowStart = config.WindowEnd = 0;
  PluginConfig = config;
  SetupStore("HideMainMenuEntry", config.HideMainMenuEntry);
  SetupStore("AutoCheck", config.AutoCheck);
  SetupStore("CheckInterval", config.CheckInterval);
  SetupStore("Notify", config.Notify);
  SetupStore("AutoDownload", config.AutoDownload);
  SetupStore("RebootMode", config.RebootMode);
  SetupStore("IdleMinutes", config.IdleMinutes);
  SetupStore("RequireChecksum", config.RequireChecksum);
  SetupStore("ShowOlder", config.ShowOlder);
  SetupStore("ShowPrerelease", config.ShowPrerelease);
  SetupStore("UpdateDir", config.UpdateDir);
  SetupStore("RebootCommand", config.RebootCommand);
  SetupStore("Board", config.Board);
  SetupStore("AllowMajorUpgrade", config.AllowMajorUpgrade);
  SetupStore("ThrottleKBps", config.ThrottleKBps);
  SetupStore("WindowStart", config.WindowStart);
  SetupStore("WindowEnd", config.WindowEnd);
  SetupStore("BackupBeforeUpdate", config.BackupBeforeUpdate);
  SetupStore("BackupScope", config.BackupScope);
  SetupStore("BackupKeep", config.BackupKeep);
  SetupStore("KeepImages", config.KeepImages);
  SetupStore("UpdateAddons", config.UpdateAddons);
  SetupStore("BackupDir", config.BackupDir);
  SetupStore("Arch", config.Arch);
}
