/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * sha256.h: SHA-256 implementation (FIPS 180-4)
 */

#ifndef __VDRSTERNELEC_SHA256_H
#define __VDRSTERNELEC_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <string>

class cSha256 {
private:
  uint32_t state[8];
  uint64_t length;
  uint8_t buffer[64];
  size_t bufferLen;
  void Transform(const uint8_t *Block);
public:
  cSha256(void) { Reset(); }
  void Reset(void);
  void Update(const void *Data, size_t Length);
  std::string HexDigest(void);
  ///< Finalizes the hash and returns the digest as lowercase hex string.
  ///< The object must be Reset() before it can be used again.
  };

std::string Sha256File(const char *FileName, volatile bool *Abort = NULL);
  ///< Returns the SHA-256 of the given file as lowercase hex string,
  ///< or an empty string on error (or if *Abort became true).

#endif //__VDRSTERNELEC_SHA256_H
