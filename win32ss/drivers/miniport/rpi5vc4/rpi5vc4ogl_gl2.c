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
#define RPI5VC4_GL2_MAX_VERTEX_ATTRIBS    16

typedef enum _RPI5VC4_OGL_SHADER_EXECUTABLE
{
    Rpi5OglShaderExecutableNone,
    Rpi5OglShaderExecutableFixedVertex,
    Rpi5OglShaderExecutableFixedFragment,
    Rpi5OglShaderExecutableGenericVertex,
    Rpi5OglShaderExecutableGenericFragment
} RPI5VC4_OGL_SHADER_EXECUTABLE;

typedef enum _RPI5VC4_OGL_PROGRAM_EXECUTABLE
{
    Rpi5OglProgramExecutableNone,
    Rpi5OglProgramExecutableFixedColor,
    Rpi5OglProgramExecutableGenericColor
} RPI5VC4_OGL_PROGRAM_EXECUTABLE;

typedef struct _RPI5VC4_OGL_SHADER
{
    GLuint Name;
    GLenum Type;
    BOOL Compiled;
    BOOL DeletePending;
    ULONG AttachedCount;
    RPI5VC4_OGL_SHADER_EXECUTABLE Executable;
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
    RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable;
    GLint PositionBinding;
    GLint ColorBinding;
    BOOL PositionBindingSet;
    BOOL ColorBindingSet;
    GLint PositionAttribute;
    GLint ColorAttribute;
    CHAR InfoLog[RPI5VC4_GL2_INFO_LOG_BYTES];
} RPI5VC4_OGL_PROGRAM, *PRPI5VC4_OGL_PROGRAM;

typedef struct _RPI5VC4_OGL_VERTEX_ATTRIB
{
    BOOL Enabled;
    GLint Size;
    GLenum Type;
    GLboolean Normalized;
    GLsizei Stride;
    const GLvoid *Pointer;
    GLfloat Current[4];
} RPI5VC4_OGL_VERTEX_ATTRIB, *PRPI5VC4_OGL_VERTEX_ATTRIB;

