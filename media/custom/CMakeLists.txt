set(CUSTOM_MEDIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/files")

if(EXISTS "${CUSTOM_MEDIA_DIR}")
    file(GLOB_RECURSE CUSTOM_MEDIA_FILES
        LIST_DIRECTORIES FALSE
        RELATIVE "${CUSTOM_MEDIA_DIR}"
        "${CUSTOM_MEDIA_DIR}/*")

    foreach(_custom_file ${CUSTOM_MEDIA_FILES})
        if(_custom_file STREQUAL ".gitkeep")
            continue()
        endif()

        set(_absolute_path "${CUSTOM_MEDIA_DIR}/${_custom_file}")
        get_filename_component(_custom_dir "${_custom_file}" DIRECTORY)

        if(_custom_dir)
            set(_destination "custom_files/${_custom_dir}")
        else()
            set(_destination "custom_files")
        endif()

        add_cd_file(
            FILE "${_absolute_path}"
            DESTINATION ${_destination}
            NO_CAB
            FOR all)
    endforeach()
endif()
