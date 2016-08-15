load("//third_party/capnproto:capnproto.bzl", "cc_capnp_library")

cc_binary(
    name = "serve",
    srcs = [
        "serve.c++",
    ],
    deps = [
        ":app",
        "//third_party/sandstorm:sandstorm",
    ],
    visibility = ["//visibility:public"],
    linkopts = [
        "-static",
    ],
)

genrule(
    name = "index.html-gz",
    srcs = ["index.html"],
    outs = ["index.html.gz"],
    cmd = "gzip -9 < $(location index.html) > $@",
    visibility = ["//visibility:public"],
)

cc_capnp_library(
    name = "app",
    srcs = ["app.capnp"],
    deps = [
        "//third_party/sandstorm:sandstorm",
    ],
)
