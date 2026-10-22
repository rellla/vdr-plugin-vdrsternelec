/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * parsers.c: Parsing of release sources and system information
 */

#include "parsers.h"
#include "json.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <string.h>

// --- Helpers -----------------------------------------------------------------

static std::string VersionPrefix(const std::string &s);

std::string Trim(const std::string &s)
{
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
     return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

static bool EndsWith(const std::string &s, const std::string &Suffix)
{
  return s.size() >= Suffix.size() && s.compare(s.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
}

static bool StartsWith(const std::string &s, const std::string &Prefix)
{
  return s.compare(0, Prefix.size(), Prefix) == 0;
}

bool IsUpdateFile(const std::string &Name)
{
  return EndsWith(Name, ".tar") || EndsWith(Name, ".img.gz");
}

static bool RegexSearch(const std::string &s, const std::string &Pattern, std::string *Group1 = NULL)
{
  try {
    std::regex re(Pattern);
    std::smatch m;
    if (!std::regex_search(s, m, re))
       return false;
    if (Group1)
       *Group1 = m.size() > 1 ? m[1].str() : "";
    return true;
    }
  catch (const std::regex_error &) {
    return false;
    }
}

bool MatchesArch(const std::string &Name, const std::string &Arch, const std::string &Filter)
{
  if (!Filter.empty())
     return RegexSearch(Name, Filter);
  if (Arch.empty())
     return false;
  size_t p = 0;
  while ((p = Name.find(Arch, p)) != std::string::npos) {
        bool startOk = p == 0 || Name[p - 1] == '-' || Name[p - 1] == '_' || Name[p - 1] == '/';
        size_t e = p + Arch.size();
        bool endOk = e == Name.size() || Name[e] == '-' || Name[e] == '_' || Name[e] == '.';
        if (startOk && endOk)
           return true;
        p++;
        }
  return false;
}

static std::string StripExtension(const std::string &Name)
{
  if (EndsWith(Name, ".img.gz"))
     return Name.substr(0, Name.size() - 7);
  if (EndsWith(Name, ".tar"))
     return Name.substr(0, Name.size() - 4);
  return Name;
}

std::string LabelFromName(const std::string &Name, const std::string &Arch)
{
  std::string base = StripExtension(Name);
  if (!Arch.empty()) {
     size_t p = base.find(Arch);
     if (p != std::string::npos && p + Arch.size() + 1 < base.size())
        return base.substr(p + Arch.size() + 1);
     }
  return base;
}

static bool ValidDate(int y, int m, int d)
{
  return y >= 2000 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

std::string ExtractDate(const std::string &s)
{
  size_t i = 0;
  while (i < s.size()) {
        if (s[i] >= '0' && s[i] <= '9') {
           size_t j = i;
           while (j < s.size() && s[j] >= '0' && s[j] <= '9')
                 j++;
           if (j - i >= 8) {
              // YYYYMMDD[hhmmss]
              std::string d = s.substr(i, 8);
              if (ValidDate(atoi(d.substr(0, 4).c_str()), atoi(d.substr(4, 2).c_str()), atoi(d.substr(6, 2).c_str())))
                 return d;
              }
           else if (j - i == 4 && j + 6 <= s.size() && (s[j] == '-' || s[j] == '.') && s[j + 3] == s[j]
                    && isdigit((unsigned char)s[j + 1]) && isdigit((unsigned char)s[j + 2]) && isdigit((unsigned char)s[j + 4]) && isdigit((unsigned char)s[j + 5])
                    && (j + 6 == s.size() || !isdigit((unsigned char)s[j + 6]))) {
              // YYYY-MM-DD or YYYY.MM.DD
              std::string d = s.substr(i, 4) + s.substr(j + 1, 2) + s.substr(j + 4, 2);
              if (ValidDate(atoi(d.substr(0, 4).c_str()), atoi(d.substr(4, 2).c_str()), atoi(d.substr(6, 2).c_str())))
                 return d;
              }
           i = j;
           }
        else
           i++;
        }
  return "";
}

time_t ParseTimestamp(const std::string &s)
{
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  int y, mo, d, h = 0, mi = 0, se = 0;
  char sep;
  int consumed = 0;
  int n = sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d%n", &y, &mo, &d, &sep, &h, &mi, &se, &consumed);
  if (n < 3)
     return 0;
  if (n < 7) {
     h = mi = se = 0;
     consumed = 0;
     }
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = se;
  // A time zone may follow (fractions of seconds are skipped): "Z" or +hh:mm
  // means UTC with an offset (GitHub). Without a time zone the time is local
  // time - the LibreELEC release scripts write the local file time into
  // releases.json.
  const char *p = s.c_str() + consumed;
  if (consumed) {
     if (*p == '.')
        while (*++p >= '0' && *p <= '9')
              ;
     if (*p == 'Z' || *p == 'z')
        return timegm(&tm);
     if (*p == '+' || *p == '-') {
        int oh = 0, om = 0;
        if (sscanf(p + 1, "%2d:%2d", &oh, &om) >= 1 || sscanf(p + 1, "%2d%2d", &oh, &om) >= 1) {
           int offset = (oh * 60 + om) * 60;
           return timegm(&tm) - (*p == '+' ? offset : -offset);
           }
        }
     }
  tm.tm_isdst = -1;
  return mktime(&tm);
}

std::string UrlDirectory(const std::string &Url)
{
  size_t q = Url.find_first_of("?#");
  std::string u = q == std::string::npos ? Url : Url.substr(0, q);
  size_t schemeEnd = u.find("://");
  size_t slash = u.rfind('/');
  if (slash == std::string::npos || (schemeEnd != std::string::npos && slash < schemeEnd + 3))
     return u + "/";
  return u.substr(0, slash + 1);
}

std::string JsonUrl(const std::string &Base)
{
  std::string b = Trim(Base);
  if (EndsWith(b, ".json"))
     return b;
  if (b.empty() || b[b.size() - 1] != '/')
     b += "/";
  return b + "releases.json";
}

std::string ResolveUrl(const std::string &Base, const std::string &Href)
{
  if (Href.find("://") != std::string::npos)
     return Href;
  size_t schemeEnd = Base.find("://");
  if (StartsWith(Href, "//"))
     return (schemeEnd != std::string::npos ? Base.substr(0, schemeEnd + 1) : std::string("https:")) + Href;
  if (StartsWith(Href, "/")) {
     if (schemeEnd == std::string::npos)
        return Href;
     size_t hostEnd = Base.find('/', schemeEnd + 3);
     return (hostEnd == std::string::npos ? Base : Base.substr(0, hostEnd)) + Href;
     }
  std::string dir = UrlDirectory(Base);
  std::string h = Href;
  while (StartsWith(h, "./"))
        h = h.substr(2);
  return dir + h;
}

static std::string JoinUrl(const std::string &Base, const std::string &SubPath, const std::string &Name)
{
  std::string u = Base;
  if (!u.empty() && u[u.size() - 1] != '/')
     u += "/";
  std::string sp = SubPath;
  while (StartsWith(sp, "/"))
        sp = sp.substr(1);
  if (!sp.empty()) {
     u += sp;
     if (u[u.size() - 1] != '/')
        u += "/";
     }
  return u + Name;
}

std::string GithubApiUrl(const std::string &Spec)
{
  std::string s = Trim(Spec);
  if (StartsWith(s, "https://api.github.com/"))
     return s;
  const char *Prefixes[] = { "https://github.com/", "http://github.com/", "github.com/", NULL };
  for (int i = 0; Prefixes[i]; i++) {
      if (StartsWith(s, Prefixes[i])) {
         s = s.substr(strlen(Prefixes[i]));
         break;
         }
      }
  // keep only owner/repo
  size_t p1 = s.find('/');
  if (p1 == std::string::npos || p1 == 0)
     return "";
  size_t p2 = s.find('/', p1 + 1);
  std::string repo = s.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1);
  if (EndsWith(repo, ".git"))
     repo = repo.substr(0, repo.size() - 4);
  if (repo.empty())
     return "";
  std::string owner = s.substr(0, p1);
  for (size_t i = 0; i < owner.size(); i++)
      if (!isalnum((unsigned char)owner[i]) && owner[i] != '-' && owner[i] != '_' && owner[i] != '.')
         return "";
  for (size_t i = 0; i < repo.size(); i++)
      if (!isalnum((unsigned char)repo[i]) && repo[i] != '-' && repo[i] != '_' && repo[i] != '.')
         return "";
  return "https://api.github.com/repos/" + owner + "/" + repo + "/releases?per_page=50";
}

std::string FormatSize(long long Size)
{
  char buf[32];
  if (Size < 0)
     return "?";
  if (Size >= 1024LL * 1024 * 1024)
     snprintf(buf, sizeof(buf), "%.1f GB", Size / (1024.0 * 1024 * 1024));
  else if (Size >= 1024LL * 1024)
     snprintf(buf, sizeof(buf), "%.0f MB", Size / (1024.0 * 1024));
  else if (Size >= 1024)
     snprintf(buf, sizeof(buf), "%.0f KB", Size / 1024.0);
  else
     snprintf(buf, sizeof(buf), "%lld B", Size);
  return buf;
}

static bool IsHexDigest(const std::string &s)
{
  if (s.size() != 64)
     return false;
  for (size_t i = 0; i < s.size(); i++)
      if (!isxdigit((unsigned char)s[i]))
         return false;
  return true;
}

static std::string Lower(const std::string &s)
{
  std::string r = s;
  for (size_t i = 0; i < r.size(); i++)
      r[i] = tolower((unsigned char)r[i]);
  return r;
}

std::string ParseSha256File(const std::string &Content, const std::string &FileName)
{
  // A file with a single checksum (e.g. <file>.sha256) is used as is, in a
  // list of checksums (e.g. SHA256SUMS) the entry must match the file name.
  std::string first;
  int count = 0;
  size_t pos = 0;
  while (pos <= Content.size()) {
        size_t nl = Content.find('\n', pos);
        std::string line = Trim(Content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? Content.size() + 1 : nl + 1;
        if (line.empty() || line[0] == '#')
           continue;
        size_t sp = line.find_first_of(" \t");
        std::string hash = line.substr(0, sp);
        if (!IsHexDigest(hash))
           continue;
        std::string name = sp == std::string::npos ? "" : Trim(line.substr(sp));
        if (!name.empty() && name[0] == '*')
           name = name.substr(1);
        size_t slash = name.rfind('/');
        if (slash != std::string::npos)
           name = name.substr(slash + 1);
        if (!FileName.empty() && name == FileName)
           return Lower(hash);
        if (first.empty())
           first = Lower(hash);
        count++;
        }
  return count == 1 ? first : "";
}

// --- os-release ----------------------------------------------------------------

static std::string Unquote(const std::string &v)
{
  std::string s = Trim(v);
  if (s.size() >= 2 && (s[0] == '"' || s[0] == '\'') && s[s.size() - 1] == s[0]) {
     char q = s[0];
     std::string r;
     for (size_t i = 1; i + 1 < s.size(); i++) {
         if (q == '"' && s[i] == '\\' && i + 2 < s.size())
            i++;
         r += s[i];
         }
     return r;
     }
  return s;
}

bool ParseOsRelease(const std::string &Content, cSysInfo &Info)
{
  std::map<std::string, std::string> kv;
  size_t pos = 0;
  while (pos < Content.size()) {
        size_t nl = Content.find('\n', pos);
        std::string line = Trim(Content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? Content.size() : nl + 1;
        if (line.empty() || line[0] == '#')
           continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
           continue;
        kv[Trim(line.substr(0, eq))] = Unquote(line.substr(eq + 1));
        }
  Info.distName = kv["NAME"];
  Info.version = kv["VERSION"];
  Info.versionId = kv["VERSION_ID"];
  const char *Prefixes[] = { "DISTRO_", "LIBREELEC_", "COREELEC_", NULL };
  for (int i = 0; Prefixes[i]; i++) {
      std::string p = Prefixes[i];
      if (Info.arch.empty())    Info.arch = kv[p + "ARCH"];
      if (Info.project.empty()) Info.project = kv[p + "PROJECT"];
      if (Info.device.empty())  Info.device = kv[p + "DEVICE"];
      if (Info.build.empty())   Info.build = kv[p + "BUILD"];
      }
  if (Info.arch.empty()) {
     // any other *_ARCH key (derived distributions)
     for (std::map<std::string, std::string>::const_iterator it = kv.begin(); it != kv.end(); ++it) {
         if (EndsWith(it->first, "_ARCH") && !it->second.empty()) {
            Info.arch = it->second;
            break;
            }
         }
     }
  return !Info.arch.empty();
}

// --- releases.json -----------------------------------------------------------

static long long JsonSize(const cJsonValue *v)
{
  if (!v)
     return -1;
  if (v->IsNumber())
     return (long long)v->Number();
  if (v->IsString() && !v->String().empty())
     return atoll(v->String().c_str());
  return -1;
}

static std::string LastPathComponent(const std::string &s)
{
  std::string t = s;
  while (!t.empty() && t[t.size() - 1] == '/')
        t.erase(t.size() - 1);
  size_t p = t.rfind('/');
  return p == std::string::npos ? t : t.substr(p + 1);
}

static bool AddJsonEntry(const cJsonValue &Entry, const std::string &Channel, const std::string &ChannelUrl, const std::string &PrettyRegex, const std::string &Arch, const std::string &Board, bool IsUboot, bool Local, std::vector<cReleaseInfo> &Releases)
{
  if (!Entry.IsObject())
     return false;
  cReleaseInfo r;
  r.name = Entry.GetString("name");
  if (r.name.empty() || !IsUpdateFile(r.name) || r.name.find('/') != std::string::npos)
     return false;
  std::string subpath = Entry.GetString("subpath");
  if (Local) {
     // stay below the directory of releases.json
     std::string p = "/" + subpath + "/";
     if (p.find("/../") != std::string::npos)
        return false;
     r.local = true;
     }
  if (IsUboot) {
     r.board = LastPathComponent(subpath);
     if (r.board.empty()) {
        // derive from file name: ...-<board>.img.gz
        std::string base = StripExtension(r.name);
        size_t dash = base.rfind('-');
        if (dash != std::string::npos)
           r.board = base.substr(dash + 1);
        }
     if (!Board.empty() && r.board != Board)
        return false;
     }
  r.channel = Channel;
  r.url = Local ? JoinUrl(ChannelUrl, EncodePath(subpath), EncodePath(r.name)) : JoinUrl(ChannelUrl, subpath, r.name);
  r.sha256 = Lower(Entry.GetString("sha256"));
  if (!IsHexDigest(r.sha256))
     r.sha256.clear();
  r.size = JsonSize(Entry.Get("size"));
  r.timestamp = ParseTimestamp(Entry.GetString("timestamp"));
  std::string label;
  if (!PrettyRegex.empty() && RegexSearch(r.name, PrettyRegex, &label) && !label.empty())
     r.label = label;
  else
     r.label = LabelFromName(r.name, Arch);
  if (IsUboot && Board.empty() && !r.board.empty())
     r.label += " (" + r.board + ")";
  Releases.push_back(r);
  return true;
}

bool ParseReleasesJson(const std::string &Json, const std::string &BaseUrl, const std::string &Arch, const std::string &ChannelFilter, const std::string &Board, std::vector<cReleaseInfo> &Releases, std::string &Error, bool Local)
{
  cJsonValue root;
  if (!JsonParse(Json, root, Error))
     return false;
  if (!root.IsObject()) {
     Error = "releases.json: top level is not an object";
     return false;
     }
  std::string base = UrlDirectory(JsonUrl(BaseUrl));
  bool channelFound = ChannelFilter.empty();
  for (size_t c = 0; c < root.Object().size(); c++) {
      const std::string &channel = root.Object()[c].first;
      const cJsonValue &ch = root.Object()[c].second;
      if (!ch.IsObject())
         continue;
      if (!ChannelFilter.empty() && channel != ChannelFilter)
         continue;
      channelFound = true;
      // files of a local releases.json are always relative to its directory
      std::string chUrl = Local ? base : ch.GetString("url", base);
      std::string pretty = ch.GetString("prettyname_regex");
      const cJsonValue *project = ch.Get("project");
      const cJsonValue *proj = project ? project->Get(Arch) : NULL;
      const cJsonValue *rels = proj ? proj->Get("releases") : NULL;
      if (!rels)
         continue;
      std::vector<const cJsonValue *> entries;
      if (rels->IsObject()) {
         for (size_t i = 0; i < rels->Object().size(); i++)
             entries.push_back(&rels->Object()[i].second);
         }
      else if (rels->IsArray()) {
         for (size_t i = 0; i < rels->Array().size(); i++)
             entries.push_back(&rels->Array()[i]);
         }
      for (size_t i = 0; i < entries.size(); i++) {
          const cJsonValue *e = entries[i];
          const cJsonValue *file = e->Get("file");
          const cJsonValue *image = e->Get("image");
          const cJsonValue *uboot = e->Get("uboot");
          // prefer the update tarball, like the LibreELEC settings add-on
          if (file && AddJsonEntry(*file, channel, chUrl, pretty, Arch, Board, false, Local, Releases))
             continue;
          if (image && AddJsonEntry(*image, channel, chUrl, pretty, Arch, Board, false, Local, Releases))
             continue;
          if (uboot && uboot->IsArray()) {
             for (size_t u = 0; u < uboot->Array().size(); u++)
                 AddJsonEntry(uboot->Array()[u], channel, chUrl, pretty, Arch, Board, true, Local, Releases);
             }
          }
      }
  if (!channelFound) {
     Error = "channel '" + ChannelFilter + "' not found";
     return false;
     }
  return true;
}

// --- GitHub ------------------------------------------------------------------

static void ParseGithubRelease(const cJsonValue &Rel, const std::string &Arch, const std::string &ChannelFilter, const std::string &Filter, std::vector<cReleaseInfo> &Releases)
{
  if (!Rel.IsObject())
     return;
  const cJsonValue *draft = Rel.Get("draft");
  if (draft && draft->Bool())
     return;
  std::string tag = Rel.GetString("tag_name");
  if (!ChannelFilter.empty() && tag.find(ChannelFilter) == std::string::npos)
     return;
  const cJsonValue *pre = Rel.Get("prerelease");
  time_t published = ParseTimestamp(Rel.GetString("published_at"));
  const cJsonValue *assets = Rel.Get("assets");
  if (!assets || !assets->IsArray())
     return;
  std::map<std::string, std::string> urls;
  for (size_t i = 0; i < assets->Array().size(); i++) {
      const cJsonValue &a = assets->Array()[i];
      urls[a.GetString("name")] = a.GetString("browser_download_url");
      }
  size_t first = Releases.size();
  std::vector<cAddonInfo> addons;
  for (size_t i = 0; i < assets->Array().size(); i++) {
      const cJsonValue &a = assets->Array()[i];
      std::string name = a.GetString("name");
      cAddonInfo ad;
      if (!IsUpdateFile(name) && ParseAddonName(name, Arch, ad.id, ad.version)) {
         ad.name = name;
         ad.url = a.GetString("browser_download_url");
         ad.size = JsonSize(a.Get("size"));
         std::string digest = a.GetString("digest");
         if (StartsWith(digest, "sha256:") && IsHexDigest(digest.substr(7)))
            ad.sha256 = Lower(digest.substr(7));
         else if (urls.count(name + ".sha256"))
            ad.sha256Url = urls[name + ".sha256"];
         if (!ad.url.empty())
            addons.push_back(ad);
         continue;
         }
      if (!IsUpdateFile(name) || !MatchesArch(name, Arch, Filter))
         continue;
      cReleaseInfo r;
      r.channel = tag;
      r.name = name;
      r.url = a.GetString("browser_download_url");
      if (r.url.empty())
         continue;
      r.size = JsonSize(a.Get("size"));
      r.timestamp = ParseTimestamp(a.GetString("updated_at"));
      if (!r.timestamp)
         r.timestamp = published;
      r.prerelease = pre && pre->Bool();
      std::string digest = a.GetString("digest");
      if (StartsWith(digest, "sha256:") && IsHexDigest(digest.substr(7)))
         r.sha256 = Lower(digest.substr(7));
      if (r.sha256.empty()) {
         std::map<std::string, std::string>::const_iterator it = urls.find(name + ".sha256");
         if (it != urls.end())
            r.sha256Url = it->second;
         }
      r.label = LabelFromName(name, Arch);
      Releases.push_back(r);
      }
  AttachAddons(Releases, first, addons);
}

bool ParseGithubReleases(const std::string &Json, const std::string &Arch, const std::string &ChannelFilter, const std::string &Filter, std::vector<cReleaseInfo> &Releases, std::string &Error)
{
  cJsonValue root;
  if (!JsonParse(Json, root, Error))
     return false;
  if (root.IsArray()) {
     for (size_t i = 0; i < root.Array().size(); i++)
         ParseGithubRelease(root.Array()[i], Arch, ChannelFilter, Filter, Releases);
     return true;
     }
  if (root.IsObject()) {
     if (!root.Get("tag_name")) {
        std::string msg = root.GetString("message");
        Error = msg.empty() ? "unexpected GitHub API response" : "GitHub: " + msg;
        return false;
        }
     ParseGithubRelease(root, Arch, ChannelFilter, Filter, Releases);
     return true;
     }
  Error = "unexpected GitHub API response";
  return false;
}

static const char *ChecksumListNames[] = { "SHA256SUMS", "sha256sums", "SHA256SUMS.txt", "sha256sums.txt", NULL };

static std::string ChecksumList(const std::set<std::string> &Names)
{
  for (int i = 0; ChecksumListNames[i]; i++)
      if (Names.count(ChecksumListNames[i]))
         return ChecksumListNames[i];
  return "";
}

// --- Directory listing ---------------------------------------------------------

static std::string HtmlUnescape(const std::string &s)
{
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '&') {
         if (s.compare(i, 5, "&amp;") == 0)  { r += '&'; i += 4; continue; }
         if (s.compare(i, 6, "&quot;") == 0) { r += '"'; i += 5; continue; }
         if (s.compare(i, 4, "&lt;") == 0)   { r += '<'; i += 3; continue; }
         if (s.compare(i, 4, "&gt;") == 0)   { r += '>'; i += 3; continue; }
         if (s.compare(i, 5, "&#39;") == 0)  { r += '\''; i += 4; continue; }
         }
      r += s[i];
      }
  return r;
}

static std::string UrlDecode(const std::string &s)
{
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
         r += char(strtol(s.substr(i + 1, 2).c_str(), NULL, 16));
         i += 2;
         }
      else
         r += s[i];
      }
  return r;
}

