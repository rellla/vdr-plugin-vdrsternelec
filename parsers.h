/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * parsers.h: Parsing of release sources and system information.
 *            This file does not depend on VDR or libcurl, so it can be
 *            unit tested on its own.
 */

#ifndef __VDRSTERNELEC_PARSERS_H
#define __VDRSTERNELEC_PARSERS_H

#include <string>
#include <time.h>
#include <vector>

// --- Release information -----------------------------------------------------

struct cReleaseInfo {
  std::string server;    ///< name of the server this release comes from
  std::string channel;   ///< channel (releases.json), tag (GitHub) or server name (index)
  std::string name;      ///< file name, e.g. LibreELEC-RPi4.aarch64-13.0.0.tar
  std::string label;     ///< human readable version, e.g. 13.0.0
  std::string url;       ///< download URL
  std::string sha256;    ///< expected checksum (lowercase hex), may be empty
  std::string sha256Url; ///< URL of a .sha256 file, used if sha256 is empty
  std::string board;     ///< u-boot board name (only for u-boot images)
  long long size;        ///< file size in bytes, -1 if unknown
  time_t timestamp;      ///< build/upload time, 0 if unknown
  bool prerelease;       ///< marked as pre-release (GitHub)
  bool local;            ///< file in a local directory (url is file://)
  cReleaseInfo(void) : size(-1), timestamp(0), prerelease(false), local(false) {}
  };

// --- System information ------------------------------------------------------

struct cSysInfo {
  std::string distName;  ///< NAME from os-release, e.g. "LibreELEC"
  std::string version;   ///< VERSION, e.g. "13.0-devel-20261020123456-abcdef0"
  std::string versionId; ///< VERSION_ID, e.g. "13.0"
  std::string arch;      ///< <DEVICE>.<ARCH>, e.g. "RPi4.aarch64"
  std::string project;   ///< e.g. "RPi"
  std::string device;    ///< e.g. "RPi4"
  std::string build;     ///< e.g. "official", "devel"
  time_t buildTime;      ///< modification time of os-release, 0 if unknown
  std::string installedName; ///< update file installed by this plugin (if known)
  std::string installedKey;  ///< ReleaseKey() of that file
  cSysInfo(void) : buildTime(0) {}
  };

bool ParseOsRelease(const std::string &Content, cSysInfo &Info);
  ///< Parses the contents of /etc/os-release. Returns true if at least the
  ///< architecture could be determined.

// --- Source parsers ----------------------------------------------------------

bool ParseReleasesJson(const std::string &Json, const std::string &BaseUrl, const std::string &Arch, const std::string &ChannelFilter, const std::string &Board, std::vector<cReleaseInfo> &Releases, std::string &Error);
  ///< Parses a LibreELEC style releases.json. BaseUrl is used as download
  ///< base if a channel provides no "url" of its own. If ChannelFilter is not
  ///< empty, only that channel is used. If Board is not empty, u-boot images
  ///< are restricted to that board.

bool ParseGithubReleases(const std::string &Json, const std::string &Arch, const std::string &ChannelFilter, const std::string &Filter, std::vector<cReleaseInfo> &Releases, std::string &Error);
  ///< Parses the result of the GitHub API call /repos/<owner>/<repo>/releases
  ///< (an array) or /releases/tags/<tag> (a single object).
  ///< If ChannelFilter is given, only tags containing it are used.

bool ParseIndexHtml(const std::string &Html, const std::string &BaseUrl, const std::string &Arch, const std::string &Filter, const std::string &Channel, std::vector<cReleaseInfo> &Releases);
  ///< Parses a plain HTTP directory listing (any HTML containing links).

struct cLocalFile {
  std::string name;
  long long size;
  time_t mtime;
  };

bool ParseLocalDirectory(const std::vector<cLocalFile> &Files, const std::string &Dir, const std::string &Arch, const std::string &Filter, const std::string &Channel, std::vector<cReleaseInfo> &Releases);
  ///< Builds the releases from the files of a local (or mounted network)
  ///< directory. Checksums are taken from <file>.sha256 or SHA256SUMS.
  ///< Returns false if Dir is no absolute path.

std::string LocalPath(const std::string &Spec);
  ///< Returns the absolute path for "/path" or "file:///path", "" if invalid.
std::string PathToFileUrl(const std::string &Path);

std::string ParseSha256File(const std::string &Content, const std::string &FileName);
  ///< Extracts the checksum for FileName from the contents of a .sha256 file
  ///< or a checksum list (SHA256SUMS). Returns an empty string if none is found.

// --- Helpers -----------------------------------------------------------------

enum eCompareResult { crUnknown, crOlder, crSame, crNewer };

eCompareResult CompareWithInstalled(const cReleaseInfo &Release, const cSysInfo &Info);
  ///< Determines whether Release is older, the same or newer than the installed system.

bool IsUpdateFile(const std::string &Name);
  ///< True for file names LibreELEC can install from /storage/.update (.tar, .img.gz).
bool MatchesArch(const std::string &Name, const std::string &Arch, const std::string &Filter);
  ///< True if the file Name belongs to the given architecture. If Filter (a
  ///< regular expression) is given, it is used instead of the architecture.
std::string LabelFromName(const std::string &Name, const std::string &Arch);
std::string ExtractDate(const std::string &s);
  ///< Returns the first plausible date (YYYYMMDD) contained in s, or "".
  ///< Recognizes YYYYMMDD[hhmmss], YYYY-MM-DD and YYYY.MM.DD.
std::string ReleaseKey(const cReleaseInfo &Release);
  ///< Sortable key YYYYMMDDHHMMSS from the timestamp or a date in the
  ///< label, name or channel. Empty if unknown.
time_t ParseTimestamp(const std::string &s);
  ///< Parses "YYYY-MM-DD HH:MM:SS" or ISO 8601 "YYYY-MM-DDTHH:MM:SSZ" (UTC).
std::string JsonUrl(const std::string &Base);
  ///< Returns the URL of releases.json for the given server URL.
std::string UrlDirectory(const std::string &Url);
  ///< Returns Url up to and including the last '/'.
std::string ResolveUrl(const std::string &Base, const std::string &Href);
std::string GithubApiUrl(const std::string &Spec);
  ///< Builds the GitHub API URL from "owner/repo" or a github.com URL.
  ///< Returns "" if Spec is invalid.
void SortReleases(std::vector<cReleaseInfo> &Releases);
  ///< Sorts newest first, keeping the order of channels as they appeared.
std::string FormatSize(long long Size);
std::string Trim(const std::string &s);

#endif //__VDRSTERNELEC_PARSERS_H
