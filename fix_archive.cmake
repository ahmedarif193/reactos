# CMake script to fix nested archive by extracting and recreating
# Usage: cmake -DARCHIVE_PATH=path/to/archive.a -P fix_archive.cmake

get_filename_component(ARCHIVE_DIR "${ARCHIVE_PATH}" DIRECTORY)
get_filename_component(ARCHIVE_NAME "${ARCHIVE_PATH}" NAME)

# Set toolchain tools - use provided ones or defaults
if(NOT CMAKE_AR)
    set(CMAKE_AR "i686-w64-mingw32-ar")
endif()
if(NOT CMAKE_RANLIB)
    set(CMAKE_RANLIB "i686-w64-mingw32-ranlib")
endif()

# Check if archive contains nested archives
execute_process(
    COMMAND ${CMAKE_AR} t "${ARCHIVE_PATH}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE archive_contents
    ERROR_QUIET
)

if(check_result EQUAL 0)
    # Check if the first line contains a nested .a file
    string(REGEX MATCH "^[^\n]*\\.a" NESTED_ARCHIVE_LINE "${archive_contents}")
    if(NOT NESTED_ARCHIVE_LINE)
        # No nested archive found, check if it just needs indexing
        execute_process(
            COMMAND ${CMAKE_RANLIB} "${ARCHIVE_PATH}"
            RESULT_VARIABLE ranlib_result
            OUTPUT_QUIET ERROR_QUIET
        )
        if(ranlib_result EQUAL 0)
            message(STATUS "Archive ${ARCHIVE_PATH} is already properly indexed")
            return()
        endif()
    else()
        message(STATUS "Archive ${ARCHIVE_PATH} contains nested archive: ${NESTED_ARCHIVE_LINE}")
    endif()
endif()

# Improved lock mechanism with PID-based locking
string(TIMESTAMP TIMESTAMP "%Y%m%d%H%M%S")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef RANDOM_SUFFIX)
set(LOCK_FILE "${ARCHIVE_PATH}.${TIMESTAMP}.${RANDOM_SUFFIX}.lock")
set(MAX_RETRIES 50)
set(RETRY_COUNT 0)

# Find any existing lock files
file(GLOB EXISTING_LOCKS "${ARCHIVE_PATH}.*.lock")
while(EXISTING_LOCKS AND RETRY_COUNT LESS MAX_RETRIES)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.2)
    math(EXPR RETRY_COUNT "${RETRY_COUNT} + 1")
    file(GLOB EXISTING_LOCKS "${ARCHIVE_PATH}.*.lock")
    # Clean up old locks (older than 30 seconds)
    foreach(LOCK ${EXISTING_LOCKS})
        file(TIMESTAMP "${LOCK}" LOCK_TIME "%s")
        string(TIMESTAMP CURRENT_TIME "%s")
        math(EXPR TIME_DIFF "${CURRENT_TIME} - ${LOCK_TIME}")
        if(TIME_DIFF GREATER 30)
            file(REMOVE "${LOCK}")
        endif()
    endforeach()
endwhile()

if(RETRY_COUNT EQUAL MAX_RETRIES)
    message(STATUS "Timeout waiting for archive lock, skipping ${ARCHIVE_PATH}")
    return()
endif()

# Create our lock
file(WRITE "${LOCK_FILE}" "locked")

# Create temporary directory
if(CMAKE_BINARY_DIR)
    set(TEMP_DIR "${CMAKE_BINARY_DIR}/temp_fix_${ARCHIVE_NAME}")
else()
    get_filename_component(BUILD_DIR "${ARCHIVE_PATH}" DIRECTORY)
    get_filename_component(BUILD_DIR "${BUILD_DIR}" DIRECTORY) 
    set(TEMP_DIR "${BUILD_DIR}/temp_fix_${ARCHIVE_NAME}")
endif()
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

# Extract archive
execute_process(
    COMMAND ${CMAKE_AR} x "${ARCHIVE_PATH}"
    WORKING_DIRECTORY "${TEMP_DIR}"
    RESULT_VARIABLE extract_result
)

if(extract_result EQUAL 0)
    # List what was extracted
    file(GLOB ALL_FILES "${TEMP_DIR}/*")
    # message(STATUS "All extracted files: ${ALL_FILES}")
    # Check if we extracted a nested archive
    file(GLOB NESTED_ARCHIVES "${TEMP_DIR}/*.a")
    if(NESTED_ARCHIVES)
        list(GET NESTED_ARCHIVES 0 NESTED_ARCHIVE)
        # message(STATUS "Found nested archive: ${NESTED_ARCHIVE}")
        # Extract the nested archive
        execute_process(
            COMMAND ${CMAKE_AR} x "${NESTED_ARCHIVE}"
            WORKING_DIRECTORY "${TEMP_DIR}"
            RESULT_VARIABLE nested_extract_result
        )
        # Remove the nested archive file
        file(REMOVE "${NESTED_ARCHIVE}")
    endif()
    
    # Get list of extracted object files  
    file(GLOB OBJECT_FILES "${TEMP_DIR}/*.o")
    list(LENGTH OBJECT_FILES NUM_OBJECTS)
    # message(STATUS "Found ${NUM_OBJECTS} object files: ${OBJECT_FILES}")
    
    if(OBJECT_FILES)
        # Recreate archive with object files
        execute_process(
            COMMAND ${CMAKE_AR} cr "${ARCHIVE_PATH}" ${OBJECT_FILES}
            RESULT_VARIABLE create_result
        )
        
        if(create_result EQUAL 0)
            # Index the new archive
            execute_process(
                COMMAND ${CMAKE_RANLIB} "${ARCHIVE_PATH}"
                RESULT_VARIABLE ranlib_result
            )
        else()
            message(STATUS "Archive creation failed: ${create_result}")
        endif()
    else()
        message(STATUS "No object files found in temp directory")
    endif()
else()
    message(STATUS "Extraction failed: ${extract_result}")
endif()

# Cleanup
file(REMOVE_RECURSE "${TEMP_DIR}")
# Remove our lock file
file(REMOVE "${LOCK_FILE}")