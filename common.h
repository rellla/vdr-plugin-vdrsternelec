/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * common.h: Common definitions
 */

#ifndef __VDRSTERNELEC_COMMON_H
#define __VDRSTERNELEC_COMMON_H

#include <vdr/tools.h>

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "unknown"
#endif

#define PLUGIN_LOG_PREFIX "vdrsternelec: "

#define esyslog_plg(a...) esyslog(PLUGIN_LOG_PREFIX a)
#define isyslog_plg(a...) isyslog(PLUGIN_LOG_PREFIX a)
#define dsyslog_plg(a...) dsyslog(PLUGIN_LOG_PREFIX a)

#endif //__VDRSTERNELEC_COMMON_H
