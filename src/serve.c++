// Copyright 2016 Steven Dee. All rights reserved.

// Hack around stdlib bug with C++14.
#include <initializer_list>   // force libstdc++ to include its config
#undef _GLIBCXX_HAVE_GETS     // correct broken config
// End hack.

#include <algorithm>

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
#include <linux/fs.h>
#include <sys/syscall.h>

#include <sandstorm/grain.capnp.h>
#include <sandstorm/web-session.capnp.h>
#include <sandstorm/hack-session.capnp.h>

typedef unsigned char byte;

namespace {

template <typename T>
class NullHandler final: public capnp::JsonCodec::Handler<T> {
 public:
  void encode(const capnp::JsonCodec& codec, capnp::ReaderFor<T> input,
              capnp::JsonValue::Builder output) const override {
  }

  typename T::Client decode(const capnp::JsonCodec& codec,
                            capnp::JsonValue::Reader input) const override {
    KJ_UNIMPLEMENTED("NullHandler::decode");
  }
};

NullHandler<sandstorm::Identity> identity_handler;
capnp::JsonCodec json;

kj::AutoCloseFd raiiOpen(kj::StringPtr name, int flags, mode_t mode = 0666) {
  int fd;
  KJ_SYSCALL(fd = open(name.cStr(), flags, mode), name);
  return kj::AutoCloseFd(fd);
}

kj::Tuple<kj::AutoCloseFd, kj::String> raiiTemp(kj::StringPtr template_) {
  int fd;
  auto name = kj::str(template_, "XXXXXX");
  KJ_SYSCALL(fd = mkstemp(name.begin()), strlen(name.begin()), strlen(name.cStr()), name.cStr(), name, name.size());
  return kj::tuple(kj::AutoCloseFd(fd), kj::mv(name));
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
  KJ_SYSCALL(fd = open(pathname.cStr(), O_RDONLY), pathname);
  KJ_SYSCALL(fdatasync(fd));
  KJ_SYSCALL(close(fd));
  KJ_IF_MAYBE(slashPos, pathname.findLast('/')) {
    syncPath(heapString(pathname.slice(0, *slashPos)));
  }
}

void writeFileAtomic(kj::StringPtr filename, kj::StringPtr content) {
  auto r = raiiTemp("var/tmp/");
  {
    kj::FdOutputStream(kj::mv(kj::get<0>(r)))
        .write(reinterpret_cast<const byte*>(content.begin()), content.size());
  }
  syncPath(kj::get<1>(r));
  KJ_SYSCALL(rename(kj::mv(kj::get<1>(r)).cStr(), filename.cStr()));
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
  // Assumes that scratchname exists and is a copy of filename.
  auto scratchname = kj::str(filename, "~");
  {
    kj::FdOutputStream(raiiOpen(scratchname, O_WRONLY | O_APPEND))
        .write(reinterpret_cast<const byte*>(content.begin()), content.size());
  }
  syncPath(scratchname);
  // XX Ubuntu 16.04.1 doesn't seem to have a definition for renameat2.
  KJ_SYSCALL(syscall(SYS_renameat2, AT_FDCWD, filename.cStr(), AT_FDCWD,
                     scratchname.cStr(), RENAME_EXCHANGE));
  {
    kj::FdOutputStream(raiiOpen(scratchname, O_WRONLY | O_APPEND))
        .write(reinterpret_cast<const byte*>(content.begin()), content.size());
  }
  syncPath(scratchname);
}

kj::String identityStr(capnp::Data::Reader identity) {
  KJ_REQUIRE(identity.size() >= 16, identity.size());
  auto ret = kj::heapString(32);
  auto it = ret.begin();
  for (auto i = 0u; i < 16; ++i) {
    sprintf(it, "%02hhx", byte(identity[i]));
    it += 2;
  }
  return ret;
}

void writeUser(sandstorm::UserInfo::Reader userInfo) {
  auto idStr = identityStr(userInfo.getIdentityId());
  writeFileAtomic(kj::str("var/users/", idStr), json.encode(userInfo));
}

class WebSessionImpl final: public sandstorm::WebSession::Server {
 public:
  WebSessionImpl(sandstorm::UserInfo::Reader userInfo,
                 sandstorm::SessionContext::Client context,
                 sandstorm::WebSession::Params::Reader params) {
    if (userInfo.hasPreferredHandle()) {
      preferredHandle = kj::heapString(userInfo.getPreferredHandle());
    } else {
      preferredHandle = kj::heapString("anon");
    }
    KJ_LOG(INFO, "new session", preferredHandle);
    writeUser(userInfo);
  }

  kj::Promise<void> get(GetContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "") {
      auto response = context.getResults().initContent();
      response.setMimeType("text/html");
      response.setEncoding("gzip");
      response.getBody().setBytes(readFile("index.html.gz").asBytes());
    } else if (path == "chats") {
      auto response = context.getResults().initContent();
      response.setMimeType("text/plain");
      response.getBody().setBytes(readFile("var/chats").asBytes());
      // TODO(soon): user profiles
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
    KJ_LOG(INFO, "Got post: ", path, ".");
    if (path == "chats") {
      kj::Vector<char> postText;
      auto content = context.getParams().getContent().getContent().asChars();
      for (auto c : content) {
        if (c != '\n')
          postText.add(c);
      }
      appendDataAtomic("var/chats",
                       kj::str(preferredHandle, ": ", postText, "\n"));
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
    KJ_LOG(INFO, "Got put: ", path, ".");
    if (path == "topic") {
      writeFileAtomic("var/topic", kj::str(context.getParams().getContent().getContent().asChars()));
      auto response = context.getResults().initRedirect();
      response.setIsPermanent(false);
      response.setSwitchToGet(true);
      response.setLocation("/");
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

  kj::String preferredHandle;
};

class UiViewImpl final: public sandstorm::UiView::Server {
 public:
  kj::Promise<void> newSession(NewSessionContext context) override {
    auto params = context.getParams();
    KJ_REQUIRE(params.getSessionType() == capnp::typeId<sandstorm::WebSession>(),
               "Unsupported session type.");

    context.getResults().setSession(
        kj::heap<WebSessionImpl>(params.getUserInfo(), params.getContext(),
                                 params.getSessionParams().getAs<sandstorm::WebSession::Params>()));

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
    KJ_LOG(INFO, "init");
    KJ_SYSCALL(mkdir("var/users", 0777));
    KJ_SYSCALL(mkdir("var/tmp", 0777));
    writeFileAtomic("var/topic", "Random chatter");
    writeFileAtomic("var/chats", "");

    return true;
  }

  kj::MainBuilder::Validity run() {
    KJ_LOG(INFO, "run");
    removeAllFiles("var/tmp");
    auto r = unlink("var/chats~");
    if (r == -1 && errno != ENOENT) {
      KJ_FAIL_SYSCALL("unlink", errno);
    }
    copyFile("var/chats", "var/chats~");

    json.addTypeHandler(identity_handler);
    // TODO(soon): identity id handler.

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
