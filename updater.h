/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * updater.h: Background thread checking for and downloading updates
 */

#ifndef __VDRSTERNELEC_UPDATER_H
#define __VDRSTERNELEC_UPDATER_H

#include "config.h"
#include "parsers.h"
#include <string>
#include <vector>
#include <vdr/thread.h>

struct cServerResult {
  cServerEntry server;
  std::string error;
  std::vector<cReleaseInfo> releases;
  };

class cUpdater : public cThread {
public:
  enum eState { usIdle, usChecking, usDownloading, usVerifying, usReady, usError };
private:
  cMutex mutex;
  cCondVar condVar;
  cSysInfo sysInfo;
  eState state;
  std::string message;
  std::vector<cServerResult> results;
  time_t lastCheck;
  time_t nextAutoCheck;
  bool checkRequested;
  bool installRequested;
  cReleaseInfo installRelease;
  cReleaseInfo current;
  std::string pendingFile;
  cReleaseInfo pendingRelease;
  long long dlNow;
  long long dlTotal;
  volatile bool abortDownload;
  std::string stateFile;
  bool autoRebootBlocked;
  std::string notifiedName;
  bool notifiedChanged;
  void SetState(eState State, const std::string &Message = "");
  void FetchServer(const cServerEntry &Server, cServerResult &Result);
  void DoCheck(bool Automatic);
  bool DoInstall(const cReleaseInfo &Release);
  bool DoInstall(const cReleaseInfo &Release, class cFileSource *Source, const std::string &FinalName, const std::string &Part);
  std::string Arch(void);
  void FindPendingUpdate(void);
  void LoadState(void);
  void SaveState(const cReleaseInfo &Release);
  static bool RemoveOtherUpdates(const std::string &Dir, const std::string &Keep);
  friend class cDownloadProgress;
protected:
  virtual void Action(void);
public:
  cUpdater(void);
  virtual ~cUpdater();
  void Init(const char *OsRelease, const char *StateFile);
  ///< Reads the system information and looks for an already downloaded update.
  ///< StateFile remembers which update was downloaded/installed by the plugin.
  void Stop(void);
  cSysInfo SysInfo(void);
  void RequestCheck(void);
  bool RequestInstall(const cReleaseInfo &Release, std::string &Error);
  void CancelDownload(void);
  eState State(void);
  std::string StateText(void);
  ///< Human readable (translated) state, including progress.
  std::string Message(void);
  ///< Detail message for the current state (error text, file name...).
  int ProgressPercent(void);
  ///< -1 if unknown
  bool Busy(void);
  ///< True while checking or downloading.
  time_t LastCheck(void);
  std::vector<cServerResult> Results(void);
  ///< Results of the last check, filtered by the settings.
  std::vector<cReleaseInfo> Releases(void);
  ///< All releases of the last check (flattened, filtered by the settings).
  bool UpdatePending(void);
  std::string PendingFile(void);
  cReleaseInfo PendingRelease(void);
  ///< The downloaded update (name, label and build time, as far as known).
  bool DiscardPending(std::string &Error);
  bool Reboot(std::string &Error);
  bool AutoRebootAllowed(void);
  ///< False if the system already rebooted for the pending update without
  ///< installing it (prevents a reboot loop).
  eCompareResult Compare(const cReleaseInfo &Release);
  bool GetNotified(std::string &Name);
  ///< Returns true (once) if the name of the last notified release changed,
  ///< so the main thread can store it in setup.conf.
  };

extern cUpdater Updater;

#endif //__VDRSTERNELEC_UPDATER_H
