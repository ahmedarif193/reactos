#!/usr/bin/env bash

ANDROID_ARCH=x86_64 \
BUILD_CONTAINER=true \
TEST_CONTAINER=true \
DEBIAN_ARCH=amd64 \
. .gitlab-ci/container/debian/test-android.sh
