PROJECT(SHELL)

add_subdirectory(shellbars)
add_subdirectory(shellfind)

spec2def(browseui.dll browseui.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ACLCustomMRU.cpp
    ACLCustomMRU.h
    aclhistory.cpp
    aclistisf.cpp
    addressband.cpp
    addresseditbox.cpp
    bandproxy.cpp
    basebarsite.cpp
    brandband.cpp
    browseui.cpp
    browseuiord.cpp
    CAutoComplete.cpp
    CShellTaskScheduler.cpp
    CShellTaskScheduler.h
    CTaskbarList.cpp
    CTaskbarList.h
    commonbrowser.cpp
    desktopipc.cpp
    globalfoldersettings.cpp
    internettoolbar.cpp
    parsecmdline.cpp
    regtreeoptions.cpp
    settings.cpp
    shellbrowser.cpp
    toolsband.cpp
    travellog.cpp
    CUserAssist.cpp
    CUserAssist.h)

list(APPEND PCH_SKIP_SOURCE
    aclmulti.c
    aclsource.c
    browseui_main.c
    compcatcachedaemon.c
    dllinstall.c
    progressdlg.c)

set_source_files_properties(browseui_main.c PROPERTIES COMPILE_DEFINITIONS
    "DllMain=WineBrowseUI_DllMain;DllCanUnloadNow=WineBrowseUI_DllCanUnloadNow;DllGetClassObject=WineBrowseUI_DllGetClassObject;DllInstall=WineBrowseUI_DllInstall;ACLShellSource_Constructor=ReactOS_ACLShellSource_Constructor")
set_source_files_properties(aclsource.c PROPERTIES COMPILE_DEFINITIONS
    "ACLShellSource_Constructor=Wine_ACLShellSource_Constructor")
set_property(SOURCE
    aclmulti.c
    aclsource.c
    browseui_main.c
    compcatcachedaemon.c
    progressdlg.c
    APPEND PROPERTY COMPILE_DEFINITIONS __WINESRC__)

add_library(browseui MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    browseui.rc
    ${CMAKE_CURRENT_BINARY_DIR}/browseui.def)

set_module_type(browseui win32dll UNICODE)
add_idl_reg_scripts(browseui registry browseui_classes.idl)
target_link_libraries(browseui shellbars shellfind uuid wine wine_dll_register cpprt atl_classes)
add_importlibs(browseui uxtheme shdocvw shlwapi shell32 comctl32 gdi32 ole32 oleaut32 user32 advapi32 mpr msvcrt kernel32 ntdll)
add_pch(browseui precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET browseui DESTINATION reactos/system32 FOR all)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(browseui PRIVATE -Wno-unused-but-set-variable)
endif()

if(NOT MSVC)
    # Binutils linker bug
    if(LTCG)
        target_link_options(browseui PRIVATE "-Wl,--allow-multiple-definition")
    endif()
endif()

add_custom_command(TARGET browseui POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
     "$<TARGET_FILE:browseui>"
     "$<TARGET_FILE_DIR:filebrowser>/$<TARGET_FILE_NAME:browseui>"
  COMMENT "Copying to output directory")