struct _RPI5VC4_OGL_GL2_STATE
{
    GLcontext *Mesa;
    GLuint NextName;
    GLuint CurrentProgram;
    BOOL CurrentExecutableReady;
    RPI5VC4_OGL_SHADER Shaders[RPI5VC4_GL2_MAX_SHADERS];
    RPI5VC4_OGL_PROGRAM Programs[RPI5VC4_GL2_MAX_PROGRAMS];
    RPI5VC4_OGL_VERTEX_ATTRIB VertexAttribs[RPI5VC4_GL2_MAX_VERTEX_ATTRIBS];
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

static RPI5VC4_OGL_SHADER_EXECUTABLE
Rpi5OglGl2SourceExecutable(
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
    static const CHAR GenericVertexMain[] =
        "attributevec4pos;attributevec4color;varyingvec4out_color;"
        "voidmain(){gl_Position=pos;out_color=color;}";
    static const CHAR GenericVertexMainVoid[] =
        "attributevec4pos;attributevec4color;varyingvec4out_color;"
        "voidmain(void){gl_Position=pos;out_color=color;}";
    static const CHAR GenericFragmentMain[] =
        "varyingvec4out_color;"
        "voidmain(){gl_FragData[0]=out_color;}";
    static const CHAR GenericFragmentMainVoid[] =
        "varyingvec4out_color;"
        "voidmain(void){gl_FragData[0]=out_color;}";
    CHAR Normalized[RPI5VC4_GL2_MAX_NORMALIZED];
    PCSTR Body;

    if (Shader->Source == NULL ||
        !Rpi5OglGl2NormalizeSource(Shader->Source,
                                   Shader->SourceLength,
                                   Normalized,
                                   sizeof(Normalized)))
    {
        return Rpi5OglShaderExecutableNone;
    }

    Body = Normalized;
    if (strncmp(Body, "#version120", sizeof("#version120") - 1) == 0)
        Body += sizeof("#version120") - 1;

    if (Shader->Type == GL_VERTEX_SHADER)
    {
        if (lstrcmpA(Body, VertexMain) == 0 ||
            lstrcmpA(Body, VertexMainNoVoid) == 0)
        {
            return Rpi5OglShaderExecutableFixedVertex;
        }
        if (lstrcmpA(Body, GenericVertexMain) == 0 ||
            lstrcmpA(Body, GenericVertexMainVoid) == 0)
        {
            return Rpi5OglShaderExecutableGenericVertex;
        }
        return Rpi5OglShaderExecutableNone;
    }
    if (lstrcmpA(Body, FragmentMain) == 0 ||
        lstrcmpA(Body, FragmentMainNoVoid) == 0)
    {
        return Rpi5OglShaderExecutableFixedFragment;
    }
    if (lstrcmpA(Body, GenericFragmentMain) == 0 ||
        lstrcmpA(Body, GenericFragmentMainVoid) == 0)
    {
        return Rpi5OglShaderExecutableGenericFragment;
    }
    return Rpi5OglShaderExecutableNone;
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
    RPI5VC4_OGL_SHADER_EXECUTABLE Executable;

    if (!Rpi5OglGl2CanChangeState(State, "glCompileShader"))
        return;
    Shader = Rpi5OglGl2FindShader(State, ShaderName);
    if (Shader == NULL)
    {
        Rpi5OglGl2ShaderNameError(State, ShaderName,
                                  "glCompileShader(shader)");
        return;
    }

    Executable = Rpi5OglGl2SourceExecutable(Shader);
    Shader->Compiled = Executable != Rpi5OglShaderExecutableNone;
    if (Shader->Compiled)
    {
        Shader->Executable = Executable;
        Shader->InfoLog[0] = '\0';
    }
    else
    {
        Rpi5OglGl2SetLog(
            Shader->InfoLog,
            "RPi5 bounded GLSL 1.20 compiler supports fixed-transform/color and the WineD3D capability shader");
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
    Program->PositionBinding = -1;
    Program->ColorBinding = -1;
    Program->PositionAttribute = -1;
    Program->ColorAttribute = -1;
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

static BOOL
Rpi5OglGl2AssignGenericAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_ GLint *ColorAttribute)
{
    GLint Candidate;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    *ColorAttribute = Program->ColorBindingSet ?
                      Program->ColorBinding : -1;
    if (*PositionAttribute >= 0 &&
        *PositionAttribute == *ColorAttribute)
    {
        return FALSE;
    }

    if (*PositionAttribute < 0)
    {
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            if (Candidate != *ColorAttribute)
            {
                *PositionAttribute = Candidate;
                break;
            }
        }
    }
    if (*ColorAttribute < 0)
    {
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            if (Candidate != *PositionAttribute)
            {
                *ColorAttribute = Candidate;
                break;
            }
        }
    }
    return *PositionAttribute >= 0 && *ColorAttribute >= 0;
}

static VOID APIENTRY
Rpi5OglBindAttribLocation(
    _In_ GLuint ProgramName,
    _In_ GLuint Index,
    _In_z_ const GLchar *Name)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glBindAttribLocation"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glBindAttribLocation(program)");
        return;
    }
    if (Index >= RPI5VC4_GL2_MAX_VERTEX_ATTRIBS)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glBindAttribLocation(index)");
        return;
    }
    if (Name == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glBindAttribLocation(name)");
        return;
    }
    if (Name[0] == 'g' && Name[1] == 'l' && Name[2] == '_')
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glBindAttribLocation(reserved name)");
        return;
    }

    if (lstrcmpA(Name, "pos") == 0)
    {
        Program->PositionBinding = Index;
        Program->PositionBindingSet = TRUE;
    }
    else if (lstrcmpA(Name, "color") == 0)
    {
        Program->ColorBinding = Index;
        Program->ColorBindingSet = TRUE;
    }
}

