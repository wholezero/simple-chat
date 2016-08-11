load("//third_party/capnproto:capnproto.bzl", "cc_capnp_library")
package(default_visibility = ["//visibility:public"])
licenses(["notice"])

cc_capnp_library(
    name = "sandstorm",
    srcs = glob(["**/*.capnp"]),
    include = ".",
)
