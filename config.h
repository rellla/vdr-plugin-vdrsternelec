/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * config.h: Plugin settings (setup.conf) and server list (servers.conf)
 */

#ifndef __VDRSTERNELEC_CONFIG_H
#define __VDRSTERNELEC_CONFIG_H

#include <string>
#include <vector>
#include <vdr/thread.h>

#define DEFAULT_UPDATE_DIR     "/storage/.update"
#define DEFAULT_REBOOT_COMMAND "reboot"
#define DEFAULT_OS_RELEASE     "/etc/os-release"

enum eRebootMode { rmManual, rmIdle, rmModes };

class cPluginConfig {
public:
  int HideMainMenuEntry;
  int AutoCheck;            ///< check for updates automatically (opt-in, default off)
  int CheckInterval;        ///< hours between automatic checks
  int Notify;               ///< show an OSD message if an update is available
  int AutoDownload;         ///< download an available update automatically
  int RebootMode;           ///< eRebootMode
  int IdleMinutes;          ///< minimum time without user activity for rmIdle
  int RequireChecksum;      ///< refuse to install files without checksum
  int ShowOlder;            ///< also list older releases (downgrade)
  int ShowPrerelease;       ///< also list GitHub pre-releases
  char UpdateDir[256];
  char RebootCommand[256];
  char Board[64];           ///< u-boot board, empty = all
  char Arch[64];            ///< override architecture, empty = auto detect
  char LastNotified[256];   ///< name of the last release we notified about
  cPluginConfig(void);
  bool SetupParse(const char *Name, const char *Value);
  };

extern cPluginConfig PluginConfig;

// --- Servers -----------------------------------------------------------------

enum eServerType { stJson, stGithub, stIndex, stDir, stSmb, stTypes };

const char *ServerTypeName(int Type);
  ///< "json", "github", "index", "dir" or "smb" (used in servers.conf)

struct cServerEntry {
  int type;               ///< eServerType
  bool enabled;
  std::string name;
  std::string url;        ///< server URL, owner/repo for GitHub, path for dir, smb:// URL for smb
  std::string channel;    ///< channel/tag to use, empty = all
  std::string filter;     ///< optional regular expression for file names
  cServerEntry(void) : type(stJson), enabled(true) {}
  std::string ToLine(void) const;
  bool FromLine(const std::string &Line);
  };

class cServerList {
private:
  cMutex mutex;
  std::vector<cServerEntry> servers;
  std::string fileName;
public:
  bool Load(const char *FileName);
  ///< Loads servers.conf. If the file does not exist, the default servers are used.
  bool Save(void);
  std::vector<cServerEntry> Get(void);
  void Set(const std::vector<cServerEntry> &Servers);
  static std::vector<cServerEntry> Defaults(void);
  };

extern cServerList ServerList;

#endif //__VDRSTERNELEC_CONFIG_H
