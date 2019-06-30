
#include "args.h"
#include "spec.h"
#include "file.h"
#include "util.h"
#include "commands.h"

#include <rang.hpp>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>


#define ERR(msg...) ERR2("creating a crate", msg)

#define LOG(msg...) std::cerr << rang::fg::gray << Util::tmSecMs() << ": " << msg << rang::style::reset << std::endl;

#define SYSCALL(res, syscall, arg) Util::ckSyscallError(res, syscall, arg)

// used paths
const char *baseArchive = "/home/yuri/jails-learning/base.txz";
//const char *jailDirectoryPath = "/home/yuri/jails-learning";
const char *jailDirectoryPath = "/home/yuri/github/crate";
const char *jailName = "_jail_create_";

//
// helpers
//

static std::string guessCrateName(const Spec &spec) {
  if (!spec.runExecutable.empty())
    return spec.runExecutable.substr(spec.runExecutable.rfind('/') + 1);
  else
    return spec.runService[0]; // XXX service might have arguments, etc.
}

static void removeRedundantJailParts(const std::string &jailPath, const Spec &spec) {

  namespace Fs = Util::Fs;
  
  // helpers
  auto P = [&jailPath](const char *subdir) {
    return STR(jailPath << '/' << subdir);
  };

  // remove items
  Fs::rmdirFlat(P("bin"));
  Fs::rmdirHier(P("boot"));
  Fs::rmdirHier(P("etc/periodic"));
  Fs::unlink(P("usr/lib/include"));
  Fs::rmdirHierExcept(P("lib"), {P("lib/libz.so.6"), P("lib/libc.so.7"), P("lib/libthr.so.3")});
  Fs::rmdirHierExcept(P("usr/lib"), {P("usr/lib/liblzma.so.5"), P("usr/lib/libbz2.so.4")});
  Fs::rmdirHier(P("usr/lib32"));
  Fs::rmdirHier(P("usr/include"));
  Fs::rmdirHier(P("sbin"));
  Fs::rmdirHier(P("usr/sbin"));
  Fs::rmdirHierExcept(P("usr/libexec"), {P("usr/libexec/ld-elf.so.1")});
  Fs::rmdirHier(P("usr/share/dtrace"));
  Fs::rmdirHier(P("usr/share/doc"));
  Fs::rmdirHier(P("usr/share/examples"));
  Fs::rmdirHier(P("usr/share/bsdconfig"));
  Fs::rmdirHier(P("usr/share/games"));
  Fs::rmdirHier(P("usr/share/i18n"));
  Fs::rmdirHier(P("usr/share/man"));
  Fs::rmdirHier(P("usr/share/misc"));
  Fs::rmdirHier(P("usr/share/pc-sysinstall"));
  Fs::rmdirHier(P("usr/share/openssl"));
  Fs::rmdirHier(P("usr/tests"));
  Fs::rmdir    (P("usr/src"));
  Fs::rmdir    (P("usr/obj"));
  Fs::rmdirHier(P("var/db/etcupdate"));
  Fs::rmdirHierExcept(P("usr/bin"), {P("usr/bin/gzip")});
  Fs::rmdirFlat(P("rescue"));
}

//
// interface
//

bool createCrate(const Args &args, const Spec &spec) {
  
  int res;

  // create a jail directory
  auto jailPath = STR(jailDirectoryPath << "/" << jailName);
  res = mkdir(jailPath.c_str(), S_IRUSR|S_IWUSR|S_IXUSR);
  if (res == -1)
    ERR("failed to create the jail directory '" << jailPath << "': " << strerror(errno))

  // unpack the base archive
  LOG("unpacking the base archive")
  //Util::runCommand(STR("tar -xf " << baseArchive << " -C " << jailPath), "unpack the system base into the jail directory");
  Util::runCommand(STR("cat " << baseArchive << " | xz --decompress --threads=8 | tar -xf - -C " << jailPath), "unpack the system base into the jail directory");

  // install packages in the jail, if needed

  // remove parts that aren't needed
  LOG("remove unnecessary parts")
  removeRedundantJailParts(jailPath, spec);
  //abort();

  // pack the jail into a .crate file
  LOG("creating the crate file")
  CrateFile::create(
    jailPath,
    !args.createOutput.empty() ? args.createOutput : STR(guessCrateName(spec) << ".crate")
  );

  // remove the create directory
  LOG("removing the the jail directory")
  Util::Fs::rmdirHier(jailPath);

  return true;
}
