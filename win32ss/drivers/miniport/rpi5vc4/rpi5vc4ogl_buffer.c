/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL buffer-object storage for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <context.h>
#include <macros.h>

#include "rpi5vc4ogl_buffer.h"

#define RPI5VC4_OGL_MAX_BUFFERS 64

typedef struct _RPI5VC4_OGL_BUFFER
{
    GLuint Name;
    BOOL Created;
    PBYTE Data;
    SIZE_T Size;
    GLenum Usage;
    GLenum Access;
    BOOL Mapped;
} RPI5VC4_OGL_BUFFER, *PRPI5VC4_OGL_BUFFER;

struct _RPI5VC4_OGL_BUFFER_STATE
{
    GLcontext *Mesa;
    GLuint NextName;
    GLuint ArrayBuffer;
    GLuint ElementArrayBuffer;
    RPI5VC4_OGL_BUFFER Buffers[RPI5VC4_OGL_MAX_BUFFERS];
};

static VOID
Rpi5OglBufferError(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Error,
    _In_z_ PCSTR Function)
{
    if (State != NULL && State->Mesa != NULL)
        gl_error(State->Mesa, Error, Function);
}

static BOOL
Rpi5OglBufferCanChangeState(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_z_ PCSTR Function)
{
    if (State == NULL)
        return FALSE;
    if (INSIDE_BEGIN_END(State->Mesa))
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION, Function);
        return FALSE;
    }
    return TRUE;
}

static PRPI5VC4_OGL_BUFFER
Rpi5OglBufferFind(
    _In_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Buffers); Index++)
    {
        if (State->Buffers[Index].Name == Name)
            return &State->Buffers[Index];
    }
    return NULL;
}

static PRPI5VC4_OGL_BUFFER
Rpi5OglBufferAllocateRecord(
    _Inout_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(State->Buffers); Index++)
    {
        if (State->Buffers[Index].Name == 0)
        {
            ZeroMemory(&State->Buffers[Index], sizeof(State->Buffers[Index]));
            State->Buffers[Index].Name = Name;
            State->Buffers[Index].Usage = GL_STATIC_DRAW;
            State->Buffers[Index].Access = GL_READ_WRITE;
            return &State->Buffers[Index];
        }
    }
    return NULL;
}

static GLuint
Rpi5OglBufferAllocateName(
    _Inout_ PRPI5VC4_OGL_BUFFER_STATE State)
{
    ULONG Attempts;
    GLuint Name;

    for (Attempts = 0; Attempts <= RTL_NUMBER_OF(State->Buffers); Attempts++)
    {
        Name = State->NextName++;
        if (Name == 0)
            continue;
        if (Rpi5OglBufferFind(State, Name) == NULL)
            return Name;
    }
    return 0;
}

static GLuint *
Rpi5OglBufferBinding(
    _Inout_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Target,
    _In_z_ PCSTR Function)
{
    switch (Target)
    {
        case GL_ARRAY_BUFFER:
            return &State->ArrayBuffer;
        case GL_ELEMENT_ARRAY_BUFFER:
            return &State->ElementArrayBuffer;
        default:
            Rpi5OglBufferError(State, GL_INVALID_ENUM, Function);
            return NULL;
    }
}

static PRPI5VC4_OGL_BUFFER
Rpi5OglBufferBoundObject(
    _Inout_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Target,
    _In_z_ PCSTR Function)
{
    GLuint *Binding = Rpi5OglBufferBinding(State, Target, Function);
    PRPI5VC4_OGL_BUFFER Buffer;

    if (Binding == NULL)
        return NULL;
    Buffer = Rpi5OglBufferFind(State, *Binding);
    if (Buffer == NULL || !Buffer->Created)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION, Function);
        return NULL;
    }
    return Buffer;
}

static BOOL
Rpi5OglBufferUsageValid(
    _In_ GLenum Usage)
{
    switch (Usage)
    {
        case GL_STREAM_DRAW:
        case GL_STREAM_READ:
        case GL_STREAM_COPY:
        case GL_STATIC_DRAW:
        case GL_STATIC_READ:
        case GL_STATIC_COPY:
        case GL_DYNAMIC_DRAW:
        case GL_DYNAMIC_READ:
        case GL_DYNAMIC_COPY:
            return TRUE;
        default:
            return FALSE;
    }
}

