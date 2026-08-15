/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded OpenGL 2 shader-program integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * This is an incremental compatibility-profile shader boundary. It accepts
 * only the GLSL 1.20 fixed-transform/color program whose semantics match the
 * fixed V3D 7.1 shaders owned by the miniport and the existing Mesa vertex
 * path. Unsupported source fails compilation, and the ICD continues to
 * advertise OpenGL 1.1 until the full OpenGL 2 contract is implemented.
 */

#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <context.h>
#include <macros.h>

#include "rpi5vc4ogl_gl2.h"

#define RPI5VC4_GL2_MAX_SHADERS       16
#define RPI5VC4_GL2_MAX_PROGRAMS       8
#define RPI5VC4_GL2_MAX_SOURCE_BYTES   (64 * 1024)
#define RPI5VC4_GL2_MAX_NORMALIZED      512
#define RPI5VC4_GL2_INFO_LOG_BYTES      192

typedef struct _RPI5VC4_OGL_SHADER
{
    GLuint Name;
    GLenum Type;
    BOOL Compiled;
    BOOL DeletePending;
    ULONG AttachedCount;
    PCHAR Source;
    ULONG SourceLength;
    CHAR InfoLog[RPI5VC4_GL2_INFO_LOG_BYTES];
} RPI5VC4_OGL_SHADER, *PRPI5VC4_OGL_SHADER;

typedef struct _RPI5VC4_OGL_PROGRAM
{
    GLuint Name;
    GLuint VertexShader;
    GLuint FragmentShader;
    BOOL Linked;
    BOOL Validated;
    BOOL DeletePending;
    CHAR InfoLog[RPI5VC4_GL2_INFO_LOG_BYTES];
} RPI5VC4_OGL_PROGRAM, *PRPI5VC4_OGL_PROGRAM;

struct _RPI5VC4_OGL_GL2_STATE
{
    GLcontext *Mesa;
    GLuint NextName;
    GLuint CurrentProgram;
    BOOL CurrentExecutableReady;
    RPI5VC4_OGL_SHADER Shaders[RPI5VC4_GL2_MAX_SHADERS];
    RPI5VC4_OGL_PROGRAM Programs[RPI5VC4_GL2_MAX_PROGRAMS];
};

static PRPI5VC4_OGL_SHADER
Rpi5OglGl2FindShader(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Shaders); Index++)
    {
        if (State->Shaders[Index].Name == Name)
            return &State->Shaders[Index];
    }
    return NULL;
}

static PRPI5VC4_OGL_PROGRAM
Rpi5OglGl2FindProgram(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Programs); Index++)
    {
        if (State->Programs[Index].Name == Name)
            return &State->Programs[Index];
    }
    return NULL;
}

static GLuint
Rpi5OglGl2AllocateName(
    _Inout_ PRPI5VC4_OGL_GL2_STATE State)
{
    GLuint Name;
    ULONG Attempts;

    for (Attempts = 0;
         Attempts < RPI5VC4_GL2_MAX_SHADERS + RPI5VC4_GL2_MAX_PROGRAMS + 1;
         Attempts++)
    {
        Name = State->NextName++;
        if (Name == 0)
        {
            Name = State->NextName++;
            if (Name == 0)
                continue;
        }
        if (Rpi5OglGl2FindShader(State, Name) == NULL &&
            Rpi5OglGl2FindProgram(State, Name) == NULL)
        {
            return Name;
        }
    }
    return 0;
}

static VOID
Rpi5OglGl2Error(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Error,
    _In_z_ PCSTR Function)
{
    if (State != NULL && State->Mesa != NULL)
        gl_error(State->Mesa, Error, Function);
}

static VOID
Rpi5OglGl2ShaderNameError(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLuint Name,
    _In_z_ PCSTR Function)
{
    Rpi5OglGl2Error(State,
                    Rpi5OglGl2FindProgram(State, Name) != NULL ?
                        GL_INVALID_OPERATION : GL_INVALID_VALUE,
                    Function);
}