bool ParseIndexHtml(const std::string &Html, const std::string &BaseUrl, const std::string &Arch, const std::string &Filter, const std::string &Channel, std::vector<cReleaseInfo> &Releases)
{
  std::string base = BaseUrl;
  if (!base.empty() && base[base.size() - 1] != '/' && !EndsWith(base, ".html") && !EndsWith(base, ".htm"))
     base += "/";
  std::vector<std::string> hrefs;
  std::set<std::string> names;
  size_t pos = 0;
  while ((pos = Html.find("href", pos)) != std::string::npos) {
        pos += 4;
        size_t p = pos;
        while (p < Html.size() && (Html[p] == ' ' || Html[p] == '\t'))
              p++;
        if (p >= Html.size() || Html[p] != '=')
           continue;
        p++;
        while (p < Html.size() && (Html[p] == ' ' || Html[p] == '\t'))
              p++;
        if (p >= Html.size())
           break;
        char q = Html[p];
        size_t end;
        if (q == '"' || q == '\'') {
           end = Html.find(q, p + 1);
           if (end == std::string::npos)
              break;
           p++;
           }
        else
           end = Html.find_first_of(" \t>", p);
        if (end == std::string::npos)
           break;
        std::string href = HtmlUnescape(Html.substr(p, end - p));
        pos = end;
        size_t q2 = href.find_first_of("?#");
        if (q2 != std::string::npos)
           href = href.substr(0, q2);
        if (href.empty())
           continue;
        hrefs.push_back(href);
        names.insert(UrlDecode(LastPathComponent(href)));
        }
  std::string list = ChecksumList(names);
  std::set<std::string> seen;
  size_t first = Releases.size();
  std::vector<cAddonInfo> addons;
  for (size_t i = 0; i < hrefs.size(); i++) {
      std::string name = UrlDecode(LastPathComponent(hrefs[i]));
      cAddonInfo ad;
      if (!IsUpdateFile(name) && !seen.count(name) && ParseAddonName(name, Arch, ad.id, ad.version)) {
         seen.insert(name);
         ad.name = name;
         ad.url = ResolveUrl(base, hrefs[i]);
         if (names.count(name + ".sha256"))
            ad.sha256Url = ad.url + ".sha256";
         else if (!list.empty())
            ad.sha256Url = ResolveUrl(base, list);
         addons.push_back(ad);
         continue;
         }
      if (!IsUpdateFile(name) || !MatchesArch(name, Arch, Filter) || seen.count(name))
         continue;
      seen.insert(name);
      cReleaseInfo r;
      r.channel = Channel;
      r.name = name;
      r.url = ResolveUrl(base, hrefs[i]);
      if (names.count(name + ".sha256"))
         r.sha256Url = r.url + ".sha256";
      else if (!list.empty())
         r.sha256Url = ResolveUrl(base, list);
      r.label = LabelFromName(name, Arch);
      Releases.push_back(r);
      }
  AttachAddons(Releases, first, addons);
  return true;
}

