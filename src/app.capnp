# Copyright 2016 Steven Dee. All rights reserved.

@0xbfe0b8cce36ee9d5;

$import "/capnp/c++.capnp".namespace("app");

struct Message {
  date @0 :Int64;   # nanoseconds since UNIX epoch.
  message @1 :Text;
  senderId @2 :Data;
  senderHandle @3 :Text;
}
