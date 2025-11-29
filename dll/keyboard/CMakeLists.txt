
list(APPEND _keyboard_layouts
    kbda1
    kbda2
    kbda3
    kbdal
    kbdarme
    kbdarmw
    kbdaze
    kbdazel
    kbdbe
    kbdbga
    kbdbgt
    kbdblr
    kbdbr
    kbdbu
    kbdbur
    kbdcan
    kbdcr
    kbdcz
    kbdcz1
    kbdda
    kbddv
    kbdeo
    kbdest
    kbdfc
    kbdfi
    kbdfr
    kbdgeo
    kbdgerg
    kbdgneo
    kbdgr
    kbdgr1
    kbdgre1
    kbdgrist
    kbdhe
    kbdheb
    kbdhu
    kbdic
    kbdinasa
    kbdinben
    kbdindev
    kbdinguj
    kbdinmal
    kbdir
    kbdit
    kbdjpn
    kbdkaz
    kbdkor
    kbdla
    kbdlt1
    kbdlv
    kbdmac
    kbdne
    kbdno
    kbdpl
    kbdpl1
    kbdpo
    kbdro
    kbdrost
    kbdru
    kbdru1
    kbdsf
    kbdsg
    kbdsl
    kbdsl1
    kbdsp
    kbdsw
    kbdtat
    kbdth0
    kbdth1
    kbdth2
    kbdth3
    kbdtuf
    kbdtuq
    kbduk
    kbdur
    kbdurs
    kbdus
    kbdusa
    kbdusl
    kbdusr
    kbdusx
    kbduzb
    kbdvntc
    kbdycc
    kbdycl)

foreach(_keyboard_layout ${_keyboard_layouts})
    spec2def(${_keyboard_layout}.dll ${_keyboard_layout}/${_keyboard_layout}.spec)

    add_library(${_keyboard_layout} MODULE
        ${_keyboard_layout}/${_keyboard_layout}.c
        ${_keyboard_layout}/${_keyboard_layout}.rc
        ${CMAKE_CURRENT_BINARY_DIR}/${_keyboard_layout}.def)

    target_include_directories(${_keyboard_layout} PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/include)
    set_entrypoint(${_keyboard_layout} 0)
    set_image_base(${_keyboard_layout} 0x5FFF0000)
    set_subsystem(${_keyboard_layout} native)

    if(MSVC)
        # Merge the .text and .rdata section into the .data section
        target_link_options(${_keyboard_layout} PRIVATE "/ignore:4254;/SECTION:.data,ER;/MERGE:.text=.data;/MERGE:.rdata=.data;/MERGE:.bss=.data;/MERGE:.edata=.data")
    elseif(REACTOS_TARGET_LINKER_IS_LLD)
        # lld lacks GNU script support and merging these sections is optional.
        # Drop any lingering GCC-style options from prior configurations.
        get_property(_kbd_link_opts TARGET ${_keyboard_layout} PROPERTY LINK_OPTIONS)
        if(_kbd_link_opts)
            set(_filtered_opts)
            set(_idx 0)
            list(LENGTH _kbd_link_opts _opt_count)
            while(_idx LESS _opt_count)
                list(GET _kbd_link_opts ${_idx} _opt)
                if(_opt STREQUAL "-Xlinker")
                    math(EXPR _next "${_idx} + 1")
                    if(_next LESS _opt_count)
                        list(GET _kbd_link_opts ${_next} _next_opt)
                        if(_next_opt MATCHES "^/(ignore|SECTION|MERGE)")
                            math(EXPR _idx "${_idx} + 2")
                            continue()
                        endif()
                    endif()
                endif()
                if(_opt MATCHES "^/(ignore|SECTION|MERGE)")
                    math(EXPR _idx "${_idx} + 1")
                    continue()
                endif()
                list(APPEND _filtered_opts "${_opt}")
                math(EXPR _idx "${_idx} + 1")
            endwhile()
        set_property(TARGET ${_keyboard_layout} PROPERTY LINK_OPTIONS ${_filtered_opts})
            unset(_filtered_opts)
            unset(_opt_count)
            unset(_idx)
        endif()
        unset(_kbd_link_opts)
    else()
        # Use a custom linker script
        target_link_options(${_keyboard_layout} PRIVATE "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/kbdlayout.lds")
    endif()

    # dynamic analysis switches
    if(STACK_PROTECTOR)
        target_compile_options(${_keyboard_layout} PRIVATE -fno-stack-protector)
    endif()

    if(RUNTIME_CHECKS)
        target_link_libraries(${_keyboard_layout} runtmchk)
    endif()

    add_dependencies(${_keyboard_layout} psdk)
    add_cd_file(TARGET ${_keyboard_layout} DESTINATION reactos/system32 FOR all)
endforeach()