// --- Local directory -------------------------------------------------------------

std::string PathToFileUrl(const std::string &Path)
{
  return "file://" + EncodePath(Path);
}

std::string EncodePath(const std::string &Path)
{
  static const char *hex = "0123456789ABCDEF";
  std::string r;
  for (size_t i = 0; i < Path.size(); i++) {
      unsigned char c = Path[i];
      if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
         r += char(c);
      else {
         r += '%';
         r += hex[c >> 4];
         r += hex[c & 0x0F];
         }
      }
  return r;
}

std::string LocalPath(const std::string &Spec)
{
  std::string s = Trim(Spec);
  if (StartsWith(s, "file://"))
     s = UrlDecode(s.substr(7));
  if (s.empty() || s[0] != '/')
     return "";
  while (s.size() > 1 && s[s.size() - 1] == '/')
        s.erase(s.size() - 1);
  return s;
}

bool IsFileSourceUrl(const std::string &Url)
{
  return StartsWith(Url, "file:///") || StartsWith(Url, "smb://");
}

bool ParseLocalDirectory(const std::vector<cLocalFile> &Files, const std::string &BaseUrl, const std::string &Arch, const std::string &Filter, const std::string &Channel, std::vector<cReleaseInfo> &Releases)
{
  if (!IsFileSourceUrl(BaseUrl))
     return false;
  std::string dir = BaseUrl;
  if (dir[dir.size() - 1] != '/')
     dir += "/";
  std::set<std::string> names;
  for (size_t i = 0; i < Files.size(); i++)
      names.insert(Files[i].name);
  std::string list = ChecksumList(names);
  size_t first = Releases.size();
  std::vector<cAddonInfo> addons;
  for (size_t i = 0; i < Files.size(); i++) {
      const cLocalFile &f = Files[i];
      cAddonInfo ad;
      if (!IsUpdateFile(f.name) && ParseAddonName(f.name, Arch, ad.id, ad.version)) {
         ad.name = f.name;
         ad.url = dir + EncodePath(f.name);
         ad.size = f.size;
         if (names.count(f.name + ".sha256"))
            ad.sha256Url = dir + EncodePath(f.name + ".sha256");
         else if (!list.empty())
            ad.sha256Url = dir + EncodePath(list);
         addons.push_back(ad);
         continue;
         }
      if (!IsUpdateFile(f.name) || !MatchesArch(f.name, Arch, Filter))
         continue;
      cReleaseInfo r;
      r.channel = Channel;
      r.name = f.name;
      r.url = dir + EncodePath(f.name);
      r.size = f.size;
      r.timestamp = f.mtime;
      r.local = true;
      if (names.count(f.name + ".sha256"))
         r.sha256Url = dir + EncodePath(f.name + ".sha256");
      else if (!list.empty())
         r.sha256Url = dir + EncodePath(list);
      r.label = LabelFromName(f.name, Arch);
      Releases.push_back(r);
      }
  AttachAddons(Releases, first, addons);
  return true;
}

