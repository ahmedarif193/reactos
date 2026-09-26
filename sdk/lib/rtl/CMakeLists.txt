
add_definitions(
    -D_NTOSKRNL_
    -DNO_RTL_INLINES
    -D_NTSYSTEM_
    -D_NTDLLBUILD_
    -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI})

if(WOW64_I386_RUNTIME)
    set_property(SOURCE processor.c APPEND PROPERTY COMPILE_DEFINITIONS WOW64_I386_RUNTIME)
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Enable this again. CORE-17637
    add_compile_options(-Wunused-result)

    add_compile_options(-Wno-missing-braces)
endif()

list(APPEND RTL_WINE_SOURCE
    actctx.c
    timerqueue.c
    threadpool.c
)
list(APPEND RTL_WINE_SOURCE_VISTA
    product.c
    threadpool.c
)
set_source_files_properties(threadpool.c PROPERTIES
    COMPILE_DEFINITIONS __WINESRC__
    SKIP_PRECOMPILE_HEADERS ON)

if(MSVC)
    # Silence warning C4267: 'initializing': conversion from 'size_t' to 'const int', possible loss of data
    set_source_files_properties(${RTL_WINE_SOURCE} PROPERTIES COMPILE_OPTIONS "/wd4267")
endif()

list(APPEND SOURCE
    ${RTL_WINE_SOURCE}
    wine_debug.c
    access.c
    acl.c
    appverifier.c
    assert.c
    atom.c
    avltable.c
    bitmap.c
    bootdata.c
    compress.c
    crc32.c
    critical.c
    dbgbuffer.c
    debug.c
    dos8dot3.c
    encode.c
    env.c
    error.c
    exception.c
    generictable.c
    hashtable.c
    handle.c
    heap.c
    heapdbg.c
    heappage.c
    heapuser.c
    image.c
    interlck.c
    largeint.c
    locale.c
    luid.c
    memstream.c
    message.c
    network.c
    nls.c
    path.c
    ppb.c
    prefix.c
    priv.c
    process.c
    processor.c
    propvar.c
    random.c
    rangelist.c
    registry.c
    res.c
    resource.c
    rxact.c
    sd.c
    security.c
    sid.c
    boundary.c
    slist.c
    splaytree.c
    sysvol.c
    thread.c
    time.c
    timezone.c
    trace.c
    unicode.c
    unicodeprefix.c
    vectoreh.c
    version.c
    wait.c
    workitem.c
    rtl.h)

if(ARCH STREQUAL "i386")
    list(APPEND ASM_SOURCE
        i386/debug_asm.S
        i386/except_asm.s
        i386/interlck.S
        i386/rtlmem.s
        i386/rtlswap.S
        i386/res_asm.s)
    list(APPEND SOURCE
        i386/except.c
        i386/thread.c)
elseif(ARCH STREQUAL "amd64")
    list(APPEND ASM_SOURCE
        amd64/debug_asm.S
        amd64/except_asm.S
        amd64/slist.S
        amd64/unwind-asm.s)
    list(APPEND SOURCE
        bitmap64.c
        byteswap.c
        dynfntbl.c
        amd64/except.c
        amd64/unwind.c
        amd64/stubs.c
        mem.c
        unwind2.c)

    # RtlWalkFrameChain protects arbitrary stack reads with native SEH. Clang
    # must model asynchronous faults so those loads remain inside the scope.
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT USE_DUMMY_PSEH)
        set_property(SOURCE amd64/unwind.c PROPERTY SKIP_PRECOMPILE_HEADERS ON)
        set_property(SOURCE amd64/unwind.c APPEND PROPERTY COMPILE_OPTIONS -Xclang -fasync-exceptions)
    endif()
elseif(ARCH STREQUAL "arm")
    list(APPEND ASM_SOURCE arm/debug_asm.S)
    list(APPEND SOURCE
        arm/except.c
        byteswap.c
        mem.c)
elseif(ARCH STREQUAL "arm64")
    list(APPEND ASM_SOURCE
        arm64/context_asm.S
        arm64/debug_asm.S
        arm64/except_asm.S)
    list(APPEND SOURCE
        bitmap64.c
        arm64/context.c
        arm64/except.c
        arm64/unwind.c
        byteswap.c
        dynfntbl.c
        mem.c
        unwind2.c)
elseif(ARCH STREQUAL "riscv64")
    list(REMOVE_ITEM SOURCE slist.c)
    list(APPEND ASM_SOURCE
        riscv64/context_asm.S
        riscv64/restore_asm.S
        riscv64/except_asm.S)
    list(APPEND SOURCE
        bitmap64.c
        byteswap.c
        dynfntbl.c
        mem.c
        riscv64/except.c
        riscv64/context.c
        riscv64/dispatch.c
        riscv64/unwind.c
        riscv64/slist.c)
endif()

add_asm_files(rtl_asm ${ASM_SOURCE})
add_library(rtl ${SOURCE} ${rtl_asm})
if(ARCH STREQUAL "riscv64")
    # One reader-driven decoder shared by native RTL and remote DbgHelp walks.
    add_library(riscv64_rvuw STATIC riscv64/rvuw.c)
    target_include_directories(riscv64_rvuw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(riscv64_rvuw xdk psdk)
    target_link_libraries(rtl PRIVATE riscv64_rvuw)
endif()
target_link_libraries(rtl PRIVATE pseh)
add_pch(rtl rtl.h SOURCE)
add_dependencies(rtl psdk asm)

if(ARCH STREQUAL "arm64")
    # The kernel must preserve its current PCR in x18 across a context restore.
    # Supply these definitions as objects so the kernel never pulls the
    # user-mode context assembly from the shared rtl archive.
    add_asm_files(rtl_kernel_context_asm arm64/context_asm.S)
    add_library(rtl_kernel_context OBJECT ${rtl_kernel_context_asm})
    target_compile_definitions(rtl_kernel_context PRIVATE RTL_KERNEL_CONTEXT)
    add_dependencies(rtl_kernel_context asm)
endif()

list(APPEND SOURCE_VISTA
    ${RTL_WINE_SOURCE_VISTA}
    condvar.c
    locale.c
    processor.c
    runonce.c
    slist.c
    srw.c
    threadpool.c
    unicode_vista.c
    utf8.c
    waitaddr.c)

if(ARCH STREQUAL "riscv64")
    list(REMOVE_ITEM SOURCE_VISTA slist.c)
endif()

add_library(rtl_vista ${SOURCE_VISTA})
add_pch(rtl_vista rtl_vista.h SOURCE_VISTA)
add_dependencies(rtl_vista psdk)
target_link_libraries(rtl_vista PRIVATE pseh)
