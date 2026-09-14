/*
 * COPYRIGHT:            See COPYING in the top level directory
 * PROJECT:              ReactOS kernel
 * FILE:                 lib/opengl32/apistubs.c
 * PURPOSE:              OpenGL32 lib, glXXX functions
 */

#include "opengl32.h"

#define USE_GL_FUNC(name, proto_args, call_args, offset, stack) \
static void GLAPIENTRY no_context_##name proto_args             \
{                                                               \
}

#define USE_GL_FUNC_RET(name, ret_type, proto_args, call_args, offset, stack) \
static ret_type GLAPIENTRY no_context_##name proto_args                       \
{                                                                             \
    if ((offset) == 261)                                                       \
        return (ret_type)(ULONG_PTR)GL_INVALID_OPERATION;                      \
    return (ret_type)0;                                                        \
}

#include "glfuncs.h"

#undef USE_GL_FUNC_RET
#undef USE_GL_FUNC

static const GLCLTPROCTABLE no_context_api_table =
{
    OPENGL_VERSION_110_ENTRIES,
    {
#define USE_GL_FUNC(name, proto_args, call_args, offset, stack) no_context_##name,
#define USE_GL_FUNC_RET(name, ret_type, proto_args, call_args, offset, stack) no_context_##name,
#include "glfuncs.h"
#undef USE_GL_FUNC_RET
#undef USE_GL_FUNC
    }
};

const GLDISPATCHTABLE*
IntGetNoContextDispatchTable(void)
{
    return &no_context_api_table.glDispatchTable;
}


#ifndef __i386__

#define USE_GL_FUNC(name, proto_args, call_args, offset, stack)         \
void GLAPIENTRY gl##name proto_args                                     \
{                                                                       \
    const GLDISPATCHTABLE * Dispatch = IntGetCurrentDispatchTable();    \
    if (!Dispatch)                                                      \
        return;                                                         \
    Dispatch->name call_args ;                                          \
}

#define USE_GL_FUNC_RET(name, ret_type, proto_args, call_args, offset, stack)   \
ret_type GLAPIENTRY gl##name proto_args                                         \
{                                                                               \
    const GLDISPATCHTABLE * Dispatch = IntGetCurrentDispatchTable();            \
    if (!Dispatch)                                                              \
        return 0;                                                               \
    return Dispatch->name call_args ;                                           \
}

#include "glfuncs.h"

#endif //__i386__

/* Unknown debug function */
GLint GLAPIENTRY glDebugEntry(GLint unknown1, GLint unknown2)
{
    return 0;
}
