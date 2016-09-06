// Copyright 2016 Steven Dee. All rights reserved.

// Hack around stdlib bug with C++14.
#include <initializer_list>   // force libstdc++ to include its config
#undef _GLIBCXX_HAVE_GETS     // correct broken config
// End hack.

#include <set>

#include <capnp/rpc-twoparty.h>
#include <errno.h>
#include <kj/common.h>
#include <kj/debug.h>
#include <kj/main.h>
#include <kj/string.h>
#include <kj/vector.h>

#include "util.h"

namespace {

constexpr auto INDEX_PATH = "index.html.gz";
constexpr auto CHATS_PATH = "var/chats";
constexpr auto CHATSIZE_PATH = "var/.chatsize";
constexpr auto TOPIC_PATH = "var/topic";


class ChatStream {
 public:
  ChatStream():
      offset(u::readFile(CHATSIZE_PATH).parseAs<uint64_t>()),
      chatData(prepareStream()),
      chatStream(u::raiiOpen(CHATS_PATH, O_WRONLY | O_APPEND)) {}

  kj::StringPtr get() const {
    return chatData;
  }

  kj::Promise<void> onNew() {
    return chatQueue.wait();
  }

  void write(kj::StringPtr line) {
    static const auto nl = "\n";
    chatData = kj::str(chatData, line, nl);
    chatStream.write(reinterpret_cast<const byte*>(line.begin()), line.size());
    chatStream.write(nl, 1);
    u::syncPath(CHATS_PATH);
    offset += line.size() + 1;
    u::writeFileAtomic(CHATSIZE_PATH, kj::str(offset));
    chatQueue.ready();
  }

 private:
  kj::String prepareStream() {
    // Called in a member initializer. Depends on offset. Initializes chatData.
    // Must be called before chatStream is initialized.
    KJ_SYSCALL(truncate(CHATS_PATH, offset));
    return u::readFile(CHATS_PATH);
  }

  uint64_t offset;
  kj::String chatData;
  kj::FdOutputStream chatStream;
  u::WaitQueue chatQueue;
};


class Topic {
 public:
  Topic(): topic(u::readFile(TOPIC_PATH)) {}

  kj::StringPtr get() const {
    return topic;
  }

  kj::Promise<void> onNew() {
    return topicQueue.wait();
  }

  void set(kj::StringPtr topic_) {
    topic = kj::heapString(topic_);
    u::writeFileAtomic(TOPIC_PATH, topic);
    topicQueue.ready();
  }

 private:
  kj::String topic;
  u::WaitQueue topicQueue;
};


class StaticIndex {
 public:
  StaticIndex(): index(u::readFile(INDEX_PATH)) {}

  kj::StringPtr get() const {
    return index;
  }

 private:
  const kj::String index;
};


class UserList {
 public:
  kj::String add(sandstorm::UserInfo::Reader userInfo) {
    KJ_DEFER(usersQueue.ready());
    kj::Vector<char> baseHandle;
    if (userInfo.hasPreferredHandle()) {
      baseHandle.addAll(u::filteredString(
              [](char x){ return x != ' ' && x != '\n' && x != ':'; },
              userInfo.getPreferredHandle()));
    } else {
      baseHandle.addAll(kj::StringPtr("anon"));
    }
    baseHandle.add('\0');
#if !UNIQUE_HANDLES
    auto handle = kj::String(baseHandle.releaseAsArray());
    onlineUsers.emplace(kj::heapString(handle));
    return kj::mv(handle);
#else
    auto startHandle = kj::String(baseHandle.releaseAsArray());
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

  void remove(kj::String const& nick) {
    onlineUsers.erase(nick);
    usersQueue.ready();
  }

  kj::String get() const {
    kj::Vector<char> ret;
    for (auto& user: onlineUsers) {
      ret.addAll(user);
      ret.add('\n');
    }
    ret.add('\0');
    return kj::String(ret.releaseAsArray());
  }

  kj::Promise<void> onNew() {
    return usersQueue.wait();
  }

 private:
  std::set<kj::String> onlineUsers;
  u::WaitQueue usersQueue;
};


struct AppState {
 public:
  const StaticIndex index;
  ChatStream chats;
  Topic topic;
  UserList users;
};


template <typename Context, typename T>
kj::Promise<void> respondWithObject(Context context, T& object, bool awaitNew) {
  auto res = [context, &object]() mutable {
    return u::respondWith(context, object.get(), "text/plain");
  };
  if (awaitNew) {
    return object.onNew().then(kj::mv(res));
  } else {
    return res();
  }
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
#if SHOW_JOINS_PARTS
    appState->chats.write(
        kj::str(handle, " (", displayIdentity(userInfo, tabId),
                ") has joined"));
#else
    puts(kj::str("app: ", handle, " (", displayIdentity(userInfo, tabId),
                 ") has joined").cStr());
    fflush(stdout);
#endif
  }