static VOID
Rpi5OglGl2ProgramNameError(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLuint Name,
    _In_z_ PCSTR Function)
{
    Rpi5OglGl2Error(State,
                    Rpi5OglGl2FindShader(State, Name) != NULL ?
                        GL_INVALID_OPERATION : GL_INVALID_VALUE,
                    Function);
}

static BOOL
Rpi5OglGl2CanChangeState(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_z_ PCSTR Function)
{
    if (State == NULL)
        return FALSE;
    if (INSIDE_BEGIN_END(State->Mesa))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION, Function);
        return FALSE;
    }
    return TRUE;
}

static VOID
Rpi5OglGl2SetLog(
    _Out_writes_(RPI5VC4_GL2_INFO_LOG_BYTES) PCHAR Destination,
    _In_z_ PCSTR Message)
{
    SIZE_T Length = lstrlenA(Message);

    if (Length >= RPI5VC4_GL2_INFO_LOG_BYTES)
        Length = RPI5VC4_GL2_INFO_LOG_BYTES - 1;
    CopyMemory(Destination, Message, Length);
    Destination[Length] = '\0';
}

static VOID
Rpi5OglGl2CopyTextResult(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_z_ PCSTR Function,
    _In_reads_bytes_(SourceLength) PCSTR Source,
    _In_ ULONG SourceLength,
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Written,
    _Out_writes_bytes_opt_(BufferSize) GLchar *Destination)
{
    ULONG CopyLength;

    if (BufferSize < 0 || (BufferSize > 0 && Destination == NULL))
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, Function);
        return;
    }

    CopyLength = 0;
    if (BufferSize > 0)
    {
        CopyLength = SourceLength;
        if (CopyLength >= (ULONG)BufferSize)
            CopyLength = BufferSize - 1;
        if (CopyLength != 0)
            CopyMemory(Destination, Source, CopyLength);
        Destination[CopyLength] = '\0';
    }
    if (Written != NULL)
        *Written = CopyLength;
}

static BOOL
Rpi5OglGl2NormalizeSource(
    _In_reads_bytes_(SourceLength) PCSTR Source,
    _In_ ULONG SourceLength,
    _Out_writes_(Capacity) PCHAR Normalized,
    _In_ ULONG Capacity)
{
    ULONG SourceIndex;
    ULONG DestinationIndex = 0;
    CHAR Character;

    for (SourceIndex = 0; SourceIndex < SourceLength; SourceIndex++)
    {
        Character = Source[SourceIndex];
        if (Character == '\0')
            return FALSE;
        if (Character == ' ' || Character == '\t' || Character == '\r' ||
            Character == '\n' || Character == '\f' || Character == '\v')
        {
            continue;
        }
        if (DestinationIndex + 1 >= Capacity)
            return FALSE;
        Normalized[DestinationIndex++] = Character;
    }
    Normalized[DestinationIndex] = '\0';
    return TRUE;
}

static BOOL
Rpi5OglGl2SourceSupported(
    _In_ PRPI5VC4_OGL_SHADER Shader)
{
    static const CHAR VertexMain[] =
        "voidmain(void){gl_Position=ftransform();gl_FrontColor=gl_Color;}";
    static const CHAR VertexMainNoVoid[] =
        "voidmain(){gl_Position=ftransform();gl_FrontColor=gl_Color;}";
    static const CHAR FragmentMain[] =
        "voidmain(void){gl_FragColor=gl_Color;}";
    static const CHAR FragmentMainNoVoid[] =
        "voidmain(){gl_FragColor=gl_Color;}";
    CHAR Normalized[RPI5VC4_GL2_MAX_NORMALIZED];
    PCSTR Body;

    if (Shader->Source == NULL ||
        !Rpi5OglGl2NormalizeSource(Shader->Source,
                                   Shader->SourceLength,
                                   Normalized,
                                   sizeof(Normalized)))
    {
        return FALSE;
    }

    Body = Normalized;
    if (strncmp(Body, "#version120", sizeof("#version120") - 1) == 0)
        Body += sizeof("#version120") - 1;

    if (Shader->Type == GL_VERTEX_SHADER)
    {
        return lstrcmpA(Body, VertexMain) == 0 ||
               lstrcmpA(Body, VertexMainNoVoid) == 0;
    }
    return lstrcmpA(Body, FragmentMain) == 0 ||
           lstrcmpA(Body, FragmentMainNoVoid) == 0;
}

