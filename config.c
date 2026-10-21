/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * config.c: Plugin settings (setup.conf) and server list (servers.conf)
 */

#include "config.h"
#include "common.h"
#include "parsers.h"
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <unistd.h>

cPluginConfig PluginConfig;
cServerList ServerList;

// --- cPluginConfig -------------------------------------------------------------

cPluginConfig::cPluginConfig(void)
{
  HideMainMenuEntry = 0;
  CheckInterval = 24;
  Notify = 1;
  AutoDownload = 0;
  RebootMode = rmManual;
  IdleMinutes = 30;
  RequireChecksum = 1;
  ShowOlder = 1;
  ShowPrerelease = 1;
  strn0cpy(UpdateDir, DEFAULT_UPDATE_DIR, sizeof(UpdateDir));
  strn0cpy(RebootCommand, DEFAULT_REBOOT_COMMAND, sizeof(RebootCommand));
  Board[0] = 0;
  Arch[0] = 0;
  LastNotified[0] = 0;
}

bool cPluginConfig::SetupParse(const char *Name, const char *Value)
{
  if      (!strcasecmp(Name, "HideMainMenuEntry")) HideMainMenuEntry = atoi(Value);
  else if (!strcasecmp(Name, "CheckInterval"))     CheckInterval = constrain(atoi(Value), 0, 24 * 7);
  else if (!strcasecmp(Name, "Notify"))            Notify = atoi(Value);
  else if (!strcasecmp(Name, "AutoDownload"))      AutoDownload = atoi(Value);
  else if (!strcasecmp(Name, "RebootMode"))        RebootMode = constrain(atoi(Value), 0, rmModes - 1);
  else if (!strcasecmp(Name, "IdleMinutes"))       IdleMinutes = constrain(atoi(Value), 1, 24 * 60);
  else if (!strcasecmp(Name, "RequireChecksum"))   RequireChecksum = atoi(Value);
  else if (!strcasecmp(Name, "ShowOlder"))         ShowOlder = atoi(Value);
  else if (!strcasecmp(Name, "ShowPrerelease"))    ShowPrerelease = atoi(Value);
  else if (!strcasecmp(Name, "UpdateDir"))         strn0cpy(UpdateDir, Value, sizeof(UpdateDir));
  else if (!strcasecmp(Name, "RebootCommand"))     strn0cpy(RebootCommand, Value, sizeof(RebootCommand));
  else if (!strcasecmp(Name, "Board"))             strn0cpy(Board, Value, sizeof(Board));
  else if (!strcasecmp(Name, "Arch"))              strn0cpy(Arch, Value, sizeof(Arch));
  else if (!strcasecmp(Name, "LastNotified"))      strn0cpy(LastNotified, Value, sizeof(LastNotified));
  else
     return false;
  return true;
}

// --- cServerEntry --------------------------------------------------------------

static const char *ServerTypeNames[stTypes] = { "json", "github", "index", "dir", "smb" };

const char *ServerTypeName(int Type)
{
  return Type >= 0 && Type < stTypes ? ServerTypeNames[Type] : "?";
}

static std::string Sanitize(const std::string &s)
{
  // '|' and line breaks would break the file format
  std::string r = Trim(s);
  for (size_t i = 0; i < r.size(); i++)
      if (r[i] == '|' || r[i] == '\n' || r[i] == '\r')
         r[i] = ' ';
  return r;
}

std::string cServerEntry::ToLine(void) const
{
  std::string f = filter;
  for (size_t i = 0; i < f.size(); i++)
      if (f[i] == '\n' || f[i] == '\r')
         f[i] = ' ';
  return std::string(enabled ? "" : "!") + ServerTypeName(type) + "|" + Sanitize(name) + "|" + Sanitize(url) + "|" + Sanitize(channel) + "|" + Trim(f);
}