// --- SMB URLs --------------------------------------------------------------------

bool ParseSmbUrl(const std::string &Url, cSmbUrl &Result)
{
  // smb://[[domain;]user[:password]@]server/share[/path]
  std::string s = Trim(Url);
  if (!StartsWith(s, "smb://"))
     return false;
  s = s.substr(6);
  Result = cSmbUrl();
  size_t slash = s.find('/');
  size_t at = s.rfind('@', slash == std::string::npos ? std::string::npos : slash);
  if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
     std::string auth = s.substr(0, at);
     s = s.substr(at + 1);
     size_t semi = auth.find(';');
     if (semi != std::string::npos) {
        Result.domain = UrlDecode(auth.substr(0, semi));
        auth = auth.substr(semi + 1);
        }
     size_t colon = auth.find(':');
     Result.user = UrlDecode(auth.substr(0, colon));
     if (colon != std::string::npos)
        Result.password = UrlDecode(auth.substr(colon + 1));
     }
  while (!s.empty() && s[s.size() - 1] == '/')
        s.erase(s.size() - 1);
  slash = s.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= s.size())
     return false; // server and share are required
  Result.server = s.substr(0, slash);
  Result.url = "smb://" + s;
  return true;
}

std::string RedactUrl(const std::string &Url)
{
  cSmbUrl u;
  if (ParseSmbUrl(Url, u) && !u.password.empty())
     return "smb://" + (u.domain.empty() ? "" : u.domain + ";") + u.user + ":***@" + u.url.substr(6);
  return Url;
}