static VOID
Rpi5OglGl2ReleaseShader(
    _Inout_ PRPI5VC4_OGL_SHADER Shader)
{
    if (Shader->Source != NULL)
        HeapFree(GetProcessHeap(), 0, Shader->Source);
    ZeroMemory(Shader, sizeof(*Shader));
}

static VOID
Rpi5OglGl2DetachProgramShaders(
    _Inout_ PRPI5VC4_OGL_GL2_STATE State,
    _Inout_ PRPI5VC4_OGL_PROGRAM Program)
{
    PRPI5VC4_OGL_SHADER Shader;
    GLuint Names[2];
    ULONG Index;

    Names[0] = Program->VertexShader;
    Names[1] = Program->FragmentShader;
    for (Index = 0; Index < RTL_NUMBER_OF(Names); Index++)
    {
        Shader = Rpi5OglGl2FindShader(State, Names[Index]);
        if (Shader == NULL)
            continue;
        if (Shader->AttachedCount != 0)
            Shader->AttachedCount--;
        if (Shader->AttachedCount == 0 && Shader->DeletePending)
            Rpi5OglGl2ReleaseShader(Shader);
    }
    Program->VertexShader = 0;
    Program->FragmentShader = 0;
}

static VOID
Rpi5OglGl2ReleaseProgram(
    _Inout_ PRPI5VC4_OGL_GL2_STATE State,
    _Inout_ PRPI5VC4_OGL_PROGRAM Program)
{
    Rpi5OglGl2DetachProgramShaders(State, Program);
    ZeroMemory(Program, sizeof(*Program));
}

static GLuint APIENTRY
Rpi5OglCreateShader(
    _In_ GLenum Type)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader = NULL;
    ULONG Index;

    if (!Rpi5OglGl2CanChangeState(State, "glCreateShader"))
        return 0;
    if (Type != GL_VERTEX_SHADER && Type != GL_FRAGMENT_SHADER)
    {
        Rpi5OglGl2Error(State, GL_INVALID_ENUM, "glCreateShader(type)");
        return 0;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(State->Shaders); Index++)
    {
        if (State->Shaders[Index].Name == 0)
        {
            Shader = &State->Shaders[Index];
            break;
        }
    }
    if (Shader == NULL || (Shader->Name = Rpi5OglGl2AllocateName(State)) == 0)
    {
        Rpi5OglGl2Error(State, GL_OUT_OF_MEMORY, "glCreateShader");
        return 0;
    }
    Shader->Type = Type;
    return Shader->Name;
}

static VOID APIENTRY
Rpi5OglShaderSource(
    _In_ GLuint ShaderName,
    _In_ GLsizei Count,
    _In_reads_(Count) const GLchar * const *Strings,
    _In_reads_opt_(Count) const GLint *Lengths)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader;
    PCHAR Source;
    SIZE_T TotalLength = 0;
    SIZE_T StringLength;
    SIZE_T Offset;
    GLsizei Index;

    if (!Rpi5OglGl2CanChangeState(State, "glShaderSource"))
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glShaderSource(shader)");
        return;
    }
    if (Count < 0 || (Count != 0 && Strings == NULL))
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glShaderSource(count)");
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (Strings[Index] == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glShaderSource(string)");
            return;
        }
        StringLength = Lengths != NULL && Lengths[Index] >= 0 ?
                       (SIZE_T)Lengths[Index] : lstrlenA(Strings[Index]);
        if (StringLength > RPI5VC4_GL2_MAX_SOURCE_BYTES - TotalLength)
        {
            Rpi5OglGl2Error(State, GL_OUT_OF_MEMORY, "glShaderSource");
            return;
        }
        TotalLength += StringLength;
    }

    Source = HeapAlloc(GetProcessHeap(), 0, TotalLength + 1);
    if (Source == NULL)
    {
        Rpi5OglGl2Error(State, GL_OUT_OF_MEMORY, "glShaderSource");
        return;
    }
    Offset = 0;
    for (Index = 0; Index < Count; Index++)
    {
        StringLength = Lengths != NULL && Lengths[Index] >= 0 ?
                       (SIZE_T)Lengths[Index] : lstrlenA(Strings[Index]);
        if (StringLength != 0)
            CopyMemory(Source + Offset, Strings[Index], StringLength);
        Offset += StringLength;
    }
    Source[Offset] = '\0';

    if (Shader->Source != NULL)
        HeapFree(GetProcessHeap(), 0, Shader->Source);
    Shader->Source = Source;
    Shader->SourceLength = (ULONG)TotalLength;
}