  ~WebSessionImpl() noexcept(false) {
#if SHOW_JOINS_PARTS
    appState->chats.write(kj::str(handle, " has left"));
#endif
    appState->users.remove(handle);
  }

  kj::Promise<void> get(GetContext context) override {
    kj::String path = kj::heapString(context.getParams().getPath());
    auto awaitNew = false;
    requireCanonicalPath(path);
    KJ_IF_MAYBE(qPos, path.findFirst('?')) {
      awaitNew = path.slice(*qPos + 1) == "new";
      path = kj::heapString(path.slice(0, *qPos));
    }
    if (path == "")
      return u::respondWith(context, appState->index.get(), "text/html", true);
    if (path == "chats")
      return respondWithObject(context, appState->chats, awaitNew);
    if (path == "users")
      return respondWithObject(context, appState->users, awaitNew);
    if (path == "topic")
      return respondWithObject(context, appState->topic, awaitNew);
    return u::respondWithNotFound(context);
  }

  kj::Promise<void> post(PostContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "chats") {
      auto chat = u::filteredString(
          [](char x){ return x != '\n'; },
          context.getParams().getContent().getContent().asChars());
      appState->chats.write(kj::str(handle, ": ", chat));
      return u::respondWithRedirect(context, "/");
    }
    return u::respondWithNotFound(context);
  }

  kj::Promise<void> put(PutContext context) override {
    auto path = context.getParams().getPath();
    requireCanonicalPath(path);
    if (path == "topic") {
      auto topic = u::filteredString(
          [](char x){ return x != '\n'; },
          context.getParams().getContent().getContent().asChars());
      appState->topic.set(topic);
      appState->chats.write(kj::str(handle, " set the topic to: ", topic));
      return u::respondWithRedirect(context, "/");
    }
    return u::respondWithNotFound(context);
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

  kj::String displayIdentity(sandstorm::UserInfo::Reader userInfo,
                             capnp::Data::Reader tabId) {
    kj::Vector<char> ret;
    if (userInfo.hasDisplayName()) {
      ret.addAll(u::filteredString(
              [](char x){ return x != '\n'; },
              userInfo.getDisplayName().getDefaultText()));
      ret.add(' ');
    }
    if (userInfo.hasIdentityId()) {
      ret.addAll(kj::StringPtr("user-"));
      ret.addAll(u::showAsHex(userInfo.getIdentityId().slice(0, 8)));
      ret.add(' ');
      ret.addAll(kj::StringPtr("tab-"));
      ret.addAll(u::showAsHex(tabId.slice(0, 4)));
    } else {
      ret.addAll(kj::StringPtr("tab-"));
      ret.addAll(u::showAsHex(tabId));
    }
    return kj::String(ret.releaseAsArray());
  }

  AppState* const appState;
  const kj::String handle;
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
    u::writeFileAtomic(TOPIC_PATH, "Random chatter");
    u::writeFileAtomic(CHATS_PATH, "");
    u::writeFileAtomic(CHATSIZE_PATH, "0");

    return true;
  }

  kj::MainBuilder::Validity run() {
    u::removeAllFiles("var/tmp");
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
      u::writeFileAtomic(CHATSIZE_PATH, kj::str(s.st_size));
    }

    auto appState = kj::heap<AppState>();

#if SHOW_JOINS_PARTS
    appState->chats.write("restarted");
#endif

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
