/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * test_parsers.c: Unit tests for the VDR independent parts
 */

#include "../json.h"
#include "../parsers.h"
#include "../sha256.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::string LocalDigits(time_t t)
{
  char buf[32];
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
  return buf;
}

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; std::string _a = (a), _b = (b); if (_a != _b) { failures++; fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, _b.c_str(), _a.c_str()); } } while (0)

static void TestJson(void)
{
  cJsonValue v;
  std::string err;
  CHECK(JsonParse("{\"a\": [1, 2.5, true, null, \"x\\u00e4\\n\"], \"b\": {\"c\": \"d\"}}", v, err));
  CHECK(v.IsObject());
  CHECK(v.Get("a") && v.Get("a")->Array().size() == 5);
  CHECK_EQ(v.Get("a")->Array()[0].AsString(), "1");
  CHECK_EQ(v.Get("a")->Array()[1].AsString(), "2.5");
  CHECK_EQ(v.Get("a")->Array()[4].String(), "x\xc3\xa4\n");
  CHECK_EQ(v.Get("b")->GetString("c"), "d");
  CHECK(!JsonParse("{\"a\": }", v, err));
  CHECK(!err.empty());
  CHECK(!JsonParse("[1, 2", v, err));
  CHECK(!JsonParse("{} x", v, err));
  CHECK(JsonParse("\"\\ud83d\\ude00\"", v, err));
  CHECK_EQ(v.String(), "\xf0\x9f\x98\x80");
}