static VOID APIENTRY
Rpi5OglLinkProgram(
    _In_ GLuint ProgramName)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_SHADER VertexShader;
    PRPI5VC4_OGL_SHADER FragmentShader;
    RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable =
        Rpi5OglProgramExecutableNone;
    GLint PositionAttribute = -1;
    GLint ColorAttribute = -1;

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
    if (VertexShader != NULL && FragmentShader != NULL &&
        VertexShader->Compiled && FragmentShader->Compiled)
    {
        if (VertexShader->Executable ==
                Rpi5OglShaderExecutableFixedVertex &&
            FragmentShader->Executable ==
                Rpi5OglShaderExecutableFixedFragment)
        {
            Executable = Rpi5OglProgramExecutableFixedColor;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableGenericVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableGenericFragment &&
                 Rpi5OglGl2AssignGenericAttributes(
                     Program,
                     &PositionAttribute,
                     &ColorAttribute))
        {
            Executable = Rpi5OglProgramExecutableGenericColor;
        }
    }
    Program->Linked = Executable != Rpi5OglProgramExecutableNone;
    if (Program->Linked)
    {
        Program->Executable = Executable;
        Program->PositionAttribute = PositionAttribute;
        Program->ColorAttribute = ColorAttribute;
        Program->InfoLog[0] = '\0';
    }
    else
    {
        Rpi5OglGl2SetLog(
            Program->InfoLog,
            "program requires a matching compiled bounded vertex/fragment pair with distinct active attributes");
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
            *Parameters = 0;
            break;
        case GL_ACTIVE_ATTRIBUTES:
            *Parameters = Program->Linked &&
                          Program->Executable ==
                              Rpi5OglProgramExecutableGenericColor ? 2 : 0;
            break;
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
            *Parameters = Program->Linked &&
                          Program->Executable ==
                              Rpi5OglProgramExecutableGenericColor ?
                          sizeof("color") : 0;
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
Rpi5OglGetActiveAttrib(
    _In_ GLuint ProgramName,
    _In_ GLuint Index,
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Length,
    _Out_opt_ GLint *Size,
    _Out_opt_ GLenum *Type,
    _Out_writes_bytes_opt_(BufferSize) GLchar *Name)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    PCSTR AttributeName;

    if (!Rpi5OglGl2CanChangeState(State, "glGetActiveAttrib"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetActiveAttrib(program)");
        return;
    }
    if (!Program->Linked ||
        Program->Executable != Rpi5OglProgramExecutableGenericColor ||
        Index >= 2)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetActiveAttrib(index)");
        return;
    }
    if (Size == NULL || Type == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetActiveAttrib(params)");
        return;
    }

    AttributeName = Index == 0 ? "pos" : "color";
    *Size = 1;
    *Type = GL_FLOAT_VEC4;
    Rpi5OglGl2CopyTextResult(State,
                             "glGetActiveAttrib",
                             AttributeName,
                             lstrlenA(AttributeName),
                             BufferSize,
                             Length,
                             Name);
}

static GLint APIENTRY
Rpi5OglGetAttribLocation(
    _In_ GLuint ProgramName,
    _In_z_ const GLchar *Name)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glGetAttribLocation"))
        return -1;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetAttribLocation(program)");
        return -1;
    }
    if (Name == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetAttribLocation(name)");
        return -1;
    }
    if (!Program->Linked)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glGetAttribLocation(link status)");
        return -1;
    }
    if (Program->Executable != Rpi5OglProgramExecutableGenericColor)
        return -1;
    if (lstrcmpA(Name, "pos") == 0)
        return Program->PositionAttribute;
    if (lstrcmpA(Name, "color") == 0)
        return Program->ColorAttribute;
    return -1;
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

static PRPI5VC4_OGL_VERTEX_ATTRIB
Rpi5OglGl2VertexAttrib(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLuint Index,
    _In_z_ PCSTR Function)
{
    if (State == NULL)
        return NULL;
    if (Index >= RPI5VC4_GL2_MAX_VERTEX_ATTRIBS)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, Function);
        return NULL;
    }
    return &State->VertexAttribs[Index];
}

