// Copyright 2016 Steven Dee. All rights reserved.

// Hack around stdlib bug with C++14.
#include <initializer_list>   // force libstdc++ to include its config
#undef _GLIBCXX_HAVE_GETS     // correct broken config
// End hack.

#if UNIQUE_HANDLES
#include <set>
#endif

#include <kj/main.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/async-io.h>
#include <kj/tuple.h>
#include <capnp/compat/json.h>
#include <capnp/rpc-twoparty.h>
#include <capnp/serialize.h>
#include <unistd.h>
#include <cstdlib>
#include <fcntl.h>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/syscall.h>

#include <sandstorm/grain.capnp.h>
#include <sandstorm/web-session.capnp.h>
#include <sandstorm/hack-session.capnp.h>

typedef unsigned char byte;

namespace {

kj::AutoCloseFd raiiOpen(kj::StringPtr name, int flags, mode_t mode = 0666) {
  int fd;
  KJ_SYSCALL(fd = open(name.cStr(), flags, mode), name);
  return kj::AutoCloseFd(fd);
}

void writeFile(kj::StringPtr filename, kj::StringPtr content) {
  kj::FdOutputStream(raiiOpen(filename, O_WRONLY | O_CREAT | O_EXCL))
      .write(reinterpret_cast<const byte*>(content.begin()), content.size());
}

kj::String readFile(kj::StringPtr filename) {
  char buf[BUFSIZ];
  kj::FdInputStream in(raiiOpen(filename, O_RDONLY));
  kj::Vector<char> ret;
  while (true) {
    auto r = in.tryRead(buf, sizeof(buf), sizeof(buf));
    ret.addAll(buf, buf + r);
    if (r < sizeof(buf))
      break;
  }
  ret.add('\0');
  return kj::String(ret.releaseAsArray());
}

void syncPath(kj::StringPtr pathname) {
  int fd;
  // XX cheez. don't sync / since it's read-only.
  if (pathname == "")
    return;
  KJ_SYSCALL(fd = open(pathname.cStr(), O_RDONLY), pathname);
  KJ_SYSCALL(fdatasync(fd));
  KJ_SYSCALL(close(fd));
  KJ_IF_MAYBE(slashPos, pathname.findLast('/')) {
    syncPath(heapString(pathname.slice(0, *slashPos)));
  }
}

void writeFileAtomic(kj::StringPtr filename, kj::StringPtr content) {
  int fd;
  auto name = kj::str("/var/tmp/tmp.XXXXXX");
  KJ_SYSCALL(fd = mkstemp(name.begin()), name);
  {
    kj::FdOutputStream(fd)
        .write(reinterpret_cast<const byte*>(content.begin()), content.size());
  }
  syncPath(name);
  KJ_SYSCALL(rename(name.cStr(), filename.cStr()));
}

void removeAllFiles(kj::StringPtr dirname) {
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

void copyFile(kj::StringPtr from, kj::StringPtr to) {
  // TODO(cleanup): this
  char buf[BUFSIZ];
  kj::FdOutputStream to_file(raiiOpen(to, O_WRONLY | O_CREAT | O_TRUNC));
  kj::FdInputStream from_file(raiiOpen(from, O_RDONLY));
  while (true) {
    auto r = from_file.tryRead(buf, BUFSIZ, BUFSIZ);
    to_file.write(buf, r);
    if (r < BUFSIZ)
      break;
  }
}

void appendDataAtomic(kj::StringPtr filename, kj::StringPtr content) {
  // Relies on short writes in Linux being atomic.
  // magic number: https://stackoverflow.com/a/24270790
  KJ_REQUIRE(content.size() <= 1008);
  kj::FdOutputStream(raiiOpen(filename, O_WRONLY | O_APPEND))
      .write(reinterpret_cast<const byte*>(content.begin()), content.size());
  syncPath(filename);
}

kj::String identityStr(capnp::Data::Reader identity) {
  kj::Vector<char> ret;
  char buf[3];
  for (auto i = 0u; i < identity.size() && i < 16; ++i) {
    sprintf(buf, "%02hhx", byte(identity[i]));
    ret.addAll(buf, buf + 2);
  }
  return kj::String(ret.releaseAsArray());
}

// TODO(soon): not global
kj::Vector<kj::Own<kj::PromiseFulfiller<void>>> chatRequests;
kj::Vector<kj::Own<kj::PromiseFulfiller<void>>> topicRequests;
#if UNIQUE_HANDLES
std::set<kj::String> onlineUsers;
#endif

void writeChat(kj::String&& line) {
  appendDataAtomic("var/chats", kj::str(kj::mv(line), "\n"));
  for (auto& fulfiller: chatRequests) {
    fulfiller->fulfill();
  }
  chatRequests.clear();
}

class WebSessionImpl final: public sandstorm::WebSession::Server {
 public:
  WebSessionImpl(sandstorm::UserInfo::Reader userInfo,
                 sandstorm::SessionContext::Client context,
                 sandstorm::WebSession::Params::Reader params,
                 capnp::Data::Reader tabId):
      handle(uniqueHandle(userInfo)) {
#if UNIQUE_HANDLES
    writeChat(kj::str(handle, " (", uniqueIdentity(userInfo, tabId),
                      ") has joined"));
#endif
  }

  ~WebSessionImpl() noexcept(false) {
    writeChat(kj::str(handle, " has left"));
  }

  // TODO(soon): factor out respondWith
  kj::Promise<void> get(GetContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "") {
      auto response = context.getResults().initContent();
      response.setMimeType("text/html");
      response.setEncoding("gzip");
      response.getBody().setBytes(readFile("index.html.gz").asBytes());
    } else if (path == "chats?new") {
      auto ret = kj::newPromiseAndFulfiller<void>();
      chatRequests.add(kj::mv(ret.fulfiller));
      return ret.promise.then([context]() mutable {
        auto response = context.getResults().initContent();
        response.setMimeType("text/plain");
        response.getBody().setBytes(readFile("var/chats").asBytes());
      });
    } else if (path == "chats") {
      auto response = context.getResults().initContent();
      response.setMimeType("text/plain");
      response.getBody().setBytes(readFile("var/chats").asBytes());
    } else if (path == "topic?new") {
      auto ret = kj::newPromiseAndFulfiller<void>();
      topicRequests.add(kj::mv(ret.fulfiller));
      return ret.promise.then([context]() mutable {
        auto response = context.getResults().initContent();
        response.setMimeType("text/plain");
        response.getBody().setBytes(readFile("var/topic").asBytes());
      });
    } else if (path == "topic") {
      auto response = context.getResults().initContent();
      response.setMimeType("text/plain");
      response.getBody().setBytes(readFile("var/topic").asBytes());
    } else {
      auto response = context.getResults().initClientError();
      response.setStatusCode(sandstorm::WebSession::Response::ClientErrorCode::BAD_REQUEST);
      response.setDescriptionHtml(kj::str("Don't know how to get ", path));
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> post(PostContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "chats") {
      kj::Vector<char> postText;
      auto content = context.getParams().getContent().getContent().asChars();
      for (auto c : content) {
        if (c != '\n')
          postText.add(c);
      }
      writeChat(kj::str(handle, ": ", postText));
      auto response = context.getResults().initRedirect();
      response.setIsPermanent(false);
      response.setSwitchToGet(true);
      response.setLocation("/");
    } else {
      auto response = context.getResults().initClientError();
      response.setStatusCode(sandstorm::WebSession::Response::ClientErrorCode::BAD_REQUEST);
      response.setDescriptionHtml(kj::str("Don't know how to post ", path));
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> put(PutContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "topic") {
      writeFileAtomic("var/topic", kj::str(context.getParams().getContent().getContent().asChars()));
      auto response = context.getResults().initRedirect();
      response.setIsPermanent(false);
      response.setSwitchToGet(true);
      response.setLocation("/");
      for (auto& fulfiller: topicRequests) {
        fulfiller->fulfill();
      }
      topicRequests.clear();
    } else {
      auto response = context.getResults().initClientError();
      response.setStatusCode(sandstorm::WebSession::Response::ClientErrorCode::BAD_REQUEST);
      response.setDescriptionHtml(kj::str("Don't know how to put ", path));
    }
    return kj::READY_NOW;
  }

 private:
  void requireCanonicalPath(kj::StringPtr path) {
    KJ_REQUIRE(!path.startsWith("/"));
    KJ_REQUIRE(!path.startsWith("./") && path != ".");
    KJ_REQUIRE(!path.startsWith("../") && path != "..");
    KJ_IF_MAYBE(slashPos, path.findFirst('/')) {
      requireCanonicalPath(path.slice(*slashPos + 1));
    }
  }

  kj::String uniqueHandle(sandstorm::UserInfo::Reader userInfo) {
    kj::Vector<char> baseHandle;
    if (userInfo.hasPreferredHandle()) {
      for (auto c : userInfo.getPreferredHandle()) {
        if (c != ' ' && c != '\n')
          baseHandle.add(c);
      }
    } else {
      baseHandle.addAll(kj::StringPtr("anon"));
    }
#if !UNIQUE_HANDLES
    return kj::heapString(baseHandle.releaseAsArray());
#else
    auto preferredHandle = kj::heapString(baseHandle.releaseAsArray());
    if (onlineUsers.find(preferredHandle) == onlineUsers.end()) {
      onlineUsers.emplace(kj::heapString(preferredHandle));
      return kj::mv(preferredHandle);
    } else {
      for (auto i = 1u; i < 8192; ++i) {
        auto handle = kj::str(preferredHandle, i);
        if (onlineUsers.find(handle) == onlineUsers.end()) {
          onlineUsers.emplace(kj::heapString(handle));
          return kj::mv(handle);
        }
      }
    }
    KJ_REQUIRE(false, "couldn't find a suitable nick");
  }

  kj::String uniqueIdentity(sandstorm::UserInfo::Reader userInfo,
                            capnp::Data::Reader tabId) {
    kj::Vector<char> ret;
    if (userInfo.hasDisplayName()) {
      ret.addAll(userInfo.getDisplayName().getDefaultText());
      ret.addAll(kj::StringPtr(", "));
    }
    if (userInfo.hasIdentityId()) {
      ret.addAll(kj::StringPtr("user-"));
      ret.addAll(identityStr(userInfo.getIdentityId()));
    } else {
      ret.addAll(kj::StringPtr("tab-"));
      ret.addAll(identityStr(tabId));
    }
    return kj::String(ret.releaseAsArray());
#endif  // UNIQUE_HANDLES
  }

  kj::String handle;
};

class UiViewImpl final: public sandstorm::UiView::Server {
 public:
  kj::Promise<void> newSession(NewSessionContext context) override {
    auto params = context.getParams();
    KJ_REQUIRE(params.getSessionType() == capnp::typeId<sandstorm::WebSession>(),
               "Unsupported session type.");

    context.getResults().setSession(
        kj::heap<WebSessionImpl>(params.getUserInfo(), params.getContext(),
                                 params.getSessionParams().getAs<sandstorm::WebSession::Params>(),
                                 params.getTabId()));

    return kj::READY_NOW;
  }
};

class Serve {
 public:
  Serve(kj::ProcessContext& context): context(context), ioContext(kj::setupAsyncIo()) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "app server",
                           "Intended to be run as the root process of a Sandstorm app.")
        .addOption({'i'}, KJ_BIND_METHOD(*this, init), "Initialize a new grain.")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

  kj::MainBuilder::Validity init() {
    KJ_SYSCALL(mkdir("var/tmp", 0777));
    writeFileAtomic("var/topic", "Random chatter");
    writeFileAtomic("var/chats", "");

    return true;
  }

  kj::MainBuilder::Validity run() {
    removeAllFiles("var/tmp");
    auto r = unlink("var/chats~");
    if (r == -1 && errno != ENOENT) {
      KJ_FAIL_SYSCALL("unlink", errno);
    }

#if UNIQUE_HANDLES
    writeChat(kj::heapString("restarted"));
#endif

    auto stream = ioContext.lowLevelProvider->wrapSocketFd(3);
    capnp::TwoPartyVatNetwork network(*stream, capnp::rpc::twoparty::Side::CLIENT);
    auto rpcSystem = capnp::makeRpcServer(network, kj::heap<UiViewImpl>());

    {
      capnp::MallocMessageBuilder message;
      auto vatId = message.getRoot<capnp::rpc::twoparty::VatId>();
      vatId.setSide(capnp::rpc::twoparty::Side::SERVER);
      sandstorm::SandstormApi<>::Client api =
          rpcSystem.bootstrap(vatId).castAs<sandstorm::SandstormApi<>>();
    }

    kj::NEVER_DONE.wait(ioContext.waitScope);
  }

 private:
  kj::ProcessContext& context;
  kj::AsyncIoContext ioContext;
};

}   // namespace

KJ_MAIN(Serve)