bool cServerEntry::FromLine(const std::string &Line)
{
  // [!]type|name|url|channel|filter  (filter is the rest of the line and may contain '|')
  std::string l = Trim(Line);
  if (l.empty() || l[0] == '#')
     return false;
  enabled = true;
  if (l[0] == '!') {
     enabled = false;
     l = l.substr(1);
     }
  std::vector<std::string> f;
  size_t pos = 0;
  for (int i = 0; i < 4; i++) {
      size_t bar = l.find('|', pos);
      if (bar == std::string::npos) {
         f.push_back(l.substr(pos));
         pos = l.size() + 1;
         break;
         }
      f.push_back(l.substr(pos, bar - pos));
      pos = bar + 1;
      }
  f.push_back(pos <= l.size() ? l.substr(pos) : "");
  while (f.size() < 5)
        f.push_back("");
  std::string t = Trim(f[0]);
  type = -1;
  for (int i = 0; i < stTypes; i++)
      if (t == ServerTypeNames[i])
         type = i;
  name = Trim(f[1]);
  url = Trim(f[2]);
  channel = Trim(f[3]);
  filter = Trim(f[4]);
  if (type < 0 || url.empty())
     return false;
  if (name.empty())
     name = url;
  return true;
}

// --- cServerList ---------------------------------------------------------------

std::vector<cServerEntry> cServerList::Defaults(void)
{
  std::vector<cServerEntry> v;
  cServerEntry e;
  e.type = stGithub;
  e.name = "VDR*ELEC";
  e.url = "Zabrimus/VDRSternELEC";
  v.push_back(e);
  return v;
}

bool cServerList::Load(const char *FileName)
{
  cMutexLock MutexLock(&mutex);
  fileName = FileName;
  servers.clear();
  FILE *f = fopen(FileName, "r");
  if (!f) {
     if (errno != ENOENT)
        esyslog_plg("can't read %s: %m", FileName);
     servers = Defaults();
     isyslog_plg("%s not found, using default servers", FileName);
     return false;
     }
  cReadLine ReadLine;
  char *s;
  int line = 0;
  while ((s = ReadLine.Read(f)) != NULL) {
        line++;
        std::string l = Trim(s);
        if (l.empty() || l[0] == '#')
           continue;
        cServerEntry e;
        if (e.FromLine(l))
           servers.push_back(e);
        else
           esyslog_plg("%s:%d: invalid server entry ignored", FileName, line);
        }
  fclose(f);
  isyslog_plg("%zu server(s) loaded from %s", servers.size(), FileName);
  return true;
}

bool cServerList::Save(void)
{
  cMutexLock MutexLock(&mutex);
  if (fileName.empty())
     return false;
  std::string tmp = fileName + ".new";
  FILE *f = fopen(tmp.c_str(), "w");
  if (!f) {
     esyslog_plg("can't write %s: %m", tmp.c_str());
     return false;
     }
  fprintf(f, "# vdrsternelec release servers\n");
  fprintf(f, "# Format: [!]type|name|url|channel|filter\n");
  fprintf(f, "#   type    json   = LibreELEC style server with releases.json\n");
  fprintf(f, "#           github = GitHub releases, url is owner/repo\n");
  fprintf(f, "#           index  = plain HTTP directory listing\n");
  fprintf(f, "#           dir    = local or mounted network directory, url is the path\n");
  fprintf(f, "#           smb    = Samba share, url is smb://[[domain;]user[:password]@]server/share/path\n");
  fprintf(f, "#           dir and smb use a releases.json in the directory if there is one\n");
  fprintf(f, "#   channel channel (json, dir, smb) or text contained in the tag (github), empty = all\n");
  fprintf(f, "#   filter  optional regular expression for file names\n");
  fprintf(f, "#   A leading '!' disables an entry.\n");
  for (size_t i = 0; i < servers.size(); i++)
      fprintf(f, "%s\n", servers[i].ToLine().c_str());
  bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
  if (fclose(f) != 0)
     ok = false;
  if (!ok || rename(tmp.c_str(), fileName.c_str()) != 0) {
     esyslog_plg("can't write %s: %m", fileName.c_str());
     unlink(tmp.c_str());
     return false;
     }
  return true;
}

std::vector<cServerEntry> cServerList::Get(void)
{
  cMutexLock MutexLock(&mutex);
  return servers;
}

void cServerList::Set(const std::vector<cServerEntry> &Servers)
{
  cMutexLock MutexLock(&mutex);
  servers = Servers;
}
