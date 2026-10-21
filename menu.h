/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * menu.h: OSD menus
 */

#ifndef __VDRSTERNELEC_MENU_H
#define __VDRSTERNELEC_MENU_H

#include "config.h"
#include "parsers.h"
#include <string>
#include <vector>
#include <vdr/menuitems.h>
#include <vdr/osdbase.h>
#include <vdr/plugin.h>

// --- cMenuUpdateMain -------------------------------------------------------------

class cMenuUpdateMain : public cOsdMenu {
private:
  std::string lastStateText;
  time_t lastCheck;
  time_t lastRefresh;
  void Set(void);
  void AddInfo(const char *Label, const std::string &Value);
  eOSState Check(void);
  eOSState Reboot(void);
  eOSState Discard(void);
  eOSState CancelDownload(void);
public:
  cMenuUpdateMain(void);
  virtual eOSState ProcessKey(eKeys Key) override;
  };

// --- cMenuReleases ---------------------------------------------------------------

class cMenuReleases : public cOsdMenu {
private:
  std::string server;
  std::vector<cReleaseInfo> releases;
  void Set(void);
  eOSState Install(void);
  eOSState Info(void);
  const cReleaseInfo *CurrentRelease(void);
public:
  cMenuReleases(const std::string &Server);
  virtual eOSState ProcessKey(eKeys Key) override;
  };

// --- cMenuServers ----------------------------------------------------------------

class cMenuServers : public cOsdMenu {
private:
  std::vector<cServerEntry> servers;
  void Set(void);
  void Save(void);
public:
  cMenuServers(void);
  virtual eOSState ProcessKey(eKeys Key) override;
  };

class cMenuServerEdit : public cOsdMenu {
private:
  std::vector<cServerEntry> *servers;
  int index;
  int type;
  int enabled;
  char name[128];
  char url[256];
  char channel[128];
  char filter[128];
  const char *typeNames[stTypes];
  void Set(void);
public:
  cMenuServerEdit(std::vector<cServerEntry> *Servers, int Index);
  ///< Index -1 creates a new entry.
  virtual eOSState ProcessKey(eKeys Key) override;
  };

// --- cMenuSetupVdrsternelec --------------------------------------------------------

class cMenuSetupVdrsternelec : public cMenuSetupPage {
private:
  cPluginConfig config;
  const char *rebootModes[rmModes];
  void Set(void);
protected:
  virtual void Store(void) override;
public:
  cMenuSetupVdrsternelec(void);
  };

#endif //__VDRSTERNELEC_MENU_H
