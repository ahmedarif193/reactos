#!/usr/bin/env bash

ANDROID_ARCH=arm64 \
BUILD_CONTAINER=true \
TEST_CONTAINER=false \
DEBIAN_ARCH=amd64 \
. .gitlab-ci/container/debian/test-android.sh