static VOID APIENTRY
Rpi5OglGenBuffers(
    _In_ GLsizei Count,
    _Out_writes_(Count) GLuint *Buffers)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;
    ULONG FreeCount = 0;
    ULONG Index;
    GLsizei BufferIndex;
    GLuint Name;

    if (!Rpi5OglBufferCanChangeState(State, "glGenBuffers"))
        return;
    if (Count < 0 || (Count != 0 && Buffers == NULL))
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE, "glGenBuffers(count)");
        return;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(State->Buffers); Index++)
    {
        if (State->Buffers[Index].Name == 0)
            FreeCount++;
    }
    if ((ULONG)Count > FreeCount)
    {
        Rpi5OglBufferError(State, GL_OUT_OF_MEMORY, "glGenBuffers");
        return;
    }

    for (BufferIndex = 0; BufferIndex < Count; BufferIndex++)
    {
        Name = Rpi5OglBufferAllocateName(State);
        Buffer = Name != 0 ? Rpi5OglBufferAllocateRecord(State, Name) : NULL;
        if (Buffer == NULL)
        {
            Rpi5OglBufferError(State, GL_OUT_OF_MEMORY, "glGenBuffers");
            return;
        }
        Buffers[BufferIndex] = Name;
    }
}

static VOID APIENTRY
Rpi5OglBindBuffer(
    _In_ GLenum Target,
    _In_ GLuint Name)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;
    GLuint *Binding;

    if (!Rpi5OglBufferCanChangeState(State, "glBindBuffer"))
        return;
    Binding = Rpi5OglBufferBinding(State, Target, "glBindBuffer(target)");
    if (Binding == NULL)
        return;
    if (Name == 0)
    {
        *Binding = 0;
        return;
    }

    Buffer = Rpi5OglBufferFind(State, Name);
    if (Buffer == NULL)
        Buffer = Rpi5OglBufferAllocateRecord(State, Name);
    if (Buffer == NULL)
    {
        Rpi5OglBufferError(State, GL_OUT_OF_MEMORY, "glBindBuffer");
        return;
    }
    Buffer->Created = TRUE;
    *Binding = Name;
}

static VOID APIENTRY
Rpi5OglDeleteBuffers(
    _In_ GLsizei Count,
    _In_reads_(Count) const GLuint *Buffers)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;
    GLsizei Index;

    if (!Rpi5OglBufferCanChangeState(State, "glDeleteBuffers"))
        return;
    if (Count < 0 || (Count != 0 && Buffers == NULL))
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE, "glDeleteBuffers(count)");
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Buffer = Rpi5OglBufferFind(State, Buffers[Index]);
        if (Buffer == NULL)
            continue;
        if (State->ArrayBuffer == Buffer->Name)
            State->ArrayBuffer = 0;
        if (State->ElementArrayBuffer == Buffer->Name)
            State->ElementArrayBuffer = 0;
        if (Buffer->Data != NULL)
            HeapFree(GetProcessHeap(), 0, Buffer->Data);
        ZeroMemory(Buffer, sizeof(*Buffer));
    }
}

static GLboolean APIENTRY
Rpi5OglIsBuffer(
    _In_ GLuint Name)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glIsBuffer"))
        return GL_FALSE;
    Buffer = Rpi5OglBufferFind(State, Name);
    return Buffer != NULL && Buffer->Created ? GL_TRUE : GL_FALSE;
}

static VOID APIENTRY
Rpi5OglBufferData(
    _In_ GLenum Target,
    _In_ GLsizeiptr Size,
    _In_opt_ const GLvoid *Data,
    _In_ GLenum Usage)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;
    PBYTE NewData = NULL;

    if (!Rpi5OglBufferCanChangeState(State, "glBufferData"))
        return;
    if (Size < 0)
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE, "glBufferData(size)");
        return;
    }
    if (!Rpi5OglBufferUsageValid(Usage))
    {
        Rpi5OglBufferError(State, GL_INVALID_ENUM, "glBufferData(usage)");
        return;
    }
    Buffer = Rpi5OglBufferBoundObject(State, Target, "glBufferData(target)");
    if (Buffer == NULL)
        return;
    if (Buffer->Mapped)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION, "glBufferData(mapped)");
        return;
    }
    if (Size != 0)
    {
        NewData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)Size);
        if (NewData == NULL)
        {
            Rpi5OglBufferError(State, GL_OUT_OF_MEMORY, "glBufferData");
            return;
        }
        if (Data != NULL)
            CopyMemory(NewData, Data, (SIZE_T)Size);
    }

    if (Buffer->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Buffer->Data);
    Buffer->Data = NewData;
    Buffer->Size = (SIZE_T)Size;
    Buffer->Usage = Usage;
}