static VOID APIENTRY
Rpi5OglCompileShader(
    _In_ GLuint ShaderName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader;

    if (!Rpi5OglGl2CanChangeState(State, "glCompileShader"))
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glCompileShader(shader)");
        return;
    }

    Shader->Compiled = Rpi5OglGl2SourceSupported(Shader);
    if (Shader->Compiled)
    {
        Shader->InfoLog[0] = '\0';
    }
    else
    {
        Rpi5OglGl2SetLog(
            Shader->InfoLog,
            "RPi5 bounded GLSL 1.20 compiler supports only the fixed-transform/color shader");
    }
}

static VOID APIENTRY
Rpi5OglGetShaderiv(
    _In_ GLuint ShaderName,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader;

    if (!Rpi5OglGl2CanChangeState(State, "glGetShaderiv"))
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glGetShaderiv(shader)");
        return;
    }
    if (Parameters == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glGetShaderiv(params)");
        return;
    }

    switch (ParameterName)
    {
        case GL_SHADER_TYPE:
            *Parameters = Shader->Type;
            break;
        case GL_DELETE_STATUS:
            *Parameters = Shader->DeletePending;
            break;
        case GL_COMPILE_STATUS:
            *Parameters = Shader->Compiled;
            break;
        case GL_INFO_LOG_LENGTH:
            *Parameters = Shader->InfoLog[0] != '\0' ?
                          lstrlenA(Shader->InfoLog) + 1 : 0;
            break;
        case GL_SHADER_SOURCE_LENGTH:
            *Parameters = Shader->Source != NULL ?
                          Shader->SourceLength + 1 : 0;
            break;
        default:
            Rpi5OglGl2Error(State, GL_INVALID_ENUM,
                            "glGetShaderiv(pname)");
            break;
    }
}

static VOID APIENTRY
Rpi5OglGetShaderInfoLog(
    _In_ GLuint ShaderName,
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Length,
    _Out_writes_bytes_opt_(BufferSize) GLchar *InfoLog)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader;

    if (!Rpi5OglGl2CanChangeState(State, "glGetShaderInfoLog"))
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glGetShaderInfoLog(shader)");
        return;
    }
    Rpi5OglGl2CopyTextResult(State,
                             "glGetShaderInfoLog",
                             Shader->InfoLog,
                             lstrlenA(Shader->InfoLog),
                             BufferSize,
                             Length,
                             InfoLog);
}

static VOID APIENTRY
Rpi5OglGetShaderSource(
    _In_ GLuint ShaderName,
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Length,
    _Out_writes_bytes_opt_(BufferSize) GLchar *Source)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader;

    if (!Rpi5OglGl2CanChangeState(State, "glGetShaderSource"))
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glGetShaderSource(shader)");
        return;
    }
    Rpi5OglGl2CopyTextResult(State,
                             "glGetShaderSource",
                             Shader->Source != NULL ? Shader->Source : "",
                             Shader->Source != NULL ? Shader->SourceLength : 0,
                             BufferSize,
                             Length,
                             Source);
}

