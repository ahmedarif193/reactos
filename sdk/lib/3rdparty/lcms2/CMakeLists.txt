add_library(lcms2
    src/cmsalpha.c
    src/cmscnvrt.c
    src/cmserr.c
    src/cmsgamma.c
    src/cmsgmt.c
    src/cmshalf.c
    src/cmsintrp.c
    src/cmsio0.c
    src/cmsio1.c
    src/cmslut.c
    src/cmsmtrx.c
    src/cmsnamed.c
    src/cmsopt.c
    src/cmspack.c
    src/cmspcs.c
    src/cmsplugin.c
    src/cmssamp.c
    src/cmstypes.c
    src/cmsvirt.c
    src/cmswtpnt.c
    src/cmsxform.c)

target_include_directories(lcms2
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(lcms2 BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/ucrt)
add_dependencies(lcms2 psdk)