static VOID APIENTRY
Rpi5OglEnableVertexAttribArray(
    _In_ GLuint Index)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;

    if (!Rpi5OglGl2CanChangeState(State, "glEnableVertexAttribArray"))
        return;
    Attribute = Rpi5OglGl2VertexAttrib(
        State, Index, "glEnableVertexAttribArray(index)");
    if (Attribute != NULL)
        Attribute->Enabled = TRUE;
}

static VOID APIENTRY
Rpi5OglDisableVertexAttribArray(
    _In_ GLuint Index)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;

    if (!Rpi5OglGl2CanChangeState(State, "glDisableVertexAttribArray"))
        return;
    Attribute = Rpi5OglGl2VertexAttrib(
        State, Index, "glDisableVertexAttribArray(index)");
    if (Attribute != NULL)
        Attribute->Enabled = FALSE;
}

static VOID APIENTRY
Rpi5OglVertexAttribPointer(
    _In_ GLuint Index,
    _In_ GLint Size,
    _In_ GLenum Type,
    _In_ GLboolean Normalized,
    _In_ GLsizei Stride,
    _In_opt_ const GLvoid *Pointer)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;

    if (!Rpi5OglGl2CanChangeState(State, "glVertexAttribPointer"))
        return;
    Attribute = Rpi5OglGl2VertexAttrib(
        State, Index, "glVertexAttribPointer(index)");
    if (Attribute == NULL)
        return;
    if (Size < 1 || Size > 4 || Stride < 0)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glVertexAttribPointer(size/stride)");
        return;
    }
    if (Type != GL_FLOAT)
    {
        Rpi5OglGl2Error(State, GL_INVALID_ENUM,
                        "glVertexAttribPointer(type)");
        return;
    }

    Attribute->Size = Size;
    Attribute->Type = Type;
    Attribute->Normalized = Normalized;
    Attribute->Stride = Stride;
    Attribute->Pointer = Pointer;
}

static VOID
Rpi5OglGl2SetCurrentAttrib(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLuint Index,
    _In_ GLfloat X,
    _In_ GLfloat Y,
    _In_ GLfloat Z,
    _In_ GLfloat W,
    _In_z_ PCSTR Function)
{
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;

    if (!Rpi5OglGl2CanChangeState(State, Function))
        return;
    Attribute = Rpi5OglGl2VertexAttrib(State, Index, Function);
    if (Attribute == NULL)
        return;
    Attribute->Current[0] = X;
    Attribute->Current[1] = Y;
    Attribute->Current[2] = Z;
    Attribute->Current[3] = W;
}

static VOID APIENTRY
Rpi5OglVertexAttrib1f(
    _In_ GLuint Index,
    _In_ GLfloat X)
{
    Rpi5OglGl2SetCurrentAttrib(Rpi5OglCurrentGl2State(), Index,
                               X, 0.0f, 0.0f, 1.0f,
                               "glVertexAttrib1f");
}

static VOID APIENTRY
Rpi5OglVertexAttrib1fv(
    _In_ GLuint Index,
    _In_reads_(1) const GLfloat *Values)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (Values == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glVertexAttrib1fv(v)");
        return;
    }
    Rpi5OglVertexAttrib1f(Index, Values[0]);
}

static VOID APIENTRY
Rpi5OglVertexAttrib2f(
    _In_ GLuint Index,
    _In_ GLfloat X,
    _In_ GLfloat Y)
{
    Rpi5OglGl2SetCurrentAttrib(Rpi5OglCurrentGl2State(), Index,
                               X, Y, 0.0f, 1.0f,
                               "glVertexAttrib2f");
}

static VOID APIENTRY
Rpi5OglVertexAttrib2fv(
    _In_ GLuint Index,
    _In_reads_(2) const GLfloat *Values)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (Values == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glVertexAttrib2fv(v)");
        return;
    }
    Rpi5OglVertexAttrib2f(Index, Values[0], Values[1]);
}