static GLboolean APIENTRY
Rpi5OglIsShader(
    _In_ GLuint ShaderName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (!Rpi5OglGl2CanChangeState(State, "glIsShader"))
        return GL_FALSE;
    return Rpi5OglGl2FindShader(State, ShaderName) != NULL;
}

static VOID APIENTRY
Rpi5OglDeleteShader(
    _In_ GLuint ShaderName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_SHADER Shader;

    if (!Rpi5OglGl2CanChangeState(State, "glDeleteShader"))
        return;
    if (ShaderName == 0)
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glDeleteShader(shader)");
        return;
    }
    if (Shader->AttachedCount != 0)
        Shader->DeletePending = TRUE;
    else
        Rpi5OglGl2ReleaseShader(Shader);
}

static GLuint APIENTRY
Rpi5OglCreateProgram(VOID)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program = NULL;
    ULONG Index;

    if (!Rpi5OglGl2CanChangeState(State, "glCreateProgram"))
        return 0;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Programs); Index++)
    {
        if (State->Programs[Index].Name == 0)
        {
            Program = &State->Programs[Index];
            break;
        }
    }
    if (Program == NULL ||
        (Program->Name = Rpi5OglGl2AllocateName(State)) == 0)
    {
        Rpi5OglGl2Error(State, GL_OUT_OF_MEMORY, "glCreateProgram");
        return 0;
    }
    return Program->Name;
}

static VOID APIENTRY
Rpi5OglAttachShader(
    _In_ GLuint ProgramName,
    _In_ GLuint ShaderName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_SHADER Shader;
    GLuint *Attachment;

    if (!Rpi5OglGl2CanChangeState(State, "glAttachShader"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glAttachShader(program)");
        return;
    }
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glAttachShader(shader)");
        return;
    }

    Attachment = Shader->Type == GL_VERTEX_SHADER ?
                 &Program->VertexShader : &Program->FragmentShader;
    if (*Attachment != 0)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glAttachShader(stage)");
        return;
    }
    *Attachment = ShaderName;
    Shader->AttachedCount++;
}

static VOID APIENTRY
Rpi5OglDetachShader(
    _In_ GLuint ProgramName,
    _In_ GLuint ShaderName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_SHADER Shader;
    GLuint *Attachment;

    if (!Rpi5OglGl2CanChangeState(State, "glDetachShader"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glDetachShader(program)");
        return;
    }
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glDetachShader(shader)");
        return;
    }
    Attachment = Shader->Type == GL_VERTEX_SHADER ?
                 &Program->VertexShader : &Program->FragmentShader;
    if (*Attachment != ShaderName)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDetachShader(attachment)");
        return;
    }
    *Attachment = 0;
    if (Shader->AttachedCount != 0)
        Shader->AttachedCount--;
    if (Shader->AttachedCount == 0 && Shader->DeletePending)
        Rpi5OglGl2ReleaseShader(Shader);
}

static VOID APIENTRY
Rpi5OglLinkProgram(
    _In_ GLuint ProgramName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_SHADER VertexShader;
    PRPI5VC4_OGL_SHADER FragmentShader;

    if (!Rpi5OglGl2CanChangeState(State, "glLinkProgram"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glLinkProgram(program)");
        return;
    }

    VertexShader = Rpi5OglGl2FindShader(State, Program->VertexShader);
    FragmentShader = Rpi5OglGl2FindShader(State,
                                          Program->FragmentShader);
    Program->Linked = VertexShader != NULL &&
                      FragmentShader != NULL &&
                      VertexShader->Compiled &&
                      FragmentShader->Compiled;
    if (Program->Linked)
    {
        Program->InfoLog[0] = '\0';
    }
    else
    {
        Rpi5OglGl2SetLog(
            Program->InfoLog,
            "program requires one compiled bounded vertex shader and one compiled bounded fragment shader");
    }
}

static VOID APIENTRY
Rpi5OglGetProgramiv(
    _In_ GLuint ProgramName,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glGetProgramiv"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetProgramiv(program)");
        return;
    }
    if (Parameters == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glGetProgramiv(params)");
        return;
    }

    switch (ParameterName)
    {
        case GL_DELETE_STATUS:
            *Parameters = Program->DeletePending;
            break;
        case GL_LINK_STATUS:
            *Parameters = Program->Linked;
            break;
        case GL_VALIDATE_STATUS:
            *Parameters = Program->Validated;
            break;
        case GL_INFO_LOG_LENGTH:
            *Parameters = Program->InfoLog[0] != '\0' ?
                          lstrlenA(Program->InfoLog) + 1 : 0;
            break;
        case GL_ATTACHED_SHADERS:
            *Parameters = (Program->VertexShader != 0) +
                          (Program->FragmentShader != 0);
            break;
        case GL_ACTIVE_UNIFORMS:
        case GL_ACTIVE_UNIFORM_MAX_LENGTH:
        case GL_ACTIVE_ATTRIBUTES:
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
            *Parameters = 0;
            break;
        default:
            Rpi5OglGl2Error(State, GL_INVALID_ENUM,
                            "glGetProgramiv(pname)");
            break;
    }
}