// --- Build dates -------------------------------------------------------------------

static bool ExtractDateTime(const std::string &s, time_t &Time, bool &HasTime)
{
  // YYYYMMDDhhmmss (as used in LibreELEC devel versions), interpreted as local time
  size_t i = 0;
  while (i < s.size()) {
        if (!isdigit((unsigned char)s[i])) {
           i++;
           continue;
           }
        size_t j = i;
        while (j < s.size() && isdigit((unsigned char)s[j]))
              j++;
        if (j - i == 14) {
           struct tm tm;
           memset(&tm, 0, sizeof(tm));
           tm.tm_year = atoi(s.substr(i, 4).c_str()) - 1900;
           tm.tm_mon = atoi(s.substr(i + 4, 2).c_str()) - 1;
           tm.tm_mday = atoi(s.substr(i + 6, 2).c_str());
           tm.tm_hour = atoi(s.substr(i + 8, 2).c_str());
           tm.tm_min = atoi(s.substr(i + 10, 2).c_str());
           tm.tm_sec = atoi(s.substr(i + 12, 2).c_str());
           tm.tm_isdst = -1;
           if (ValidDate(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) && tm.tm_hour < 24 && tm.tm_min < 60 && tm.tm_sec < 61) {
              Time = mktime(&tm);
              HasTime = true;
              return true;
              }
           }
        i = j;
        }
  std::string d = ExtractDate(s);
  if (d.empty())
     return false;
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = atoi(d.substr(0, 4).c_str()) - 1900;
  tm.tm_mon = atoi(d.substr(4, 2).c_str()) - 1;
  tm.tm_mday = atoi(d.substr(6, 2).c_str());
  tm.tm_hour = 12; // avoid date changes by time zones
  tm.tm_isdst = -1;
  Time = mktime(&tm);
  HasTime = false;
  return true;
}

