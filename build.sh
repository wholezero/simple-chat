#!/bin/sh
# Copyright 2016 Steven Dee. All rights reserved.
set -eu

bazel build :all --compilation_mode=opt --strip=always
rm -rf target
mkdir -p target
tar xf bazel-bin/all.tar -C target
