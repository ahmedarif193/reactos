# Copyright 2026 Google
# SPDX-License-Identifier: MIT

inc_gfxstream_android = include_directories('include')
deps_gfxstream_android = [dependency('android-utils')]
if host_machine.system() == 'android'
    dep_android_minigbm_headers = dependency('android-minigbm-headers')
    dep_android_nativebase_headers = dependency('android-nativebase-headers')
    dep_android_aemu_gralloc_headers = dependency('android-aemu-gralloc-headers')

    files_gfxstream_android = files(
        'ANativeWindowAndroid.cpp',
        'GfxStreamGralloc.cpp',
        'GrallocGoldfish.cpp',
        'GrallocMinigbm.cpp',
    )

    deps_gfxstream_android += [
        dep_android_minigbm_headers,
        dep_android_nativebase_headers,
        dep_android_aemu_gralloc_headers,
    ]
else
    files_gfxstream_android = files(
        'ANativeWindowEmulated.cpp',
        'GrallocEmulated.cpp',
    )
endif

libgfxstream_guest_android = static_library(
    'gfxstream_android',
    files_gfxstream_android,
    cpp_args: gfxstream_guest_args,
    include_directories: [inc_gfxstream_android, inc_platform_virtgpu, inc_src, inc_include],
    link_with: [libplatform_virtgpu],
    dependencies: deps_gfxstream_android + dep_android,
)

idep_gfxstream_guest_android = declare_dependency(
    link_with: libgfxstream_guest_android,
    include_directories: inc_gfxstream_android,
)

gfxstream_deps += idep_gfxstream_guest_android
