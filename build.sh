#!/bin/sh
# Copyright 2016 Steven Dee. All rights reserved.
set -eu

bazel build :target --compilation_mode=opt
rm -rf target
tar xf bazel-bin/target.tar
