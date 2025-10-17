set(CUSTOM_MEDIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/files")

if(IS_DIRECTORY "${CUSTOM_MEDIA_DIR}")
    file(GLOB_RECURSE CUSTOM_MEDIA_FILES
        LIST_DIRECTORIES FALSE
        RELATIVE "${CUSTOM_MEDIA_DIR}"
        "${CUSTOM_MEDIA_DIR}/*")

    if(CUSTOM_MEDIA_FILES)
        list(REMOVE_ITEM CUSTOM_MEDIA_FILES ".gitkeep")
        list(FILTER CUSTOM_MEDIA_FILES EXCLUDE REGEX "^$")
        list(SORT CUSTOM_MEDIA_FILES)

        foreach(_custom_file ${CUSTOM_MEDIA_FILES})
            set(_absolute_path "${CUSTOM_MEDIA_DIR}/${_custom_file}")
            if(NOT EXISTS "${_absolute_path}")
                message(WARNING "Skipping missing custom media payload: ${_absolute_path}")
                continue()
            endif()

            get_filename_component(_custom_dir "${_custom_file}" DIRECTORY)

            if(_custom_dir)
                string(REPLACE "\\" "/" _custom_dir "${_custom_dir}")
                set(_destination "reactos/custom_files/${_custom_dir}")
            else()
                set(_destination "reactos/custom_files")
            endif()

            add_cd_file(
                FILE "${_absolute_path}"
                DESTINATION "${_destination}"
                NO_CAB
                FOR all)
        endforeach()
    endif()
elseif(EXISTS "${CUSTOM_MEDIA_DIR}")
    message(WARNING "Custom media path exists but is not a directory: ${CUSTOM_MEDIA_DIR}")
endif()
