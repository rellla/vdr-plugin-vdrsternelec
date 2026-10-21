/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * filesource.h: Access to local directories and SMB shares
 */

#ifndef __VDRSTERNELEC_FILESOURCE_H
#define __VDRSTERNELEC_FILESOURCE_H

#include "http.h"
#include "parsers.h"
#include <string>
#include <sys/types.h>
#include <vector>

class cFileSource {
protected:
  virtual bool OpenFile(const std::string &Url, long long &Size, std::string &Error) = 0;
  virtual ssize_t ReadChunk(char *Buffer, size_t Length) = 0;
  ///< Returns the number of bytes read, 0 at the end of the file, -1 on errors.
  virtual void CloseFile(void) = 0;
public:
  virtual ~cFileSource() {}
  virtual bool List(const std::string &DirUrl, std::vector<cLocalFile> &Files, std::string &Error) = 0;
  ///< Lists the regular files in DirUrl. Size and time are only filled in
  ///< for update files and JSON files (-1/0 otherwise).
  bool Read(const std::string &Url, std::string &Content, size_t MaxSize, std::string &Error);
  bool Copy(const std::string &Url, const std::string &FileName, cHttpProgress *Progress, std::string &Error);
  ///< Copies Url into the local file FileName (created or truncated).
  };

cFileSource *NewFileSource(const std::string &Url, const cSmbUrl *Credentials, std::string &Error);
  ///< Creates a source for a file:/// or smb:// URL. Credentials are used for
  ///< SMB (NULL = guest access). Returns NULL and sets Error on failure.

bool HaveSmbSupport(void);
  ///< False if the plugin was built without libsmbclient.

#endif //__VDRSTERNELEC_FILESOURCE_H