bool InstalledBuildDate(const cSysInfo &Info, time_t &Time, bool &HasTime)
{
  time_t t;
  bool hasTime;
  if (ExtractDateTime(Info.version, t, hasTime)) {
     if (!hasTime && Info.buildTime) {
        // the image file time is more precise if it is the same day
        struct tm a, b;
        localtime_r(&t, &a);
        localtime_r(&Info.buildTime, &b);
        if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) {
           t = Info.buildTime;
           hasTime = true;
           }
        }
     Time = t;
     HasTime = hasTime;
     return true;
     }
  if (Info.buildTime) {
     Time = Info.buildTime;
     HasTime = true;
     return true;
     }
  return false;
}

bool ReleaseBuildDate(const cReleaseInfo &Release, time_t &Time, bool &HasTime)
{
  if (ExtractDateTime(Release.name, Time, HasTime) && HasTime)
     return true;
  if (Release.timestamp) {
     Time = Release.timestamp;
     HasTime = true;
     return true;
     }
  if (ExtractDateTime(Release.label, Time, HasTime) || ExtractDateTime(Release.name, Time, HasTime) || ExtractDateTime(Release.channel, Time, HasTime))
     return true;
  return false;
}

std::string FormatBuildDate(time_t Time, bool HasTime)
{
  char buf[32];
  struct tm tm;
  localtime_r(&Time, &tm);
  strftime(buf, sizeof(buf), HasTime ? "%d.%m.%Y %H:%M" : "%d.%m.%Y", &tm);
  return buf;
}

