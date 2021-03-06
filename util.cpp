// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "util.h"
#include "err.h"

#include <string>
#include <vector>
#include <set>
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <filesystem>
#include <algorithm>
#include <cctype>

#include <rang.hpp>

#define _WITH_GETLINE // it breaks on 11.3 w/out this, but the manpage getline(3) doesn't mention _WITH_GETLINE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <pwd.h>


#define SYSCALL(res, syscall, arg ...) Util::ckSyscallError(res, syscall, arg)

static uid_t myuid = ::getuid();

// consts
static const char sepFilePath = '/';
static const char sepFileExt = '.';

// OnDestroy/RunAtEnd

OnDestroy::OnDestroy(const std::function<void()> &newFnAction) : fnAction(newFnAction) { }

OnDestroy::~OnDestroy() {
  // releasing the resource in destructor, likely cumulative after some exception, catch the potential exception here
  try {
    fnAction();
  } catch (const Exception &e) {
    std::cerr << rang::fg::yellow << "EXCEPTION while another error is in progress: " << e.what() << rang::style::reset << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "EXCEPTION while another error is in progress (std::exception):" << rang::fg::red << e.what() << rang::style::reset << std::endl;
  } catch (...) {
    std::cerr << rang::fg::red << "UNKNOWN EXCEPTION while another error is in progress: " << rang::style::reset << std::endl;
  }
}

void OnDestroy::doNow() {
  fnAction(); // releasing the resource in a regular way: let exceptions propagate
  fnAction = []() { };
}

RunAtEnd::RunAtEnd()
{ }

RunAtEnd::RunAtEnd(const std::function<void()> &newFnAction)
: std::unique_ptr<OnDestroy>(new OnDestroy(newFnAction))
{ }

void RunAtEnd::reset(const std::function<void()> &newFnAction) {
  std::unique_ptr<OnDestroy>::reset(new OnDestroy(newFnAction));
}

void RunAtEnd::doNow() {
  get()->doNow();
}


