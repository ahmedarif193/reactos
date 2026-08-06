list(APPEND SOURCE
    liblber/bprint.c
    liblber/decode.c
    liblber/encode.c
    liblber/io.c
    liblber/memory.c
    liblber/options.c
    liblber/sockbuf.c
    libldap/abandon.c
    libldap/add.c
    libldap/avl.c
    libldap/charray.c
    libldap/compare.c
    libldap/controls.c
    libldap/cyrus.c
    libldap/delete.c
    libldap/error.c
    libldap/extended.c
    libldap/fetch.c
    libldap/filter.c
    libldap/free.c
    libldap/getattr.c
    libldap/getdn.c
    libldap/getentry.c
    libldap/getvalues.c
    libldap/init.c
    libldap/lbase64.c
    libldap/ldif.c
    libldap/modify.c
    libldap/modrdn.c
    libldap/open.c
    libldap/options.c
    libldap/os-ip.c
    libldap/references.c
    libldap/request.c
    libldap/result.c
    libldap/sasl.c
    libldap/sasl_w.c
    libldap/schema.c
    libldap/search.c
    libldap/sortctrl.c
    libldap/tavl.c
    libldap/thr_nt.c
    libldap/tls2.c
    libldap/tls_w.c
    libldap/unbind.c
    libldap/url.c
    libldap/utf-8.c
    libldap/util-int.c
    libldap/vlvctrl.c)

add_library(ldap STATIC ${SOURCE})
add_dependencies(ldap psdk)
target_include_directories(ldap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ldap PRIVATE _DLL __USE_CRTIMP _CRT_DECLARE_NONSTDC_NAMES=1 strcasecmp=_stricmp strncasecmp=_strnicmp)
target_link_libraries(ldap oldnames)