// --- Addons, versions, time windows ----------------------------------------------

static bool IsVersionString(const std::string &s)
{
  if (s.empty() || !isdigit((unsigned char)s[0]))
     return false;
  for (size_t i = 0; i < s.size(); i++)
      if (!isdigit((unsigned char)s[i]) && s[i] != '.')
         return false;
  return true;
}

bool ParseAddonName(const std::string &Name, const std::string &Arch, std::string &Id, std::string &Version)
{
  std::string base;
  if (EndsWith(Name, ".zip"))
     base = Name.substr(0, Name.size() - 4);
  else if (EndsWith(Name, ".tar.gz"))
     base = Name.substr(0, Name.size() - 7);
  else
     return false;
  if (Arch.empty())
     return false;
  size_t p = 0;
  for (;;) {
      p = base.find(Arch + "-", p);
      if (p == std::string::npos)
         return false;
      if (p == 0 || base[p - 1] == '-')
         break;
      p++;
      }
  std::string rest = base.substr(p + Arch.size() + 1);
  size_t dash = rest.rfind('-');
  if (dash == std::string::npos || dash == 0)
     return false;
  Version = rest.substr(dash + 1);
  Id = rest.substr(0, dash);
  if (!IsVersionString(Version))
     return false;
  // the id must be a plain name (it becomes a directory name)
  for (size_t i = 0; i < Id.size(); i++)
      if (!isalnum((unsigned char)Id[i]) && Id[i] != '.' && Id[i] != '-' && Id[i] != '_')
         return false;
  return Id[0] != '.' && Id.find("..") == std::string::npos;
}

static std::string MajorMinor(const std::string &Version)
{
  std::string v = VersionPrefix(Version);
  size_t dot = v.find('.');
  if (dot == std::string::npos)
     return v;
  size_t dot2 = v.find('.', dot + 1);
  return dot2 == std::string::npos ? v : v.substr(0, dot2);
}

void AttachAddons(std::vector<cReleaseInfo> &Releases, size_t First, const std::vector<cAddonInfo> &Addons)
{
  std::map<std::string, std::vector<const cAddonInfo *> > byId;
  for (size_t i = 0; i < Addons.size(); i++)
      byId[Addons[i].id].push_back(&Addons[i]);
  for (std::map<std::string, std::vector<const cAddonInfo *> >::const_iterator it = byId.begin(); it != byId.end(); ++it) {
      const std::vector<const cAddonInfo *> &cand = it->second;
      std::set<std::string> addonVersions;
      for (size_t i = 0; i < cand.size(); i++)
          addonVersions.insert(MajorMinor(cand[i]->version));
      std::set<std::string> releaseVersions;
      for (size_t r = First; r < Releases.size(); r++)
          releaseVersions.insert(MajorMinor(Releases[r].label));
      // the newest release without an addon of the same major.minor version
      // gets the newest addon version that doesn't belong to another release
      std::string newestUnmatched;
      for (size_t r = First; r < Releases.size(); r++) {
          std::string mm = MajorMinor(Releases[r].label);
          if (!addonVersions.count(mm) && (newestUnmatched.empty() || strverscmp(mm.c_str(), newestUnmatched.c_str()) > 0))
             newestUnmatched = mm;
          }
      for (size_t r = First; r < Releases.size(); r++) {
          std::string mm = MajorMinor(Releases[r].label);
          const cAddonInfo *best = NULL;
          for (size_t i = 0; i < cand.size(); i++) {
              const cAddonInfo *a = cand[i];
              std::string amm = MajorMinor(a->version);
              bool fits = addonVersions.count(mm) ? amm == mm : (mm == newestUnmatched && !releaseVersions.count(amm));
              if (fits && (!best || strverscmp(a->version.c_str(), best->version.c_str()) > 0))
                 best = a;
              }
          if (best)
             Releases[r].addons.push_back(*best);
          }
      }
}