namespace Util {

void runCommand(const std::string &cmd, const std::string &what) {
  int res = ::system(cmd.c_str());
  SYSCALL(res, "system", what.c_str()); // syscall failure
  // command failure
  if (res != 0)
    ERR2("run external command", "the command '" << what << "' failed with the exit status " << res)
}

std::string runCommandGetOutput(const std::string &cmd, const std::string &what) {
  // start the command
  FILE *f = ::popen(cmd.c_str(), "r");
  if (f == nullptr)
    ERR2("run external command", "popen failed for (" << cmd << ")")
  // read command's output
  std::ostringstream ss;
  char buf[1025];
  size_t nbytes;
  do {
    nbytes = ::fread(buf, 1, sizeof(buf)-1, f);
    if (nbytes > 0) {
      buf[nbytes] = 0;
      ss << buf;
    }
  } while (nbytes == sizeof(buf)-1);
  // cleanup
  ::fclose(f);
  //
  return ss.str();
}

void ckSyscallError(int res, const char *syscall, const char *arg, const std::function<bool(int)> whiteWash) {
  if (res == -1 && !whiteWash(errno))
    ERR2("system call", "'" << syscall << "' failed, arg=" << arg << ": " << strerror(errno))
}

std::string tmSecMs() {
  static bool firstTime = true;
  static timeval tpStart;
  if (firstTime) {
    gettimeofday(&tpStart, NULL);
    firstTime = false;
  }

  struct timeval tp;
  gettimeofday(&tp, NULL);

  auto sec = tp.tv_sec - tpStart.tv_sec;
  auto usec = tp.tv_usec - tpStart.tv_usec;
  if (usec < 0) {
    sec--;
    usec += 1000000;
  }

  return STR(sec << "." << std::setw(3) << std::setfill('0') << usec/1000);
}

std::string filePathToBareName(const std::string &path) {
  auto i = path.rfind(sepFilePath);
  std::string p = (i != std::string::npos ? path.substr(i + 1) : path);
  i = p.find(sepFileExt);
  return i != std::string::npos ? p.substr(0, i) : p;
}

std::string filePathToFileName(const std::string &path) {
  auto i = path.rfind(sepFilePath);
  return i != std::string::npos ? path.substr(i + 1) : path;
}

int getSysctlInt(const char *name) {
  int value;
  size_t size = sizeof(value);

  SYSCALL(::sysctlbyname(name, &value, &size, nullptr, 0), "sysctlbyname (get int)", name);

  return value;
}

void setSysctlInt(const char *name, int value) {
  SYSCALL(::sysctlbyname(name, nullptr, nullptr, &value, sizeof(value)), "sysctlbyname (set int)", name);
}

std::string getSysctlString(const char *name) {
  char buf[256];
  size_t size = sizeof(buf) - 1;
  SYSCALL(::sysctlbyname(name, buf, &size, nullptr, 0), "sysctlbyname (get string)", name);
  buf[size] = 0;
  return buf;
}

void ensureKernelModuleIsLoaded(const char *name) {
  SYSCALL(::kldload("ipfw_nat"), "kldload", name, [](int err) {return err == EEXIST;});
}

std::string gethostname() {
  char name[256];
  SYSCALL(::gethostname(name, sizeof(name)), "gethostname", "");
  return name;
}

std::vector<std::string> splitString(const std::string &str, const std::string &delimiter) {
  std::vector<std::string> res;
  std::string s = str;

  size_t pos = 0;
  while ((pos = s.find(delimiter)) != std::string::npos) {
    if (pos > 0)
      res.push_back(s.substr(0, pos));
    s.erase(0, pos + delimiter.length());
  }
  if (!s.empty())
    res.push_back(s);

  return res;
}

std::string stripTrailingSpace(const std::string &str) {
  unsigned sz = str.size();
  while (sz > 0 && ::isspace(str[sz-1]))
    sz--;
  return str.substr(0, sz);
}

unsigned toUInt(const std::string &str) {
  std::size_t pos;
  auto u = std::stoul(str, &pos);
  if (pos != str.size())
    ERR2("convert string to unsigned", "trailing characters in string '" << str << "'")
  return u;
}

std::string pathSubstituteVarsInPath(const std::string &path) {
  if (path.size() > 5 && path.substr(0, 5) == "$HOME")
    return STR(::getpwuid(myuid)->pw_dir << path.substr(5));

  return path;
}

std::string pathSubstituteVarsInString(const std::string &str) {
  auto substOne = [](const std::string &str, const std::string &key, const std::string &val) {
    std::string s = str;
    while (true) {
      auto off = s.find(key);
      if (off != std::string::npos && (off + key.size() == s.size() || !std::isalnum(s[off + key.size()])))
        s = STR(s.substr(0, off) << val << s.substr(off + key.size()));
      if (off == std::string::npos)
        break;
    }
    return s;
  };

  auto uidInfo = ::getpwuid(myuid);
  std::string s = str;
  for (auto kv : std::map<std::string, std::string>({{"$HOME", uidInfo->pw_dir}, {"$USER", uidInfo->pw_name}}))
    s = substOne(s, kv.first, kv.second);

  return s;
}

std::vector<std::string> reverseVector(const std::vector<std::string> &v) {
  auto vc = v;
  std::reverse(vc.begin(), vc.end());
  return vc;
}

namespace Fs {

namespace fs = std::filesystem;

bool fileExists(const std::string &path) {
  struct stat sb;
  return ::stat(path.c_str(), &sb) == 0 && sb.st_mode & S_IFREG;
}

bool dirExists(const std::string &path) {
  struct stat sb;
  return ::stat(path.c_str(), &sb) == 0 && sb.st_mode & S_IFDIR;
}

std::vector<std::string> readFileLines(int fd) {
  std::vector<std::string> lines;

  FILE *file = ::fdopen(fd, "r");
  char *line = nullptr;
  size_t len = 0;
  ssize_t read;
  // read lines
  while ((read = ::getline(&line, &len, file)) != -1)
    lines.push_back(line);
  // error?
  if (::ferror(file))
    ERR2("read file", "reading file failed")
  // clean up
  ::free(line);
  if (::fdclose(file, nullptr) != 0)
    std::cerr << "reading file failed: " << strerror(errno) << std::endl;

  return lines;
}

size_t getFileSize(int fd) {
  struct stat sb;
  if (::fstat(fd, &sb) == -1)
    ERR2("get file size", STR("failed to stat the file: " << strerror(errno)))
  return sb.st_size;
}

void writeFile(const std::string &data, int fd) {
  auto res = ::write(fd, data.c_str(), data.size());
  if (res == -1) {
    auto err = STR("failed to write file: " << strerror(errno));
    (void)::close(fd);
    ERR2("write file", err)
  } else if (res != (int)data.size()) {
    (void)::close(fd);
    ERR2("write file", "short write in file, attempted to write " << data.size() << " bytes, actually wrote only " << res << " bytes")
  }
}

void writeFile(const std::string &data, const std::string &file) {
  int fd;
  SYSCALL(fd = ::open(file.c_str(), O_WRONLY|O_CREAT), "open", file.c_str());

  writeFile(data, fd);

  SYSCALL(::close(fd), "close", file.c_str());
}

void appendFile(const std::string &data, const std::string &file) {
  int fd;
  SYSCALL(fd = ::open(file.c_str(), O_WRONLY|O_CREAT|O_APPEND), "open", file.c_str());

  writeFile(data, fd);

  SYSCALL(::close(fd), "close", file.c_str());
}

void chmod(const std::string &path, mode_t mode) {
  SYSCALL(::chmod(path.c_str(), mode), "chmod", path.c_str());
}

void chown(const std::string &path, uid_t owner, gid_t group) {
  SYSCALL(::chown(path.c_str(), owner, group), "chown", path.c_str());
}

void link(const std::string &name1, const std::string &name2) {
  SYSCALL(::link(name1.c_str(), name2.c_str()), "link", name1.c_str());
}

void unlink(const std::string &file) {
  auto res = ::unlink(file.c_str());
  if (res == -1 && errno == EPERM) { // this unlink function clears the schg extended flag in case of EPERM, because in our context EPERM often indicates schg
    SYSCALL(::chflags(file.c_str(), 0/*flags*/), "chflags", file.c_str());
    SYSCALL(::unlink(file.c_str()), "unlink (2)", file.c_str()); // repeat the unlink call
    return;
  }
  SYSCALL(res, "unlink (1)", file.c_str());
}

void mkdir(const std::string &dir, mode_t mode) {
  SYSCALL(::mkdir(dir.c_str(), mode), "mkdir", dir.c_str());
}

void rmdir(const std::string &dir) {
  auto res = ::rmdir(dir.c_str());
  if (res == -1 && errno == EPERM) { // this rmdir function clears the schg extended flag in case of EPERM, because in our context EPERM often indicates schg
    SYSCALL(::chflags(dir.c_str(), 0/*flags*/), "chflags", dir.c_str());
    SYSCALL(::rmdir(dir.c_str()), "rmdir (2)", dir.c_str()); // repeat the rmdir call
    return;
  }
  SYSCALL(res, "rmdir (1)", dir.c_str());
}

void rmdirFlat(const std::string &dir) {
  for (const auto &entry : fs::directory_iterator(dir))
    unlink(entry.path());
  rmdir(dir);
}

void rmdirHier(const std::string &dir) {
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.is_symlink())
      unlink(entry.path());
    else if (entry.is_directory())
      rmdirHier(entry.path());
    else
      unlink(entry.path());
  }
  rmdir(dir);
}