static VOID APIENTRY
Rpi5OglGetProgramInfoLog(
    _In_ GLuint ProgramName,
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Length,
    _Out_writes_bytes_opt_(BufferSize) GLchar *InfoLog)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glGetProgramInfoLog"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetProgramInfoLog(program)");
        return;
    }
    Rpi5OglGl2CopyTextResult(State,
                             "glGetProgramInfoLog",
                             Program->InfoLog,
                             lstrlenA(Program->InfoLog),
                             BufferSize,
                             Length,
                             InfoLog);
}

static VOID APIENTRY
Rpi5OglGetAttachedShaders(
    _In_ GLuint ProgramName,
    _In_ GLsizei MaximumCount,
    _Out_opt_ GLsizei *Count,
    _Out_writes_opt_(MaximumCount) GLuint *Shaders)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    GLsizei Written = 0;

    if (!Rpi5OglGl2CanChangeState(State, "glGetAttachedShaders"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetAttachedShaders(program)");
        return;
    }
    if (MaximumCount < 0 || (MaximumCount != 0 && Shaders == NULL))
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetAttachedShaders(maxCount)");
        return;
    }

    if (Program->VertexShader != 0 && Written < MaximumCount)
        Shaders[Written++] = Program->VertexShader;
    if (Program->FragmentShader != 0 && Written < MaximumCount)
        Shaders[Written++] = Program->FragmentShader;
    if (Count != NULL)
        *Count = Written;
}

static VOID APIENTRY
Rpi5OglValidateProgram(
    _In_ GLuint ProgramName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glValidateProgram"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glValidateProgram(program)");
        return;
    }
    Program->Validated = Program->Linked;
    if (Program->Validated)
    {
        Program->InfoLog[0] = '\0';
    }
    else
    {
        Rpi5OglGl2SetLog(Program->InfoLog,
                         "program has no linked bounded executable");
    }
}

static GLboolean APIENTRY
Rpi5OglIsProgram(
    _In_ GLuint ProgramName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (!Rpi5OglGl2CanChangeState(State, "glIsProgram"))
        return GL_FALSE;
    return Rpi5OglGl2FindProgram(State, ProgramName) != NULL;
}

static VOID APIENTRY
Rpi5OglUseProgram(
    _In_ GLuint ProgramName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_PROGRAM Previous;
    GLuint PreviousName;

    if (!Rpi5OglGl2CanChangeState(State, "glUseProgram"))
        return;
    Program = ProgramName != 0 ?
              Rpi5OglGl2FindProgram(State, ProgramName) : NULL;
    if (ProgramName != 0 && Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glUseProgram(program)");
        return;
    }
    if (Program != NULL && !Program->Linked)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUseProgram(link status)");
        return;
    }

    PreviousName = State->CurrentProgram;
    State->CurrentProgram = ProgramName;
    State->CurrentExecutableReady = Program != NULL;
    if (PreviousName == 0 || PreviousName == ProgramName)
        return;
    Previous = Rpi5OglGl2FindProgram(State, PreviousName);
    if (Previous != NULL && Previous->DeletePending)
        Rpi5OglGl2ReleaseProgram(State, Previous);
}