static void TestSha256(void)
{
  cSha256 s;
  CHECK_EQ(s.HexDigest(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  s.Reset();
  s.Update("abc", 3);
  CHECK_EQ(s.HexDigest(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  s.Reset();
  const char *t = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  // feed in odd chunks to exercise buffering
  s.Update(t, 5);
  s.Update(t + 5, strlen(t) - 5);
  CHECK_EQ(s.HexDigest(), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  s.Reset();
  std::string million(1000000, 'a');
  s.Update(million.data(), million.size());
  CHECK_EQ(s.HexDigest(), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void TestOsRelease(void)
{
  const char *osr =
    "NAME=\"LibreELEC\"\n"
    "VERSION=\"13.0-devel-20261020103000-abc1234\"\n"
    "ID=\"libreelec\"\n"
    "VERSION_ID=\"13.0\"\n"
    "# comment\n"
    "LIBREELEC_ARCH=\"RPi4.aarch64\"\n"
    "LIBREELEC_BUILD=\"devel\"\n"
    "LIBREELEC_PROJECT=\"RPi\"\n"
    "LIBREELEC_DEVICE=\"RPi4\"\n";
  cSysInfo info;
  CHECK(ParseOsRelease(osr, info));
  CHECK_EQ(info.distName, "LibreELEC");
  CHECK_EQ(info.version, "13.0-devel-20261020103000-abc1234");
  CHECK_EQ(info.versionId, "13.0");
  CHECK_EQ(info.arch, "RPi4.aarch64");
  CHECK_EQ(info.project, "RPi");
  CHECK_EQ(info.device, "RPi4");
  CHECK_EQ(info.build, "devel");
  cSysInfo ce;
  CHECK(ParseOsRelease("NAME=CoreELEC\nCOREELEC_ARCH='Amlogic-ng.arm'\n", ce));
  CHECK_EQ(ce.arch, "Amlogic-ng.arm");
  cSysInfo none;
  CHECK(!ParseOsRelease("NAME=Ubuntu\n", none));
}

static const char *ReleasesJson =
  "{\n"
  " \"LibreELEC-12.0\": {\n"
  "  \"canary\": 21,\n"
  "  \"prettyname_regex\": \"^LibreELEC-.*-([0-9]+\\\\.[0-9]+\\\\.[0-9]+)\",\n"
  "  \"project\": {\n"
  "   \"RPi4.aarch64\": {\n"
  "    \"displayName\": \"Raspberry Pi 4\",\n"
  "    \"releases\": {\n"
  "     \"0\": {\"file\": {\"name\": \"LibreELEC-RPi4.aarch64-12.0.0.tar\", \"sha256\": \"" "0000000000000000000000000000000000000000000000000000000000000000" "\", \"size\": \"150000000\", \"subpath\": \"12.0/RPi\", \"timestamp\": \"2024-04-10 12:00:00\"},\n"
  "            \"image\": {\"name\": \"LibreELEC-RPi4.aarch64-12.0.0.img.gz\", \"sha256\": \"x\", \"size\": \"1\", \"subpath\": \"12.0/RPi\", \"timestamp\": \"2024-04-10 12:00:00\"}},\n"
  "     \"1\": {\"file\": {\"name\": \"LibreELEC-RPi4.aarch64-12.0.1.tar\", \"sha256\": \"" "1111111111111111111111111111111111111111111111111111111111111111" "\", \"size\": 150000001, \"subpath\": \"12.0/RPi\", \"timestamp\": \"2024-06-01 08:30:00\"}}\n"
  "    }\n"
  "   },\n"
  "   \"Generic.x86_64\": {\"releases\": {\"0\": {\"file\": {\"name\": \"LibreELEC-Generic.x86_64-12.0.0.tar\"}}}}\n"
  "  }\n"
  " },\n"
  " \"LibreELEC-12.2\": {\n"
  "  \"url\": \"https://test.example.org/\",\n"
  "  \"project\": {\n"
  "   \"RPi4.aarch64\": {\"releases\": {\"0\": {\"image\": {\"name\": \"LibreELEC-RPi4.aarch64-12.2-nightly-20251005-7adb5c2.img.gz\", \"sha256\": \"" "2222222222222222222222222222222222222222222222222222222222222222" "\", \"size\": \"200\", \"subpath\": \"12.2/RPi/RPi4\", \"timestamp\": \"2025-10-05 08:50:14\"}}}},\n"
  "   \"A64.aarch64\": {\"releases\": {\"0\": {\"uboot\": [\n"
  "      {\"name\": \"LibreELEC-A64.aarch64-12.2-nightly-20250905-cc764b5-orangepi-win.img.gz\", \"subpath\": \"12.2/Allwinner/orangepi-win\", \"sha256\": \"" "3333333333333333333333333333333333333333333333333333333333333333" "\"},\n"
  "      {\"name\": \"LibreELEC-A64.aarch64-12.2-nightly-20250905-cc764b5-pine64.img.gz\", \"subpath\": \"12.2/Allwinner/pine64\"}]}}}\n"
  "  }\n"
  " }\n"
  "}\n";

static void TestReleasesJson(void)
{
  std::vector<cReleaseInfo> r;
  std::string err;
  CHECK(ParseReleasesJson(ReleasesJson, "https://releases.example.org", "RPi4.aarch64", "", "", r, err));
  CHECK(r.size() == 3);
  if (r.size() == 3) {
     CHECK_EQ(r[0].channel, "LibreELEC-12.0");
     CHECK_EQ(r[0].name, "LibreELEC-RPi4.aarch64-12.0.0.tar"); // tar preferred over image
     CHECK_EQ(r[0].url, "https://releases.example.org/12.0/RPi/LibreELEC-RPi4.aarch64-12.0.0.tar");
     CHECK_EQ(r[0].label, "12.0.0");
     CHECK(r[0].size == 150000000);
     CHECK(r[1].size == 150000001);
     CHECK_EQ(r[1].sha256, "1111111111111111111111111111111111111111111111111111111111111111");
     CHECK_EQ(r[2].url, "https://test.example.org/12.2/RPi/RPi4/LibreELEC-RPi4.aarch64-12.2-nightly-20251005-7adb5c2.img.gz");
     CHECK_EQ(r[2].label, "12.2-nightly-20251005-7adb5c2");
     CHECK(r[2].timestamp == ParseTimestamp("2025-10-05 08:50:14"));
     }
  // channel filter
  r.clear();
  CHECK(ParseReleasesJson(ReleasesJson, "https://releases.example.org/releases.json", "RPi4.aarch64", "LibreELEC-12.2", "", r, err));
  CHECK(r.size() == 1);
  r.clear();
  CHECK(!ParseReleasesJson(ReleasesJson, "https://releases.example.org/", "RPi4.aarch64", "nope", "", r, err));
  // u-boot
  r.clear();
  CHECK(ParseReleasesJson(ReleasesJson, "https://releases.example.org/", "A64.aarch64", "", "", r, err));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     CHECK_EQ(r[0].board, "orangepi-win");
     CHECK_EQ(r[0].label, "12.2-nightly-20250905-cc764b5-orangepi-win (orangepi-win)");
     }
  r.clear();
  CHECK(ParseReleasesJson(ReleasesJson, "https://releases.example.org/", "A64.aarch64", "", "pine64", r, err));
  CHECK(r.size() == 1 && r[0].board == "pine64");
  // unknown arch -> empty but ok
  r.clear();
  CHECK(ParseReleasesJson(ReleasesJson, "https://releases.example.org/", "RK3399.arm", "", "", r, err));
  CHECK(r.empty());
  CHECK(!ParseReleasesJson("[]", "x", "RPi4.aarch64", "", "", r, err));
}

static void TestGithub(void)
{
  const char *json =
    "[{\"tag_name\": \"nightly\", \"draft\": false, \"prerelease\": true, \"published_at\": \"2026-10-20T10:00:00Z\",\n"
    "  \"assets\": [\n"
    "   {\"name\": \"LibreELEC-RPi4.aarch64-13.0-devel-20261020103000-abc1234.tar\", \"size\": 123, \"updated_at\": \"2026-10-20T11:00:00Z\",\n"
    "    \"browser_download_url\": \"https://github.com/o/r/releases/download/nightly/LibreELEC-RPi4.aarch64-13.0-devel-20261020103000-abc1234.tar\",\n"
    "    \"digest\": \"sha256:ABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCD\"},\n"
    "   {\"name\": \"LibreELEC-RPi4.aarch64-13.0-devel-20261020103000-abc1234.img.gz\", \"size\": 456,\n"
    "    \"browser_download_url\": \"https://github.com/o/r/releases/download/nightly/LibreELEC-RPi4.aarch64-13.0-devel-20261020103000-abc1234.img.gz\"},\n"
    "   {\"name\": \"LibreELEC-RPi4.aarch64-13.0-devel-20261020103000-abc1234.img.gz.sha256\", \"browser_download_url\": \"https://x/sum\"},\n"
    "   {\"name\": \"LibreELEC-RPi5.aarch64-13.0-devel-20261020103000-abc1234.tar\", \"browser_download_url\": \"https://x/rpi5\"}\n"
    "  ]},\n"
    " {\"tag_name\": \"draft1\", \"draft\": true, \"assets\": [{\"name\": \"LibreELEC-RPi4.aarch64-x.tar\", \"browser_download_url\": \"u\"}]}]\n";
  std::vector<cReleaseInfo> r;
  std::string err;
  CHECK(ParseGithubReleases(json, "RPi4.aarch64", "", "", r, err));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     CHECK_EQ(r[0].channel, "nightly");
     CHECK(r[0].prerelease);
     CHECK_EQ(r[0].sha256, "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
     CHECK(r[0].timestamp == ParseTimestamp("2026-10-20T11:00:00Z"));
     CHECK_EQ(r[0].label, "13.0-devel-20261020103000-abc1234");
     CHECK(r[1].sha256.empty());
     CHECK_EQ(r[1].sha256Url, "https://x/sum");
     CHECK(r[1].timestamp == ParseTimestamp("2026-10-20T10:00:00Z"));
     }
  r.clear();
  CHECK(ParseGithubReleases(json, "RPi4.aarch64", "other", "", r, err));
  CHECK(r.empty());
  r.clear();
  CHECK(ParseGithubReleases(json, "", "", "RPi5", r, err));
  CHECK(r.size() == 1);
  CHECK(!ParseGithubReleases("{\"message\": \"API rate limit exceeded\"}", "RPi4.aarch64", "", "", r, err));
  CHECK_EQ(err, "GitHub: API rate limit exceeded");
  CHECK_EQ(GithubApiUrl("Zabrimus/VDRSternELEC"), "https://api.github.com/repos/Zabrimus/VDRSternELEC/releases?per_page=50");
  CHECK_EQ(GithubApiUrl("https://github.com/Zabrimus/VDRSternELEC.git"), "https://api.github.com/repos/Zabrimus/VDRSternELEC/releases?per_page=50");
  CHECK_EQ(GithubApiUrl("https://github.com/Zabrimus/VDRSternELEC/releases"), "https://api.github.com/repos/Zabrimus/VDRSternELEC/releases?per_page=50");
  CHECK_EQ(GithubApiUrl("nonsense"), "");
  CHECK_EQ(GithubApiUrl("a b/c"), "");
}

static void TestGithubVdrSternElec(void)
{
  // naming as used by Zabrimus/VDRSternELEC: one tag per date and distribution
  const char *json =
    "[{\"tag_name\": \"2026-07-24.1-CoreELEC-20-Release\", \"published_at\": \"2026-07-24T17:01:00Z\", \"assets\": [\n"
    "   {\"name\": \"CE-Amlogic-ne-Amlogic-ne.aarch64-addon.cefbrowser-20.5.1.zip\", \"browser_download_url\": \"u1\"},\n"
    "   {\"name\": \"CE-Amlogic-ne-Amlogic-ne.aarch64-channellogos-20.5.1.tar.gz\", \"browser_download_url\": \"u2\"},\n"
    "   {\"name\": \"CE-Amlogic-ne.aarch64-20.5-VDR-0724.tar\", \"size\": 322000000, \"browser_download_url\": \"u3\", \"digest\": \"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},\n"
    "   {\"name\": \"CE-Amlogic-ne.aarch64-20.5-VDR-0724.tar.sha256\", \"browser_download_url\": \"u4\"},\n"
    "   {\"name\": \"CE-Amlogic-ng.arm-20.5-VDR-0724.tar\", \"browser_download_url\": \"u5\"}]},\n"
    " {\"tag_name\": \"2026-07-10.1-CoreELEC-20-Release\", \"assets\": [\n"
    "   {\"name\": \"CE-Amlogic-ne.aarch64-20.5-VDR-0710.tar\", \"browser_download_url\": \"u6\"}]},\n"
    " {\"tag_name\": \"2026-07-24.1-LibreELEC-13-Release\", \"assets\": [\n"
    "   {\"name\": \"LE-RPi4.aarch64-13.0-VDR-0724.tar\", \"browser_download_url\": \"u7\"}]}]\n";
  std::vector<cReleaseInfo> r;
  std::string err;
  CHECK(ParseGithubReleases(json, "Amlogic-ne.aarch64", "CoreELEC-20", "", r, err));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     CHECK_EQ(r[0].name, "CE-Amlogic-ne.aarch64-20.5-VDR-0724.tar");
     CHECK_EQ(r[0].label, "20.5-VDR-0724");
     CHECK_EQ(r[0].url, "u3");
     CHECK_EQ(ReleaseKey(r[0]), LocalDigits(ParseTimestamp("2026-07-24T17:01:00Z")));
     CHECK_EQ(ReleaseKey(r[1]), "20260710000000"); // from the tag
     }
  r.clear();
  CHECK(ParseGithubReleases(json, "RPi4.aarch64", "LibreELEC-13", "", r, err));
  CHECK(r.size() == 1);
  r.clear();
  CHECK(ParseGithubReleases(json, "RPi4.aarch64", "LibreELEC-12", "", r, err));
  CHECK(r.empty());
  // comparison by build time of the installed system
  cSysInfo sys;
  sys.version = "20.5";
  sys.buildTime = ParseTimestamp("2026-07-12 10:00:00");
  cReleaseInfo a;
  a.name = "CE-Amlogic-ne.aarch64-20.5-VDR-0724.tar";
  a.label = "20.5-VDR-0724";
  a.channel = "2026-07-24.1-CoreELEC-20-Release";
  CHECK(CompareWithInstalled(a, sys) == crNewer);
  a.channel = "2026-07-12.1-CoreELEC-20-Release";
  CHECK(CompareWithInstalled(a, sys) == crUnknown);
  a.channel = "2026-07-01.1-CoreELEC-20-Release";
  CHECK(CompareWithInstalled(a, sys) == crOlder);
  // the plugin knows what it installed
  sys.installedName = "CE-Amlogic-ne.aarch64-20.5-VDR-0710.tar";
  sys.installedKey = "20260710000000";
  a.channel = "2026-07-24.1-CoreELEC-20-Release";
  CHECK(CompareWithInstalled(a, sys) == crNewer);
  a.name = sys.installedName;
  CHECK(CompareWithInstalled(a, sys) == crSame);
}

static void TestIndex(void)
{
  const char *html =
    "<html><body><a href=\"../\">../</a>\n"
    "<a href=\"LibreELEC-RPi4.aarch64-13.0.0.tar\">x</a>\n"
    "<a href=\"LibreELEC-RPi4.aarch64-13.0.0.tar.sha256\">x</a>\n"
    "<a href='sub/LibreELEC-RPi4.aarch64-13.0.1.img.gz'>y</a>\n"
    "<a href=\"/abs/LibreELEC-RPi4.aarch64-12.9.9.img.gz?x=1\">z</a>\n"
    "<a href=\"LibreELEC-RPi5.aarch64-13.0.0.tar\">other arch</a>\n"
    "<a href=\"LibreELEC-RPi4.aarch64-13.0.0.tar\">duplicate</a>\n"
    "</body></html>";
  std::vector<cReleaseInfo> r;
  CHECK(ParseIndexHtml(html, "http://host:8080/vdr", "RPi4.aarch64", "", "Local", r));
  CHECK(r.size() == 3);
  if (r.size() == 3) {
     CHECK_EQ(r[0].url, "http://host:8080/vdr/LibreELEC-RPi4.aarch64-13.0.0.tar");
     CHECK_EQ(r[0].sha256Url, "http://host:8080/vdr/LibreELEC-RPi4.aarch64-13.0.0.tar.sha256");
     CHECK_EQ(r[0].channel, "Local");
     CHECK_EQ(r[1].url, "http://host:8080/vdr/sub/LibreELEC-RPi4.aarch64-13.0.1.img.gz");
     CHECK(r[1].sha256Url.empty());
     CHECK_EQ(r[2].url, "http://host:8080/abs/LibreELEC-RPi4.aarch64-12.9.9.img.gz");
     }
}

static void TestLocalDirectory(void)
{
  std::vector<cLocalFile> files;
  const char *names[] = { "LibreELEC-RPi4.aarch64-13.0-VDR-0724.tar", "LibreELEC-RPi4.aarch64-13.0-VDR-0724.tar.sha256",
                          "LibreELEC-RPi4.aarch64-13.0-VDR-0801.img.gz", "LibreELEC-RPi5.aarch64-13.0-VDR-0801.tar",
                          "notes.txt", NULL };
  for (int i = 0; names[i]; i++) {
      cLocalFile f;
      f.name = names[i];
      f.size = 1000 + i;
      f.mtime = 1000000 + i;
      files.push_back(f);
      }
  std::vector<cReleaseInfo> r;
  CHECK(ParseLocalDirectory(files, PathToFileUrl("/media/NAS Share/vdrelec") + "/", "RPi4.aarch64", "", "NAS", r));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     CHECK(r[0].local);
     CHECK_EQ(r[0].url, "file:///media/NAS%20Share/vdrelec/LibreELEC-RPi4.aarch64-13.0-VDR-0724.tar");
     CHECK_EQ(r[0].sha256Url, "file:///media/NAS%20Share/vdrelec/LibreELEC-RPi4.aarch64-13.0-VDR-0724.tar.sha256");
     CHECK(r[0].size == 1000);
     CHECK(r[0].timestamp == 1000000);
     CHECK_EQ(r[0].channel, "NAS");
     CHECK(r[1].sha256Url.empty());
     }
  // checksum list
  cLocalFile sums;
  sums.name = "SHA256SUMS";
  sums.size = 10;
  sums.mtime = 0;
  files.push_back(sums);
  r.clear();
  CHECK(ParseLocalDirectory(files, "file:///srv/up", "RPi4.aarch64", "", "x", r));
  CHECK(r.size() == 2 && r[1].sha256Url == "file:///srv/up/SHA256SUMS");
  r.clear();
  CHECK(!ParseLocalDirectory(files, "relative/path", "RPi4.aarch64", "", "x", r));
  CHECK(!ParseLocalDirectory(files, "http://host/dir/", "RPi4.aarch64", "", "x", r));
  // SMB share
  r.clear();
  CHECK(ParseLocalDirectory(files, "smb://nas/share/vdr elec", "RPi4.aarch64", "", "NAS", r));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     CHECK(r[0].local);
     CHECK_EQ(r[0].url, "smb://nas/share/vdr elec/LibreELEC-RPi4.aarch64-13.0-VDR-0724.tar");
     CHECK_EQ(r[0].sha256Url, "smb://nas/share/vdr elec/LibreELEC-RPi4.aarch64-13.0-VDR-0724.tar.sha256");
     }
  CHECK_EQ(LocalPath("file:///media/NAS%20Share/"), "/media/NAS Share");
  CHECK_EQ(LocalPath("/storage/updates/"), "/storage/updates");
  CHECK_EQ(LocalPath("smb://nas/share"), "");
  // directory listing with checksum list
  std::vector<cReleaseInfo> h;
  CHECK(ParseIndexHtml("<a href=\"LibreELEC-RPi4.aarch64-13.0.tar\">a</a><a href=\"SHA256SUMS\">s</a>", "http://h/d/", "RPi4.aarch64", "", "c", h));
  CHECK(h.size() == 1 && h[0].sha256Url == "http://h/d/SHA256SUMS");
}

static void TestSmbUrl(void)
{
  cSmbUrl u;
  CHECK(ParseSmbUrl("smb://nas/vdr/updates/", u));
  CHECK_EQ(u.url, "smb://nas/vdr/updates");
  CHECK_EQ(u.server, "nas");
  CHECK(u.user.empty() && u.password.empty() && u.domain.empty());
  CHECK(ParseSmbUrl("smb://WORKGROUP;user:p%40ss:w@192.168.1.10/share", u));
  CHECK_EQ(u.url, "smb://192.168.1.10/share");
  CHECK_EQ(u.domain, "WORKGROUP");
  CHECK_EQ(u.user, "user");
  CHECK_EQ(u.password, "p@ss:w");
  CHECK(ParseSmbUrl("smb://guest@nas/share/a@b", u));
  CHECK_EQ(u.user, "guest");
  CHECK_EQ(u.url, "smb://nas/share/a@b");
  CHECK(!ParseSmbUrl("smb://nas", u));
  CHECK(!ParseSmbUrl("smb://nas/", u));
  CHECK(!ParseSmbUrl("http://nas/share", u));
  CHECK_EQ(RedactUrl("smb://user:secret@nas/share/x"), "smb://user:***@nas/share/x");
  CHECK_EQ(RedactUrl("smb://nas/share/x"), "smb://nas/share/x");
  CHECK_EQ(RedactUrl("https://x/y"), "https://x/y");
  CHECK(IsFileSourceUrl("smb://nas/share/x.tar"));
  CHECK(IsFileSourceUrl("file:///x.tar"));
  CHECK(!IsFileSourceUrl("file://host/x.tar"));
  CHECK(!IsFileSourceUrl("https://x/x.tar"));
}

static void TestLocalJson(void)
{
  const char *json =
    "{\"VDRSternELEC-13\": {\"url\": \"https://elsewhere.example.org/\", \"project\": {\"RPi4.aarch64\": {\"releases\": {\n"
    "  \"0\": {\"file\": {\"name\": \"LibreELEC-RPi4.aarch64-13.0-devel-20261101120000-abc1234.tar\", \"subpath\": \"13.0/RPi\", \"sha256\": \"" "1111111111111111111111111111111111111111111111111111111111111111" "\"}},\n"
    "  \"1\": {\"file\": {\"name\": \"evil.tar\", \"subpath\": \"../../etc\"}},\n"
    "  \"2\": {\"file\": {\"name\": \"my file.tar\", \"subpath\": \"\"}}}}}}}";
  std::vector<cReleaseInfo> r;
  std::string err;
  CHECK(ParseReleasesJson(json, "smb://nas/share/vdr/releases.json", "RPi4.aarch64", "", "", r, err, true));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     CHECK(r[0].local);
     CHECK_EQ(r[0].url, "smb://nas/share/vdr/13.0/RPi/LibreELEC-RPi4.aarch64-13.0-devel-20261101120000-abc1234.tar");
     CHECK_EQ(r[0].sha256, "1111111111111111111111111111111111111111111111111111111111111111");
     CHECK_EQ(r[1].url, "smb://nas/share/vdr/my%20file.tar");
     }
  r.clear();
  CHECK(ParseReleasesJson(json, PathToFileUrl("/media/usb") + "/releases.json", "RPi4.aarch64", "", "", r, err, true));
  CHECK(r.size() == 2 && r[0].url == "file:///media/usb/13.0/RPi/LibreELEC-RPi4.aarch64-13.0-devel-20261101120000-abc1234.tar");
  // remote: channel url is used, not marked local
  r.clear();
  CHECK(ParseReleasesJson(json, "https://x/releases.json", "RPi4.aarch64", "", "", r, err));
  CHECK(r.size() == 3 && !r[0].local && r[0].url == "https://elsewhere.example.org/13.0/RPi/LibreELEC-RPi4.aarch64-13.0-devel-20261101120000-abc1234.tar");
}

static void TestBuildDate(void)
{
  // os-release of LibreELEC >= 12 uses DISTRO_*
  cSysInfo info;
  CHECK(ParseOsRelease("NAME=\"LibreELEC\"\nVERSION=\"13.0-devel-20261020103000-abc1234\"\nDISTRO_ARCH=\"RPi4.aarch64\"\nDISTRO_BUILD=\"community\"\nDISTRO_PROJECT=\"RPi\"\nDISTRO_DEVICE=\"RPi4\"\n", info));
  CHECK_EQ(info.arch, "RPi4.aarch64");
  CHECK_EQ(info.project, "RPi");
  CHECK_EQ(info.device, "RPi4");
  CHECK_EQ(info.build, "community");
  time_t t;
  bool hasTime;
  CHECK(InstalledBuildDate(info, t, hasTime));
  CHECK(hasTime);
  CHECK_EQ(FormatBuildDate(t, hasTime), "20.10.2026 10:30");
  // date only, refined by the time of os-release on the same day
  cSysInfo n;
  n.version = "12.2-nightly-20251005-7adb5c2";
  CHECK(InstalledBuildDate(n, t, hasTime));
  CHECK(!hasTime);
  CHECK_EQ(FormatBuildDate(t, hasTime), "05.10.2025");
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = 125; tm.tm_mon = 9; tm.tm_mday = 5; tm.tm_hour = 3; tm.tm_min = 7; tm.tm_isdst = -1;
  n.buildTime = mktime(&tm);
  CHECK(InstalledBuildDate(n, t, hasTime));
  CHECK(hasTime);
  CHECK_EQ(FormatBuildDate(t, hasTime), "05.10.2025 03:07");
  // no date in the version: time of os-release
  cSysInfo v;
  v.version = "13.0.1";
  v.buildTime = n.buildTime;
  CHECK(InstalledBuildDate(v, t, hasTime) && hasTime);
  v.buildTime = 0;
  CHECK(!InstalledBuildDate(v, t, hasTime));
  // releases
  cReleaseInfo r;
  r.name = "LibreELEC-RPi4.aarch64-13.0-devel-20261101120000-abc1234.tar";
  r.timestamp = ParseTimestamp("2026-11-02 08:00:00");
  CHECK(ReleaseBuildDate(r, t, hasTime) && hasTime);
  CHECK_EQ(FormatBuildDate(t, hasTime), "01.11.2026 12:00"); // build time from the name wins
  r.name = "CE-Amlogic-ne.aarch64-20.5-VDR-0724.tar";
  CHECK(ReleaseBuildDate(r, t, hasTime) && hasTime && t == r.timestamp);
  r.timestamp = 0;
  r.channel = "2026-07-24.1-CoreELEC-20-Release";
  CHECK(ReleaseBuildDate(r, t, hasTime) && !hasTime);
  CHECK_EQ(FormatBuildDate(t, hasTime), "24.07.2026");
  r.channel = "";
  CHECK(!ReleaseBuildDate(r, t, hasTime));
}

static void TestAddons(void)
{
  std::string id, ver;
  CHECK(ParseAddonName("LE-RPi4.aarch64-addon.cefbrowser-12.80.7.2.zip", "RPi4.aarch64", id, ver));
  CHECK_EQ(id, "addon.cefbrowser");
  CHECK_EQ(ver, "12.80.7.2");
  CHECK(ParseAddonName("LE-RPi4.aarch64-channellogos-12.80.7.1.tar.gz", "RPi4.aarch64", id, ver));
  CHECK_EQ(id, "channellogos");
  CHECK(ParseAddonName("CE-Amlogic-ng-Amlogic-ng.arm-driver.dvb.dvb-latest-20.5.0.zip", "Amlogic-ng.arm", id, ver));
  CHECK_EQ(id, "driver.dvb.dvb-latest");
  CHECK_EQ(ver, "20.5.0");
  CHECK(ParseAddonName("LE-Generic.x86_64-yt-dlp-12.2.0.1.zip", "Generic.x86_64", id, ver));
  CHECK_EQ(id, "yt-dlp");
  CHECK(!ParseAddonName("LE-RPi4.aarch64-13.0-VDR-1016.tar", "RPi4.aarch64", id, ver));
  CHECK(!ParseAddonName("LE-RPi5.aarch64-yt-dlp-12.2.0.1.zip", "RPi4.aarch64", id, ver));
  CHECK(!ParseAddonName("LE-RPi4.aarch64-yt-dlp.zip", "RPi4.aarch64", id, ver));
  CHECK(!ParseAddonName("LE-RPi4.aarch64-../evil-1.0.zip", "RPi4.aarch64", id, ver));
  CHECK(!ParseAddonName("source.zip", "RPi4.aarch64", id, ver));
  // Generic release: 12.2 and 13.0 images with addons for both in one tag
  const char *json =
    "{\"tag_name\": \"2026-10-16.1-LibreELEC-Generic-Release\", \"published_at\": \"2026-10-16T12:00:00Z\", \"assets\": [\n"
    "  {\"name\": \"LE-Generic.x86_64-12.2-VDR-1016.tar\", \"browser_download_url\": \"u1\"},\n"
    "  {\"name\": \"LE-Generic.x86_64-13.0-VDR-1016.tar\", \"browser_download_url\": \"u2\"},\n"
    "  {\"name\": \"LE-Generic.x86_64-addon.cefbrowser-12.2.0.2.zip\", \"browser_download_url\": \"c1\", \"digest\": \"sha256:" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" "\"},\n"
    "  {\"name\": \"LE-Generic.x86_64-addon.cefbrowser-12.80.7.2.zip\", \"browser_download_url\": \"c2\"},\n"
    "  {\"name\": \"LE-Generic.x86_64-addon.cefbrowser-12.80.7.2.zip.sha256\", \"browser_download_url\": \"c2s\"},\n"
    "  {\"name\": \"LE-Generic.x86_64-channellogos-12.80.7.1.tar.gz\", \"browser_download_url\": \"l2\"},\n"
    "  {\"name\": \"LE-Generic-legacy.x86_64-yt-dlp-12.80.7.1.zip\", \"browser_download_url\": \"other\"}]}";
  std::vector<cReleaseInfo> r;
  std::string err;
  CHECK(ParseGithubReleases(json, "Generic.x86_64", "", "", r, err));
  CHECK(r.size() == 2);
  if (r.size() == 2) {
     const cReleaseInfo &le12 = r[0].label.compare(0, 4, "12.2") == 0 ? r[0] : r[1];
     const cReleaseInfo &le13 = r[0].label.compare(0, 4, "12.2") == 0 ? r[1] : r[0];
     CHECK(le12.addons.size() == 1); // channellogos only exists in 12.80 (LE 13)
     if (le12.addons.size() == 1) {
        CHECK_EQ(le12.addons[0].version, "12.2.0.2");
        CHECK_EQ(le12.addons[0].sha256, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        }
     CHECK(le13.addons.size() == 2);
     for (size_t i = 0; i < le13.addons.size(); i++) {
         if (le13.addons[i].id == "addon.cefbrowser") {
            CHECK_EQ(le13.addons[i].version, "12.80.7.2");
            CHECK_EQ(le13.addons[i].sha256Url, "c2s");
            }
         else
            CHECK_EQ(le13.addons[i].id, "channellogos");
         }
     }
  // single version: belongs to the release
  std::vector<cLocalFile> files(2);
  files[0].name = "CE-Amlogic-ne.aarch64-20.5-VDR-0724.tar"; files[0].size = 1; files[0].mtime = 0;
  files[1].name = "CE-Amlogic-ne-Amlogic-ne.aarch64-addon.cefbrowser-20.5.1.zip"; files[1].size = 2; files[1].mtime = 0;
  r.clear();
  CHECK(ParseLocalDirectory(files, "file:///x/", "Amlogic-ne.aarch64", "", "x", r));
  CHECK(r.size() == 1 && r[0].addons.size() == 1 && r[0].addons[0].url == "file:///x/CE-Amlogic-ne-Amlogic-ne.aarch64-addon.cefbrowser-20.5.1.zip");
  CHECK(MajorVersion("13.0-devel-20261020103000-abc1234") == 13);
  CHECK(MajorVersion("20.5-VDR-0724") == 20);
  CHECK(MajorVersion("12.2") == 12);
  CHECK(MajorVersion("nightly") == -1);
  CHECK(InTimeWindow(0, 0, 1234));
  CHECK(InTimeWindow(300, 500, 300));
  CHECK(InTimeWindow(300, 500, 459));
  CHECK(!InTimeWindow(300, 500, 500));
  CHECK(!InTimeWindow(300, 500, 1200));
  CHECK(InTimeWindow(2300, 100, 2330));
  CHECK(InTimeWindow(2300, 100, 30));
  CHECK(!InTimeWindow(2300, 100, 100));
  CHECK(!InTimeWindow(2300, 100, 1200));
}

static void TestHelpers(void)
{
  CHECK_EQ(ParseSha256File("0123456789abcdef0123456789abcdef0123456789ABCDEF0123456789abcdef  LibreELEC.tar\n", "LibreELEC.tar"), "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  CHECK_EQ(ParseSha256File("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  a.tar\nbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb *dir/b.tar\n", "b.tar"), "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  CHECK_EQ(ParseSha256File("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n", "x"), "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  CHECK_EQ(ParseSha256File("<html>404</html>", "x"), "");
  // list without matching entry
  CHECK_EQ(ParseSha256File("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  a.tar\nbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb  b.tar\n", "c.tar"), "");
  // single entry with a different name (renamed file) is still used
  CHECK_EQ(ParseSha256File("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  old.tar\n", "new.tar"), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(IsUpdateFile("a.tar"));
  CHECK(IsUpdateFile("a.img.gz"));
  CHECK(!IsUpdateFile("a.tar.sha256"));
  CHECK(!IsUpdateFile("a.img"));
  CHECK(MatchesArch("LibreELEC-RPi4.aarch64-13.0.tar", "RPi4.aarch64", ""));
  CHECK(!MatchesArch("LibreELEC-RPi4.aarch64-13.0.tar", "RPi.aarch64", ""));
  CHECK(!MatchesArch("LibreELEC-Generic-legacy.x86_64-13.0.tar", "Generic.x86_64", ""));
  CHECK(!MatchesArch("LibreELEC-xRPi4.aarch64-13.0.tar", "RPi4.aarch64", ""));
  CHECK(MatchesArch("foo.tar", "RPi4.aarch64", "^foo"));
  CHECK(!MatchesArch("foo.tar", "", "[invalid"));
  CHECK_EQ(ExtractDate("13.0-devel-20261020103000-abc1234"), "20261020");
  CHECK_EQ(ExtractDate("12.2-nightly-20251005-7adb5c2"), "20251005");
  CHECK_EQ(ExtractDate("12.0.1"), "");
  CHECK_EQ(ExtractDate("build-12345678"), "");
  CHECK_EQ(ExtractDate("2026-07-24.1-LibreELEC-13-Release"), "20260724");
  CHECK_EQ(ExtractDate("v2026.07.24"), "20260724");
  CHECK_EQ(ExtractDate("20.5-VDR-0724"), "");
  CHECK_EQ(ExtractDate("2026-13-01"), "");
  CHECK_EQ(JsonUrl("https://releases.libreelec.tv"), "https://releases.libreelec.tv/releases.json");
  CHECK_EQ(JsonUrl("https://x/y/releases.json"), "https://x/y/releases.json");
  CHECK_EQ(UrlDirectory("https://x/y/releases.json"), "https://x/y/");
  CHECK_EQ(UrlDirectory("https://x"), "https://x/");
  CHECK_EQ(ResolveUrl("https://x/a/b/", "../c.tar"), "https://x/a/b/../c.tar");
  CHECK_EQ(ResolveUrl("https://x/a/b/", "//y/c.tar"), "https://y/c.tar");
  CHECK_EQ(FormatSize(150000000), "143 MB");
  CHECK_EQ(FormatSize(-1), "?");
  CHECK(ParseTimestamp("1970-01-02T00:00:00Z") == 86400);
  CHECK(ParseTimestamp("1970-01-02T00:00:01Z") == 86401);
  CHECK(ParseTimestamp("1970-01-02T00:00:01.123Z") == 86401);
  CHECK(ParseTimestamp("1970-01-02T02:00:00+02:00") == 86400);
  CHECK(ParseTimestamp("1970-01-01T22:00:00-0200") == 86400);
  // without time zone: local time (releases.json written by releases.py)
  {
    time_t t = ParseTimestamp("2026-10-21 22:47:00");
    struct tm tm;
    localtime_r(&t, &tm);
    CHECK(tm.tm_year == 126 && tm.tm_mon == 9 && tm.tm_mday == 21 && tm.tm_hour == 22 && tm.tm_min == 47);
    CHECK_EQ(FormatBuildDate(t, true), "21.10.2026 22:47");
    t = ParseTimestamp("2026-01-15 08:00:00"); // winter time
    CHECK_EQ(FormatBuildDate(t, true), "15.01.2026 08:00");
  }
  CHECK(ParseTimestamp("") == 0);
}

static void TestCompare(void)
{
  cSysInfo sys;
  sys.version = "13.0-devel-20261020103000-abc1234";
  sys.buildTime = ParseTimestamp("2026-10-20 10:30:00");
  cReleaseInfo r;
  r.name = "LibreELEC-RPi4.aarch64-13.0-devel-20261020103000-abc1234.tar";
  r.label = "13.0-devel-20261020103000-abc1234";
  CHECK(CompareWithInstalled(r, sys) == crSame);
  r.label = "13.0-devel-20261021090000-def5678";
  r.name = "LibreELEC-RPi4.aarch64-" + r.label + ".tar";
  CHECK(CompareWithInstalled(r, sys) == crNewer);
  r.label = "13.0-devel-20261001090000-def5678";
  r.name = "LibreELEC-RPi4.aarch64-" + r.label + ".tar";
  CHECK(CompareWithInstalled(r, sys) == crOlder);
  // same day, later build
  r.label = "13.0-devel-20261020200000-fff0000";
  r.name = "x";
  r.timestamp = ParseTimestamp("2026-10-20 20:00:00");
  CHECK(CompareWithInstalled(r, sys) == crNewer);
  r.timestamp = 0;
  CHECK(CompareWithInstalled(r, sys) == crUnknown);
  // releases
  cSysInfo rel;
  rel.version = "12.0.1";
  cReleaseInfo r2;
  r2.label = "12.0.2";
  CHECK(CompareWithInstalled(r2, rel) == crNewer);
  r2.label = "12.0.0";
  CHECK(CompareWithInstalled(r2, rel) == crOlder);
  r2.label = "12.0.1";
  CHECK(CompareWithInstalled(r2, rel) == crSame);
  r2.label = "12.0.10";
  CHECK(CompareWithInstalled(r2, rel) == crNewer);
  // dated build vs release
  r2.label = "13.0-nightly-20261020-abc";
  CHECK(CompareWithInstalled(r2, rel) == crNewer);
  r2.label = "12.0-nightly-20261020-abc";
  CHECK(CompareWithInstalled(r2, rel) == crUnknown);
  cSysInfo empty;
  CHECK(CompareWithInstalled(r2, empty) == crUnknown);
  // u-boot label suffix is ignored
  cSysInfo ub;
  ub.version = "12.2-nightly-20250905-cc764b5";
  cReleaseInfo r3;
  r3.name = "LibreELEC-A64.aarch64-12.2-nightly-20250905-cc764b5-pine64.img.gz";
  r3.label = "12.2-nightly-20250905-cc764b5-pine64 (pine64)";
  CHECK(CompareWithInstalled(r3, ub) == crSame);
}

static void TestSort(void)
{
  std::vector<cReleaseInfo> v(4);
  v[0].channel = "A"; v[0].name = "x-20250101.tar"; v[0].label = "a";
  v[1].channel = "B"; v[1].name = "x-20260101.tar"; v[1].label = "b";
  v[2].channel = "A"; v[2].name = "x-20250301.tar"; v[2].label = "c";
  v[3].channel = "A"; v[3].name = "y.tar"; v[3].label = "d"; v[3].timestamp = ParseTimestamp("2025-02-01 00:00:00");
  SortReleases(v);
  CHECK_EQ(v[0].label, "c");
  CHECK_EQ(v[1].label, "d");
  CHECK_EQ(v[2].label, "a");
  CHECK_EQ(v[3].label, "b");
}

int main(void)
{
  TestJson();
  TestSha256();
  TestOsRelease();
  TestReleasesJson();
  TestGithub();
  TestGithubVdrSternElec();
  TestIndex();
  TestLocalDirectory();
  TestSmbUrl();
  TestLocalJson();
  TestBuildDate();
  TestAddons();
  TestHelpers();
  TestCompare();
  TestSort();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
