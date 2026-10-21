/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * http.h: Thin wrapper around libcurl
 */

#ifndef __VDRSTERNELEC_HTTP_H
#define __VDRSTERNELEC_HTTP_H

#include <string>

class cHttpProgress {
public:
  virtual ~cHttpProgress() {}
  virtual bool Progress(long long Now, long long Total) = 0;
  ///< Called during a download. Total is -1 if unknown.
  ///< Return false to abort the transfer.
  };

void HttpGlobalInit(void);
void HttpGlobalCleanup(void);

bool HttpGet(const std::string &Url, std::string &Result, std::string &Error, size_t MaxSize = 32 * 1024 * 1024, const char *Accept = NULL);
  ///< Fetches Url into Result. Fails if the result exceeds MaxSize bytes.
  ///< On HTTP errors (>= 400) false is returned and Result holds the body.

bool HttpDownload(const std::string &Url, const std::string &FileName, cHttpProgress *Progress, std::string &Error);
  ///< Downloads Url into the given file (which is created or truncated).

#endif //__VDRSTERNELEC_HTTP_H