static VOID APIENTRY
Rpi5OglBufferSubData(
    _In_ GLenum Target,
    _In_ GLintptr Offset,
    _In_ GLsizeiptr Size,
    _In_reads_bytes_(Size) const GLvoid *Data)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glBufferSubData"))
        return;
    if (Offset < 0 || Size < 0 || (Size != 0 && Data == NULL))
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glBufferSubData(offset/size/data)");
        return;
    }
    Buffer = Rpi5OglBufferBoundObject(State, Target,
                                      "glBufferSubData(target)");
    if (Buffer == NULL)
        return;
    if (Buffer->Mapped)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION,
                           "glBufferSubData(mapped)");
        return;
    }
    if ((SIZE_T)Offset > Buffer->Size ||
        (SIZE_T)Size > Buffer->Size - (SIZE_T)Offset)
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glBufferSubData(range)");
        return;
    }
    if (Size != 0)
        CopyMemory(Buffer->Data + (SIZE_T)Offset, Data, (SIZE_T)Size);
}

static VOID APIENTRY
Rpi5OglGetBufferSubData(
    _In_ GLenum Target,
    _In_ GLintptr Offset,
    _In_ GLsizeiptr Size,
    _Out_writes_bytes_(Size) GLvoid *Data)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glGetBufferSubData"))
        return;
    if (Offset < 0 || Size < 0 || (Size != 0 && Data == NULL))
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glGetBufferSubData(offset/size/data)");
        return;
    }
    Buffer = Rpi5OglBufferBoundObject(State, Target,
                                      "glGetBufferSubData(target)");
    if (Buffer == NULL)
        return;
    if (Buffer->Mapped)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION,
                           "glGetBufferSubData(mapped)");
        return;
    }
    if ((SIZE_T)Offset > Buffer->Size ||
        (SIZE_T)Size > Buffer->Size - (SIZE_T)Offset)
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glGetBufferSubData(range)");
        return;
    }
    if (Size != 0)
        CopyMemory(Data, Buffer->Data + (SIZE_T)Offset, (SIZE_T)Size);
}

static GLvoid * APIENTRY
Rpi5OglMapBuffer(
    _In_ GLenum Target,
    _In_ GLenum Access)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glMapBuffer"))
        return NULL;
    if (Access != GL_READ_ONLY && Access != GL_WRITE_ONLY &&
        Access != GL_READ_WRITE)
    {
        Rpi5OglBufferError(State, GL_INVALID_ENUM, "glMapBuffer(access)");
        return NULL;
    }
    Buffer = Rpi5OglBufferBoundObject(State, Target, "glMapBuffer(target)");
    if (Buffer == NULL)
        return NULL;
    if (Buffer->Mapped)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION, "glMapBuffer(mapped)");
        return NULL;
    }
    Buffer->Mapped = TRUE;
    Buffer->Access = Access;
    return Buffer->Data;
}

static GLboolean APIENTRY
Rpi5OglUnmapBuffer(
    _In_ GLenum Target)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glUnmapBuffer"))
        return GL_FALSE;
    Buffer = Rpi5OglBufferBoundObject(State, Target,
                                      "glUnmapBuffer(target)");
    if (Buffer == NULL)
        return GL_FALSE;
    if (!Buffer->Mapped)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION,
                           "glUnmapBuffer(mapped)");
        return GL_FALSE;
    }
    Buffer->Mapped = FALSE;
    return GL_TRUE;
}

static VOID APIENTRY
Rpi5OglGetBufferParameteriv(
    _In_ GLenum Target,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glGetBufferParameteriv"))
        return;
    if (Parameters == NULL)
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glGetBufferParameteriv(params)");
        return;
    }
    Buffer = Rpi5OglBufferBoundObject(State, Target,
                                      "glGetBufferParameteriv(target)");
    if (Buffer == NULL)
        return;

    switch (ParameterName)
    {
        case GL_BUFFER_SIZE:
            *Parameters = (GLint)Buffer->Size;
            break;
        case GL_BUFFER_USAGE:
            *Parameters = (GLint)Buffer->Usage;
            break;
        case GL_BUFFER_ACCESS:
            *Parameters = (GLint)Buffer->Access;
            break;
        case GL_BUFFER_MAPPED:
            *Parameters = Buffer->Mapped ? GL_TRUE : GL_FALSE;
            break;
        default:
            Rpi5OglBufferError(State, GL_INVALID_ENUM,
                               "glGetBufferParameteriv(pname)");
            break;
    }
}

