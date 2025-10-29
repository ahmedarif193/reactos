cmake_minimum_required(VERSION 3.17)

ros_add_rust_executable(rustwin
    CRATE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BINARY_NAME rustwin
    # RUSTC can be overridden if desired; LIBRARY_PATH is handled by rust.cmake
    ENV RUSTC=${ROS_RUSTC_EXECUTABLE}
    DESTINATION reactos/system32
    NAME_ON_CD rustwin.exe
    SKIP_IF_MISSING)
