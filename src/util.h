// Copyright 2016 Steven Dee. All rights reserved.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <kj/async.h>
#include <kj/common.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/vector.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <sandstorm/web-session.capnp.h>

typedef unsigned char byte;

namespace u {

class WaitQueue {
 public:
  inline kj::Promise<void> wait() {
    // Returns a promise that resolves when ready() is called.
    auto ret = kj::newPromiseAndFulfiller<void>();
    requests.add(kj::mv(ret.fulfiller));
    return kj::mv(ret.promise);
  }

  inline void ready() {
    for (auto& request: requests) {
      request->fulfill();
    }
    requests.clear();
  }

 private:
  kj::Vector<kj::Own<kj::PromiseFulfiller<void>>> requests;
};

inline kj::AutoCloseFd raiiOpen(kj::StringPtr name, int flags, mode_t mode = 0666) {
  int fd;
  KJ_SYSCALL(fd = open(name.cStr(), flags, mode), name);
  return kj::AutoCloseFd(fd);
}

inline uint64_t getFileSize(int fd, kj::StringPtr filename) {
  struct stat stats;
  KJ_SYSCALL(fstat(fd, &stats), filename);
  KJ_REQUIRE(S_ISREG(stats.st_mode), "Not a regular file.", filename);
  return stats.st_size;
}

inline kj::String readFile(kj::StringPtr filename) {
  auto fd = raiiOpen(filename, O_RDONLY);
  auto size = getFileSize(fd, filename);
  auto ret = kj::heapString(size);
  kj::FdInputStream in(kj::mv(fd));
  in.read(ret.begin(), size);
  return kj::mv(ret);
}

inline kj::String dirName(kj::StringPtr path) {
  KJ_IF_MAYBE(slashPos, path.findLast('/')) {
    return kj::heapString(path.slice(0, *slashPos));
  } else {
    return kj::heapString("");
  }
}

inline void syncPath(kj::StringPtr pathname) {
  // XX cheez. don't sync / since it's read-only.
  if (pathname == "")
    return;

  {
    auto fd = raiiOpen(pathname, O_RDONLY);
    KJ_SYSCALL(fdatasync(fd));
  }
  syncPath(dirName(pathname));
}

inline void writeFileAtomic(kj::StringPtr filename, kj::StringPtr content) {
  auto name = kj::str("/var/tmp/tmp.XXXXXX");
  int fd;
  KJ_SYSCALL(fd = mkstemp(name.begin()), name);
  {
    kj::FdOutputStream stream{kj::AutoCloseFd(fd)};   // XX most vexing parse
    stream.write(reinterpret_cast<const byte*>(content.begin()), content.size());
    fdatasync(fd);
  }
  syncPath(dirName(name));
  KJ_SYSCALL(rename(name.cStr(), filename.cStr()));
}

inline void removeAllFiles(kj::StringPtr dirname) {
  // Removes all files in a directory. Non-recursive (doesn't remove subdirectories.)
  DIR *dir = opendir(dirname.cStr());
  KJ_DEFER(KJ_SYSCALL(closedir(dir)));
  struct dirent *next_file;

  while ((next_file = readdir(dir))) {
    if (!strcmp(next_file->d_name, ".") || !strcmp(next_file->d_name, "..")) {
      continue;
    }
    KJ_SYSCALL(remove(kj::str(dirname, "/", next_file->d_name).cStr()),
               dirname, next_file->d_name);
  }
}

inline kj::String showAsHex(kj::ArrayPtr<const byte> arr) {
  kj::Vector<char> ret(arr.size() * 2 + 1);
  char buf[3];
  for (auto c: arr) {
    sprintf(buf, "%02hhx", c);
    ret.addAll(buf, buf + 2);
  }
  ret.add('\0');
  return kj::String(ret.releaseAsArray());
}

template <typename Context>
kj::Promise<void> respondWith(Context ctx, kj::StringPtr body, kj::StringPtr mimeType,
                              bool gzip = false) {
  auto response = ctx.getResults().initContent();
  response.setMimeType(mimeType);
  if (gzip)
    response.setEncoding("gzip");
  response.getBody().setBytes(body.asBytes());
  return kj::READY_NOW;
}

template <typename Context>
kj::Promise<void> respondWithRedirect(Context context, kj::StringPtr location) {
  auto response = context.getResults().initRedirect();
  response.setIsPermanent(false);
  response.setSwitchToGet(true);
  response.setLocation(kj::heapString(location));
  return kj::READY_NOW;
}

template <typename Context>
kj::Promise<void> respondWithNotFound(Context context) {
  auto response = context.getResults().initClientError();
  response.setStatusCode(sandstorm::WebSession::Response::ClientErrorCode::NOT_FOUND);
  return kj::READY_NOW;
}

template <typename Pred>
kj::String filteredString(Pred&& pred, kj::ArrayPtr<const char> input) {
  kj::Vector<char> ret(input.size() + 1);
  for (auto c: input) {
    if (pred(c))
      ret.add(c);
  }
  ret.add('\0');
  return kj::String(ret.releaseAsArray());
}

}   // namespace u
