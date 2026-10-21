/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * filesource.c: Access to local directories and SMB shares
 */

#include "filesource.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef WITH_SMB
#include <libsmbclient.h>
#endif

#define COPY_BUFFER_SIZE (256 * 1024)

// --- cFileSource -------------------------------------------------------------------

bool cFileSource::Read(const std::string &Url, std::string &Content, size_t MaxSize, std::string &Error)
{
  long long size;
  Content.clear();
  if (!OpenFile(Url, size, Error))
     return false;
  char buf[16384];
  bool ok = true;
  for (;;) {
      ssize_t n = ReadChunk(buf, sizeof(buf));
      if (n < 0) {
         Error = std::string(RedactUrl(Url)) + ": " + strerror(errno);
         ok = false;
         break;
         }
      if (n == 0)
         break;
      if (Content.size() + n > MaxSize) {
         Error = RedactUrl(Url) + ": file too large";
         ok = false;
         break;
         }
      Content.append(buf, n);
      }
  CloseFile();
  return ok;
}

bool cFileSource::Copy(const std::string &Url, const std::string &FileName, cHttpProgress *Progress, std::string &Error)
{
  long long size;
  if (!OpenFile(Url, size, Error))
     return false;
  FILE *f = fopen(FileName.c_str(), "wb");
  if (!f) {
     Error = "can't create " + FileName;
     CloseFile();
     return false;
     }
  char *buf = new char[COPY_BUFFER_SIZE];
  long long done = 0;
  bool ok = true;
  for (;;) {
      ssize_t n = ReadChunk(buf, COPY_BUFFER_SIZE);
      if (n < 0) {
         Error = RedactUrl(Url) + ": " + strerror(errno);
         ok = false;
         break;
         }
      if (n == 0)
         break;
      if (fwrite(buf, 1, n, f) != size_t(n)) {
         Error = "write error (disk full?)";
         ok = false;
         break;
         }
      done += n;
      if (Progress && !Progress->Progress(done, size)) {
         Error = "aborted";
         ok = false;
         break;
         }
      }
  delete[] buf;
  CloseFile();
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

// --- cLocalSource ------------------------------------------------------------------

class cLocalSource : public cFileSource {
private:
  int fd;
protected:
  virtual bool OpenFile(const std::string &Url, long long &Size, std::string &Error);
  virtual ssize_t ReadChunk(char *Buffer, size_t Length);
  virtual void CloseFile(void);
public:
  cLocalSource(void) : fd(-1) {}
  virtual ~cLocalSource() { CloseFile(); }
  virtual bool List(const std::string &DirUrl, std::vector<cLocalFile> &Files, std::string &Error);
  };

bool cLocalSource::List(const std::string &DirUrl, std::vector<cLocalFile> &Files, std::string &Error)
{
  std::string dir = LocalPath(DirUrl);
  Files.clear();
  DIR *d = opendir(dir.c_str());
  if (!d) {
     Error = dir + ": " + strerror(errno);
     return false;
     }
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
           continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
           continue;
        cLocalFile f;
        f.name = e->d_name;
        f.size = st.st_size;
        f.mtime = st.st_mtime;
        Files.push_back(f);
        }
  closedir(d);
  return true;
}

bool cLocalSource::OpenFile(const std::string &Url, long long &Size, std::string &Error)
{
  CloseFile();
  std::string path = LocalPath(Url);
  fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  struct stat st;
  if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
     Error = path + ": " + (fd < 0 ? strerror(errno) : "not a regular file");
     CloseFile();
     return false;
     }
  Size = st.st_size;
  return true;
}

ssize_t cLocalSource::ReadChunk(char *Buffer, size_t Length)
{
  ssize_t n;
  do {
     n = read(fd, Buffer, Length);
     } while (n < 0 && errno == EINTR);
  return n;
}

void cLocalSource::CloseFile(void)
{
  if (fd >= 0)
     close(fd);
  fd = -1;
}

// --- cSmbSource --------------------------------------------------------------------

#ifdef WITH_SMB

static bool StatWanted(const std::string &Name)
{
  // size and time are only needed for update files and JSON files
  size_t n = Name.size();
  return IsUpdateFile(Name) || (n > 5 && Name.compare(n - 5, 5, ".json") == 0);
}

class cSmbSource : public cFileSource {
private:
  SMBCCTX *ctx;
  SMBCFILE *file;
  cSmbUrl credentials;
  static void AuthData(SMBCCTX *c, const char *Server, const char *Share, char *Workgroup, int WorkgroupLen, char *User, int UserLen, char *Password, int PasswordLen);
protected:
  virtual bool OpenFile(const std::string &Url, long long &Size, std::string &Error);
  virtual ssize_t ReadChunk(char *Buffer, size_t Length);
  virtual void CloseFile(void);
public:
  cSmbSource(void) : ctx(NULL), file(NULL) {}
  virtual ~cSmbSource();
  bool Init(const cSmbUrl *Credentials, std::string &Error);
  virtual bool List(const std::string &DirUrl, std::vector<cLocalFile> &Files, std::string &Error);
  };

