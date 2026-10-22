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
#define DEFAULT_BACKUP_DIR     "/storage/backup/vdrsternelec"
#define DEFAULT_WORK_DIR       "/storage/.cache/vdrsternelec"

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
  int AllowMajorUpgrade;    ///< offer releases with another major version (e.g. 12 -> 13)
  int ThrottleKBps;         ///< download limit while recording, 0 = none
  int WindowStart;          ///< hhmm, automatic downloads and reboots only in this window
  int WindowEnd;            ///< hhmm, WindowStart == WindowEnd: always
  int BackupBeforeUpdate;   ///< create a backup before rebooting into an update
  int BackupScope;          ///< eBackupScope
  int BackupKeep;           ///< number of backups to keep
  int KeepImages;           ///< number of update files to keep for rollback, 0 = none
  int UpdateAddons;         ///< update installed addons together with the system
  char BackupDir[256];
  char Board[64];           ///< u-boot board, empty = all
  char Arch[64];            ///< override architecture, empty = auto detect
  char LastNotified[256];   ///< name of the last release we notified about
  // not in the setup menu (setup.conf only, mainly for tests)
  char WorkDir[256];        ///< staging area for addons, restore script
  char RootDir[256];        ///< prefix for system paths, empty = /
  char ServiceCommand[64];  ///< systemctl
  char VdrStopCommand[256];
  char VdrStartCommand[256];
  char DetachCommand[256];  ///< runs the restore script outside of VDR's service
  char InstallScript[256];  ///< VDR*ELEC's install.sh
  char RunScriptCommand[256]; ///< runs install.sh outside of VDR's service and waits for it
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