static VOID APIENTRY
Rpi5OglVertexAttrib3f(
    _In_ GLuint Index,
    _In_ GLfloat X,
    _In_ GLfloat Y,
    _In_ GLfloat Z)
{
    Rpi5OglGl2SetCurrentAttrib(Rpi5OglCurrentGl2State(), Index,
                               X, Y, Z, 1.0f,
                               "glVertexAttrib3f");
}

static VOID APIENTRY
Rpi5OglVertexAttrib3fv(
    _In_ GLuint Index,
    _In_reads_(3) const GLfloat *Values)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (Values == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glVertexAttrib3fv(v)");
        return;
    }
    Rpi5OglVertexAttrib3f(Index, Values[0], Values[1], Values[2]);
}

static VOID APIENTRY
Rpi5OglVertexAttrib4f(
    _In_ GLuint Index,
    _In_ GLfloat X,
    _In_ GLfloat Y,
    _In_ GLfloat Z,
    _In_ GLfloat W)
{
    Rpi5OglGl2SetCurrentAttrib(Rpi5OglCurrentGl2State(), Index,
                               X, Y, Z, W,
                               "glVertexAttrib4f");
}

static VOID APIENTRY
Rpi5OglVertexAttrib4fv(
    _In_ GLuint Index,
    _In_reads_(4) const GLfloat *Values)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (Values == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glVertexAttrib4fv(v)");
        return;
    }
    Rpi5OglVertexAttrib4f(Index, Values[0], Values[1],
                          Values[2], Values[3]);
}

static VOID APIENTRY
Rpi5OglGetVertexAttribfv(
    _In_ GLuint Index,
    _In_ GLenum ParameterName,
    _Out_ GLfloat *Parameters)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;

    if (!Rpi5OglGl2CanChangeState(State, "glGetVertexAttribfv"))
        return;
    Attribute = Rpi5OglGl2VertexAttrib(
        State, Index, "glGetVertexAttribfv(index)");
    if (Attribute == NULL)
        return;
    if (Parameters == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetVertexAttribfv(params)");
        return;
    }

    switch (ParameterName)
    {
        case GL_CURRENT_VERTEX_ATTRIB:
            CopyMemory(Parameters, Attribute->Current,
                       sizeof(Attribute->Current));
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *Parameters = Attribute->Enabled;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *Parameters = Attribute->Size;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *Parameters = Attribute->Stride;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *Parameters = Attribute->Type;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *Parameters = Attribute->Normalized;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *Parameters = 0.0f;
            break;
        default:
            Rpi5OglGl2Error(State, GL_INVALID_ENUM,
                            "glGetVertexAttribfv(pname)");
            break;
    }
}

static VOID APIENTRY
Rpi5OglGetVertexAttribPointerv(
    _In_ GLuint Index,
    _In_ GLenum ParameterName,
    _Outptr_ GLvoid **Pointer)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;

    if (!Rpi5OglGl2CanChangeState(State, "glGetVertexAttribPointerv"))
        return;
    Attribute = Rpi5OglGl2VertexAttrib(
        State, Index, "glGetVertexAttribPointerv(index)");
    if (Attribute == NULL)
        return;
    if (ParameterName != GL_VERTEX_ATTRIB_ARRAY_POINTER)
    {
        Rpi5OglGl2Error(State, GL_INVALID_ENUM,
                        "glGetVertexAttribPointerv(pname)");
        return;
    }
    if (Pointer == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetVertexAttribPointerv(pointer)");
        return;
    }
    *Pointer = (GLvoid *)Attribute->Pointer;
}

typedef struct _RPI5VC4_OGL_GL2_PROC
{
    PCSTR Name;
    PROC Procedure;
} RPI5VC4_OGL_GL2_PROC, *PRPI5VC4_OGL_GL2_PROC;

