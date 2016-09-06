load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")

pkg_tar(
    name = "proc",
    files = [
        "//src:cpuinfo",
    ],
    package_dir = "proc",
    mode = "0444",
)

pkg_tar(
    name = "target",
    files = [
        "//src:serve",
        "//src:index.html.gz",
    ],
    deps = [
        ":proc",
    ],
    mode = "0444",
    modes = {"serve": "0555"},
    package_dir = "target",
)
