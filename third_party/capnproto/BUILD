load("//third_party/capnproto:capnproto.bzl", "cc_capnp_library")

package(default_visibility = ["//visibility:public"])
licenses(["notice"])

cc_library(
    name = "kj",
    srcs = glob([
        "c++/src/kj/**/*.c++",
    ], exclude = [
        "c++/src/kj/**/*-test.c++",
    ]),
    hdrs = glob([
        "c++/src/kj/**/*.h",
    ]),
    includes = ["c++/src"],
    defines = [
        "KJ_USE_FUTEX=0",
    ],
    copts = [
        "-Wno-sign-compare",
        "-Wno-strict-aliasing",
    ],
    linkopts = [
        "-pthread",
    ],
)

cc_library(
    name = "capnp-lib",
    srcs = glob([
        "c++/src/capnp/**/*.c++",
    ], exclude = [
        "c++/src/capnp/compiler/capnp.c++",
        "c++/src/capnp/compiler/capnp-c++.c++",
        "c++/src/capnp/compiler/capnpc-capnp.c++",
        "c++/src/capnp/test-util.c++",
        "c++/src/capnp/**/*-test.c++",
    ]),
    hdrs = glob([
        "c++/src/capnp/**/*.h",
    ]),
    deps = [":kj"],
    includes = ["c++/src"],
    copts = [
        "-Wno-sign-compare",
    ],
)

cc_binary(
    name = "capnp",
    srcs = ["c++/src/capnp/compiler/capnp.c++"],
    deps = [":capnp-lib"],
)

genrule(
    name = "capnpc-bin",
    srcs = [":capnp"],
    outs = ["capnpc"],
    cmd = "ln -s $$PWD/$(location :capnp) $@",
)

cc_binary(
    name = "capnpc-c++",
    srcs = ["c++/src/capnp/compiler/capnpc-c++.c++"],
    deps = [":capnp-lib"],
)

cc_binary(
    name = "capnpc-capnp",
    srcs = ["c++/src/capnp/compiler/capnpc-capnp.c++"],
    deps = [":capnp-lib"],
)

filegroup(
    name = "capnp-capnp",
    srcs = glob([
        "c++/src/capnp/**/*.capnp",
    ]),
)

cc_capnp_library(
    name = "test",
    data = glob(["c++/src/capnp/testdata/**"]),
    srcs = ["c++/src/capnp/test.capnp"],
)

cc_capnp_library(
    name = "test-import",
    srcs = ["c++/src/capnp/test-import.capnp"],
    deps = [":test"],
    include = "c++/src/capnp",
)
