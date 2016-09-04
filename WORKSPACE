http_archive(
    name = "io_bazel_rules_closure",
    sha256 = "8c8a0f7f1327178bc8654e658cb6fff1171936e3033c5e263d513a7901a75b31",
    strip_prefix = "rules_closure-0.2.5",
    url = "http://bazel-mirror.storage.googleapis.com/github.com/bazelbuild/rules_closure/archive/0.2.5.tar.gz",
)
load("@io_bazel_rules_closure//closure:defs.bzl", "closure_repositories")
closure_repositories()

new_http_archive(
    name = "sqlite",
    build_file = "BUILD.sqlite",
    sha256 = "b7a8bccbe55df471f3f4ba84e789372606025eaccd09b05f80a41591282a2a41",
    strip_prefix = "sqlite-amalgamation-3140100",
    url = "https://www.sqlite.org/2016/sqlite-amalgamation-3140100.zip",
)