int MajorVersion(const std::string &Version)
{
  std::string v = VersionPrefix(Trim(Version));
  if (v.empty())
     return -1;
  return atoi(v.c_str());
}

bool InTimeWindow(int Start, int End, int Now)
{
  if (Start == End)
     return true;
  if (Start < End)
     return Now >= Start && Now < End;
  return Now >= Start || Now < End; // over midnight
}

// --- Version comparison --------------------------------------------------------

static std::string VersionPrefix(const std::string &s)
{
  size_t i = 0;
  while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.'))
        i++;
  std::string v = s.substr(0, i);
  while (!v.empty() && v[v.size() - 1] == '.')
        v.erase(v.size() - 1);
  return v;
}

static int VersionCompare(const std::string &a, const std::string &b)
{
  int r = strverscmp(a.c_str(), b.c_str());
  return r < 0 ? -1 : r > 0 ? 1 : 0;
}

std::string ReleaseKey(const cReleaseInfo &r)
{
  // YYYYMMDDHHMMSS from the timestamp if known, else from a date in the
  // label, file name or channel (GitHub tag)
  if (r.timestamp) {
     char buf[32];
     struct tm tm;
     localtime_r(&r.timestamp, &tm);
     strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
     return buf;
     }
  std::string d = ExtractDate(r.label);
  if (d.empty())
     d = ExtractDate(r.name);
  if (d.empty())
     d = ExtractDate(r.channel);
  return d.empty() ? "" : d + "000000";
}

static std::string StripBoard(const std::string &Label)
{
  size_t paren = Label.find(" (");
  return paren == std::string::npos ? Label : Label.substr(0, paren);
}

eCompareResult CompareWithInstalled(const cReleaseInfo &Release, const cSysInfo &Info)
{
  // 1. we know which file was installed by this plugin
  if (!Info.installedName.empty()) {
     if (Release.name == Info.installedName)
        return crSame;
     std::string k = ReleaseKey(Release);
     if (!k.empty() && !Info.installedKey.empty() && k != Info.installedKey)
        return k > Info.installedKey ? crNewer : crOlder;
     }
  const std::string &installed = Info.version;
  if (installed.empty())
     return crUnknown;
  std::string label = StripBoard(Release.label);
  if (label == installed || (installed.size() >= 8 && Release.name.find("-" + installed + ".") != std::string::npos)
                         || (installed.size() >= 8 && Release.name.find("-" + installed + "-") != std::string::npos))
     return crSame;
  // 2. both carry a full date
  std::string dr = ExtractDate(label);
  if (dr.empty())
     dr = ExtractDate(Release.name);
  std::string di = ExtractDate(installed);
  if (!dr.empty() && !di.empty()) {
     if (dr != di)
        return dr > di ? crNewer : crOlder;
     // same day: different build of the same day
     if (Release.timestamp && Info.buildTime)
        return Release.timestamp > Info.buildTime + 3600 ? crNewer : crOlder;
     return crUnknown;
     }
  // 3. release versions without dates
  std::string vr = VersionPrefix(label);
  std::string vi = VersionPrefix(installed);
  if (dr.empty() && di.empty() && !vr.empty() && !vi.empty() && vr != vi)
     return VersionCompare(vr, vi) > 0 ? crNewer : crOlder;
  // 4. different major.minor decides
  std::string mr = vr.substr(0, vr.find('.', vr.find('.') == std::string::npos ? vr.size() : vr.find('.') + 1));
  std::string mi = vi.substr(0, vi.find('.', vi.find('.') == std::string::npos ? vi.size() : vi.find('.') + 1));
  if (!mr.empty() && !mi.empty() && mr != mi)
     return VersionCompare(mr, mi) > 0 ? crNewer : crOlder;
  // 5. compare the release date with the build time of the installed system
  if (Info.buildTime) {
     std::string k = ReleaseKey(Release);
     if (!k.empty()) {
        // allow one day of tolerance, builds are uploaded after they were made
        time_t t = Release.timestamp ? Release.timestamp : ParseTimestamp(k.substr(0, 4) + "-" + k.substr(4, 2) + "-" + k.substr(6, 2));
        if (t > Info.buildTime + 86400)
           return crNewer;
        if (t < Info.buildTime - 86400)
           return crOlder;
        return crUnknown;
        }
     }
  if (dr.empty() && di.empty()) {
     int c = VersionCompare(label, installed);
     return c > 0 ? crNewer : c < 0 ? crOlder : crSame;
     }
  return crUnknown;
}

// --- Sorting -----------------------------------------------------------------

void SortReleases(std::vector<cReleaseInfo> &Releases)
{
  std::map<std::string, size_t> order;
  for (size_t i = 0; i < Releases.size(); i++) {
      std::string k = Releases[i].server + "\n" + Releases[i].channel;
      if (!order.count(k)) {
         size_t n = order.size();
         order[k] = n;
         }
      }
  std::stable_sort(Releases.begin(), Releases.end(), [&order](const cReleaseInfo &a, const cReleaseInfo &b) {
    size_t oa = order[a.server + "\n" + a.channel];
    size_t ob = order[b.server + "\n" + b.channel];
    if (oa != ob)
       return oa < ob;
    std::string ka = ReleaseKey(a), kb = ReleaseKey(b);
    if (ka != kb)
       return ka > kb;
    return strverscmp(a.label.c_str(), b.label.c_str()) > 0;
    });
}
