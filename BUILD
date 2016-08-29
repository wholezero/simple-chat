load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")

pkg_tar(
    name = "target",
    files = [
        "//src:serve",
        "//src:index.html.gz",
    ],
    package_dir = "target",
    mode = "0755",
)