static VOID APIENTRY
Rpi5OglDeleteProgram(
    _In_ GLuint ProgramName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glDeleteProgram"))
        return;
    if (ProgramName == 0)
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glDeleteProgram(program)");
        return;
    }
    if (State->CurrentProgram == ProgramName)
        Program->DeletePending = TRUE;
    else
        Rpi5OglGl2ReleaseProgram(State, Program);
}

typedef struct _RPI5VC4_OGL_GL2_PROC
{
    PCSTR Name;
    PROC Procedure;
} RPI5VC4_OGL_GL2_PROC, *PRPI5VC4_OGL_GL2_PROC;

static const RPI5VC4_OGL_GL2_PROC Rpi5OglGl2Procedures[] =
{
    {"glAttachShader", (PROC)Rpi5OglAttachShader},
    {"glCompileShader", (PROC)Rpi5OglCompileShader},
    {"glCreateProgram", (PROC)Rpi5OglCreateProgram},
    {"glCreateShader", (PROC)Rpi5OglCreateShader},
    {"glDeleteProgram", (PROC)Rpi5OglDeleteProgram},
    {"glDeleteShader", (PROC)Rpi5OglDeleteShader},
    {"glDetachShader", (PROC)Rpi5OglDetachShader},
    {"glGetAttachedShaders", (PROC)Rpi5OglGetAttachedShaders},
    {"glGetProgramInfoLog", (PROC)Rpi5OglGetProgramInfoLog},
    {"glGetProgramiv", (PROC)Rpi5OglGetProgramiv},
    {"glGetShaderInfoLog", (PROC)Rpi5OglGetShaderInfoLog},
    {"glGetShaderiv", (PROC)Rpi5OglGetShaderiv},
    {"glGetShaderSource", (PROC)Rpi5OglGetShaderSource},
    {"glIsProgram", (PROC)Rpi5OglIsProgram},
    {"glIsShader", (PROC)Rpi5OglIsShader},
    {"glLinkProgram", (PROC)Rpi5OglLinkProgram},
    {"glShaderSource", (PROC)Rpi5OglShaderSource},
    {"glUseProgram", (PROC)Rpi5OglUseProgram},
    {"glValidateProgram", (PROC)Rpi5OglValidateProgram},
};

BOOL
Rpi5OglGl2Initialize(
    _Outptr_ PRPI5VC4_OGL_GL2_STATE *State,
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_GL2_STATE NewState;

    *State = NULL;
    NewState = HeapAlloc(GetProcessHeap(),
                         HEAP_ZERO_MEMORY,
                         sizeof(*NewState));
    if (NewState == NULL)
        return FALSE;
    NewState->Mesa = Mesa;
    NewState->NextName = 1;
    *State = NewState;
    return TRUE;
}

VOID
Rpi5OglGl2Cleanup(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    ULONG Index;

    if (State == NULL)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Shaders); Index++)
    {
        if (State->Shaders[Index].Source != NULL)
            HeapFree(GetProcessHeap(), 0, State->Shaders[Index].Source);
    }
    HeapFree(GetProcessHeap(), 0, State);
}

BOOL
Rpi5OglGl2ProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    return State != NULL &&
           State->CurrentProgram != 0 &&
           State->CurrentExecutableReady;
}

GLuint
Rpi5OglGl2CurrentProgramName(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    return Rpi5OglGl2ProgramActive(State) ? State->CurrentProgram : 0;
}

PROC
Rpi5OglGl2GetProcAddress(
    _In_z_ LPCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Rpi5OglGl2Procedures); Index++)
    {
        if (lstrcmpA(Name, Rpi5OglGl2Procedures[Index].Name) == 0)
            return Rpi5OglGl2Procedures[Index].Procedure;
    }
    return NULL;
}
