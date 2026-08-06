function(add_d3dx9_winetest __version)
    set(module d3dx9_${__version}_winetest)
    set(source_dir ${REACTOS_SOURCE_DIR}/modules/rostests/winetests/d3dx9_36)

    add_executable(${module}
        ${source_dir}/asm.c
        ${source_dir}/core.c
        ${source_dir}/effect.c
        ${source_dir}/line.c
        ${source_dir}/math.c
        ${source_dir}/mesh.c
        ${source_dir}/shader.c
        ${source_dir}/surface.c
        ${source_dir}/texture.c
        ${source_dir}/volume.c
        ${source_dir}/xfile.c
        ${source_dir}/testlist.c
        ${source_dir}/rsrc.rc)
    target_compile_definitions(${module} PRIVATE
        D3DX_SDK_VERSION=${__version}
        USE_WINE_TODOS
        WINETEST_USE_DBG_SPRINTF
        __WINESRC__
        isnan=_isnan)
    target_include_directories(${module} PRIVATE ${source_dir})

    if(MSVC)
        remove_target_compile_option(${module} "/we4477")
    else()
        target_compile_options(${module} PRIVATE -Wno-sizeof-array-div)
    endif()

    target_link_libraries(${module} uuid dxguid)
    set_module_type(${module} win32cui)
    add_importlibs(${module} d3dx9_${__version} d3d9 user32 gdi32 msvcrt kernel32)
    add_rostests_file(TARGET ${module})
endfunction()
