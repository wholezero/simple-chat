load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")

pkg_tar(
    name = "all",
    files = [
        "//src:serve",
        "//src:index.html.gz",
    ],
    mode = "0755",
)