static void Copy0(char *Dest, int Len, const std::string &Value)
{
  if (Len > 0) {
     strncpy(Dest, Value.c_str(), Len - 1);
     Dest[Len - 1] = 0;
     }
}

void cSmbSource::AuthData(SMBCCTX *c, const char *Server, const char *Share, char *Workgroup, int WorkgroupLen, char *User, int UserLen, char *Password, int PasswordLen)
{
  cSmbSource *s = (cSmbSource *)smbc_getOptionUserData(c);
  if (!s)
     return;
  if (!s->credentials.domain.empty())
     Copy0(Workgroup, WorkgroupLen, s->credentials.domain);
  // empty user and password = anonymous access
  Copy0(User, UserLen, s->credentials.user);
  Copy0(Password, PasswordLen, s->credentials.password);
}

bool cSmbSource::Init(const cSmbUrl *Credentials, std::string &Error)
{
  if (Credentials)
     credentials = *Credentials;
  ctx = smbc_new_context();
  if (!ctx) {
     Error = "smbc_new_context() failed";
     return false;
     }
  smbc_setDebug(ctx, 0);
  smbc_setTimeout(ctx, 20000);
  smbc_setOptionUserData(ctx, this);
  smbc_setFunctionAuthDataWithContext(ctx, AuthData);
  if (!smbc_init_context(ctx)) {
     Error = std::string("smbc_init_context() failed: ") + strerror(errno);
     smbc_free_context(ctx, 1);
     ctx = NULL;
     return false;
     }
  return true;
}

cSmbSource::~cSmbSource()
{
  CloseFile();
  if (ctx)
     smbc_free_context(ctx, 1);
}

static std::string SmbError(const std::string &Url, int Errno)
{
  if (Errno == EACCES || Errno == EPERM)
     return RedactUrl(Url) + ": access denied (check user and password)";
  return RedactUrl(Url) + ": " + strerror(Errno);
}

bool cSmbSource::List(const std::string &DirUrl, std::vector<cLocalFile> &Files, std::string &Error)
{
  Files.clear();
  std::string dir = DirUrl;
  while (dir.size() > 6 && dir[dir.size() - 1] == '/')
        dir.erase(dir.size() - 1);
  SMBCFILE *d = smbc_getFunctionOpendir(ctx)(ctx, dir.c_str());
  if (!d) {
     Error = SmbError(dir, errno);
     return false;
     }
  std::vector<std::string> names;
  struct smbc_dirent *e;
  while ((e = smbc_getFunctionReaddir(ctx)(ctx, d)) != NULL) {
        if (e->smbc_type == SMBC_FILE && e->name[0] != '.')
           names.push_back(e->name);
        }
  smbc_getFunctionClosedir(ctx)(ctx, d);
  for (size_t i = 0; i < names.size(); i++) {
      cLocalFile f;
      f.name = names[i];
      f.size = -1;
      f.mtime = 0;
      if (StatWanted(f.name)) {
         struct stat st;
         if (smbc_getFunctionStat(ctx)(ctx, (dir + "/" + EncodePath(f.name)).c_str(), &st) == 0) {
            f.size = st.st_size;
            f.mtime = st.st_mtime;
            }
         }
      Files.push_back(f);
      }
  return true;
}

bool cSmbSource::OpenFile(const std::string &Url, long long &Size, std::string &Error)
{
  CloseFile();
  struct stat st;
  if (smbc_getFunctionStat(ctx)(ctx, Url.c_str(), &st) != 0) {
     Error = SmbError(Url, errno);
     return false;
     }
  file = smbc_getFunctionOpen(ctx)(ctx, Url.c_str(), O_RDONLY, 0);
  if (!file) {
     Error = SmbError(Url, errno);
     return false;
     }
  Size = st.st_size;
  return true;
}

ssize_t cSmbSource::ReadChunk(char *Buffer, size_t Length)
{
  return smbc_getFunctionRead(ctx)(ctx, file, Buffer, Length);
}

void cSmbSource::CloseFile(void)
{
  if (file && ctx)
     smbc_getFunctionClose(ctx)(ctx, file);
  file = NULL;
}

#endif // WITH_SMB

// --- Factory -------------------------------------------------------------------------

bool HaveSmbSupport(void)
{
#ifdef WITH_SMB
  return true;
#else
  return false;
#endif
}

cFileSource *NewFileSource(const std::string &Url, const cSmbUrl *Credentials, std::string &Error)
{
  if (Url.compare(0, 8, "file:///") == 0)
     return new cLocalSource;
  if (Url.compare(0, 6, "smb://") == 0) {
#ifdef WITH_SMB
     cSmbSource *s = new cSmbSource;
     if (!s->Init(Credentials, Error)) {
        delete s;
        return NULL;
        }
     return s;
#else
     (void)Credentials;
     Error = "SMB support not available (plugin built without libsmbclient)";
     return NULL;
#endif
     }
  Error = "unsupported URL: " + RedactUrl(Url);
  return NULL;
}
