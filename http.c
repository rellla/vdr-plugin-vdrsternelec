/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * http.c: Thin wrapper around libcurl
 */

#include "http.h"
#include "common.h"
#include <curl/curl.h>
#include <cstdio>
#include <unistd.h>

#define USER_AGENT "vdr-plugin-vdrsternelec/" PLUGIN_VERSION

void HttpGlobalInit(void)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

void HttpGlobalCleanup(void)
{
  curl_global_cleanup();
}

struct tStringSink {
  std::string *data;
  size_t maxSize;
  bool overflow;
  };

static size_t StringWriter(char *Ptr, size_t Size, size_t NMemb, void *UserData)
{
  tStringSink *s = (tStringSink *)UserData;
  size_t n = Size * NMemb;
  if (s->data->size() + n > s->maxSize) {
     s->overflow = true;
     return 0;
     }
  s->data->append(Ptr, n);
  return n;
}

static size_t FileWriter(char *Ptr, size_t Size, size_t NMemb, void *UserData)
{
  return fwrite(Ptr, Size, NMemb, (FILE *)UserData) * Size;
}

static int ProgressCallback(void *ClientP, curl_off_t DlTotal, curl_off_t DlNow, curl_off_t, curl_off_t)
{
  cHttpProgress *p = (cHttpProgress *)ClientP;
  if (p && !p->Progress((long long)DlNow, DlTotal > 0 ? (long long)DlTotal : -1))
     return 1; // abort
  return 0;
}

static CURL *NewHandle(const std::string &Url, char *ErrorBuffer)
{
  CURL *curl = curl_easy_init();
  if (!curl)
     return NULL;
  ErrorBuffer[0] = 0;
  curl_easy_setopt(curl, CURLOPT_URL, Url.c_str());
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, ErrorBuffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  // abort if the transfer stalls for 60 seconds
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  // local files (file://) only if explicitly requested, never via redirects
  bool file = Url.compare(0, 7, "file://") == 0;
#if LIBCURL_VERSION_NUM >= 0x075500 // 7.85.0
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, file ? "file" : "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, file ? (long)CURLPROTO_FILE : (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
  return curl;
}

static std::string ErrorText(CURLcode Res, const char *ErrorBuffer)
{
  if (ErrorBuffer && *ErrorBuffer)
     return ErrorBuffer;
  return curl_easy_strerror(Res);
}

bool HttpGet(const std::string &Url, std::string &Result, std::string &Error, size_t MaxSize, const char *Accept)
{
  char errbuf[CURL_ERROR_SIZE];
  Result.clear();
  CURL *curl = NewHandle(Url, errbuf);
  if (!curl) {
     Error = "curl_easy_init() failed";
     return false;
     }
  tStringSink sink = { &Result, MaxSize, false };
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StringWriter);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // all supported encodings
  struct curl_slist *headers = NULL;
  if (Accept) {
     std::string h = std::string("Accept: ") + Accept;
     headers = curl_slist_append(headers, h.c_str());
     curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
     }
  dsyslog_plg("GET %s", Url.c_str());
  // keep the body of error responses, servers (GitHub) explain the error there
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
  CURLcode res = curl_easy_perform(curl);
  bool ok = res == CURLE_OK;
  if (!ok)
     Error = sink.overflow ? "response too large" : ErrorText(res, errbuf);
  else {
     long code = 0;
     curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
     if (code >= 400) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HTTP error %ld", code);
        Error = buf;
        ok = false;
        }
     }
  curl_easy_cleanup(curl);
  if (headers)
     curl_slist_free_all(headers);
  return ok;
}

bool HttpDownload(const std::string &Url, const std::string &FileName, cHttpProgress *Progress, std::string &Error)
{
  FILE *f = fopen(FileName.c_str(), "wb");
  if (!f) {
     Error = "can't create " + FileName;
     return false;
     }
  char errbuf[CURL_ERROR_SIZE];
  CURL *curl = NewHandle(Url, errbuf);
  if (!curl) {
     fclose(f);
     Error = "curl_easy_init() failed";
     return false;
     }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriter);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, Progress);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  isyslog_plg("%s %s to %s", Url.compare(0, 7, "file://") == 0 ? "copying" : "downloading", Url.c_str(), FileName.c_str());
  CURLcode res = curl_easy_perform(curl);
  bool ok = res == CURLE_OK;
  if (!ok)
     Error = res == CURLE_ABORTED_BY_CALLBACK ? "aborted" : ErrorText(res, errbuf);
  curl_easy_cleanup(curl);
  if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
     if (ok)
        Error = "write error (disk full?)";
     ok = false;
     }
  if (fclose(f) != 0) {
     if (ok)
        Error = "write error";
     ok = false;
     }
  return ok;
}