static VOID APIENTRY
Rpi5OglGetBufferPointerv(
    _In_ GLenum Target,
    _In_ GLenum ParameterName,
    _Outptr_ GLvoid **Parameters)
{
    PRPI5VC4_OGL_BUFFER_STATE State = Rpi5OglCurrentBufferState();
    PRPI5VC4_OGL_BUFFER Buffer;

    if (!Rpi5OglBufferCanChangeState(State, "glGetBufferPointerv"))
        return;
    if (ParameterName != GL_BUFFER_MAP_POINTER)
    {
        Rpi5OglBufferError(State, GL_INVALID_ENUM,
                           "glGetBufferPointerv(pname)");
        return;
    }
    if (Parameters == NULL)
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glGetBufferPointerv(params)");
        return;
    }
    Buffer = Rpi5OglBufferBoundObject(State, Target,
                                      "glGetBufferPointerv(target)");
    if (Buffer == NULL)
        return;
    *Parameters = Buffer->Mapped ? Buffer->Data : NULL;
}

typedef struct _RPI5VC4_OGL_BUFFER_PROC
{
    PCSTR Name;
    PROC Procedure;
} RPI5VC4_OGL_BUFFER_PROC, *PRPI5VC4_OGL_BUFFER_PROC;

#define RPI5VC4_OGL_BUFFER_PROC_PAIR(Name, Procedure) \
    {Name, (PROC)Procedure}, {Name "ARB", (PROC)Procedure}

static const RPI5VC4_OGL_BUFFER_PROC Rpi5OglBufferProcedures[] =
{
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glBindBuffer", Rpi5OglBindBuffer),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glBufferData", Rpi5OglBufferData),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glBufferSubData", Rpi5OglBufferSubData),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glDeleteBuffers", Rpi5OglDeleteBuffers),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glGenBuffers", Rpi5OglGenBuffers),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glGetBufferParameteriv",
                                 Rpi5OglGetBufferParameteriv),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glGetBufferPointerv",
                                 Rpi5OglGetBufferPointerv),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glGetBufferSubData",
                                 Rpi5OglGetBufferSubData),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glIsBuffer", Rpi5OglIsBuffer),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glMapBuffer", Rpi5OglMapBuffer),
    RPI5VC4_OGL_BUFFER_PROC_PAIR("glUnmapBuffer", Rpi5OglUnmapBuffer),
};

#undef RPI5VC4_OGL_BUFFER_PROC_PAIR

BOOL
Rpi5OglBufferInitialize(
    _Outptr_ PRPI5VC4_OGL_BUFFER_STATE *State,
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_BUFFER_STATE NewState;

    *State = NULL;
    NewState = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         sizeof(*NewState));
    if (NewState == NULL)
        return FALSE;
    NewState->Mesa = Mesa;
    NewState->NextName = 1;
    *State = NewState;
    return TRUE;
}

VOID
Rpi5OglBufferCleanup(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State)
{
    ULONG Index;

    if (State == NULL)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Buffers); Index++)
    {
        if (State->Buffers[Index].Data != NULL)
            HeapFree(GetProcessHeap(), 0, State->Buffers[Index].Data);
    }
    HeapFree(GetProcessHeap(), 0, State);
}

GLuint
Rpi5OglBufferCurrentName(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Target)
{
    if (State == NULL)
        return 0;
    if (Target == GL_ARRAY_BUFFER)
        return State->ArrayBuffer;
    if (Target == GL_ELEMENT_ARRAY_BUFFER)
        return State->ElementArrayBuffer;
    return 0;
}

