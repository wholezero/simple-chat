// Copyright 2016 Steven Dee. All rights reserved.

// Hack around stdlib bug with C++14.
#include <initializer_list>   // force libstdc++ to include its config
#undef _GLIBCXX_HAVE_GETS     // correct broken config
// End hack.

#include <set>

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

constexpr auto INDEX_PATH = "index.html.gz";
constexpr auto CHATS_PATH = "var/chats";
constexpr auto CHATSIZE_PATH = "var/.chatsize";
constexpr auto TOPIC_PATH = "var/topic";

kj::AutoCloseFd raiiOpen(kj::StringPtr name, int flags, mode_t mode = 0666) {
  int fd;
  KJ_SYSCALL(fd = open(name.cStr(), flags, mode), name);
  return kj::AutoCloseFd(fd);
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

kj::String showAsHex(kj::ArrayPtr<const byte> arr) {
  kj::Vector<char> ret;
  char buf[3];
  for (auto c: arr) {
    sprintf(buf, "%02hhx", c);
    ret.addAll(buf, buf + 2);
  }
  ret.add('\0');
  return kj::heapString(ret.releaseAsArray());
}

class WaitQueue {
 public:
  kj::Promise<void> wait() {
    // Returns a promise that resolves when ready() is called.
    auto ret = kj::newPromiseAndFulfiller<void>();
    requests.add(kj::mv(ret.fulfiller));
    return kj::mv(ret.promise);
  }

  void ready() {
    for (auto& request: requests) {
      request->fulfill();
    }
    requests.clear();
  }

 private:
  kj::Vector<kj::Own<kj::PromiseFulfiller<void>>> requests;
};

class ChatStream {
 public:
  ChatStream():
      offset(readFile(CHATSIZE_PATH).parseAs<uint64_t>()),
      chatData(prepareStream()),
      chatStream(raiiOpen(CHATS_PATH, O_WRONLY | O_APPEND)) {}

  kj::StringPtr get() const {
    return kj::StringPtr(chatData.begin(), chatData.end());
  }

  kj::Promise<void> onNew() {
    return chatQueue.wait();
  }

  void write(kj::StringPtr line) {
    static const auto nl = "\n";
    chatData.addAll(line);
    chatData.add('\n');
    chatStream.write(reinterpret_cast<const byte*>(line.begin()), line.size());
    chatStream.write(nl, 1);
    syncPath(CHATS_PATH);
    offset += line.size() + 1;
    writeFileAtomic(CHATSIZE_PATH, kj::str(offset));
    chatQueue.ready();
  }

 private:
  kj::Vector<char> prepareStream() {
    // Called in a member initializer. Depends on offset. Initializes chatData.
    // Must be called before chatStream is initialized.
    KJ_SYSCALL(truncate(CHATS_PATH, offset));
    kj::Vector<char> ret;
    ret.addAll(readFile(CHATS_PATH));
    return kj::mv(ret);
  }

  uint64_t offset;
  kj::Vector<char> chatData;
  kj::FdOutputStream chatStream;
  WaitQueue chatQueue;
};

class Topic {
 public:
  Topic(): topic(readFile(TOPIC_PATH)) {}

  kj::StringPtr get() const {
    return topic;
  }

  kj::Promise<void> onNew() {
    return topicQueue.wait();
  }

  void set(kj::StringPtr topic_) {
    topic = kj::heapString(topic_);
    writeFileAtomic(TOPIC_PATH, topic);
    topicQueue.ready();
  }

 private:
  kj::String topic;
  WaitQueue topicQueue;
};

class StaticIndex {
 public:
  StaticIndex(): index(readFile(INDEX_PATH)) {}

  kj::StringPtr get() {
    return index;
  }

 private:
  kj::String index;
};

class UserList {
 public:
  kj::String add(sandstorm::UserInfo::Reader userInfo) {
    KJ_DEFER(usersQueue.ready());
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
    auto handle = kj::heapString(baseHandle.releaseAsArray());
    onlineUsers.emplace(kj::heapString(handle));
    return kj::mv(handle);
#else
    auto startHandle = kj::heapString(baseHandle.releaseAsArray());
    if (onlineUsers.find(startHandle) == onlineUsers.end()) {
      onlineUsers.emplace(kj::heapString(startHandle));
      return kj::mv(startHandle);
    } else {
      for (auto i = 1u; i < 8192; ++i) {
        auto handle = kj::str(startHandle, i);
        if (onlineUsers.find(handle) == onlineUsers.end()) {
          onlineUsers.emplace(kj::heapString(handle));
          return kj::mv(handle);
        }
      }
    }
    KJ_REQUIRE(false, "couldn't find a suitable nick");
#endif  // UNIQUE_HANDLES
  }

  void remove(kj::String&& nick) {
    onlineUsers.erase(nick);
    usersQueue.ready();
  }

  kj::String get() const {
    kj::Vector<char> ret;
    for (auto& user: onlineUsers) {
      ret.addAll(user);
      ret.add('\n');
    }
    return kj::heapString(ret.releaseAsArray());
  }

  kj::Promise<void> onNew() {
    return usersQueue.wait();
  }

 private:
  std::set<kj::String> onlineUsers;
  WaitQueue usersQueue;
};

struct AppState {
 public:
  StaticIndex index;
  ChatStream chats;
  Topic topic;
  UserList users;
};

template <typename Context>
kj::Promise<void> respondWith(Context ctx, kj::StringPtr body, kj::StringPtr mimeType) {
  auto response = ctx.getResults().initContent();
  response.setMimeType(mimeType);
  response.getBody().setBytes(body.asBytes());
  return kj::READY_NOW;
}

template <typename Context>
kj::Promise<void> respondWith(Context ctx, kj::StringPtr body, kj::StringPtr mimeType,
                              kj::StringPtr encoding) {
  auto response = ctx.getResults().initContent();
  response.setMimeType(mimeType);
  response.setEncoding(encoding);
  response.getBody().setBytes(body.asBytes());
  return kj::READY_NOW;
}

class WebSessionImpl final: public sandstorm::WebSession::Server {
 public:
  WebSessionImpl(sandstorm::UserInfo::Reader userInfo,
                 sandstorm::SessionContext::Client context,
                 sandstorm::WebSession::Params::Reader params,
                 capnp::Data::Reader tabId,
                 AppState* appState):
      appState(appState),
      handle(appState->users.add(userInfo)) {
    appState->chats.write(
        kj::str(handle, " (", uniqueIdentity(userInfo, tabId),
                ") has joined"));
  }

  ~WebSessionImpl() noexcept(false) {
    appState->chats.write(kj::str(handle, " has left"));
    appState->users.remove(kj::mv(handle));
  }

  kj::Promise<void> get(GetContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "") {
      return respondWith(context, appState->index.get(), "text/html", "gzip");
    } else if (path == "chats?new") {
      return appState->chats.onNew().then([context, this]() mutable {
        respondWith(context, appState->chats.get(), "text/plain");
      });
    } else if (path == "chats") {
      return respondWith(context, appState->chats.get(), "text/plain");
    } else if (path == "topic?new") {
      return appState->topic.onNew().then([context, this]() mutable {
        respondWith(context, appState->topic.get(), "text/plain");
      });
    } else if (path == "topic") {
      return respondWith(context, appState->topic.get(), "text/plain");
    } else if (path == "users?new") {
      return appState->users.onNew().then([context, this]() mutable {
        respondWith(context, appState->users.get(), "text/plain");
      });
    } else if (path == "users") {
      return respondWith(context, appState->users.get(), "text/plain");
    } else {
      auto response = context.getResults().initClientError();
      response.setStatusCode(sandstorm::WebSession::Response::ClientErrorCode::BAD_REQUEST);
      response.setDescriptionHtml(kj::str("Don't know how to get ", path));
      return kj::READY_NOW;
    }
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
      appState->chats.write(kj::str(handle, ": ", postText));
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
      appState->topic.set(kj::str(context.getParams().getContent().getContent().asChars()));
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

  kj::String uniqueIdentity(sandstorm::UserInfo::Reader userInfo,
                            capnp::Data::Reader tabId) {
    kj::Vector<char> ret;
    if (userInfo.hasDisplayName()) {
      for (auto c: userInfo.getDisplayName().getDefaultText()) {
        if (c != '\n')
          ret.add(c);
      }
      ret.add(' ');
    }
    if (userInfo.hasIdentityId()) {
      ret.addAll(kj::StringPtr("user-"));
      ret.addAll(showAsHex(userInfo.getIdentityId().slice(0, 8)));
      ret.add(' ');
      ret.addAll(kj::StringPtr("tab-"));
      ret.addAll(showAsHex(tabId.slice(0, 4)));
    } else {
      ret.addAll(kj::StringPtr("tab-"));
      ret.addAll(showAsHex(tabId));
    }
    return kj::String(ret.releaseAsArray());
  }

  AppState* const appState;
  kj::String handle;
};

class UiViewImpl final: public sandstorm::UiView::Server {
 public:
  UiViewImpl(kj::Own<AppState>&& appState): appState(kj::mv(appState)) {}
  kj::Promise<void> newSession(NewSessionContext context) override {
    auto params = context.getParams();
    KJ_REQUIRE(params.getSessionType() == capnp::typeId<sandstorm::WebSession>(),
               "Unsupported session type.");

    context.getResults().setSession(
        kj::heap<WebSessionImpl>(params.getUserInfo(), params.getContext(),
                                 params.getSessionParams().getAs<sandstorm::WebSession::Params>(),
                                 params.getTabId(), appState));

    return kj::READY_NOW;
  }

 private:
  kj::Own<AppState> appState;
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
    writeFileAtomic(TOPIC_PATH, "Random chatter");
    writeFileAtomic(CHATS_PATH, "");
    writeFileAtomic(CHATSIZE_PATH, "0");

    return true;
  }

  kj::MainBuilder::Validity run() {
    removeAllFiles("var/tmp");
    auto r = unlink("var/chats~");
    if (r == -1 && errno != ENOENT) {
      KJ_FAIL_SYSCALL("unlink", errno);
    }
    if (access(CHATSIZE_PATH, F_OK) == -1) {
      if (errno != ENOENT) {
        KJ_FAIL_SYSCALL("access", errno);
      }
      struct stat s;
      KJ_SYSCALL(stat(CHATS_PATH, &s));
      writeFileAtomic(CHATSIZE_PATH, kj::str(s.st_size));
    }

    auto appState = kj::heap<AppState>();

    appState->chats.write("restarted");

    auto stream = ioContext.lowLevelProvider->wrapSocketFd(3);
    capnp::TwoPartyVatNetwork network(*stream, capnp::rpc::twoparty::Side::CLIENT);
    auto rpcSystem = capnp::makeRpcServer(network, kj::heap<UiViewImpl>(kj::mv(appState)));

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
