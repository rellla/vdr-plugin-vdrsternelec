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
#include "updater.h"
#include <vdr/interface.h>
#include <vdr/menu.h>
#include <vdr/recording.h>
#include <vdr/skins.h>

// characters allowed in URLs, channel names and regular expressions
static const char *UrlChars = " abcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%^|\\{}<>\"";

static std::string FormatDate(const cReleaseInfo &r)
{
  char buf[32];
  if (r.timestamp) {
     struct tm tm;
     localtime_r(&r.timestamp, &tm);
     strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &tm);
     return buf;
     }
  std::string d = ExtractDate(r.name);
  if (d.size() == 8)
     return d.substr(6, 2) + "." + d.substr(4, 2) + "." + d.substr(0, 4);
  return "";
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
  AddInfo(tr("Installed"), info.version.empty() ? tr("unknown") : info.version);
  AddInfo(tr("Device"), info.arch.empty() ? tr("unknown") : info.arch);
  AddInfo(tr("Status"), lastStateText);
  if (!message.empty())
     AddInfo(state == cUpdater::usError ? tr("Error") : tr("File"), message);
  AddInfo(tr("Last check"), lastCheck ? std::string(*DayDateTime(lastCheck)) : std::string(tr("never")));
  Add(new cOsdItem("", osUnknown, false));
  Add(new cOsdItem(tr("Check for updates"), osUser1));
  if (state == cUpdater::usDownloading || state == cUpdater::usVerifying)
     Add(new cOsdItem(tr("Cancel download"), osUser4));
  if (Updater.UpdatePending()) {
     Add(new cOsdItem(tr("Reboot now and install update"), osUser2));
     Add(new cOsdItem(tr("Discard downloaded update"), osUser3));
     }
  Add(new cOsdItem(tr("Release servers..."), osUser5));
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
  Skins.Message(mtInfo, tr("Rebooting..."));
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
     tr("Date"), FormatDate(*r).empty() ? tr("unknown") : FormatDate(*r).c_str(),
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
  Set();
}

void cMenuServerEdit::Set(void)
{
  Clear();
  Add(new cMenuEditStraItem(tr("Type"), &type, stTypes, typeNames));
  Add(new cMenuEditBoolItem(tr("Enabled"), &enabled));
  Add(new cMenuEditStrItem(tr("Name"), name, sizeof(name)));
  Add(new cMenuEditStrItem(type == stGithub ? tr("Repository") : tr("URL"), url, sizeof(url), UrlChars));
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

// --- cMenuSetupVdrsternelec --------------------------------------------------------------

cMenuSetupVdrsternelec::cMenuSetupVdrsternelec(void)
{
  config = PluginConfig;
  rebootModes[rmManual] = tr("manually");
  rebootModes[rmIdle] = tr("automatically when idle");
  Set();
}

void cMenuSetupVdrsternelec::Set(void)
{
  Clear();
  Add(new cMenuEditBoolItem(tr("Hide main menu entry"), &config.HideMainMenuEntry));
  Add(new cMenuEditIntItem(tr("Check interval (h)"), &config.CheckInterval, 0, 24 * 7, tr("off")));
  Add(new cMenuEditBoolItem(tr("Notify about new releases"), &config.Notify));
  Add(new cMenuEditBoolItem(tr("Download new releases automatically"), &config.AutoDownload));
  Add(new cMenuEditStraItem(tr("Reboot to install"), &config.RebootMode, rmModes, rebootModes));
  Add(new cMenuEditIntItem(tr("Idle time before reboot (min)"), &config.IdleMinutes, 1, 24 * 60));
  Add(new cMenuEditBoolItem(tr("Require checksum"), &config.RequireChecksum));
  Add(new cMenuEditBoolItem(tr("Show older releases"), &config.ShowOlder));
  Add(new cMenuEditBoolItem(tr("Show pre-releases"), &config.ShowPrerelease));
  Add(new cMenuEditStrItem(tr("Update directory"), config.UpdateDir, sizeof(config.UpdateDir), UrlChars));
  Add(new cMenuEditStrItem(tr("Reboot command"), config.RebootCommand, sizeof(config.RebootCommand), UrlChars));
  Add(new cMenuEditStrItem(tr("U-Boot board"), config.Board, sizeof(config.Board), UrlChars));
  Add(new cMenuEditStrItem(tr("Architecture (empty = auto)"), config.Arch, sizeof(config.Arch), UrlChars));
}

void cMenuSetupVdrsternelec::Store(void)
{
  // LastNotified is maintained by the updater, don't overwrite it
  strn0cpy(config.LastNotified, PluginConfig.LastNotified, sizeof(config.LastNotified));
  PluginConfig = config;
  SetupStore("HideMainMenuEntry", config.HideMainMenuEntry);
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
  SetupStore("Arch", config.Arch);
}
