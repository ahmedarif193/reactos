cmake_minimum_required(VERSION 3.17)

# Use the shared Rust integration helper
ros_add_rust_executable(rusthello
    CRATE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    BINARY_NAME rusthello
    # RUSTC can be overridden if desired; LIBRARY_PATH is handled by rust.cmake
    ENV RUSTC=${ROS_RUSTC_EXECUTABLE}
    DESTINATION reactos/system32
    NAME_ON_CD rusthello.exe
    SKIP_IF_MISSING)

# Convenience target: run under Wine if available
if(ROS_RUST_ENABLED AND ROS_RUST_TARGET_TRIPLE)
    ros_rust_profile_dir(_rust_profile_dir)
    set(_rusthello_exe ${CMAKE_CURRENT_BINARY_DIR}/cargo-rusthello/${ROS_RUST_TARGET_TRIPLE}/${_rust_profile_dir}/rusthello.exe)

    # Try both wine64 (for amd64) and wine
    find_program(_WINE64 wine64)
    find_program(_WINE wine)

    if(ARCH STREQUAL "amd64")
        if(_WINE64)
            add_custom_target(run-rusthello
                COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${_WINE64} ${_rusthello_exe}
                DEPENDS rusthello
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                USES_TERMINAL
                COMMENT "Running rusthello under wine64")
        elseif(_WINE)
            add_custom_target(run-rusthello
                COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${_WINE} ${_rusthello_exe}
                DEPENDS rusthello
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                USES_TERMINAL
                COMMENT "Running rusthello under wine")
        endif()
    else()
        if(_WINE)
            add_custom_target(run-rusthello
                COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${_WINE} ${_rusthello_exe}
                DEPENDS rusthello
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                USES_TERMINAL
                COMMENT "Running rusthello under wine")
        elseif(_WINE64)
            add_custom_target(run-rusthello
                COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${_WINE64} ${_rusthello_exe}
                DEPENDS rusthello
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                USES_TERMINAL
                COMMENT "Running rusthello under wine64")
        endif()
    endif()
endif()