bool rmdirFlatExcept(const std::string &dir, const std::set<std::string> &except) {
  bool someSkipped = false;
  for (const auto &entry : fs::directory_iterator(dir))
    if (except.find(entry.path()) == except.end())
      unlink(entry.path());
    else
      someSkipped = true;
  if (!someSkipped)
    rmdir(dir);
  return someSkipped;
}

bool rmdirHierExcept(const std::string &dir, const std::set<std::string> &except) {
  unsigned cntSkip = 0;
  for (const auto &entry : fs::directory_iterator(dir))
    if (except.find(entry.path()) == except.end()) {
      if (entry.is_symlink())
        unlink(entry.path());
      else if (entry.is_directory())
        cntSkip += rmdirHierExcept(entry.path(), except);
      else
        unlink(entry.path());
    } else {
      cntSkip++;
    }
  if (cntSkip == 0)
    rmdir(dir);
  return cntSkip > 0;
}

bool isXzArchive(const char *file) {
  int res;
  struct stat sb;
  res = ::stat(file, &sb);
  if (res == -1)
    return false; // can't stat: can't be an XZ archive file

  if (sb.st_mode & S_IFREG && sb.st_size > 0x100) { // the XZ archive file can't be too small
    uint8_t signature[5];
    // read the signature
    int fd = ::open(file, O_RDONLY);
    if (fd == -1)
      return false; // can't open: can't be an XZ archive
    auto res = ::read(fd, signature, sizeof(signature));
    if (res != sizeof(signature)) {
      (void)::close(fd);
      return false; // can't read the file or read returned not 5: can't be an XZ archive file
    }
    if (::close(fd) == -1)
      return false; // failed to close the file: there is something wrong, we don't accept it as an XZ archive
    // check signature
    return signature[0]==0xfd && signature[1]==0x37 && signature[2]==0x7a && signature[3]==0x58 && signature[4]==0x5a;
  } else {
    return false; // not a regular file: can't be an XZ archive file
  }
}

