/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * maintenance.h: Backups, restore, images for rollback and addons
 */

#ifndef __VDRSTERNELEC_MAINTENANCE_H
#define __VDRSTERNELEC_MAINTENANCE_H

#include <string>
#include <time.h>
#include <vector>

// --- Commands ----------------------------------------------------------------------

int RunCommand(const std::vector<std::string> &Argv, std::string *Output = NULL, size_t MaxOutput = 65536);
  ///< Runs a program (without shell) and returns its exit code, -1 on errors.

// --- Backups -------------------------------------------------------------------------

enum eBackupScope { bsVdr, bsConfig, bsScopes };

struct cBackupInfo {
  std::string file;        ///< full path of the archive
  time_t created;
  std::string reason;      ///< "manual" or the name of the update
  std::string fromVersion; ///< system version at the time of the backup
  std::string paths;       ///< archived paths (relative to /), space separated
  long long size;
  cBackupInfo(void) : created(0), size(-1) {}
  };

bool CreateBackup(const std::string &Dir, int Scope, const std::string &Reason, const std::string &FromVersion, cBackupInfo &Info, std::string &Error);
std::vector<cBackupInfo> ListBackups(const std::string &Dir);
  ///< Newest first.
void PruneBackups(const std::string &Dir, int Keep);
bool DeleteBackup(const cBackupInfo &Info);

bool StartRestore(const cBackupInfo &Backup, bool Reboot, const std::string &WorkDir, const std::string &KeepFile, std::string &Error);
  ///< Restores the backup in a separate process: VDR is stopped first (so it
  ///< doesn't overwrite the restored files when it exits), then the archived
  ///< paths are replaced, then VDR is started again (or the system rebooted).
  ///< KeepFile (the plugin's state file) is kept as it is.

// --- Images for rollback ---------------------------------------------------------------

bool KeepImage(const std::string &File, const std::string &Sha256, const std::string &Dir, int Count, std::string &Error);
  ///< Stores a (hard link to a) downloaded update file in Dir, together with its
  ///< checksum, and keeps only the Count newest ones.

// --- Addons ----------------------------------------------------------------------------

std::string AddonTargetDir(const std::string &Id);
  ///< Directory the addon is extracted into (it contains the folder Id).
bool AddonInstalled(const std::string &Id);
bool IsDriverAddon(const std::string &Id);
  ///< Kernel driver addons (driver.*) have to match the kernel, so they are
  ///< installed before rebooting into the new version.
bool CheckArchive(const std::string &File, const std::string &Id, std::string &Error);
  ///< All entries of the archive must be inside the folder Id.
bool InstallAddon(const std::string &File, const std::string &Id, std::string &Error);

#endif //__VDRSTERNELEC_MAINTENANCE_H