BOOL
Rpi5OglBufferResolveRange(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Name,
    _In_opt_ const GLvoid *Pointer,
    _In_ SIZE_T Bytes,
    _Out_ const BYTE **Data)
{
    PRPI5VC4_OGL_BUFFER Buffer;
    SIZE_T Offset;

    if (State == NULL || Name == 0 || Data == NULL)
        return FALSE;
    Buffer = Rpi5OglBufferFind(State, Name);
    if (Buffer == NULL || !Buffer->Created || Buffer->Mapped)
        return FALSE;
    Offset = (SIZE_T)(ULONG_PTR)Pointer;
    if (Offset > Buffer->Size || Bytes > Buffer->Size - Offset ||
        (Bytes != 0 && Buffer->Data == NULL))
    {
        return FALSE;
    }
    *Data = Buffer->Data != NULL ? Buffer->Data + Offset : NULL;
    return TRUE;
}

BOOL
Rpi5OglBufferReadElementIndices(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Indices,
    _In_ GLsizei Count,
    _Out_writes_(Count) GLuint *Values,
    _Out_opt_ GLuint *MaximumIndex)
{
    const BYTE *Source;
    SIZE_T ElementBytes;
    SIZE_T Bytes;
    GLuint Maximum = 0;
    GLsizei Index;

    if (State == NULL || Count < 0 || (Count != 0 && Values == NULL))
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE,
                           "glDrawElements(count/values)");
        return FALSE;
    }
    switch (Type)
    {
        case GL_UNSIGNED_BYTE:
            ElementBytes = sizeof(GLubyte);
            break;
        case GL_UNSIGNED_SHORT:
            ElementBytes = sizeof(GLushort);
            break;
        case GL_UNSIGNED_INT:
            ElementBytes = sizeof(GLuint);
            break;
        default:
            Rpi5OglBufferError(State, GL_INVALID_ENUM,
                               "glDrawElements(type)");
            return FALSE;
    }
    if (Count == 0)
    {
        if (MaximumIndex != NULL)
            *MaximumIndex = 0;
        return TRUE;
    }
    if ((SIZE_T)Count > (SIZE_T)-1 / ElementBytes)
    {
        Rpi5OglBufferError(State, GL_INVALID_OPERATION,
                           "glDrawElements(index range)");
        return FALSE;
    }
    Bytes = (SIZE_T)Count * ElementBytes;
    if (State->ElementArrayBuffer != 0)
    {
        if (!Rpi5OglBufferResolveRange(State,
                                       State->ElementArrayBuffer,
                                       Indices,
                                       Bytes,
                                       &Source))
        {
            Rpi5OglBufferError(State, GL_INVALID_OPERATION,
                               "glDrawElements(element buffer)");
            return FALSE;
        }
    }
    else
    {
        if (Indices == NULL)
        {
            Rpi5OglBufferError(State, GL_INVALID_OPERATION,
                               "glDrawElements(indices)");
            return FALSE;
        }
        Source = (const BYTE *)Indices;
    }

    for (Index = 0; Index < Count; Index++)
    {
        GLuint Value;

        if (Type == GL_UNSIGNED_BYTE)
            Value = Source[Index];
        else if (Type == GL_UNSIGNED_SHORT)
            Value = *(const UNALIGNED GLushort *)(Source +
                    (SIZE_T)Index * ElementBytes);
        else
            Value = *(const UNALIGNED GLuint *)(Source +
                    (SIZE_T)Index * ElementBytes);
        Values[Index] = Value;
        if (Value > Maximum)
            Maximum = Value;
    }
    if (MaximumIndex != NULL)
        *MaximumIndex = Maximum;
    return TRUE;
}

BOOL
Rpi5OglBufferGetIntegerv(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    GLuint Name;

    if (ParameterName == GL_ARRAY_BUFFER_BINDING)
        Name = State != NULL ? State->ArrayBuffer : 0;
    else if (ParameterName == GL_ELEMENT_ARRAY_BUFFER_BINDING)
        Name = State != NULL ? State->ElementArrayBuffer : 0;
    else
        return FALSE;

    if (Parameters == NULL)
    {
        Rpi5OglBufferError(State, GL_INVALID_VALUE, "glGetIntegerv(params)");
        return TRUE;
    }
    *Parameters = (GLint)Name;
    return TRUE;
}

PROC
Rpi5OglBufferGetProcAddress(
    _In_z_ LPCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Rpi5OglBufferProcedures); Index++)
    {
        if (lstrcmpA(Name, Rpi5OglBufferProcedures[Index].Name) == 0)
            return Rpi5OglBufferProcedures[Index].Procedure;
    }
    return NULL;
}
