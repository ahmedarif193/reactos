#!/usr/bin/env bash

ANDROID_ARCH=arm64 \
BUILD_CONTAINER=false \
TEST_CONTAINER=true \
DEBIAN_ARCH=arm64 \
. .gitlab-ci/container/debian/test-android.sh
