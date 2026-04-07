# ndis-version.cmake
#
# Per-driver NDIS version selection helper. Usage in a driver's CMakeLists:
#
#     include(${CMAKE_SOURCE_DIR}/sdk/cmake/ndis-version.cmake)
#     set_ndis_version(mydriver 620 miniport)
#
# The second argument is the MAJOR*10 + MINOR version number (50, 51, 60,
# 61, 620). The third is one of: miniport, protocol, filter.
#
# The helper applies the corresponding preprocessor defines and, for NDIS 6
# versions, bumps NTDDI_VERSION to match. Legacy compat types (PNDIS_PACKET,
# etc.) are enabled via NDIS_LEGACY_DRIVER=1 so that ndis.h parses cleanly
# even for pure NDIS 6 drivers.
#
# Added for the dev-nt6-1 branch (NT 5.2 -> NT 6.1 API upgrade).

function(set_ndis_version target version role)
    set(defs
        NDIS_LEGACY_DRIVER=1
    )

    if(version EQUAL 620)
        list(APPEND defs NDIS620 NDIS620_${role} NTDDI_VERSION=0x06010000 _WIN32_WINNT=0x0601)
    elseif(version EQUAL 61)
        list(APPEND defs NDIS61 NDIS61_${role} NTDDI_VERSION=0x06000100 _WIN32_WINNT=0x0600)
    elseif(version EQUAL 60)
        list(APPEND defs NDIS60 NDIS60_${role} NTDDI_VERSION=0x06000000 _WIN32_WINNT=0x0600)
    elseif(version EQUAL 51)
        list(APPEND defs NDIS51 NDIS51_${role} NTDDI_VERSION=0x05010000 _WIN32_WINNT=0x0501)
    elseif(version EQUAL 50)
        list(APPEND defs NDIS50 NDIS50_${role} NTDDI_VERSION=0x05000000 _WIN32_WINNT=0x0500)
    else()
        message(FATAL_ERROR "set_ndis_version: unsupported version ${version}")
    endif()

    string(TOUPPER "${role}" role_upper)
    if(role_upper STREQUAL "MINIPORT")
        list(APPEND defs NDIS_MINIPORT_DRIVER)
    endif()

    target_compile_definitions(${target} PRIVATE ${defs})
endfunction()