char isElfFileOrDir(const std::string &file) { // find if the file is a regular file, has the exec bit set, and is a dynamic ELF file
  int res;

  struct stat sb;
  res = ::stat(file.c_str(), &sb);
  if (res == -1) {
    WARN("isElfFile: failed to stat the file '" << file << "': " << strerror(errno))
    return 'N'; // ? what else to do after the above
  }

  // directory?
  if (sb.st_mode & S_IFDIR)
    return 'D';

  // object files aren't dynamic ELFs
  if (file.size() > 2 && file[file.size()-1] == 'o' && file[file.size()-2] == '.')
    return 'N';

  if (sb.st_mode & S_IFREG /*&& sb.st_mode & S_IXUSR*/ && sb.st_size > 0x80) { // this reference claims that ELF can be as small as 142 bytes: http://timelessname.com/elfbin/
    // x-bit is disabled above: some .so files have no exec bit, particularly /usr/lib/pam_*.so
    uint8_t signature[4];
    // read the signature
    int fd = ::open(file.c_str(), O_RDONLY);
    if (fd == -1) {
      WARN("isElfFile: failed to open the file '" << file << "': " << strerror(errno))
      return 'N'; // ? what else to do after the above
    }
    auto res = ::read(fd, signature, sizeof(signature));
    if (res == -1)
      WARN("isElfFile: failed to read signature from '" << file << "': " << strerror(errno))
    if (::close(fd) == -1)
      WARN("isElfFile: failed to close the file '" << file << "': " << strerror(errno))
    // decide
    return res == 4 && signature[0]==0x7f && signature[1]==0x45 && signature[2]==0x4c && signature[3]==0x46 ? 'E' : 'N';
  } else {
    return 'N';
  }
}

std::set<std::string> findElfFiles(const std::string &dir) {
  std::set<std::string> s;
  std::function<void(const std::string&)> addElfFilesToSet;
  addElfFilesToSet = [&s,&addElfFilesToSet](const std::string &dir) {
    for (const auto &entry : fs::directory_iterator(dir))
      switch (isElfFileOrDir(entry.path())) {
      case 'E':
        s.insert(entry.path());
        break;
      case 'D':
        addElfFilesToSet(entry.path());
        break;
      default:
        ; // do nothing
      }
  };

  addElfFilesToSet(dir);

  return s;
}

bool hasExtension(const char *file, const char *extension) {
  auto ext = ::strrchr(file, '.');
  return ext != nullptr && ::strcmp(ext, extension) == 0;
}

void copyFile(const std::string &srcFile, const std::string &dstFile) {
  try {
    fs::copy_file(srcFile, dstFile);
  } catch (fs::filesystem_error& e) {
    ERR2("copy file", "could not copy file file1.txt: " << e.what())
  }
}

std::vector<std::string> expandWildcards(const std::string &wildcardPath, const std::string &cmdPrefix) {
  return splitString(runCommandGetOutput(STR(cmdPrefix << "/bin/ls " << wildcardPath), "wildcard expansion"), "\n"); // XXX the 'wildcard' library might help (?)
}

}

}