static const RPI5VC4_OGL_GL2_PROC Rpi5OglGl2Procedures[] =
{
    {"glAttachShader", (PROC)Rpi5OglAttachShader},
    {"glBindAttribLocation", (PROC)Rpi5OglBindAttribLocation},
    {"glCompileShader", (PROC)Rpi5OglCompileShader},
    {"glCreateProgram", (PROC)Rpi5OglCreateProgram},
    {"glCreateShader", (PROC)Rpi5OglCreateShader},
    {"glDeleteProgram", (PROC)Rpi5OglDeleteProgram},
    {"glDeleteShader", (PROC)Rpi5OglDeleteShader},
    {"glDetachShader", (PROC)Rpi5OglDetachShader},
    {"glDisableVertexAttribArray", (PROC)Rpi5OglDisableVertexAttribArray},
    {"glEnableVertexAttribArray", (PROC)Rpi5OglEnableVertexAttribArray},
    {"glGetActiveAttrib", (PROC)Rpi5OglGetActiveAttrib},
    {"glGetAttachedShaders", (PROC)Rpi5OglGetAttachedShaders},
    {"glGetAttribLocation", (PROC)Rpi5OglGetAttribLocation},
    {"glGetProgramInfoLog", (PROC)Rpi5OglGetProgramInfoLog},
    {"glGetProgramiv", (PROC)Rpi5OglGetProgramiv},
    {"glGetShaderInfoLog", (PROC)Rpi5OglGetShaderInfoLog},
    {"glGetShaderiv", (PROC)Rpi5OglGetShaderiv},
    {"glGetShaderSource", (PROC)Rpi5OglGetShaderSource},
    {"glGetVertexAttribfv", (PROC)Rpi5OglGetVertexAttribfv},
    {"glGetVertexAttribPointerv", (PROC)Rpi5OglGetVertexAttribPointerv},
    {"glIsProgram", (PROC)Rpi5OglIsProgram},
    {"glIsShader", (PROC)Rpi5OglIsShader},
    {"glLinkProgram", (PROC)Rpi5OglLinkProgram},
    {"glShaderSource", (PROC)Rpi5OglShaderSource},
    {"glUseProgram", (PROC)Rpi5OglUseProgram},
    {"glValidateProgram", (PROC)Rpi5OglValidateProgram},
    {"glVertexAttrib1f", (PROC)Rpi5OglVertexAttrib1f},
    {"glVertexAttrib1fv", (PROC)Rpi5OglVertexAttrib1fv},
    {"glVertexAttrib2f", (PROC)Rpi5OglVertexAttrib2f},
    {"glVertexAttrib2fv", (PROC)Rpi5OglVertexAttrib2fv},
    {"glVertexAttrib3f", (PROC)Rpi5OglVertexAttrib3f},
    {"glVertexAttrib3fv", (PROC)Rpi5OglVertexAttrib3fv},
    {"glVertexAttrib4f", (PROC)Rpi5OglVertexAttrib4f},
    {"glVertexAttrib4fv", (PROC)Rpi5OglVertexAttrib4fv},
    {"glVertexAttribPointer", (PROC)Rpi5OglVertexAttribPointer},
};

