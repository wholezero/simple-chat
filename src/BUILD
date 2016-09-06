load("//third_party/capnproto:capnproto.bzl", "cc_capnp_library")

exports_files(["cpuinfo"])

cc_binary(
    name = "serve",
    srcs = [
        "serve.c++",
        "util.c++",
        "util.h",
    ],
    deps = [
        "//third_party/sandstorm:sandstorm",
    ],
    defines = [
        "UNIQUE_HANDLES=0",
        "SHOW_JOINS_PARTS=0",
    ],
    visibility = ["//visibility:public"],
    linkopts = [
        "-static",
        "-Wl,--strip-all",
    ],
)

genrule(
    name = "index.html-gz",
    srcs = ["index.html"],
    outs = ["index.html.gz"],
    cmd = "gzip -9 < $(location index.html) > $@",
    visibility = ["//visibility:public"],
)