BOOL
Rpi5OglGl2Initialize(
    _Outptr_ PRPI5VC4_OGL_GL2_STATE *State,
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_GL2_STATE NewState;
    ULONG Index;

    *State = NULL;
    NewState = HeapAlloc(GetProcessHeap(),
                         HEAP_ZERO_MEMORY,
                         sizeof(*NewState));
    if (NewState == NULL)
        return FALSE;
    NewState->Mesa = Mesa;
    NewState->NextName = 1;
    for (Index = 0; Index < RTL_NUMBER_OF(NewState->VertexAttribs); Index++)
    {
        NewState->VertexAttribs[Index].Size = 4;
        NewState->VertexAttribs[Index].Type = GL_FLOAT;
        NewState->VertexAttribs[Index].Current[3] = 1.0f;
    }
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

static BOOL
Rpi5OglGl2ReadVertexAttrib(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLint AttributeIndex,
    _In_ GLint VertexIndex,
    _Out_writes_(4) GLfloat Values[4])
{
    PRPI5VC4_OGL_VERTEX_ATTRIB Attribute;
    const BYTE *Source;
    SIZE_T EffectiveStride;
    SIZE_T Offset;
    GLint Component;

    if (AttributeIndex < 0 ||
        AttributeIndex >= RPI5VC4_GL2_MAX_VERTEX_ATTRIBS)
    {
        return FALSE;
    }
    Attribute = &State->VertexAttribs[AttributeIndex];
    CopyMemory(Values, Attribute->Current, sizeof(Attribute->Current));
    if (!Attribute->Enabled)
        return TRUE;
    if (Attribute->Pointer == NULL || Attribute->Type != GL_FLOAT ||
        Attribute->Size < 1 || Attribute->Size > 4)
    {
        return FALSE;
    }

    EffectiveStride = Attribute->Stride != 0 ?
                      (SIZE_T)Attribute->Stride :
                      (SIZE_T)Attribute->Size * sizeof(GLfloat);
    if (EffectiveStride != 0 &&
        (SIZE_T)VertexIndex > (SIZE_T)-1 / EffectiveStride)
    {
        return FALSE;
    }
    Offset = (SIZE_T)VertexIndex * EffectiveStride;
    Source = (const BYTE *)Attribute->Pointer + Offset;
    Values[0] = 0.0f;
    Values[1] = 0.0f;
    Values[2] = 0.0f;
    Values[3] = 1.0f;
    for (Component = 0; Component < Attribute->Size; Component++)
    {
        CopyMemory(&Values[Component],
                   Source + Component * sizeof(GLfloat),
                   sizeof(GLfloat));
    }
    return TRUE;
}

static GLubyte
Rpi5OglGl2FloatColor(
    _In_ GLfloat Value)
{
    if (Value <= 0.0f)
        return 0;
    if (Value >= 1.0f)
        return 255;
    return (GLubyte)(Value * 255.0f + 0.5f);
}

RPI5VC4_OGL_GL2_DRAW_RESULT
Rpi5OglGl2BuildTriangle(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _Out_writes_(3) RPI5VC4_OGL_GL2_VERTEX Vertices[3])
{
    PRPI5VC4_OGL_PROGRAM Program;
    GLfloat Position[4];
    GLfloat Color[4];
    GLint Vertex;
    GLint Component;

    if (!Rpi5OglGl2ProgramActive(State))
        return Rpi5OglGl2DrawNotApplicable;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        Program->Executable != Rpi5OglProgramExecutableGenericColor)
    {
        return Rpi5OglGl2DrawNotApplicable;
    }
    if (Mode != GL_TRIANGLES || Count != 3)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(bounded generic draw)");
        return Rpi5OglGl2DrawRejected;
    }
    if (First < 0 || First > 0x7ffffffd)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glDrawArrays(first)");
        return Rpi5OglGl2DrawRejected;
    }
    if (Vertices == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glDrawArrays(vertices)");
        return Rpi5OglGl2DrawRejected;
    }

    for (Vertex = 0; Vertex < 3; Vertex++)
    {
        if (!Rpi5OglGl2ReadVertexAttrib(
                State,
                Program->PositionAttribute,
                First + Vertex,
                Position) ||
            !Rpi5OglGl2ReadVertexAttrib(
                State,
                Program->ColorAttribute,
                First + Vertex,
                Color))
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glDrawArrays(vertex input)");
            return Rpi5OglGl2DrawRejected;
        }
        CopyMemory(Vertices[Vertex].Position,
                   Position,
                   sizeof(Position));
        for (Component = 0; Component < 4; Component++)
        {
            Vertices[Vertex].Color[Component] =
                Rpi5OglGl2FloatColor(Color[Component]);
        }
    }
    return Rpi5OglGl2DrawReady;
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
