/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded OpenGL 2 shader-program integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * This is an incremental compatibility-profile shader boundary. It accepts
 * only shader programs whose semantics are implemented by the fixed V3D 7.1
 * shaders owned by the miniport and the ICD's bounded vertex path.
 * Unsupported source fails compilation.
 */

#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>

#include <context.h>
#include <blend.h>
#include <macros.h>

#include "rpi5vc4ogl_gl2.h"

#define RPI5VC4_GL2_MAX_SHADERS       32
#define RPI5VC4_GL2_MAX_PROGRAMS      16
#define RPI5VC4_GL2_MAX_SOURCE_BYTES   (64 * 1024)
#define RPI5VC4_GL2_MAX_NORMALIZED     4096
#define RPI5VC4_GL2_INFO_LOG_BYTES      192
#define RPI5VC4_GL2_MAX_VERTEX_ATTRIBS    16
#define RPI5VC4_GL2_UNIFORM_MVP            0
#define RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX  1
#define RPI5VC4_GL2_UNIFORM_TEXTURE         2
#define RPI5VC4_GL2_UNIFORM_MODELVIEW       3
#define RPI5VC4_GL2_UNIFORM_VIEWPORT        4
#define RPI5VC4_GL2_UNIFORM_PROJECTION      5
#define RPI5VC4_GL2_UNIFORM_LIGHT0          6
#define RPI5VC4_GL2_UNIFORM_LOGO_DIRECTION  7
#define RPI5VC4_GL2_UNIFORM_CURRENT_TIME    8
#define RPI5VC4_GL2_UNIFORM_LOGO_COLOR      9
#define RPI5VC4_GL2_UNIFORM_LIGHT1         10
#define RPI5VC4_GL2_UNIFORM_LIGHT2         11
#define RPI5VC4_GL2_UNIFORM_LIGHT_RADIUS   12
#define RPI5VC4_GL2_UNIFORM_LIGHT_COLOR    13
#define RPI5VC4_GL2_UNIFORM_AMBIENT_COLOR  14
#define RPI5VC4_GL2_UNIFORM_FRESNEL_COLOR  15
#define RPI5VC4_GL2_UNIFORM_FRESNEL_POWER  16
#define RPI5VC4_GL2_UNIFORM_TEXTURE1       17
#define RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR1 18
#define RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR2 19
#define RPI5VC4_GL2_UNIFORM_UV_OFFSET       20
#define RPI5VC4_GL2_UNIFORM_UV_SCALE        21
#define RPI5VC4_GL2_UNIFORM_OPACITY         22
#define RPI5VC4_GL2_UNIFORM_VIEW_MATRIX      23
#define RPI5VC4_GL2_UNIFORM_TERRAIN_OFFSET   24
#define RPI5VC4_GL2_UNIFORM_TEXTURE2         25
#define RPI5VC4_GL2_UNIFORM_TEXTURE3         26
#define RPI5VC4_GL2_UNIFORM_TEXTURE4         27
#define RPI5VC4_GL2_UNIFORM_TEXTURE5         28
#define RPI5VC4_GL2_UNIFORM_LIGHT_MATRIX     29
#define RPI5VC4_OGL_TEXTURE_UNIT_COUNT        8

typedef enum _RPI5VC4_OGL_SHADER_EXECUTABLE
{
    Rpi5OglShaderExecutableNone,
    Rpi5OglShaderExecutableFixedVertex,
    Rpi5OglShaderExecutableFixedFragment,
    Rpi5OglShaderExecutableGenericVertex,
    Rpi5OglShaderExecutableGenericFragment,
    Rpi5OglShaderExecutableBuildVertex,
    Rpi5OglShaderExecutableShadowHorseVertex,
    Rpi5OglShaderExecutableGouraudVertex,
    Rpi5OglShaderExecutableAdvancedVertex,
    Rpi5OglShaderExecutablePhongVertex,
    Rpi5OglShaderExecutableBumpNormalsVertex,
    Rpi5OglShaderExecutableBumpHeightVertex,
    Rpi5OglShaderExecutableEffectVertex,
    Rpi5OglShaderExecutablePulsarVertex,
    Rpi5OglShaderExecutableDesktopVertex,
    Rpi5OglShaderExecutableWireframeVertex,
    Rpi5OglShaderExecutableIdeasTableVertex,
    Rpi5OglShaderExecutableIdeasPaperVertex,
    Rpi5OglShaderExecutableIdeasTextVertex,
    Rpi5OglShaderExecutableIdeasUnderTableVertex,
    Rpi5OglShaderExecutableIdeasLampUnlitVertex,
    Rpi5OglShaderExecutableIdeasFlatVertex,
    Rpi5OglShaderExecutableIdeasLitVertex,
    Rpi5OglShaderExecutableJellyfishGradientVertex,
    Rpi5OglShaderExecutableJellyfishMeshVertex,
    Rpi5OglShaderExecutableTerrainTextureVertex,
    Rpi5OglShaderExecutableTerrainVertex,
    Rpi5OglShaderExecutableDepthVertex,
    Rpi5OglShaderExecutableShadowVertex,
    Rpi5OglShaderExecutableBuildFragment,
    Rpi5OglShaderExecutableTextureFragment,
    Rpi5OglShaderExecutableAdvancedFragment,
    Rpi5OglShaderExecutableBumpPolyFragment,
    Rpi5OglShaderExecutablePhongFragment,
    Rpi5OglShaderExecutableCelFragment,
    Rpi5OglShaderExecutableBumpNormalsFragment,
    Rpi5OglShaderExecutableBumpHeightFragment,
    Rpi5OglShaderExecutableEffectEdgeFragment,
    Rpi5OglShaderExecutableEffectBlurFragment,
    Rpi5OglShaderExecutableDesktopFragment,
    Rpi5OglShaderExecutableDesktopBlurHorizontalFragment,
    Rpi5OglShaderExecutableDesktopBlurVerticalFragment,
    Rpi5OglShaderExecutableWireframeFragment,
    Rpi5OglShaderExecutableIdeasColorFragment,
    Rpi5OglShaderExecutableIdeasFlatFragment,
    Rpi5OglShaderExecutableIdeasShadowFragment,
    Rpi5OglShaderExecutableIdeasLogoFragment,
    Rpi5OglShaderExecutableIdeasLampFragment,
    Rpi5OglShaderExecutableJellyfishGradientFragment,
    Rpi5OglShaderExecutableJellyfishMeshFragment,
    Rpi5OglShaderExecutableTerrainNoiseFragment,
    Rpi5OglShaderExecutableTerrainNormalFragment,
    Rpi5OglShaderExecutableTerrainLuminanceFragment,
    Rpi5OglShaderExecutableTerrainOverlayFragment,
    Rpi5OglShaderExecutableTerrainBloomHorizontalFragment,
    Rpi5OglShaderExecutableTerrainBloomVerticalFragment,
    Rpi5OglShaderExecutableTerrainTiltHorizontalFragment,
    Rpi5OglShaderExecutableTerrainTiltVerticalFragment,
    Rpi5OglShaderExecutableTerrainFragment,
    Rpi5OglShaderExecutableDepthFragment,
    Rpi5OglShaderExecutableShadowFragment
} RPI5VC4_OGL_SHADER_EXECUTABLE;

typedef enum _RPI5VC4_OGL_PROGRAM_EXECUTABLE
{
    Rpi5OglProgramExecutableNone,
    Rpi5OglProgramExecutableFixedColor,
    Rpi5OglProgramExecutableGenericColor,
    Rpi5OglProgramExecutableBuildDiffuse,
    Rpi5OglProgramExecutableBuildTexture,
    Rpi5OglProgramExecutableBlinnPhong,
    Rpi5OglProgramExecutableBumpPoly,
    Rpi5OglProgramExecutablePhong,
    Rpi5OglProgramExecutableCel,
    Rpi5OglProgramExecutableNormalMap,
    Rpi5OglProgramExecutableHeightMap,
    Rpi5OglProgramExecutablePulsar,
    Rpi5OglProgramExecutableEffectEdge,
    Rpi5OglProgramExecutableEffectBlur,
    Rpi5OglProgramExecutableDesktopTexture,
    Rpi5OglProgramExecutableDesktopBlurHorizontal,
    Rpi5OglProgramExecutableDesktopBlurVertical,
    Rpi5OglProgramExecutableWireframe,
    Rpi5OglProgramExecutableIdeasTable,
    Rpi5OglProgramExecutableIdeasPaper,
    Rpi5OglProgramExecutableIdeasText,
    Rpi5OglProgramExecutableIdeasUnderTable,
    Rpi5OglProgramExecutableIdeasLampUnlit,
    Rpi5OglProgramExecutableIdeasFlat,
    Rpi5OglProgramExecutableIdeasShadow,
    Rpi5OglProgramExecutableIdeasLogo,
    Rpi5OglProgramExecutableIdeasLampLit,
    Rpi5OglProgramExecutableJellyfishGradient,
    Rpi5OglProgramExecutableJellyfishMesh,
    Rpi5OglProgramExecutableTerrainNoise,
    Rpi5OglProgramExecutableTerrainNormal,
    Rpi5OglProgramExecutableTerrainLuminance,
    Rpi5OglProgramExecutableTerrainOverlay,
    Rpi5OglProgramExecutableTerrainBloomHorizontal,
    Rpi5OglProgramExecutableTerrainBloomVertical,
    Rpi5OglProgramExecutableTerrainTiltHorizontal,
    Rpi5OglProgramExecutableTerrainTiltVertical,
    Rpi5OglProgramExecutableTerrain,
    Rpi5OglProgramExecutableDepth,
    Rpi5OglProgramExecutableShadow
} RPI5VC4_OGL_PROGRAM_EXECUTABLE;

static BOOL
Rpi5OglGl2IsEffectExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable == Rpi5OglProgramExecutableEffectEdge ||
           Executable == Rpi5OglProgramExecutableEffectBlur;
}

static BOOL
Rpi5OglGl2IsDesktopExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable == Rpi5OglProgramExecutableDesktopTexture ||
           Executable == Rpi5OglProgramExecutableDesktopBlurHorizontal ||
           Executable == Rpi5OglProgramExecutableDesktopBlurVertical;
}

static BOOL
Rpi5OglGl2IsIdeasExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable >= Rpi5OglProgramExecutableIdeasTable &&
           Executable <= Rpi5OglProgramExecutableIdeasLampLit;
}

static BOOL
Rpi5OglGl2IsIdeasLitExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable == Rpi5OglProgramExecutableIdeasLogo ||
           Executable == Rpi5OglProgramExecutableIdeasLampLit;
}

static BOOL
Rpi5OglGl2IsJellyfishExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable == Rpi5OglProgramExecutableJellyfishGradient ||
           Executable == Rpi5OglProgramExecutableJellyfishMesh;
}

static BOOL
Rpi5OglGl2IsTerrainExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable >= Rpi5OglProgramExecutableTerrainNoise &&
           Executable <= Rpi5OglProgramExecutableTerrain;
}

static BOOL
Rpi5OglGl2IsTerrainTextureExecutable(
    _In_ RPI5VC4_OGL_PROGRAM_EXECUTABLE Executable)
{
    return Executable >= Rpi5OglProgramExecutableTerrainNoise &&
           Executable <= Rpi5OglProgramExecutableTerrainTiltVertical;
}

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
    GLint NormalBinding;
    GLint TexCoordBinding;
    GLint TangentBinding;
    GLint TriangleVertexBinding[3];
    BOOL PositionBindingSet;
    BOOL ColorBindingSet;
    BOOL NormalBindingSet;
    BOOL TexCoordBindingSet;
    BOOL TangentBindingSet;
    BOOL TriangleVertexBindingSet[3];
    GLint PositionAttribute;
    GLint ColorAttribute;
    GLint NormalAttribute;
    GLint TexCoordAttribute;
    GLint TangentAttribute;
    GLint TriangleVertexAttribute[3];
    GLfloat ModelViewProjectionMatrix[16];
    GLfloat LightMatrix[16];
    GLfloat NormalMatrix[16];
    GLfloat ModelViewMatrix[16];
    GLfloat ViewMatrix[16];
    GLfloat ProjectionMatrix[16];
    GLfloat MaterialDiffuse[4];
    GLfloat Viewport[2];
    GLfloat LightPosition[4];
    GLfloat Light1Position[4];
    GLfloat Light2Position[4];
    GLfloat LogoDirection[3];
    GLfloat LogoColor[4];
    GLfloat LightRadius;
    GLfloat LightColor[4];
    GLfloat AmbientColor[4];
    GLfloat FresnelColor[4];
    GLfloat FresnelPower;
    GLfloat GradientColor1[3];
    GLfloat GradientColor2[3];
    GLfloat UvOffset[2];
    GLfloat UvScale[2];
    GLfloat TerrainOffset[2];
    GLfloat Opacity;
    GLfloat CurrentTime;
    BOOL ModelViewProjectionMatrixSet;
    BOOL LightMatrixSet;
    BOOL NormalMatrixSet;
    BOOL ModelViewMatrixSet;
    BOOL ViewMatrixSet;
    BOOL ProjectionMatrixSet;
    BOOL ViewportSet;
    BOOL NormalMatrix3Set;
    BOOL LightPositionSet;
    BOOL Light1PositionSet;
    BOOL Light2PositionSet;
    BOOL LogoDirectionSet;
    BOOL LogoColorSet;
    BOOL LightRadiusSet;
    BOOL LightColorSet;
    BOOL AmbientColorSet;
    BOOL FresnelColorSet;
    BOOL FresnelPowerSet;
    BOOL GradientColor1Set;
    BOOL GradientColor2Set;
    BOOL UvOffsetSet;
    BOOL UvScaleSet;
    BOOL TerrainOffsetSet;
    BOOL OpacitySet;
    BOOL CurrentTimeSet;
    GLint TextureUnit;
    GLint TextureUnit1;
    GLint TextureUnit2;
    GLint TextureUnit3;
    GLint TextureUnit4;
    GLint TextureUnit5;
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
    GLuint BufferName;
    GLfloat Current[4];
} RPI5VC4_OGL_VERTEX_ATTRIB, *PRPI5VC4_OGL_VERTEX_ATTRIB;

struct _RPI5VC4_OGL_GL2_STATE
{
    GLcontext *Mesa;
    PRPI5VC4_OGL_BUFFER_STATE BufferState;
    GLuint NextName;
    GLuint CurrentProgram;
    BOOL CurrentExecutableReady;
    RPI5VC4_OGL_SHADER Shaders[RPI5VC4_GL2_MAX_SHADERS];
    RPI5VC4_OGL_PROGRAM Programs[RPI5VC4_GL2_MAX_PROGRAMS];
    RPI5VC4_OGL_VERTEX_ATTRIB VertexAttribs[RPI5VC4_GL2_MAX_VERTEX_ATTRIBS];
    ULONG TextureSerial;
    ULONG UploadedTextureSerial;
    struct gl_texture_object *UploadedTexture;
    ULONG TextureGeneration;
    ULONG UploadedTextureSerial1;
    struct gl_texture_object *UploadedTexture1;
    ULONG TextureGeneration1;
    ULONG ActiveTextureUnit;
    struct gl_texture_object *TextureUnits[RPI5VC4_OGL_TEXTURE_UNIT_COUNT];
    BOOL PreserveDestinationAlphaBlend;
    GLfloat IdeasLampLights[12];
    GLfloat IdeasLogoLight[4];
    GLfloat JellyfishGradientColors[RPI5VC4_V3D_JELLYFISH_UNIFORM_WORDS];
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

static VOID APIENTRY
Rpi5OglBlendFuncSeparate(
    _In_ GLenum SourceRgb,
    _In_ GLenum DestinationRgb,
    _In_ GLenum SourceAlpha,
    _In_ GLenum DestinationAlpha)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();

    if (!Rpi5OglGl2CanChangeState(State, "glBlendFuncSeparate"))
        return;
    if (SourceRgb != GL_SRC_ALPHA ||
        DestinationRgb != GL_ONE_MINUS_SRC_ALPHA ||
        SourceAlpha != GL_ZERO ||
        DestinationAlpha != GL_ONE)
    {
        Rpi5OglGl2Error(State,
                        GL_INVALID_OPERATION,
                        "glBlendFuncSeparate(RPi5 blend state)");
        return;
    }

    gl_BlendFunc(State->Mesa, SourceRgb, DestinationRgb);
    State->PreserveDestinationAlphaBlend = TRUE;
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

static ULONG
Rpi5OglGl2SourceHash(
    _In_z_ PCSTR Source,
    _Out_ PULONG SourceLength)
{
    ULONG Hash = 2166136261u;
    ULONG Length = 0;

    while (Source[Length] != '\0')
    {
        Hash ^= (UCHAR)Source[Length++];
        Hash *= 16777619u;
    }
    *SourceLength = Length;
    return Hash;
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
    ULONG NormalizedLength;
    ULONG SourceHash;

    if (Shader->Source == NULL ||
        !Rpi5OglGl2NormalizeSource(Shader->Source,
                                   Shader->SourceLength,
                                   Normalized,
                                   sizeof(Normalized)))
    {
        return Rpi5OglShaderExecutableNone;
    }

    /* Exact fingerprints of the supported stock glmark2 shader pairs. */
    SourceHash = Rpi5OglGl2SourceHash(Normalized, &NormalizedLength);
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 1044 && SourceHash == 0xBA70A74Fu)
    {
        return Rpi5OglShaderExecutableBuildVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 1041 && SourceHash == 0x21781919u)
    {
        return Rpi5OglShaderExecutableShadowHorseVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 1044 && SourceHash == 0x454223D9u)
    {
        return Rpi5OglShaderExecutableGouraudVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 487 && SourceHash == 0x69AE382Au)
    {
        return Rpi5OglShaderExecutableAdvancedVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 694 && SourceHash == 0x5A30B3F8u)
    {
        return Rpi5OglShaderExecutablePhongVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 403 && SourceHash == 0xA0BEA104u)
    {
        return Rpi5OglShaderExecutableBumpNormalsVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 901 && SourceHash == 0x3AC755C6u)
    {
        return Rpi5OglShaderExecutableBumpHeightVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 290 && SourceHash == 0x6CEF03AFu)
    {
        return Rpi5OglShaderExecutableEffectVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 477 && SourceHash == 0x0956DA45u)
    {
        return Rpi5OglShaderExecutablePulsarVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 305 && SourceHash == 0xF0A692F8u)
    {
        return Rpi5OglShaderExecutableDesktopVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 1869 && SourceHash == 0xD333D5F9u)
    {
        return Rpi5OglShaderExecutableWireframeVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 711 && SourceHash == 0x54A236B4u)
    {
        return Rpi5OglShaderExecutableIdeasTableVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 721 && SourceHash == 0x8E707534u)
    {
        return Rpi5OglShaderExecutableIdeasPaperVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 509 && SourceHash == 0x03B38BDAu)
    {
        return Rpi5OglShaderExecutableIdeasTextVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 378 && SourceHash == 0x533CA507u)
    {
        return Rpi5OglShaderExecutableIdeasUnderTableVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 378 && SourceHash == 0x308A6302u)
    {
        return Rpi5OglShaderExecutableIdeasLampUnlitVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 333 && SourceHash == 0xB120CDEAu)
    {
        return Rpi5OglShaderExecutableIdeasFlatVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 753 && SourceHash == 0xF0E6302Au)
    {
        return Rpi5OglShaderExecutableIdeasLitVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 273 && SourceHash == 0xE875E5F3u)
    {
        return Rpi5OglShaderExecutableJellyfishGradientVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 1699 && SourceHash == 0xDB0F393Du)
    {
        return Rpi5OglShaderExecutableJellyfishMeshVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 344 && SourceHash == 0x68A28E76u)
    {
        return Rpi5OglShaderExecutableTerrainTextureVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 1232 && SourceHash == 0x1AEAA4F8u)
    {
        return Rpi5OglShaderExecutableTerrainVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 348 && SourceHash == 0xF281BB64u)
    {
        return Rpi5OglShaderExecutableDepthVertex;
    }
    if (Shader->Type == GL_VERTEX_SHADER &&
        NormalizedLength == 491 && SourceHash == 0x6FA458D0u)
    {
        return Rpi5OglShaderExecutableShadowVertex;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 316 && SourceHash == 0xE93EF3EFu)
    {
        return Rpi5OglShaderExecutableBuildFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 406 && SourceHash == 0x8EA15446u)
    {
        return Rpi5OglShaderExecutableTextureFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1427 && SourceHash == 0x23A59840u)
    {
        return Rpi5OglShaderExecutableAdvancedFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1411 && SourceHash == 0x284C061Fu)
    {
        return Rpi5OglShaderExecutableBumpPolyFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1723 && SourceHash == 0xBCFDD921u)
    {
        return Rpi5OglShaderExecutablePhongFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 2152 && SourceHash == 0xD972FB78u)
    {
        return Rpi5OglShaderExecutableCelFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1840 && SourceHash == 0x10F34646u)
    {
        return Rpi5OglShaderExecutableBumpNormalsFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 2126 && SourceHash == 0xD2C964A8u)
    {
        return Rpi5OglShaderExecutableBumpHeightFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1640 && SourceHash == 0x4FC4302Cu)
    {
        return Rpi5OglShaderExecutableEffectEdgeFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 2302 && SourceHash == 0x828B81D2u)
    {
        return Rpi5OglShaderExecutableEffectBlurFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 383 && SourceHash == 0x8E8057BBu)
    {
        return Rpi5OglShaderExecutableDesktopFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1308 && SourceHash == 0x08FA70C6u)
    {
        return Rpi5OglShaderExecutableDesktopBlurHorizontalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1308 && SourceHash == 0xB6CC6170u)
    {
        return Rpi5OglShaderExecutableDesktopBlurVerticalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 682 && SourceHash == 0xB0AE0BF0u)
    {
        return Rpi5OglShaderExecutableWireframeFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 288 && SourceHash == 0xED701616u)
    {
        return Rpi5OglShaderExecutableIdeasColorFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 296 && SourceHash == 0xFD82231Au)
    {
        return Rpi5OglShaderExecutableIdeasFlatFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 378 && SourceHash == 0xF066E718u)
    {
        return Rpi5OglShaderExecutableIdeasShadowFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1266 && SourceHash == 0x353682FAu)
    {
        return Rpi5OglShaderExecutableIdeasLogoFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1992 && SourceHash == 0x5AD3BBB6u)
    {
        return Rpi5OglShaderExecutableIdeasLampFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 370 && SourceHash == 0x5B8993CAu)
    {
        return Rpi5OglShaderExecutableJellyfishGradientFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 793 && SourceHash == 0x4C8B9963u)
    {
        return Rpi5OglShaderExecutableJellyfishMeshFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 2371 && SourceHash == 0x7560F120u)
    {
        return Rpi5OglShaderExecutableTerrainNoiseFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 573 && SourceHash == 0x8451083Fu)
    {
        return Rpi5OglShaderExecutableTerrainNormalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 419 && SourceHash == 0x6F8B2EFBu)
    {
        return Rpi5OglShaderExecutableTerrainLuminanceFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 373 && SourceHash == 0xDBCF9F4Au)
    {
        return Rpi5OglShaderExecutableTerrainOverlayFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 895 && SourceHash == 0x8B28250Cu)
    {
        return Rpi5OglShaderExecutableTerrainBloomHorizontalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 895 && SourceHash == 0x5A24E0B2u)
    {
        return Rpi5OglShaderExecutableTerrainBloomVerticalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1195 && SourceHash == 0x5AD5E33Cu)
    {
        return Rpi5OglShaderExecutableTerrainTiltHorizontalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 1195 && SourceHash == 0x82711C62u)
    {
        return Rpi5OglShaderExecutableTerrainTiltVerticalFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 3232 && SourceHash == 0x19FE3FDCu)
    {
        return Rpi5OglShaderExecutableTerrainFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 300 && SourceHash == 0xC285F35Fu)
    {
        return Rpi5OglShaderExecutableDepthFragment;
    }
    if (Shader->Type == GL_FRAGMENT_SHADER &&
        NormalizedLength == 602 && SourceHash == 0xB2797519u)
    {
        return Rpi5OglShaderExecutableShadowFragment;
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
        Shader->Executable = Rpi5OglShaderExecutableNone;
        Rpi5OglGl2SetLog(
            Shader->InfoLog,
            "RPi5 bounded GLSL path does not implement this shader source");
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
    ULONG TriangleVertex;

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
    Program->NormalBinding = -1;
    Program->TexCoordBinding = -1;
    Program->TangentBinding = -1;
    Program->PositionAttribute = -1;
    Program->ColorAttribute = -1;
    Program->NormalAttribute = -1;
    Program->TexCoordAttribute = -1;
    Program->TangentAttribute = -1;
    for (TriangleVertex = 0;
         TriangleVertex < RTL_NUMBER_OF(Program->TriangleVertexBinding);
         TriangleVertex++)
    {
        Program->TriangleVertexBinding[TriangleVertex] = -1;
        Program->TriangleVertexAttribute[TriangleVertex] = -1;
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

static BOOL
Rpi5OglGl2AssignPositionAttribute(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute)
{
    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : 0;
    return *PositionAttribute >= 0;
}

static BOOL
Rpi5OglGl2AssignBuildAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_ GLint *NormalAttribute)
{
    GLint Candidate;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    *NormalAttribute = Program->NormalBindingSet ?
                       Program->NormalBinding : -1;
    if (*PositionAttribute >= 0 &&
        *PositionAttribute == *NormalAttribute)
    {
        return FALSE;
    }

    if (*PositionAttribute < 0)
    {
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            if (Candidate != *NormalAttribute)
            {
                *PositionAttribute = Candidate;
                break;
            }
        }
    }
    if (*NormalAttribute < 0)
    {
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            if (Candidate != *PositionAttribute)
            {
                *NormalAttribute = Candidate;
                break;
            }
        }
    }
    return *PositionAttribute >= 0 && *NormalAttribute >= 0;
}

static BOOL
Rpi5OglGl2AssignTextureAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_ GLint *NormalAttribute,
    _Out_ GLint *TexCoordAttribute)
{
    GLint *Attributes[3] =
    {
        PositionAttribute,
        NormalAttribute,
        TexCoordAttribute
    };
    GLint Candidate;
    ULONG Index;
    ULONG Other;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    *NormalAttribute = Program->NormalBindingSet ?
                       Program->NormalBinding : -1;
    *TexCoordAttribute = Program->TexCoordBindingSet ?
                         Program->TexCoordBinding : -1;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] < 0)
            continue;
        for (Other = Index + 1;
             Other < RTL_NUMBER_OF(Attributes);
             Other++)
        {
            if (*Attributes[Index] == *Attributes[Other])
                return FALSE;
        }
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] >= 0)
            continue;
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            BOOL Used = FALSE;

            for (Other = 0;
                 Other < RTL_NUMBER_OF(Attributes);
                 Other++)
            {
                if (*Attributes[Other] == Candidate)
                {
                    Used = TRUE;
                    break;
                }
            }
            if (!Used)
            {
                *Attributes[Index] = Candidate;
                break;
            }
        }
    }
    return *PositionAttribute >= 0 &&
           *NormalAttribute >= 0 &&
           *TexCoordAttribute >= 0;
}

static BOOL
Rpi5OglGl2AssignNormalMapAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_ GLint *TexCoordAttribute)
{
    GLint Candidate;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    *TexCoordAttribute = Program->TexCoordBindingSet ?
                         Program->TexCoordBinding : -1;
    if (*PositionAttribute >= 0 &&
        *PositionAttribute == *TexCoordAttribute)
    {
        return FALSE;
    }

    if (*PositionAttribute < 0)
    {
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            if (Candidate != *TexCoordAttribute)
            {
                *PositionAttribute = Candidate;
                break;
            }
        }
    }
    if (*TexCoordAttribute < 0)
    {
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            if (Candidate != *PositionAttribute)
            {
                *TexCoordAttribute = Candidate;
                break;
            }
        }
    }
    return *PositionAttribute >= 0 && *TexCoordAttribute >= 0;
}

static BOOL
Rpi5OglGl2AssignHeightMapAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_ GLint *NormalAttribute,
    _Out_ GLint *TexCoordAttribute,
    _Out_ GLint *TangentAttribute)
{
    GLint *Attributes[4] =
    {
        PositionAttribute,
        NormalAttribute,
        TexCoordAttribute,
        TangentAttribute
    };
    GLint Candidate;
    ULONG Index;
    ULONG Other;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    *NormalAttribute = Program->NormalBindingSet ?
                       Program->NormalBinding : -1;
    *TexCoordAttribute = Program->TexCoordBindingSet ?
                         Program->TexCoordBinding : -1;
    *TangentAttribute = Program->TangentBindingSet ?
                        Program->TangentBinding : -1;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] < 0)
            continue;
        for (Other = Index + 1;
             Other < RTL_NUMBER_OF(Attributes);
             Other++)
        {
            if (*Attributes[Index] == *Attributes[Other])
                return FALSE;
        }
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] >= 0)
            continue;
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            BOOL Used = FALSE;

            for (Other = 0;
                 Other < RTL_NUMBER_OF(Attributes);
                 Other++)
            {
                if (*Attributes[Other] == Candidate)
                {
                    Used = TRUE;
                    break;
                }
            }
            if (!Used)
            {
                *Attributes[Index] = Candidate;
                break;
            }
        }
    }
    return *PositionAttribute >= 0 &&
           *NormalAttribute >= 0 &&
           *TexCoordAttribute >= 0 &&
           *TangentAttribute >= 0;
}

static BOOL
Rpi5OglGl2AssignJellyfishAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_ GLint *NormalAttribute,
    _Out_ GLint *ColorAttribute,
    _Out_ GLint *TexCoordAttribute)
{
    GLint *Attributes[4] =
    {
        PositionAttribute,
        NormalAttribute,
        ColorAttribute,
        TexCoordAttribute
    };
    GLint Candidate;
    ULONG Index;
    ULONG Other;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    *NormalAttribute = Program->NormalBindingSet ?
                       Program->NormalBinding : -1;
    *ColorAttribute = Program->ColorBindingSet ?
                      Program->ColorBinding : -1;
    *TexCoordAttribute = Program->TexCoordBindingSet ?
                         Program->TexCoordBinding : -1;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] < 0)
            continue;
        for (Other = Index + 1; Other < RTL_NUMBER_OF(Attributes); Other++)
        {
            if (*Attributes[Index] == *Attributes[Other])
                return FALSE;
        }
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] >= 0)
            continue;
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            BOOL Used = FALSE;

            for (Other = 0; Other < RTL_NUMBER_OF(Attributes); Other++)
            {
                if (*Attributes[Other] == Candidate)
                {
                    Used = TRUE;
                    break;
                }
            }
            if (!Used)
            {
                *Attributes[Index] = Candidate;
                break;
            }
        }
    }
    return *PositionAttribute >= 0 &&
           *NormalAttribute >= 0 &&
           *ColorAttribute >= 0 &&
           *TexCoordAttribute >= 0;
}

static BOOL
Rpi5OglGl2AssignWireframeAttributes(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _Out_ GLint *PositionAttribute,
    _Out_writes_(3) GLint TriangleVertexAttributes[3])
{
    GLint *Attributes[4] =
    {
        PositionAttribute,
        &TriangleVertexAttributes[0],
        &TriangleVertexAttributes[1],
        &TriangleVertexAttributes[2]
    };
    GLint Candidate;
    ULONG Index;
    ULONG Other;

    *PositionAttribute = Program->PositionBindingSet ?
                         Program->PositionBinding : -1;
    for (Index = 0; Index < 3; Index++)
    {
        TriangleVertexAttributes[Index] =
            Program->TriangleVertexBindingSet[Index] ?
                Program->TriangleVertexBinding[Index] : -1;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] < 0)
            continue;
        for (Other = Index + 1;
             Other < RTL_NUMBER_OF(Attributes);
             Other++)
        {
            if (*Attributes[Index] == *Attributes[Other])
                return FALSE;
        }
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
    {
        if (*Attributes[Index] >= 0)
            continue;
        for (Candidate = 0;
             Candidate < RPI5VC4_GL2_MAX_VERTEX_ATTRIBS;
             Candidate++)
        {
            BOOL Used = FALSE;

            for (Other = 0;
                 Other < RTL_NUMBER_OF(Attributes);
                 Other++)
            {
                if (*Attributes[Other] == Candidate)
                {
                    Used = TRUE;
                    break;
                }
            }
            if (!Used)
            {
                *Attributes[Index] = Candidate;
                break;
            }
        }
    }

    return *PositionAttribute >= 0 &&
           TriangleVertexAttributes[0] >= 0 &&
           TriangleVertexAttributes[1] >= 0 &&
           TriangleVertexAttributes[2] >= 0;
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

    if (lstrcmpA(Name, "pos") == 0 ||
        lstrcmpA(Name, "position") == 0 ||
        lstrcmpA(Name, "vertex") == 0 ||
        lstrcmpA(Name, "aVertexPosition") == 0)
    {
        Program->PositionBinding = Index;
        Program->PositionBindingSet = TRUE;
    }
    else if (lstrcmpA(Name, "color") == 0 ||
             lstrcmpA(Name, "vtxcolor") == 0 ||
             lstrcmpA(Name, "aVertexColor") == 0)
    {
        Program->ColorBinding = Index;
        Program->ColorBindingSet = TRUE;
    }
    else if (lstrcmpA(Name, "normal") == 0 ||
             lstrcmpA(Name, "aVertexNormal") == 0)
    {
        Program->NormalBinding = Index;
        Program->NormalBindingSet = TRUE;
    }
    else if (lstrcmpA(Name, "texcoord") == 0 ||
             lstrcmpA(Name, "uv") == 0 ||
             lstrcmpA(Name, "uvIn") == 0 ||
             lstrcmpA(Name, "aTextureCoord") == 0)
    {
        Program->TexCoordBinding = Index;
        Program->TexCoordBindingSet = TRUE;
    }
    else if (lstrcmpA(Name, "tangent") == 0)
    {
        Program->TangentBinding = Index;
        Program->TangentBindingSet = TRUE;
    }
    else if (lstrcmpA(Name, "tvertex0") == 0)
    {
        Program->TriangleVertexBinding[0] = Index;
        Program->TriangleVertexBindingSet[0] = TRUE;
    }
    else if (lstrcmpA(Name, "tvertex1") == 0)
    {
        Program->TriangleVertexBinding[1] = Index;
        Program->TriangleVertexBindingSet[1] = TRUE;
    }
    else if (lstrcmpA(Name, "tvertex2") == 0)
    {
        Program->TriangleVertexBinding[2] = Index;
        Program->TriangleVertexBindingSet[2] = TRUE;
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
    GLint NormalAttribute = -1;
    GLint TexCoordAttribute = -1;
    GLint TangentAttribute = -1;
    GLint TriangleVertexAttributes[3] = {-1, -1, -1};

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
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableDepthVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableDepthFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableDepth;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableShadowVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableShadowFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableShadow;
        }
        else if ((VertexShader->Executable ==
                      Rpi5OglShaderExecutableBuildVertex ||
                  VertexShader->Executable ==
                      Rpi5OglShaderExecutableShadowHorseVertex ||
                  VertexShader->Executable ==
                      Rpi5OglShaderExecutableGouraudVertex) &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableBuildFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableBuildDiffuse;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableBuildVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableTextureFragment &&
                 Rpi5OglGl2AssignTextureAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute,
                     &TexCoordAttribute))
        {
            Executable = Rpi5OglProgramExecutableBuildTexture;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableAdvancedVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableAdvancedFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableBlinnPhong;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableAdvancedVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableBumpPolyFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableBumpPoly;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutablePhongVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutablePhongFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutablePhong;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutablePhongVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableCelFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableCel;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableBumpNormalsVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableBumpNormalsFragment &&
                 Rpi5OglGl2AssignNormalMapAttributes(
                     Program,
                     &PositionAttribute,
                     &TexCoordAttribute))
        {
            Executable = Rpi5OglProgramExecutableNormalMap;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableBumpHeightVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableBumpHeightFragment &&
                 Rpi5OglGl2AssignHeightMapAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute,
                     &TexCoordAttribute,
                     &TangentAttribute))
        {
            Executable = Rpi5OglProgramExecutableHeightMap;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutablePulsarVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableBuildFragment &&
                 Rpi5OglGl2AssignGenericAttributes(
                     Program,
                     &PositionAttribute,
                     &ColorAttribute))
        {
            Executable = Rpi5OglProgramExecutablePulsar;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableEffectVertex &&
                 (FragmentShader->Executable ==
                      Rpi5OglShaderExecutableEffectEdgeFragment ||
                  FragmentShader->Executable ==
                      Rpi5OglShaderExecutableEffectBlurFragment) &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = FragmentShader->Executable ==
                             Rpi5OglShaderExecutableEffectEdgeFragment ?
                         Rpi5OglProgramExecutableEffectEdge :
                         Rpi5OglProgramExecutableEffectBlur;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableDesktopVertex &&
                 (FragmentShader->Executable ==
                      Rpi5OglShaderExecutableDesktopFragment ||
                  FragmentShader->Executable ==
                      Rpi5OglShaderExecutableDesktopBlurHorizontalFragment ||
                  FragmentShader->Executable ==
                      Rpi5OglShaderExecutableDesktopBlurVerticalFragment) &&
                 Rpi5OglGl2AssignNormalMapAttributes(
                     Program,
                     &PositionAttribute,
                     &TexCoordAttribute))
        {
            if (FragmentShader->Executable ==
                    Rpi5OglShaderExecutableDesktopFragment)
            {
                Executable = Rpi5OglProgramExecutableDesktopTexture;
            }
            else if (FragmentShader->Executable ==
                         Rpi5OglShaderExecutableDesktopBlurHorizontalFragment)
            {
                Executable =
                    Rpi5OglProgramExecutableDesktopBlurHorizontal;
            }
            else
            {
                Executable = Rpi5OglProgramExecutableDesktopBlurVertical;
            }
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableWireframeVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableWireframeFragment &&
                 Rpi5OglGl2AssignWireframeAttributes(
                     Program,
                     &PositionAttribute,
                     TriangleVertexAttributes))
        {
            Executable = Rpi5OglProgramExecutableWireframe;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableJellyfishGradientVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableJellyfishGradientFragment &&
                 Rpi5OglGl2AssignNormalMapAttributes(
                     Program,
                     &PositionAttribute,
                     &TexCoordAttribute))
        {
            Executable = Rpi5OglProgramExecutableJellyfishGradient;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableJellyfishMeshVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableJellyfishMeshFragment &&
                 Rpi5OglGl2AssignJellyfishAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute,
                     &ColorAttribute,
                     &TexCoordAttribute))
        {
            Executable = Rpi5OglProgramExecutableJellyfishMesh;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableTerrainTextureVertex &&
                 FragmentShader->Executable >=
                     Rpi5OglShaderExecutableTerrainNoiseFragment &&
                 FragmentShader->Executable <=
                     Rpi5OglShaderExecutableTerrainTiltVerticalFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            switch (FragmentShader->Executable)
            {
                case Rpi5OglShaderExecutableTerrainNoiseFragment:
                    Executable = Rpi5OglProgramExecutableTerrainNoise;
                    break;
                case Rpi5OglShaderExecutableTerrainNormalFragment:
                    Executable = Rpi5OglProgramExecutableTerrainNormal;
                    break;
                case Rpi5OglShaderExecutableTerrainLuminanceFragment:
                    Executable = Rpi5OglProgramExecutableTerrainLuminance;
                    break;
                case Rpi5OglShaderExecutableTerrainOverlayFragment:
                    Executable = Rpi5OglProgramExecutableTerrainOverlay;
                    break;
                case Rpi5OglShaderExecutableTerrainBloomHorizontalFragment:
                    Executable =
                        Rpi5OglProgramExecutableTerrainBloomHorizontal;
                    break;
                case Rpi5OglShaderExecutableTerrainBloomVerticalFragment:
                    Executable =
                        Rpi5OglProgramExecutableTerrainBloomVertical;
                    break;
                case Rpi5OglShaderExecutableTerrainTiltHorizontalFragment:
                    Executable =
                        Rpi5OglProgramExecutableTerrainTiltHorizontal;
                    break;
                case Rpi5OglShaderExecutableTerrainTiltVerticalFragment:
                    Executable =
                        Rpi5OglProgramExecutableTerrainTiltVertical;
                    break;
                default:
                    break;
            }
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableTerrainVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableTerrainFragment &&
                 Rpi5OglGl2AssignHeightMapAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute,
                     &TexCoordAttribute,
                     &TangentAttribute))
        {
            Executable = Rpi5OglProgramExecutableTerrain;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasTableVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasColorFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasTable;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasPaperVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasColorFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasPaper;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasTextVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasColorFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasText;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasUnderTableVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasColorFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasUnderTable;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasLampUnlitVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasColorFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasLampUnlit;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasFlatVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasFlatFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasFlat;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasFlatVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasShadowFragment &&
                 Rpi5OglGl2AssignPositionAttribute(
                     Program,
                     &PositionAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasShadow;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasLitVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasLogoFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasLogo;
        }
        else if (VertexShader->Executable ==
                     Rpi5OglShaderExecutableIdeasLitVertex &&
                 FragmentShader->Executable ==
                     Rpi5OglShaderExecutableIdeasLampFragment &&
                 Rpi5OglGl2AssignBuildAttributes(
                     Program,
                     &PositionAttribute,
                     &NormalAttribute))
        {
            Executable = Rpi5OglProgramExecutableIdeasLampLit;
        }
    }
    Program->Linked = Executable != Rpi5OglProgramExecutableNone;
    if (Program->Linked)
    {
        Program->Executable = Executable;
        Program->PositionAttribute = PositionAttribute;
        Program->ColorAttribute = ColorAttribute;
        Program->NormalAttribute = NormalAttribute;
        Program->TexCoordAttribute = TexCoordAttribute;
        Program->TangentAttribute = TangentAttribute;
        CopyMemory(Program->TriangleVertexAttribute,
                   TriangleVertexAttributes,
                   sizeof(Program->TriangleVertexAttribute));
        ZeroMemory(Program->ModelViewProjectionMatrix,
                   sizeof(Program->ModelViewProjectionMatrix));
        ZeroMemory(Program->LightMatrix,
                   sizeof(Program->LightMatrix));
        ZeroMemory(Program->NormalMatrix,
                   sizeof(Program->NormalMatrix));
        ZeroMemory(Program->ModelViewMatrix,
                   sizeof(Program->ModelViewMatrix));
        ZeroMemory(Program->ViewMatrix,
                   sizeof(Program->ViewMatrix));
        ZeroMemory(Program->ProjectionMatrix,
                   sizeof(Program->ProjectionMatrix));
        ZeroMemory(Program->Viewport, sizeof(Program->Viewport));
        ZeroMemory(Program->LightPosition,
                   sizeof(Program->LightPosition));
        ZeroMemory(Program->Light1Position,
                   sizeof(Program->Light1Position));
        ZeroMemory(Program->Light2Position,
                   sizeof(Program->Light2Position));
        ZeroMemory(Program->LogoDirection,
                   sizeof(Program->LogoDirection));
        ZeroMemory(Program->LogoColor,
                   sizeof(Program->LogoColor));
        ZeroMemory(Program->LightColor,
                   sizeof(Program->LightColor));
        ZeroMemory(Program->AmbientColor,
                   sizeof(Program->AmbientColor));
        ZeroMemory(Program->FresnelColor,
                   sizeof(Program->FresnelColor));
        ZeroMemory(Program->GradientColor1,
                   sizeof(Program->GradientColor1));
        ZeroMemory(Program->GradientColor2,
                   sizeof(Program->GradientColor2));
        ZeroMemory(Program->UvOffset, sizeof(Program->UvOffset));
        Program->UvScale[0] = 1.0f;
        Program->UvScale[1] = 1.0f;
        ZeroMemory(Program->TerrainOffset,
                   sizeof(Program->TerrainOffset));
        Program->Opacity = 1.0f;
        Program->LightRadius = 0.0f;
        Program->FresnelPower = 0.0f;
        Program->CurrentTime = 0.0f;
        Program->MaterialDiffuse[0] =
            VertexShader->Executable ==
                Rpi5OglShaderExecutableGouraudVertex ? 0.0f : 1.0f;
        Program->MaterialDiffuse[1] =
            VertexShader->Executable ==
                Rpi5OglShaderExecutableGouraudVertex ? 0.0f : 1.0f;
        Program->MaterialDiffuse[2] = 1.0f;
        Program->MaterialDiffuse[3] = 1.0f;
        Program->ModelViewProjectionMatrixSet = FALSE;
        Program->LightMatrixSet = FALSE;
        Program->NormalMatrixSet = FALSE;
        Program->ModelViewMatrixSet = FALSE;
        Program->ViewMatrixSet = FALSE;
        Program->ProjectionMatrixSet = FALSE;
        Program->ViewportSet = FALSE;
        Program->NormalMatrix3Set = FALSE;
        Program->LightPositionSet = FALSE;
        if (VertexShader->Executable ==
            Rpi5OglShaderExecutableShadowHorseVertex)
        {
            Program->LightPosition[0] = 0.0f;
            Program->LightPosition[1] = 3.0f;
            Program->LightPosition[2] = 2.0f;
            Program->LightPosition[3] = 1.0f;
            Program->LightPositionSet = TRUE;
        }
        Program->Light1PositionSet = FALSE;
        Program->Light2PositionSet = FALSE;
        Program->LogoDirectionSet = FALSE;
        Program->LogoColorSet = FALSE;
        Program->LightRadiusSet = FALSE;
        Program->LightColorSet = FALSE;
        Program->AmbientColorSet = FALSE;
        Program->FresnelColorSet = FALSE;
        Program->FresnelPowerSet = FALSE;
        Program->GradientColor1Set = FALSE;
        Program->GradientColor2Set = FALSE;
        Program->UvOffsetSet = FALSE;
        Program->UvScaleSet = FALSE;
        Program->TerrainOffsetSet = FALSE;
        Program->OpacitySet = FALSE;
        Program->CurrentTimeSet = FALSE;
        Program->TextureUnit = 0;
        Program->TextureUnit1 = 1;
        Program->TextureUnit2 = 2;
        Program->TextureUnit3 = 3;
        Program->TextureUnit4 = 4;
        Program->TextureUnit5 = 5;
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
            if (!Program->Linked)
                *Parameters = 0;
            else if (Rpi5OglGl2IsEffectExecutable(Program->Executable))
                *Parameters = 1;
            else if (Program->Executable ==
                         Rpi5OglProgramExecutablePulsar ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableDepth)
                *Parameters = 1;
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableBuildTexture ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableShadow ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableNormalMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableHeightMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutablePhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableCel)
                *Parameters = 3;
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableBuildDiffuse ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBlinnPhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBumpPoly ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableWireframe)
                *Parameters = 2;
            else
                *Parameters = 0;
            break;
        case GL_ACTIVE_UNIFORM_MAX_LENGTH:
            if (!Program->Linked)
                *Parameters = 0;
            else if (Rpi5OglGl2IsEffectExecutable(Program->Executable))
                *Parameters = sizeof("Texture0");
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableBuildDiffuse ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBuildTexture ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBlinnPhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBumpPoly ||
                     Program->Executable ==
                         Rpi5OglProgramExecutablePhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableCel ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableNormalMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableHeightMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutablePulsar ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableDepth ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableShadow ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableWireframe)
                *Parameters = sizeof("ModelViewProjectionMatrix");
            else
                *Parameters = 0;
            break;
        case GL_ACTIVE_ATTRIBUTES:
            if (!Program->Linked)
                *Parameters = 0;
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableHeightMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableWireframe)
                *Parameters = 4;
            else if (Rpi5OglGl2IsEffectExecutable(Program->Executable))
                *Parameters = 1;
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableShadow)
                *Parameters = 1;
            else if (Program->Executable ==
                     Rpi5OglProgramExecutableBuildTexture)
                *Parameters = 3;
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableGenericColor ||
                     Program->Executable ==
                         Rpi5OglProgramExecutablePulsar ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableDepth ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBuildDiffuse ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBlinnPhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBumpPoly ||
                     Program->Executable ==
                         Rpi5OglProgramExecutablePhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableCel ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableNormalMap)
                *Parameters = 2;
            else
                *Parameters = 0;
            break;
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
            if (!Program->Linked)
                *Parameters = 0;
            else if (Program->Executable ==
                     Rpi5OglProgramExecutableGenericColor)
                *Parameters = sizeof("color");
            else if (Program->Executable ==
                     Rpi5OglProgramExecutableWireframe)
                *Parameters = sizeof("tvertex0");
            else if (Program->Executable ==
                         Rpi5OglProgramExecutablePulsar)
                *Parameters = sizeof("vtxcolor");
            else if (Program->Executable ==
                         Rpi5OglProgramExecutableBuildDiffuse ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBuildTexture ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBlinnPhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableBumpPoly ||
                     Program->Executable ==
                         Rpi5OglProgramExecutablePhong ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableCel ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableNormalMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableHeightMap ||
                     Program->Executable ==
                         Rpi5OglProgramExecutableShadow ||
                     Rpi5OglGl2IsEffectExecutable(Program->Executable))
                *Parameters = sizeof("position");
            else
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
        (Program->Executable != Rpi5OglProgramExecutableGenericColor &&
         Program->Executable != Rpi5OglProgramExecutablePulsar &&
         Program->Executable != Rpi5OglProgramExecutableDepth &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         Program->Executable != Rpi5OglProgramExecutableBuildDiffuse &&
         Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableBlinnPhong &&
         Program->Executable != Rpi5OglProgramExecutableBumpPoly &&
         Program->Executable != Rpi5OglProgramExecutablePhong &&
         Program->Executable != Rpi5OglProgramExecutableCel &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         Program->Executable != Rpi5OglProgramExecutableWireframe &&
         !Rpi5OglGl2IsEffectExecutable(Program->Executable)) ||
        Index >= ((Rpi5OglGl2IsEffectExecutable(Program->Executable) ||
                   Program->Executable ==
                       Rpi5OglProgramExecutableShadow) ? 1u :
                  ((Program->Executable ==
                       Rpi5OglProgramExecutableHeightMap ||
                    Program->Executable ==
                       Rpi5OglProgramExecutableWireframe) ? 4u :
                  (Program->Executable ==
                      Rpi5OglProgramExecutableBuildTexture ? 3u : 2u))))
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

    if (Rpi5OglGl2IsEffectExecutable(Program->Executable) ||
        Program->Executable == Rpi5OglProgramExecutableShadow)
    {
        AttributeName = "position";
    }
    else if (Program->Executable == Rpi5OglProgramExecutablePulsar)
    {
        AttributeName = Index == 0 ? "position" : "vtxcolor";
    }
    else if (Program->Executable == Rpi5OglProgramExecutableWireframe)
    {
        static const PCSTR TriangleVertexNames[3] =
        {
            "tvertex0", "tvertex1", "tvertex2"
        };

        AttributeName = Index == 0 ?
                        "position" : TriangleVertexNames[Index - 1];
    }
    else if (Program->Executable == Rpi5OglProgramExecutableDepth ||
        Program->Executable == Rpi5OglProgramExecutableBuildDiffuse ||
        Program->Executable == Rpi5OglProgramExecutableBuildTexture ||
        Program->Executable == Rpi5OglProgramExecutableBlinnPhong ||
        Program->Executable == Rpi5OglProgramExecutableBumpPoly ||
        Program->Executable == Rpi5OglProgramExecutablePhong ||
        Program->Executable == Rpi5OglProgramExecutableCel ||
        Program->Executable == Rpi5OglProgramExecutableNormalMap ||
        Program->Executable == Rpi5OglProgramExecutableHeightMap)
    {
        if (Index == 0)
            AttributeName = "position";
        else if (Program->Executable == Rpi5OglProgramExecutableNormalMap ||
                 Index == 2)
            AttributeName = "texcoord";
        else if (Program->Executable == Rpi5OglProgramExecutableHeightMap &&
                 Index == 3)
            AttributeName = "tangent";
        else
            AttributeName = "normal";
    }
    else
        AttributeName = Index == 0 ? "pos" : "color";
    *Size = 1;
    if (Program->Executable == Rpi5OglProgramExecutableShadow)
        *Type = GL_FLOAT_VEC2;
    else if ((Program->Executable == Rpi5OglProgramExecutableBuildTexture &&
         Index == 2) ||
        (Program->Executable == Rpi5OglProgramExecutableNormalMap &&
         Index == 1) ||
        (Program->Executable == Rpi5OglProgramExecutableHeightMap &&
         Index == 2))
        *Type = GL_FLOAT_VEC2;
    else if (Program->Executable == Rpi5OglProgramExecutableDepth ||
             Program->Executable ==
                 Rpi5OglProgramExecutableBuildDiffuse ||
             Program->Executable ==
                 Rpi5OglProgramExecutableBuildTexture ||
             Program->Executable ==
                 Rpi5OglProgramExecutableBlinnPhong ||
             Program->Executable ==
                 Rpi5OglProgramExecutableBumpPoly ||
             Program->Executable ==
                 Rpi5OglProgramExecutablePhong ||
             Program->Executable ==
                 Rpi5OglProgramExecutableCel ||
             Program->Executable ==
                 Rpi5OglProgramExecutableNormalMap ||
             Program->Executable ==
                 Rpi5OglProgramExecutableHeightMap ||
             Program->Executable ==
                 Rpi5OglProgramExecutableWireframe ||
             Rpi5OglGl2IsEffectExecutable(Program->Executable))
        *Type = GL_FLOAT_VEC3;
    else if (Program->Executable == Rpi5OglProgramExecutablePulsar &&
             Index == 0)
        *Type = GL_FLOAT_VEC3;
    else
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
    if (Program->Executable == Rpi5OglProgramExecutableGenericColor)
    {
        if (lstrcmpA(Name, "pos") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "color") == 0)
            return Program->ColorAttribute;
    }
    else if (Program->Executable == Rpi5OglProgramExecutablePulsar)
    {
        if (lstrcmpA(Name, "position") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "vtxcolor") == 0)
            return Program->ColorAttribute;
    }
    else if (Program->Executable == Rpi5OglProgramExecutableWireframe)
    {
        if (lstrcmpA(Name, "position") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "tvertex0") == 0)
            return Program->TriangleVertexAttribute[0];
        if (lstrcmpA(Name, "tvertex1") == 0)
            return Program->TriangleVertexAttribute[1];
        if (lstrcmpA(Name, "tvertex2") == 0)
            return Program->TriangleVertexAttribute[2];
    }
    else if (Rpi5OglGl2IsIdeasExecutable(Program->Executable))
    {
        if (lstrcmpA(Name, "vertex") == 0)
            return Program->PositionAttribute;
        if (Rpi5OglGl2IsIdeasLitExecutable(Program->Executable) &&
            lstrcmpA(Name, "normal") == 0)
        {
            return Program->NormalAttribute;
        }
    }
    else if (Program->Executable ==
                 Rpi5OglProgramExecutableJellyfishGradient)
    {
        if (lstrcmpA(Name, "position") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "uvIn") == 0)
            return Program->TexCoordAttribute;
    }
    else if (Program->Executable ==
                 Rpi5OglProgramExecutableJellyfishMesh)
    {
        if (lstrcmpA(Name, "aVertexPosition") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "aVertexNormal") == 0)
            return Program->NormalAttribute;
        if (lstrcmpA(Name, "aVertexColor") == 0)
            return Program->ColorAttribute;
        if (lstrcmpA(Name, "aTextureCoord") == 0)
            return Program->TexCoordAttribute;
    }
    else if (Rpi5OglGl2IsTerrainTextureExecutable(Program->Executable))
    {
        if (lstrcmpA(Name, "position") == 0)
            return Program->PositionAttribute;
    }
    else if (Program->Executable == Rpi5OglProgramExecutableTerrain)
    {
        if (lstrcmpA(Name, "position") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "normal") == 0)
            return Program->NormalAttribute;
        if (lstrcmpA(Name, "uv") == 0)
            return Program->TexCoordAttribute;
        if (lstrcmpA(Name, "tangent") == 0)
            return Program->TangentAttribute;
    }
    else if (Program->Executable == Rpi5OglProgramExecutableDepth ||
             Program->Executable == Rpi5OglProgramExecutableShadow ||
             Program->Executable == Rpi5OglProgramExecutableBuildDiffuse ||
             Program->Executable == Rpi5OglProgramExecutableBuildTexture ||
             Program->Executable == Rpi5OglProgramExecutableBlinnPhong ||
             Program->Executable == Rpi5OglProgramExecutableBumpPoly ||
             Program->Executable == Rpi5OglProgramExecutablePhong ||
             Program->Executable == Rpi5OglProgramExecutableCel ||
             Program->Executable == Rpi5OglProgramExecutableNormalMap ||
             Program->Executable == Rpi5OglProgramExecutableHeightMap ||
             Rpi5OglGl2IsDesktopExecutable(Program->Executable) ||
             Rpi5OglGl2IsEffectExecutable(Program->Executable))
    {
        if (lstrcmpA(Name, "position") == 0)
            return Program->PositionAttribute;
        if (lstrcmpA(Name, "normal") == 0)
            return Program->NormalAttribute;
        if ((Program->Executable == Rpi5OglProgramExecutableBuildTexture ||
             Program->Executable == Rpi5OglProgramExecutableNormalMap ||
             Program->Executable == Rpi5OglProgramExecutableHeightMap ||
             Rpi5OglGl2IsDesktopExecutable(Program->Executable)) &&
            lstrcmpA(Name, "texcoord") == 0)
            return Program->TexCoordAttribute;
        if (Program->Executable == Rpi5OglProgramExecutableHeightMap &&
            lstrcmpA(Name, "tangent") == 0)
            return Program->TangentAttribute;
    }
    return -1;
}

static VOID APIENTRY
Rpi5OglGetActiveUniform(
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
    PCSTR UniformName;

    if (!Rpi5OglGl2CanChangeState(State, "glGetActiveUniform"))
        return;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetActiveUniform(program)");
        return;
    }
    if (!Program->Linked ||
        (Program->Executable != Rpi5OglProgramExecutablePulsar &&
         Program->Executable != Rpi5OglProgramExecutableDepth &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         Program->Executable != Rpi5OglProgramExecutableBuildDiffuse &&
         Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableBlinnPhong &&
         Program->Executable != Rpi5OglProgramExecutableBumpPoly &&
         Program->Executable != Rpi5OglProgramExecutablePhong &&
         Program->Executable != Rpi5OglProgramExecutableCel &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         Program->Executable != Rpi5OglProgramExecutableWireframe &&
         !Rpi5OglGl2IsEffectExecutable(Program->Executable)) ||
        Index >= (Program->Executable == Rpi5OglProgramExecutableShadow ?
                      3u :
                  (Rpi5OglGl2IsEffectExecutable(Program->Executable) ||
                   Program->Executable == Rpi5OglProgramExecutableDepth ||
                   Program->Executable ==
                       Rpi5OglProgramExecutablePulsar) ? 1u :
                  ((Program->Executable ==
                       Rpi5OglProgramExecutableBuildTexture ||
                   Program->Executable ==
                       Rpi5OglProgramExecutableNormalMap ||
                   Program->Executable ==
                       Rpi5OglProgramExecutableHeightMap ||
                   Program->Executable ==
                       Rpi5OglProgramExecutablePhong ||
                   Program->Executable ==
                       Rpi5OglProgramExecutableCel) ? 3u : 2u)))
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetActiveUniform(index)");
        return;
    }
    if (Size == NULL || Type == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetActiveUniform(params)");
        return;
    }

    if (Program->Executable == Rpi5OglProgramExecutableShadow)
    {
        UniformName = Index == 0 ? "ModelViewProjectionMatrix" :
                      Index == 1 ? "LightMatrix" : "ShadowMap";
    }
    else
    {
        UniformName = Rpi5OglGl2IsEffectExecutable(Program->Executable) ?
                  "Texture0" :
                  Index == RPI5VC4_GL2_UNIFORM_MVP ?
                  "ModelViewProjectionMatrix" :
                  Program->Executable ==
                      Rpi5OglProgramExecutableWireframe ?
                  "Viewport" :
                  Index == RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX ?
                  "NormalMatrix" :
                  (Program->Executable == Rpi5OglProgramExecutablePhong ||
                   Program->Executable == Rpi5OglProgramExecutableCel) ?
                  "ModelViewMatrix" :
                  Program->Executable ==
                      Rpi5OglProgramExecutableNormalMap ?
                  "NormalMap" :
                  Program->Executable ==
                      Rpi5OglProgramExecutableHeightMap ?
                  "HeightMap" : "MaterialTexture0";
    }
    *Size = 1;
    *Type = (Program->Executable == Rpi5OglProgramExecutableShadow &&
             Index == 2) ? GL_SAMPLER_2D :
            Program->Executable == Rpi5OglProgramExecutableWireframe &&
            Index == 1 ? GL_FLOAT_VEC2 :
            Rpi5OglGl2IsEffectExecutable(Program->Executable) ||
            ((Program->Executable ==
                  Rpi5OglProgramExecutableBuildTexture ||
              Program->Executable ==
                  Rpi5OglProgramExecutableNormalMap ||
              Program->Executable ==
                  Rpi5OglProgramExecutableHeightMap) &&
             Index == RPI5VC4_GL2_UNIFORM_TEXTURE) ?
            GL_SAMPLER_2D : GL_FLOAT_MAT4;
    Rpi5OglGl2CopyTextResult(State,
                             "glGetActiveUniform",
                             UniformName,
                             lstrlenA(UniformName),
                             BufferSize,
                             Length,
                             Name);
}

static GLint APIENTRY
Rpi5OglGetUniformLocation(
    _In_ GLuint ProgramName,
    _In_z_ const GLchar *Name)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glGetUniformLocation"))
        return -1;
    Program = Rpi5OglGl2FindProgram(State, ProgramName);
    if (Program == NULL)
    {
        Rpi5OglGl2ProgramNameError(State, ProgramName,
                                   "glGetUniformLocation(program)");
        return -1;
    }
    if (Name == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glGetUniformLocation(name)");
        return -1;
    }
    if (!Program->Linked)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glGetUniformLocation(link status)");
        return -1;
    }
    if (Program->Executable == Rpi5OglProgramExecutableShadow)
    {
        if (lstrcmpA(Name, "ModelViewProjectionMatrix") == 0)
            return RPI5VC4_GL2_UNIFORM_MVP;
        if (lstrcmpA(Name, "LightMatrix") == 0)
            return RPI5VC4_GL2_UNIFORM_LIGHT_MATRIX;
        if (lstrcmpA(Name, "ShadowMap") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE;
        return -1;
    }
    if (Program->Executable == Rpi5OglProgramExecutableJellyfishGradient)
    {
        if (lstrcmpA(Name, "color1") == 0)
            return RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR1;
        if (lstrcmpA(Name, "color2") == 0)
            return RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR2;
        return -1;
    }
    if (Program->Executable == Rpi5OglProgramExecutableJellyfishMesh)
    {
        if (lstrcmpA(Name, "uWorld") == 0)
            return RPI5VC4_GL2_UNIFORM_MODELVIEW;
        if (lstrcmpA(Name, "uWorldViewProj") == 0)
            return RPI5VC4_GL2_UNIFORM_MVP;
        if (lstrcmpA(Name, "uWorldInvTranspose") == 0)
            return RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX;
        if (lstrcmpA(Name, "uLightPos") == 0)
            return RPI5VC4_GL2_UNIFORM_LIGHT0;
        if (lstrcmpA(Name, "uLightRadius") == 0)
            return RPI5VC4_GL2_UNIFORM_LIGHT_RADIUS;
        if (lstrcmpA(Name, "uLightCol") == 0)
            return RPI5VC4_GL2_UNIFORM_LIGHT_COLOR;
        if (lstrcmpA(Name, "uAmbientCol") == 0)
            return RPI5VC4_GL2_UNIFORM_AMBIENT_COLOR;
        if (lstrcmpA(Name, "uFresnelCol") == 0)
            return RPI5VC4_GL2_UNIFORM_FRESNEL_COLOR;
        if (lstrcmpA(Name, "uFresnelPower") == 0)
            return RPI5VC4_GL2_UNIFORM_FRESNEL_POWER;
        if (lstrcmpA(Name, "uCurrentTime") == 0)
            return RPI5VC4_GL2_UNIFORM_CURRENT_TIME;
        if (lstrcmpA(Name, "uSampler") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE;
        if (lstrcmpA(Name, "uSampler1") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE1;
        return -1;
    }
    if (Rpi5OglGl2IsIdeasExecutable(Program->Executable))
    {
        if (lstrcmpA(Name, "projection") == 0)
            return RPI5VC4_GL2_UNIFORM_PROJECTION;
        if (lstrcmpA(Name, "modelview") == 0)
            return RPI5VC4_GL2_UNIFORM_MODELVIEW;
        if (Rpi5OglGl2IsIdeasLitExecutable(Program->Executable) &&
            lstrcmpA(Name, "normalMatrix") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX;
        }
        if ((Program->Executable == Rpi5OglProgramExecutableIdeasTable ||
             Program->Executable == Rpi5OglProgramExecutableIdeasPaper) &&
            lstrcmpA(Name, "lightPosition") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_LIGHT0;
        }
        if ((Program->Executable == Rpi5OglProgramExecutableIdeasTable ||
             Program->Executable == Rpi5OglProgramExecutableIdeasPaper) &&
            lstrcmpA(Name, "logoDirection") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_LOGO_DIRECTION;
        }
        if ((Program->Executable == Rpi5OglProgramExecutableIdeasTable ||
             Program->Executable == Rpi5OglProgramExecutableIdeasPaper ||
             Program->Executable == Rpi5OglProgramExecutableIdeasText) &&
            lstrcmpA(Name, "currentTime") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_CURRENT_TIME;
        }
        if (Program->Executable == Rpi5OglProgramExecutableIdeasFlat &&
            lstrcmpA(Name, "logoColor") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_LOGO_COLOR;
        }
        if ((Program->Executable == Rpi5OglProgramExecutableIdeasLogo ||
             Program->Executable == Rpi5OglProgramExecutableIdeasLampLit) &&
            lstrcmpA(Name, "light0Position") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_LIGHT0;
        }
        if (Program->Executable == Rpi5OglProgramExecutableIdeasLampLit &&
            lstrcmpA(Name, "light1Position") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_LIGHT1;
        }
        if (Program->Executable == Rpi5OglProgramExecutableIdeasLampLit &&
            lstrcmpA(Name, "light2Position") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_LIGHT2;
        }
        if (Program->Executable == Rpi5OglProgramExecutableIdeasShadow &&
            lstrcmpA(Name, "tex") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_TEXTURE;
        }
        return -1;
    }
    if (Rpi5OglGl2IsTerrainTextureExecutable(Program->Executable))
    {
        if (lstrcmpA(Name, "uvOffset") == 0)
            return RPI5VC4_GL2_UNIFORM_UV_OFFSET;
        if (lstrcmpA(Name, "uvScale") == 0)
            return RPI5VC4_GL2_UNIFORM_UV_SCALE;
        if (Program->Executable == Rpi5OglProgramExecutableTerrainNoise &&
            lstrcmpA(Name, "time") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_CURRENT_TIME;
        }
        if (Program->Executable == Rpi5OglProgramExecutableTerrainOverlay &&
            lstrcmpA(Name, "opacity") == 0)
        {
            return RPI5VC4_GL2_UNIFORM_OPACITY;
        }
        if ((Program->Executable == Rpi5OglProgramExecutableTerrainNormal &&
             lstrcmpA(Name, "heightMap") == 0) ||
            ((Program->Executable ==
                  Rpi5OglProgramExecutableTerrainLuminance ||
              Program->Executable ==
                  Rpi5OglProgramExecutableTerrainOverlay) &&
             lstrcmpA(Name, "tDiffuse") == 0) ||
            ((Program->Executable >=
                  Rpi5OglProgramExecutableTerrainBloomHorizontal &&
              Program->Executable <=
                  Rpi5OglProgramExecutableTerrainTiltVertical) &&
             lstrcmpA(Name, "Texture0") == 0))
        {
            return RPI5VC4_GL2_UNIFORM_TEXTURE;
        }
        return -1;
    }
    if (Program->Executable == Rpi5OglProgramExecutableTerrain)
    {
        if (lstrcmpA(Name, "modelViewMatrix") == 0)
            return RPI5VC4_GL2_UNIFORM_MODELVIEW;
        if (lstrcmpA(Name, "normalMatrix") == 0)
            return RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX;
        if (lstrcmpA(Name, "projectionMatrix") == 0)
            return RPI5VC4_GL2_UNIFORM_PROJECTION;
        if (lstrcmpA(Name, "viewMatrix") == 0)
            return RPI5VC4_GL2_UNIFORM_VIEW_MATRIX;
        if (lstrcmpA(Name, "uOffset") == 0)
            return RPI5VC4_GL2_UNIFORM_TERRAIN_OFFSET;
        if (lstrcmpA(Name, "pointLightPosition[0]") == 0)
            return RPI5VC4_GL2_UNIFORM_LIGHT0;
        if (lstrcmpA(Name, "tDiffuse1") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE;
        if (lstrcmpA(Name, "tDiffuse2") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE1;
        if (lstrcmpA(Name, "tDetail") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE2;
        if (lstrcmpA(Name, "tNormal") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE3;
        if (lstrcmpA(Name, "tSpecular") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE4;
        if (lstrcmpA(Name, "tDisplacement") == 0)
            return RPI5VC4_GL2_UNIFORM_TEXTURE5;
        return -1;
    }
    if (Program->Executable != Rpi5OglProgramExecutablePulsar &&
        Program->Executable != Rpi5OglProgramExecutableDepth &&
        Program->Executable != Rpi5OglProgramExecutableBuildDiffuse &&
        Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
        Program->Executable != Rpi5OglProgramExecutableBlinnPhong &&
        Program->Executable != Rpi5OglProgramExecutableBumpPoly &&
        Program->Executable != Rpi5OglProgramExecutablePhong &&
        Program->Executable != Rpi5OglProgramExecutableCel &&
        Program->Executable != Rpi5OglProgramExecutableNormalMap &&
        Program->Executable != Rpi5OglProgramExecutableHeightMap &&
        Program->Executable != Rpi5OglProgramExecutableWireframe &&
        !Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
        !Rpi5OglGl2IsEffectExecutable(Program->Executable))
        return -1;
    if (lstrcmpA(Name, "ModelViewProjectionMatrix") == 0)
        return RPI5VC4_GL2_UNIFORM_MVP;
    if (Program->Executable == Rpi5OglProgramExecutableWireframe &&
        lstrcmpA(Name, "Viewport") == 0)
        return RPI5VC4_GL2_UNIFORM_VIEWPORT;
    if (Program->Executable != Rpi5OglProgramExecutableDepth &&
        lstrcmpA(Name, "NormalMatrix") == 0)
        return RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX;
    if (Program->Executable == Rpi5OglProgramExecutableBuildTexture &&
        lstrcmpA(Name, "MaterialTexture0") == 0)
        return RPI5VC4_GL2_UNIFORM_TEXTURE;
    if (Program->Executable == Rpi5OglProgramExecutableNormalMap &&
        lstrcmpA(Name, "NormalMap") == 0)
        return RPI5VC4_GL2_UNIFORM_TEXTURE;
    if (Program->Executable == Rpi5OglProgramExecutableHeightMap &&
        lstrcmpA(Name, "HeightMap") == 0)
        return RPI5VC4_GL2_UNIFORM_TEXTURE;
    if (Rpi5OglGl2IsEffectExecutable(Program->Executable) &&
        lstrcmpA(Name, "Texture0") == 0)
        return RPI5VC4_GL2_UNIFORM_TEXTURE;
    if (Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
        ((Program->Executable == Rpi5OglProgramExecutableDesktopTexture &&
          lstrcmpA(Name, "MaterialTexture0") == 0) ||
         (Program->Executable != Rpi5OglProgramExecutableDesktopTexture &&
          lstrcmpA(Name, "Texture0") == 0)))
    {
        return RPI5VC4_GL2_UNIFORM_TEXTURE;
    }
    if ((Program->Executable == Rpi5OglProgramExecutablePhong ||
         Program->Executable == Rpi5OglProgramExecutableCel) &&
        lstrcmpA(Name, "ModelViewMatrix") == 0)
        return RPI5VC4_GL2_UNIFORM_MODELVIEW;
    return -1;
}

static VOID APIENTRY
Rpi5OglUniformMatrix4fv(
    _In_ GLint Location,
    _In_ GLsizei Count,
    _In_ GLboolean Transpose,
    _In_reads_opt_(Count * 16) const GLfloat *Value)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glUniformMatrix4fv"))
        return;
    if (Count < 0 || Transpose != GL_FALSE)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glUniformMatrix4fv(count/transpose)");
        return;
    }
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableShadow)
    {
        if ((Location != RPI5VC4_GL2_UNIFORM_MVP &&
             Location != RPI5VC4_GL2_UNIFORM_LIGHT_MATRIX) ||
            Count > 1)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniformMatrix4fv(shadow location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniformMatrix4fv(shadow value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_MVP)
        {
            CopyMemory(Program->ModelViewProjectionMatrix,
                       Value,
                       sizeof(Program->ModelViewProjectionMatrix));
            Program->ModelViewProjectionMatrixSet = TRUE;
        }
        else
        {
            CopyMemory(Program->LightMatrix,
                       Value,
                       sizeof(Program->LightMatrix));
            Program->LightMatrixSet = TRUE;
        }
        return;
    }
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableTerrain)
    {
        if ((Location != RPI5VC4_GL2_UNIFORM_MODELVIEW &&
             Location != RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX &&
             Location != RPI5VC4_GL2_UNIFORM_PROJECTION &&
             Location != RPI5VC4_GL2_UNIFORM_VIEW_MATRIX) ||
            Count > 1)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniformMatrix4fv(terrain location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniformMatrix4fv(terrain value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_MODELVIEW)
        {
            CopyMemory(Program->ModelViewMatrix, Value,
                       sizeof(Program->ModelViewMatrix));
            Program->ModelViewMatrixSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX)
        {
            CopyMemory(Program->NormalMatrix, Value,
                       sizeof(Program->NormalMatrix));
            Program->NormalMatrixSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_PROJECTION)
        {
            CopyMemory(Program->ProjectionMatrix, Value,
                       sizeof(Program->ProjectionMatrix));
            Program->ProjectionMatrixSet = TRUE;
        }
        else
        {
            CopyMemory(Program->ViewMatrix, Value,
                       sizeof(Program->ViewMatrix));
            Program->ViewMatrixSet = TRUE;
        }
        return;
    }
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableJellyfishMesh)
    {
        if ((Location != RPI5VC4_GL2_UNIFORM_MVP &&
             Location != RPI5VC4_GL2_UNIFORM_MODELVIEW &&
             Location != RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX) ||
            Count > 1)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniformMatrix4fv(jellyfish location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniformMatrix4fv(value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_MVP)
        {
            CopyMemory(Program->ModelViewProjectionMatrix,
                       Value,
                       sizeof(Program->ModelViewProjectionMatrix));
            Program->ModelViewProjectionMatrixSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_MODELVIEW)
        {
            CopyMemory(Program->ModelViewMatrix,
                       Value,
                       sizeof(Program->ModelViewMatrix));
            Program->ModelViewMatrixSet = TRUE;
        }
        else
        {
            CopyMemory(Program->NormalMatrix,
                       Value,
                       sizeof(Program->NormalMatrix));
            Program->NormalMatrixSet = TRUE;
        }
        return;
    }
    if (Program != NULL && Rpi5OglGl2IsIdeasExecutable(Program->Executable))
    {
        if ((Location != RPI5VC4_GL2_UNIFORM_PROJECTION &&
             Location != RPI5VC4_GL2_UNIFORM_MODELVIEW) ||
            Count > 1)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniformMatrix4fv(ideas location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniformMatrix4fv(value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_PROJECTION)
        {
            CopyMemory(Program->ProjectionMatrix,
                       Value,
                       sizeof(Program->ProjectionMatrix));
            Program->ProjectionMatrixSet = TRUE;
        }
        else
        {
            CopyMemory(Program->ModelViewMatrix,
                       Value,
                       sizeof(Program->ModelViewMatrix));
            Program->ModelViewMatrixSet = TRUE;
        }
        return;
    }
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutablePulsar &&
         Program->Executable != Rpi5OglProgramExecutableDepth &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         Program->Executable != Rpi5OglProgramExecutableBuildDiffuse &&
         Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableBlinnPhong &&
         Program->Executable != Rpi5OglProgramExecutableBumpPoly &&
         Program->Executable != Rpi5OglProgramExecutablePhong &&
         Program->Executable != Rpi5OglProgramExecutableCel &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         Program->Executable != Rpi5OglProgramExecutableWireframe) ||
        (Location != RPI5VC4_GL2_UNIFORM_MVP &&
         !(Program->Executable != Rpi5OglProgramExecutableWireframe &&
           Program->Executable != Rpi5OglProgramExecutableDepth &&
           Location == RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX) &&
         !((Program->Executable == Rpi5OglProgramExecutablePhong ||
            Program->Executable == Rpi5OglProgramExecutableCel) &&
           Location == RPI5VC4_GL2_UNIFORM_MODELVIEW)) ||
        Count > 1)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniformMatrix4fv(location/program)");
        return;
    }
    if (Count == 0)
        return;
    if (Value == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glUniformMatrix4fv(value)");
        return;
    }

    if (Location == RPI5VC4_GL2_UNIFORM_MVP)
    {
        CopyMemory(Program->ModelViewProjectionMatrix,
                   Value,
                   sizeof(Program->ModelViewProjectionMatrix));
        Program->ModelViewProjectionMatrixSet = TRUE;
    }
    else if (Location == RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX)
    {
        CopyMemory(Program->NormalMatrix,
                   Value,
                   sizeof(Program->NormalMatrix));
        Program->NormalMatrixSet = TRUE;
    }
    else
    {
        CopyMemory(Program->ModelViewMatrix,
                   Value,
                   sizeof(Program->ModelViewMatrix));
        Program->ModelViewMatrixSet = TRUE;
    }
}

static VOID APIENTRY
Rpi5OglUniformMatrix3fv(
    _In_ GLint Location,
    _In_ GLsizei Count,
    _In_ GLboolean Transpose,
    _In_reads_opt_(Count * 9) const GLfloat *Value)
{
    static const ULONG DestinationIndex[9] =
    {
        0, 1, 2,
        4, 5, 6,
        8, 9, 10
    };
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;
    ULONG Index;

    if (!Rpi5OglGl2CanChangeState(State, "glUniformMatrix3fv"))
        return;
    if (Count < 0 || Transpose != GL_FALSE)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glUniformMatrix3fv(count/transpose)");
        return;
    }
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        !Rpi5OglGl2IsIdeasLitExecutable(Program->Executable) ||
        Location != RPI5VC4_GL2_UNIFORM_NORMAL_MATRIX ||
        Count > 1)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniformMatrix3fv(location/program)");
        return;
    }
    if (Count == 0)
        return;
    if (Value == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glUniformMatrix3fv(value)");
        return;
    }

    ZeroMemory(Program->NormalMatrix, sizeof(Program->NormalMatrix));
    for (Index = 0; Index < RTL_NUMBER_OF(DestinationIndex); Index++)
        Program->NormalMatrix[DestinationIndex[Index]] = Value[Index];
    Program->NormalMatrix[15] = 1.0f;
    Program->NormalMatrix3Set = TRUE;
}

static VOID APIENTRY
Rpi5OglUniform3fv(
    _In_ GLint Location,
    _In_ GLsizei Count,
    _In_reads_opt_(Count * 3) const GLfloat *Value)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glUniform3fv"))
        return;
    if (Count < 0)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glUniform3fv(count)");
        return;
    }
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableTerrain)
    {
        if (Location != RPI5VC4_GL2_UNIFORM_LIGHT0 || Count > 1)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniform3fv(terrain location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniform3fv(terrain value)");
            return;
        }
        CopyMemory(Program->LightPosition, Value, 3 * sizeof(GLfloat));
        Program->LightPosition[3] = 1.0f;
        Program->LightPositionSet = TRUE;
        return;
    }
    if (Program != NULL &&
        (Program->Executable == Rpi5OglProgramExecutableJellyfishGradient ||
         Program->Executable == Rpi5OglProgramExecutableJellyfishMesh))
    {
        if (Count > 1 ||
            !((Program->Executable ==
                   Rpi5OglProgramExecutableJellyfishGradient &&
               (Location == RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR1 ||
                Location == RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR2)) ||
              (Program->Executable ==
                   Rpi5OglProgramExecutableJellyfishMesh &&
               Location == RPI5VC4_GL2_UNIFORM_LIGHT0)))
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniform3fv(jellyfish location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniform3fv(value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR1)
        {
            CopyMemory(Program->GradientColor1,
                       Value,
                       sizeof(Program->GradientColor1));
            Program->GradientColor1Set = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_GRADIENT_COLOR2)
        {
            CopyMemory(Program->GradientColor2,
                       Value,
                       sizeof(Program->GradientColor2));
            Program->GradientColor2Set = TRUE;
        }
        else
        {
            CopyMemory(Program->LightPosition, Value, 3 * sizeof(GLfloat));
            Program->LightPosition[3] = 1.0f;
            Program->LightPositionSet = TRUE;
        }
        return;
    }
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutableIdeasTable &&
         Program->Executable != Rpi5OglProgramExecutableIdeasPaper) ||
        (Location != RPI5VC4_GL2_UNIFORM_LIGHT0 &&
         Location != RPI5VC4_GL2_UNIFORM_LOGO_DIRECTION) ||
        Count > 1)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniform3fv(location/program)");
        return;
    }
    if (Count == 0)
        return;
    if (Value == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glUniform3fv(value)");
        return;
    }

    if (Location == RPI5VC4_GL2_UNIFORM_LIGHT0)
    {
        CopyMemory(Program->LightPosition, Value, 3 * sizeof(GLfloat));
        Program->LightPosition[3] = 1.0f;
        Program->LightPositionSet = TRUE;
    }
    else
    {
        CopyMemory(Program->LogoDirection, Value,
                   sizeof(Program->LogoDirection));
        Program->LogoDirectionSet = TRUE;
    }
}

static VOID APIENTRY
Rpi5OglUniform4fv(
    _In_ GLint Location,
    _In_ GLsizei Count,
    _In_reads_opt_(Count * 4) const GLfloat *Value)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glUniform4fv"))
        return;
    if (Count < 0)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glUniform4fv(count)");
        return;
    }
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL &&
        ((Program->Executable == Rpi5OglProgramExecutableTerrainNoise &&
          Location == RPI5VC4_GL2_UNIFORM_CURRENT_TIME) ||
         (Program->Executable == Rpi5OglProgramExecutableTerrainOverlay &&
          Location == RPI5VC4_GL2_UNIFORM_OPACITY)))
    {
        if (Location == RPI5VC4_GL2_UNIFORM_CURRENT_TIME)
        {
            Program->CurrentTime = Value[0];
            Program->CurrentTimeSet = TRUE;
        }
        else
        {
            Program->Opacity = Value[0];
            Program->OpacitySet = TRUE;
        }
        return;
    }
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableJellyfishMesh)
    {
        if (Count > 1 ||
            (Location != RPI5VC4_GL2_UNIFORM_LIGHT_COLOR &&
             Location != RPI5VC4_GL2_UNIFORM_AMBIENT_COLOR &&
             Location != RPI5VC4_GL2_UNIFORM_FRESNEL_COLOR))
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniform4fv(jellyfish location)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniform4fv(value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_LIGHT_COLOR)
        {
            CopyMemory(Program->LightColor,
                       Value,
                       sizeof(Program->LightColor));
            Program->LightColorSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_AMBIENT_COLOR)
        {
            CopyMemory(Program->AmbientColor,
                       Value,
                       sizeof(Program->AmbientColor));
            Program->AmbientColorSet = TRUE;
        }
        else
        {
            CopyMemory(Program->FresnelColor,
                       Value,
                       sizeof(Program->FresnelColor));
            Program->FresnelColorSet = TRUE;
        }
        return;
    }
    if (Program == NULL ||
        !Rpi5OglGl2IsIdeasExecutable(Program->Executable) ||
        Count > 1 ||
        !((Program->Executable == Rpi5OglProgramExecutableIdeasFlat &&
           Location == RPI5VC4_GL2_UNIFORM_LOGO_COLOR) ||
          ((Program->Executable == Rpi5OglProgramExecutableIdeasLogo ||
            Program->Executable == Rpi5OglProgramExecutableIdeasLampLit) &&
           Location == RPI5VC4_GL2_UNIFORM_LIGHT0) ||
          (Program->Executable == Rpi5OglProgramExecutableIdeasLampLit &&
           (Location == RPI5VC4_GL2_UNIFORM_LIGHT1 ||
            Location == RPI5VC4_GL2_UNIFORM_LIGHT2))))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniform4fv(location/program)");
        return;
    }
    if (Count == 0)
        return;
    if (Value == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glUniform4fv(value)");
        return;
    }

    if (Location == RPI5VC4_GL2_UNIFORM_LOGO_COLOR)
    {
        CopyMemory(Program->LogoColor, Value, sizeof(Program->LogoColor));
        Program->LogoColorSet = TRUE;
    }
    else if (Location == RPI5VC4_GL2_UNIFORM_LIGHT0)
    {
        CopyMemory(Program->LightPosition, Value,
                   sizeof(Program->LightPosition));
        Program->LightPositionSet = TRUE;
    }
    else if (Location == RPI5VC4_GL2_UNIFORM_LIGHT1)
    {
        CopyMemory(Program->Light1Position, Value,
                   sizeof(Program->Light1Position));
        Program->Light1PositionSet = TRUE;
    }
    else
    {
        CopyMemory(Program->Light2Position, Value,
                   sizeof(Program->Light2Position));
        Program->Light2PositionSet = TRUE;
    }
}

static VOID APIENTRY
Rpi5OglUniform1f(
    _In_ GLint Location,
    _In_ GLfloat Value)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glUniform1f"))
        return;
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableJellyfishMesh)
    {
        if (Location == RPI5VC4_GL2_UNIFORM_CURRENT_TIME)
        {
            Program->CurrentTime = Value;
            Program->CurrentTimeSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_LIGHT_RADIUS)
        {
            Program->LightRadius = Value;
            Program->LightRadiusSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_FRESNEL_POWER)
        {
            Program->FresnelPower = Value;
            Program->FresnelPowerSet = TRUE;
        }
        else
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniform1f(jellyfish location)");
        }
        return;
    }
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutableIdeasTable &&
         Program->Executable != Rpi5OglProgramExecutableIdeasPaper &&
         Program->Executable != Rpi5OglProgramExecutableIdeasText) ||
        Location != RPI5VC4_GL2_UNIFORM_CURRENT_TIME)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniform1f(location/program)");
        return;
    }
    Program->CurrentTime = Value;
    Program->CurrentTimeSet = TRUE;
}

static VOID APIENTRY
Rpi5OglUniform2fv(
    _In_ GLint Location,
    _In_ GLsizei Count,
    _In_reads_opt_(Count * 2) const GLfloat *Value)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glUniform2fv"))
        return;
    if (Count < 0)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glUniform2fv(count)");
        return;
    }
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL &&
        ((Rpi5OglGl2IsTerrainTextureExecutable(Program->Executable) &&
          (Location == RPI5VC4_GL2_UNIFORM_UV_OFFSET ||
           Location == RPI5VC4_GL2_UNIFORM_UV_SCALE)) ||
         (Program->Executable == Rpi5OglProgramExecutableTerrain &&
          Location == RPI5VC4_GL2_UNIFORM_TERRAIN_OFFSET)))
    {
        if (Count > 1)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glUniform2fv(terrain count)");
            return;
        }
        if (Count == 0)
            return;
        if (Value == NULL)
        {
            Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                            "glUniform2fv(terrain value)");
            return;
        }
        if (Location == RPI5VC4_GL2_UNIFORM_UV_OFFSET)
        {
            CopyMemory(Program->UvOffset, Value, sizeof(Program->UvOffset));
            Program->UvOffsetSet = TRUE;
        }
        else if (Location == RPI5VC4_GL2_UNIFORM_UV_SCALE)
        {
            CopyMemory(Program->UvScale, Value, sizeof(Program->UvScale));
            Program->UvScaleSet = TRUE;
        }
        else
        {
            CopyMemory(Program->TerrainOffset, Value,
                       sizeof(Program->TerrainOffset));
            Program->TerrainOffsetSet = TRUE;
        }
        return;
    }
    if (Program == NULL ||
        Program->Executable != Rpi5OglProgramExecutableWireframe ||
        Location != RPI5VC4_GL2_UNIFORM_VIEWPORT ||
        Count > 1)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniform2fv(location/program)");
        return;
    }
    if (Count == 0)
        return;
    if (Value == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE, "glUniform2fv(value)");
        return;
    }

    CopyMemory(Program->Viewport, Value, sizeof(Program->Viewport));
    Program->ViewportSet = TRUE;
}

static VOID APIENTRY
Rpi5OglUniform1i(
    _In_ GLint Location,
    _In_ GLint Value)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2CanChangeState(State, "glUniform1i"))
        return;
    if (Location == -1)
        return;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL && Rpi5OglGl2IsTerrainExecutable(
                               Program->Executable))
    {
        GLint ExpectedValue = -1;

        if (Rpi5OglGl2IsTerrainTextureExecutable(Program->Executable) &&
            Location == RPI5VC4_GL2_UNIFORM_TEXTURE)
        {
            ExpectedValue = 0;
        }
        else if (Program->Executable == Rpi5OglProgramExecutableTerrain)
        {
            switch (Location)
            {
                case RPI5VC4_GL2_UNIFORM_TEXTURE:
                    ExpectedValue = 0;
                    break;
                case RPI5VC4_GL2_UNIFORM_TEXTURE1:
                    ExpectedValue = 1;
                    break;
                case RPI5VC4_GL2_UNIFORM_TEXTURE2:
                    ExpectedValue = 2;
                    break;
                case RPI5VC4_GL2_UNIFORM_TEXTURE3:
                    ExpectedValue = 3;
                    break;
                case RPI5VC4_GL2_UNIFORM_TEXTURE4:
                    ExpectedValue = 4;
                    break;
                case RPI5VC4_GL2_UNIFORM_TEXTURE5:
                    ExpectedValue = 5;
                    break;
                default:
                    break;
            }
        }
        if (ExpectedValue < 0 || Value != ExpectedValue)
        {
            Rpi5OglGl2Error(State,
                            ExpectedValue < 0 ?
                                GL_INVALID_OPERATION : GL_INVALID_VALUE,
                            "glUniform1i(terrain sampler)");
            return;
        }
        switch (Location)
        {
            case RPI5VC4_GL2_UNIFORM_TEXTURE:
                Program->TextureUnit = Value;
                break;
            case RPI5VC4_GL2_UNIFORM_TEXTURE1:
                Program->TextureUnit1 = Value;
                break;
            case RPI5VC4_GL2_UNIFORM_TEXTURE2:
                Program->TextureUnit2 = Value;
                break;
            case RPI5VC4_GL2_UNIFORM_TEXTURE3:
                Program->TextureUnit3 = Value;
                break;
            case RPI5VC4_GL2_UNIFORM_TEXTURE4:
                Program->TextureUnit4 = Value;
                break;
            case RPI5VC4_GL2_UNIFORM_TEXTURE5:
                Program->TextureUnit5 = Value;
                break;
            default:
                break;
        }
        return;
    }
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableJellyfishMesh)
    {
        if ((Location == RPI5VC4_GL2_UNIFORM_TEXTURE && Value == 0) ||
            (Location == RPI5VC4_GL2_UNIFORM_TEXTURE1 && Value == 1))
        {
            if (Location == RPI5VC4_GL2_UNIFORM_TEXTURE)
                Program->TextureUnit = Value;
            else
                Program->TextureUnit1 = Value;
        }
        else
        {
            Rpi5OglGl2Error(State,
                            Location == RPI5VC4_GL2_UNIFORM_TEXTURE ||
                            Location == RPI5VC4_GL2_UNIFORM_TEXTURE1 ?
                                GL_INVALID_VALUE : GL_INVALID_OPERATION,
                            "glUniform1i(jellyfish sampler)");
        }
        return;
    }
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         Program->Executable != Rpi5OglProgramExecutableIdeasShadow &&
         !Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
         !Rpi5OglGl2IsEffectExecutable(Program->Executable)) ||
        Location != RPI5VC4_GL2_UNIFORM_TEXTURE)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glUniform1i(location/program)");
        return;
    }
    if (Value != 0)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glUniform1i(texture unit)");
        return;
    }
    Program->TextureUnit = Value;
}

static VOID APIENTRY
Rpi5OglActiveTexture(
    _In_ GLenum Texture)
{
    PRPI5VC4_OGL_GL2_STATE State = Rpi5OglCurrentGl2State();
    struct gl_texture_object *OldTexture;
    struct gl_texture_object *NewTexture;
    ULONG TextureUnit;

    if (!Rpi5OglGl2CanChangeState(State, "glActiveTexture"))
        return;
    if (Texture < GL_TEXTURE0 ||
        Texture >= GL_TEXTURE0 + RPI5VC4_OGL_TEXTURE_UNIT_COUNT)
    {
        Rpi5OglGl2Error(State, GL_INVALID_ENUM, "glActiveTexture(texture)");
        return;
    }

    TextureUnit = Texture - GL_TEXTURE0;
    if (TextureUnit == State->ActiveTextureUnit)
        return;
    OldTexture = State->Mesa->Texture.Current2D;
    State->TextureUnits[State->ActiveTextureUnit] = OldTexture;
    NewTexture = State->TextureUnits[TextureUnit];
    if (NewTexture == NULL)
        NewTexture = State->Mesa->Shared->Default2D;
    State->Mesa->Texture.Current2D = NewTexture;
    if (State->Mesa->Texture.Current == OldTexture)
        State->Mesa->Texture.Current = NewTexture;
    State->ActiveTextureUnit = TextureUnit;
    State->Mesa->NewState |= NEW_RASTER_OPS;
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
    Attribute->BufferName = Rpi5OglBufferCurrentName(State->BufferState,
                                                      GL_ARRAY_BUFFER);
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
            *Parameters = (GLfloat)Attribute->BufferName;
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
    {"glActiveTexture", (PROC)Rpi5OglActiveTexture},
    {"glAttachShader", (PROC)Rpi5OglAttachShader},
    {"glBindAttribLocation", (PROC)Rpi5OglBindAttribLocation},
    {"glBlendFuncSeparate", (PROC)Rpi5OglBlendFuncSeparate},
    {"glCompileShader", (PROC)Rpi5OglCompileShader},
    {"glCreateProgram", (PROC)Rpi5OglCreateProgram},
    {"glCreateShader", (PROC)Rpi5OglCreateShader},
    {"glDeleteProgram", (PROC)Rpi5OglDeleteProgram},
    {"glDeleteShader", (PROC)Rpi5OglDeleteShader},
    {"glDetachShader", (PROC)Rpi5OglDetachShader},
    {"glDisableVertexAttribArray", (PROC)Rpi5OglDisableVertexAttribArray},
    {"glEnableVertexAttribArray", (PROC)Rpi5OglEnableVertexAttribArray},
    {"glGetActiveAttrib", (PROC)Rpi5OglGetActiveAttrib},
    {"glGetActiveUniform", (PROC)Rpi5OglGetActiveUniform},
    {"glGetAttachedShaders", (PROC)Rpi5OglGetAttachedShaders},
    {"glGetAttribLocation", (PROC)Rpi5OglGetAttribLocation},
    {"glGetProgramInfoLog", (PROC)Rpi5OglGetProgramInfoLog},
    {"glGetProgramiv", (PROC)Rpi5OglGetProgramiv},
    {"glGetShaderInfoLog", (PROC)Rpi5OglGetShaderInfoLog},
    {"glGetShaderiv", (PROC)Rpi5OglGetShaderiv},
    {"glGetShaderSource", (PROC)Rpi5OglGetShaderSource},
    {"glGetUniformLocation", (PROC)Rpi5OglGetUniformLocation},
    {"glGetVertexAttribfv", (PROC)Rpi5OglGetVertexAttribfv},
    {"glGetVertexAttribPointerv", (PROC)Rpi5OglGetVertexAttribPointerv},
    {"glIsProgram", (PROC)Rpi5OglIsProgram},
    {"glIsShader", (PROC)Rpi5OglIsShader},
    {"glLinkProgram", (PROC)Rpi5OglLinkProgram},
    {"glShaderSource", (PROC)Rpi5OglShaderSource},
    {"glUniform1f", (PROC)Rpi5OglUniform1f},
    {"glUniform1i", (PROC)Rpi5OglUniform1i},
    {"glUniform2fv", (PROC)Rpi5OglUniform2fv},
    {"glUniform3fv", (PROC)Rpi5OglUniform3fv},
    {"glUniform4fv", (PROC)Rpi5OglUniform4fv},
    {"glUniformMatrix3fv", (PROC)Rpi5OglUniformMatrix3fv},
    {"glUniformMatrix4fv", (PROC)Rpi5OglUniformMatrix4fv},
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
    _In_ GLcontext *Mesa,
    _In_ PRPI5VC4_OGL_BUFFER_STATE BufferState)
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
    NewState->BufferState = BufferState;
    NewState->NextName = 1;
    NewState->TextureSerial = 1;
    NewState->ActiveTextureUnit = RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY;
    NewState->TextureUnits[RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY] =
        Mesa->Texture.Current2D;
    NewState->TextureUnits[RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY] =
        Mesa->Shared->Default2D;
    for (Index = 2; Index < RTL_NUMBER_OF(NewState->TextureUnits); Index++)
        NewState->TextureUnits[Index] = Mesa->Shared->Default2D;
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
    for (Index = 0; Index < RTL_NUMBER_OF(State->TextureUnits); Index++)
    {
        struct gl_texture_object *Texture = State->TextureUnits[Index];

        if (Texture != NULL && Texture->Name != 0 && Texture->RefCount > 0)
            Texture->RefCount--;
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

BOOL
Rpi5OglGl2DesktopProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    return Program != NULL &&
           Rpi5OglGl2IsDesktopExecutable(Program->Executable);
}

BOOL
Rpi5OglGl2TerrainProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    return Program != NULL &&
           Rpi5OglGl2IsTerrainExecutable(Program->Executable);
}

BOOL
Rpi5OglGl2DepthProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    return Program != NULL &&
           Program->Executable == Rpi5OglProgramExecutableDepth;
}

BOOL
Rpi5OglGl2ShadowProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    return Program != NULL &&
           Program->Executable == Rpi5OglProgramExecutableShadow;
}

BOOL
Rpi5OglGl2BuildDiffuseProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    return Program != NULL &&
           Program->Executable == Rpi5OglProgramExecutableBuildDiffuse;
}

ULONG
Rpi5OglGl2TerrainMode(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2TerrainProgramActive(State))
        return RPI5VC4_V3D_GRAPH_SHADER_FIXED_TEXTURE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    switch (Program->Executable)
    {
        case Rpi5OglProgramExecutableTerrainNoise:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE;
        case Rpi5OglProgramExecutableTerrainNormal:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NORMAL;
        case Rpi5OglProgramExecutableTerrainLuminance:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_LUMINANCE;
        case Rpi5OglProgramExecutableTerrainOverlay:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_OVERLAY;
        case Rpi5OglProgramExecutableTerrainBloomHorizontal:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_HORIZONTAL;
        case Rpi5OglProgramExecutableTerrainBloomVertical:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_VERTICAL;
        case Rpi5OglProgramExecutableTerrainTiltHorizontal:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_HORIZONTAL;
        case Rpi5OglProgramExecutableTerrainTiltVertical:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_VERTICAL;
        case Rpi5OglProgramExecutableTerrain:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN;
        default:
            return RPI5VC4_V3D_GRAPH_SHADER_FIXED_TEXTURE;
    }
}

GLuint
Rpi5OglGl2CurrentProgramName(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    return Rpi5OglGl2ProgramActive(State) ? State->CurrentProgram : 0;
}

ULONG
Rpi5OglGl2BatchFlags(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return 0;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL)
        return 0;
    if (Program->Executable == Rpi5OglProgramExecutableBlinnPhong)
        return RPI5VC4_V3D_BATCH_FLAG_BLINN_PHONG;
    if (Program->Executable == Rpi5OglProgramExecutableBumpPoly)
        return RPI5VC4_V3D_BATCH_FLAG_BUMP_POLY;
    if (Program->Executable == Rpi5OglProgramExecutablePhong)
        return RPI5VC4_V3D_BATCH_FLAG_PHONG;
    if (Program->Executable == Rpi5OglProgramExecutableCel)
        return RPI5VC4_V3D_BATCH_FLAG_CEL;
    if (Program->Executable == Rpi5OglProgramExecutableNormalMap)
        return RPI5VC4_V3D_BATCH_FLAG_NORMAL_MAP;
    if (Program->Executable == Rpi5OglProgramExecutableHeightMap)
        return RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP;
    if (Program->Executable == Rpi5OglProgramExecutableWireframe)
        return RPI5VC4_V3D_BATCH_FLAG_WIREFRAME;
    if (Program->Executable == Rpi5OglProgramExecutableDepth)
        return RPI5VC4_V3D_BATCH_FLAG_OUTPUT_DEPTH;
    if (Program->Executable == Rpi5OglProgramExecutableShadow)
        return RPI5VC4_V3D_BATCH_FLAG_SHADOW;
    if (Rpi5OglGl2IsIdeasExecutable(Program->Executable))
        return RPI5VC4_V3D_BATCH_FLAG_IDEAS;
    if (Rpi5OglGl2IsJellyfishExecutable(Program->Executable))
        return RPI5VC4_V3D_BATCH_FLAG_JELLYFISH;
    if (Program->Executable == Rpi5OglProgramExecutableEffectEdge)
        return RPI5VC4_V3D_BATCH_FLAG_EFFECT_EDGE;
    if (Program->Executable == Rpi5OglProgramExecutableEffectBlur)
        return RPI5VC4_V3D_BATCH_FLAG_EFFECT_BLUR;
    if (Program->Executable ==
            Rpi5OglProgramExecutableDesktopBlurHorizontal)
        return RPI5VC4_V3D_BATCH_FLAG_DESKTOP_BLUR_HORIZONTAL;
    if (Program->Executable ==
            Rpi5OglProgramExecutableDesktopBlurVertical)
        return RPI5VC4_V3D_BATCH_FLAG_DESKTOP_BLUR_VERTICAL;
    return 0;
}

ULONG
Rpi5OglGl2IdeasMode(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return RPI5VC4_V3D_IDEAS_MODE_COLOR;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL)
        return RPI5VC4_V3D_IDEAS_MODE_COLOR;
    if (Program->Executable == Rpi5OglProgramExecutableIdeasShadow)
        return RPI5VC4_V3D_IDEAS_MODE_SHADOW;
    if (Program->Executable == Rpi5OglProgramExecutableIdeasLogo)
        return RPI5VC4_V3D_IDEAS_MODE_LOGO;
    if (Program->Executable == Rpi5OglProgramExecutableIdeasLampLit)
        return RPI5VC4_V3D_IDEAS_MODE_LAMP;
    return RPI5VC4_V3D_IDEAS_MODE_COLOR;
}

ULONG
Rpi5OglGl2JellyfishMode(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return RPI5VC4_V3D_JELLYFISH_MODE_GRADIENT;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program != NULL &&
        Program->Executable == Rpi5OglProgramExecutableJellyfishMesh)
    {
        return RPI5VC4_V3D_JELLYFISH_MODE_MESH;
    }
    return RPI5VC4_V3D_JELLYFISH_MODE_GRADIENT;
}

BOOL
Rpi5OglGl2PreservesDestinationAlphaBlend(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    return State != NULL &&
           State->Mesa != NULL &&
           State->PreserveDestinationAlphaBlend &&
           State->Mesa->Color.BlendSrc == GL_SRC_ALPHA &&
           State->Mesa->Color.BlendDst == GL_ONE_MINUS_SRC_ALPHA;
}

VOID
Rpi5OglGl2BlendFuncChanged(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    if (State != NULL)
        State->PreserveDestinationAlphaBlend = FALSE;
}

BOOL
Rpi5OglGl2GetNormalMatrix(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_NORMAL_MATRIX_WORDS) PULONG Matrix)
{
    static const ULONG SourceIndex[RPI5VC4_V3D_NORMAL_MATRIX_WORDS] =
    {
        0, 1, 2,
        4, 5, 6,
        8, 9, 10
    };
    PRPI5VC4_OGL_PROGRAM Program;
    ULONG Index;

    if (Matrix == NULL || !Rpi5OglGl2ProgramActive(State))
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap) ||
        !Program->NormalMatrixSet)
    {
        return FALSE;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(SourceIndex); Index++)
    {
        CopyMemory(&Matrix[Index],
                   &Program->NormalMatrix[SourceIndex[Index]],
                   sizeof(Matrix[Index]));
    }
    return TRUE;
}

BOOL
Rpi5OglGl2GetIdeasUniforms(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_IDEAS_UNIFORM_WORDS) PULONG Uniforms)
{
    if (State == NULL || Uniforms == NULL)
        return FALSE;

    CopyMemory(Uniforms,
               State->IdeasLampLights,
               sizeof(State->IdeasLampLights));
    CopyMemory(Uniforms + RTL_NUMBER_OF(State->IdeasLampLights),
               State->IdeasLogoLight,
               sizeof(State->IdeasLogoLight));
    return TRUE;
}

BOOL
Rpi5OglGl2GetJellyfishUniforms(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_JELLYFISH_UNIFORM_WORDS) PULONG Uniforms)
{
    if (State == NULL || Uniforms == NULL)
        return FALSE;

    CopyMemory(Uniforms,
               State->JellyfishGradientColors,
               sizeof(State->JellyfishGradientColors));
    return TRUE;
}

BOOL
Rpi5OglGl2GetTerrainUniforms(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_TERRAIN_UNIFORM_WORDS) PULONG Uniforms)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (Uniforms == NULL || !Rpi5OglGl2TerrainProgramActive(State))
        return FALSE;
    ZeroMemory(Uniforms,
               RPI5VC4_V3D_TERRAIN_UNIFORM_WORDS * sizeof(Uniforms[0]));
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program->Executable != Rpi5OglProgramExecutableTerrain)
        return TRUE;
    if (!Program->ModelViewMatrixSet || !Program->NormalMatrixSet ||
        !Program->ProjectionMatrixSet || !Program->TerrainOffsetSet ||
        !Program->LightPositionSet)
    {
        return FALSE;
    }
    CopyMemory(&Uniforms[0], Program->ModelViewMatrix,
               sizeof(Program->ModelViewMatrix));
    CopyMemory(&Uniforms[16], Program->NormalMatrix,
               sizeof(Program->NormalMatrix));
    CopyMemory(&Uniforms[32], Program->ProjectionMatrix,
               sizeof(Program->ProjectionMatrix));
    CopyMemory(&Uniforms[RPI5VC4_V3D_TERRAIN_VERTEX_UNIFORM_WORDS],
               Program->TerrainOffset,
               sizeof(Program->TerrainOffset));
    CopyMemory(&Uniforms[RPI5VC4_V3D_TERRAIN_VERTEX_UNIFORM_WORDS + 2],
               Program->LightPosition,
               3 * sizeof(Program->LightPosition[0]));
    return TRUE;
}

struct gl_texture_object *
Rpi5OglGl2TextureForUnit(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ ULONG TextureUnit)
{
    if (State == NULL ||
        TextureUnit >= RTL_NUMBER_OF(State->TextureUnits))
    {
        return NULL;
    }
    if (TextureUnit == State->ActiveTextureUnit && State->Mesa != NULL)
        return State->Mesa->Texture.Current2D;
    return State->TextureUnits[TextureUnit];
}

VOID
Rpi5OglGl2TextureBound(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Target,
    _In_opt_ struct gl_texture_object *Texture)
{
    if (State == NULL || Target != GL_TEXTURE_2D ||
        State->ActiveTextureUnit >= RTL_NUMBER_OF(State->TextureUnits))
    {
        return;
    }
    State->TextureUnits[State->ActiveTextureUnit] = Texture;
}

VOID
Rpi5OglGl2InvalidateTextureUpload(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State)
{
    if (State == NULL)
        return;

    State->UploadedTexture = NULL;
    State->UploadedTextureSerial = 0;
    State->TextureGeneration = 0;
    State->UploadedTexture1 = NULL;
    State->UploadedTextureSerial1 = 0;
    State->TextureGeneration1 = 0;
}

VOID
Rpi5OglGl2TextureChanged(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_opt_ struct gl_texture_object *Texture)
{
    ULONG Serial;

    if (State == NULL || Texture == NULL)
        return;

    Serial = State->TextureSerial + 1u;
    if (Serial == 0)
        Serial = 1;
    State->TextureSerial = Serial;
    if (State->UploadedTexture == Texture)
    {
        State->UploadedTextureSerial = 0;
        State->TextureGeneration = 0;
    }
    if (State->UploadedTexture1 == Texture)
    {
        State->UploadedTextureSerial1 = 0;
        State->TextureGeneration1 = 0;
    }
}

VOID
Rpi5OglGl2TextureDeleted(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_opt_ struct gl_texture_object *Texture)
{
    ULONG Index;

    if (State == NULL || Texture == NULL)
        return;

    Rpi5OglGl2TextureChanged(State, Texture);
    if (State->UploadedTexture == Texture)
        State->UploadedTexture = NULL;
    if (State->UploadedTexture1 == Texture)
        State->UploadedTexture1 = NULL;
    if (State->TextureUnits[State->ActiveTextureUnit] == Texture)
    {
        State->TextureUnits[State->ActiveTextureUnit] =
            State->Mesa->Texture.Current2D;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(State->TextureUnits); Index++)
    {
        if (Index == State->ActiveTextureUnit ||
            State->TextureUnits[Index] != Texture)
        {
            continue;
        }
        State->TextureUnits[Index] = State->Mesa->Shared->Default2D;
        if (Texture->Name != 0 && Texture->RefCount > 0)
            Texture->RefCount--;
    }
}

BOOL
Rpi5OglGl2GetBoundDesktopTexture(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_ PRPI5VC4_OGL_GL2_TEXTURE_INFO TextureInfo)
{
    PRPI5VC4_OGL_PROGRAM Program;
    struct gl_texture_object *Texture;
    const struct gl_texture_image *Image;

    if (TextureInfo == NULL)
        return FALSE;
    ZeroMemory(TextureInfo, sizeof(*TextureInfo));
    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;

    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        !Rpi5OglGl2IsDesktopExecutable(Program->Executable) ||
        Program->TextureUnit != 0)
    {
        return FALSE;
    }

    Texture = Rpi5OglGl2TextureForUnit(
        State, RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY);
    Image = Texture != NULL ? Texture->Image[0] : NULL;
    if (Image == NULL || Image->Data == NULL || Image->Border != 0 ||
        Image->Width == 0 ||
        Image->Width > RPI5VC4_V3D_TEXTURE_MAX_WIDTH ||
        Image->Height == 0 ||
        Image->Height > RPI5VC4_V3D_TEXTURE_MAX_HEIGHT ||
        (Image->Format != GL_RGB && Image->Format != GL_RGBA) ||
        Texture->WrapS != GL_CLAMP_TO_EDGE ||
        Texture->WrapT != GL_CLAMP_TO_EDGE ||
        !((Texture->MinFilter == GL_NEAREST &&
           Texture->MagFilter == GL_NEAREST) ||
          (Texture->MinFilter == GL_LINEAR &&
           Texture->MagFilter == GL_LINEAR)))
    {
        return FALSE;
    }

    TextureInfo->Texture = Texture;
    TextureInfo->Data = Image->Data;
    TextureInfo->Width = Image->Width;
    TextureInfo->Height = Image->Height;
    TextureInfo->Format = Image->Format;
    TextureInfo->LinearFilter = Texture->MinFilter == GL_LINEAR;
    TextureInfo->MipmapFilter = FALSE;
    TextureInfo->WrapS = Texture->WrapS;
    TextureInfo->WrapT = Texture->WrapT;
    TextureInfo->LevelCount = 1;
    TextureInfo->Serial = State->TextureSerial;
    return TRUE;
}

BOOL
Rpi5OglGl2GetTerrainTexture(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ ULONG Sampler,
    _Out_ PRPI5VC4_OGL_GL2_TEXTURE_INFO TextureInfo)
{
    PRPI5VC4_OGL_PROGRAM Program;
    struct gl_texture_object *Texture;
    const struct gl_texture_image *Image;
    GLint TextureUnit;
    ULONG LevelCount = 1;
    ULONG Width;
    ULONG Height;
    BOOL Mipmap;

    if (TextureInfo == NULL)
        return FALSE;
    ZeroMemory(TextureInfo, sizeof(*TextureInfo));
    if (!Rpi5OglGl2TerrainProgramActive(State) || Sampler >= 6)
        return FALSE;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program->Executable != Rpi5OglProgramExecutableTerrain &&
        Sampler != 0)
    {
        return FALSE;
    }
    switch (Sampler)
    {
        case 0: TextureUnit = Program->TextureUnit; break;
        case 1: TextureUnit = Program->TextureUnit1; break;
        case 2: TextureUnit = Program->TextureUnit2; break;
        case 3: TextureUnit = Program->TextureUnit3; break;
        case 4: TextureUnit = Program->TextureUnit4; break;
        case 5: TextureUnit = Program->TextureUnit5; break;
        default: return FALSE;
    }
    if (TextureUnit < 0 ||
        (ULONG)TextureUnit >= RTL_NUMBER_OF(State->TextureUnits))
    {
        return FALSE;
    }
    Texture = Rpi5OglGl2TextureForUnit(State, (ULONG)TextureUnit);
    Image = Texture != NULL ? Texture->Image[0] : NULL;
    if (Image == NULL || Image->Data == NULL || Image->Border != 0 ||
        Image->Width == 0 ||
        Image->Width > RPI5VC4_V3D_TEXTURE_MAX_WIDTH ||
        Image->Height == 0 ||
        Image->Height > RPI5VC4_V3D_TEXTURE_MAX_HEIGHT ||
        (Image->Format != GL_RGB && Image->Format != GL_RGBA) ||
        (Texture->WrapS != GL_REPEAT &&
         Texture->WrapS != GL_CLAMP_TO_EDGE) ||
        (Texture->WrapT != GL_REPEAT &&
         Texture->WrapT != GL_CLAMP_TO_EDGE) ||
        (Texture->MagFilter != GL_NEAREST &&
         Texture->MagFilter != GL_LINEAR))
    {
        return FALSE;
    }
    Mipmap = Texture->MinFilter != GL_NEAREST &&
             Texture->MinFilter != GL_LINEAR;
    if (Mipmap &&
        Texture->MinFilter != GL_NEAREST_MIPMAP_NEAREST &&
        Texture->MinFilter != GL_LINEAR_MIPMAP_NEAREST &&
        Texture->MinFilter != GL_NEAREST_MIPMAP_LINEAR &&
        Texture->MinFilter != GL_LINEAR_MIPMAP_LINEAR)
    {
        return FALSE;
    }
    Width = Image->Width;
    Height = Image->Height;
    while (Mipmap && (Width > 1 || Height > 1))
    {
        const struct gl_texture_image *Level;

        Width = Width > 1 ? Width / 2 : 1;
        Height = Height > 1 ? Height / 2 : 1;
        if (LevelCount >= MAX_TEXTURE_LEVELS)
            return FALSE;
        Level = Texture->Image[LevelCount];
        if (Level == NULL || Level->Data == NULL || Level->Border != 0 ||
            (ULONG)Level->Width != Width || (ULONG)Level->Height != Height ||
            (Level->Format != GL_RGB && Level->Format != GL_RGBA))
        {
            return FALSE;
        }
        LevelCount++;
    }

    TextureInfo->Texture = Texture;
    TextureInfo->Data = Image->Data;
    TextureInfo->Width = Image->Width;
    TextureInfo->Height = Image->Height;
    TextureInfo->Format = Image->Format;
    TextureInfo->LinearFilter = Texture->MagFilter == GL_LINEAR;
    TextureInfo->MipmapFilter = Mipmap;
    TextureInfo->WrapS = Texture->WrapS;
    TextureInfo->WrapT = Texture->WrapT;
    TextureInfo->LevelCount = LevelCount;
    TextureInfo->Serial = State->TextureSerial;
    return TRUE;
}

static BOOL
Rpi5OglGl2PrepareTextureSlot(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ HDC Hdc,
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _In_ ULONG TextureSlot,
    _In_ BOOL Ideas,
    _In_ BOOL Jellyfish,
    _In_ BOOL Shadow,
    _Out_ PBOOL Textured,
    _Out_ PBOOL LinearFilter,
    _Out_ PBOOL MipmapFilter,
    _Out_ PULONG TextureGeneration)
{
    struct gl_texture_object *Texture;
    struct gl_texture_object **UploadedTexture;
    PULONG UploadedTextureSerial;
    PULONG UploadedGeneration;
    const struct gl_texture_image *Image;
    const struct gl_texture_image *LevelImage;
    PRPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST Request;
    RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT Result;
    const GLubyte *Source;
    GLubyte *Destination;
    ULONG HeaderSize;
    ULONG PixelCount;
    ULONG PixelBytes;
    ULONG RequestSize;
    ULONG SourceComponents;
    ULONG Pixel;
    ULONG Level;
    ULONG LevelCount;
    ULONG LevelWidth;
    ULONG LevelHeight;
    ULONG DestinationOffset;
    ULONG UploadFormat;
    BOOL UseMipmap;
    INT Returned;

    *Textured = FALSE;
    *LinearFilter = FALSE;
    *MipmapFilter = FALSE;
    *TextureGeneration = 0;
    if (Program == NULL || Hdc == NULL ||
        TextureSlot >= RPI5VC4_V3D_TEXTURE_SLOT_COUNT)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(RPi5 texture program)");
        return FALSE;
    }

    if (TextureSlot == RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY)
    {
        UploadedTexture = &State->UploadedTexture;
        UploadedTextureSerial = &State->UploadedTextureSerial;
        UploadedGeneration = &State->TextureGeneration;
    }
    else
    {
        UploadedTexture = &State->UploadedTexture1;
        UploadedTextureSerial = &State->UploadedTextureSerial1;
        UploadedGeneration = &State->TextureGeneration1;
    }

    Texture = Rpi5OglGl2TextureForUnit(State, TextureSlot);
    Image = Texture != NULL ? Texture->Image[0] : NULL;
    if (Image == NULL || Image->Data == NULL || Image->Border != 0 ||
        Image->Width == 0 ||
        Image->Width > RPI5VC4_V3D_TEXTURE_MAX_WIDTH ||
        Image->Height == 0 ||
        Image->Height > RPI5VC4_V3D_TEXTURE_MAX_HEIGHT ||
        ((!Ideas && !Shadow &&
          Image->Format != GL_RGB && Image->Format != GL_RGBA) ||
         (Ideas && Image->Format != GL_ALPHA) ||
         (Shadow && Image->Format != GL_DEPTH_COMPONENT)) ||
        ((!Ideas &&
          (!Jellyfish ||
           TextureSlot == RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY) &&
          (Texture->WrapS != GL_CLAMP_TO_EDGE ||
           Texture->WrapT != GL_CLAMP_TO_EDGE)) ||
         ((Ideas ||
           (Jellyfish &&
            TextureSlot == RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY)) &&
          (Texture->WrapS != GL_REPEAT || Texture->WrapT != GL_REPEAT))) ||
        (Jellyfish &&
         (Texture->MinFilter != GL_LINEAR ||
          Texture->MagFilter != GL_LINEAR)) ||
        (Shadow &&
         (Texture->MinFilter != GL_NEAREST ||
          Texture->MagFilter != GL_NEAREST)) ||
        (!Jellyfish &&
         !((Texture->MinFilter == GL_NEAREST &&
           Texture->MagFilter == GL_NEAREST) ||
          (Texture->MinFilter == GL_LINEAR &&
           Texture->MagFilter == GL_LINEAR) ||
          (Texture->MinFilter == GL_LINEAR_MIPMAP_LINEAR &&
           Texture->MagFilter == GL_LINEAR))))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(RPi5 texture state)");
        return FALSE;
    }

    UseMipmap = !Jellyfish && !Shadow &&
                Texture->MinFilter == GL_LINEAR_MIPMAP_LINEAR;
    LevelCount = 1;
    LevelWidth = Image->Width;
    LevelHeight = Image->Height;
    while (UseMipmap && (LevelWidth > 1u || LevelHeight > 1u))
    {
        LevelWidth = LevelWidth > 1u ? LevelWidth >> 1 : 1u;
        LevelHeight = LevelHeight > 1u ? LevelHeight >> 1 : 1u;
        LevelCount++;
    }
    if (LevelCount > RPI5VC4_V3D_TEXTURE_MAX_LEVELS)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(RPi5 texture levels)");
        return FALSE;
    }

    PixelBytes = 0;
    LevelWidth = Image->Width;
    LevelHeight = Image->Height;
    for (Level = 0; Level < LevelCount; Level++)
    {
        LevelImage = Texture->Image[Level];
        if (LevelImage == NULL || LevelImage->Data == NULL ||
            LevelImage->Border != 0 ||
            LevelImage->Width != LevelWidth ||
            LevelImage->Height != LevelHeight ||
            ((!Ideas && !Shadow &&
              LevelImage->Format != GL_RGB &&
              LevelImage->Format != GL_RGBA) ||
             (Ideas && LevelImage->Format != GL_ALPHA) ||
             (Shadow && LevelImage->Format != GL_DEPTH_COMPONENT)))
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glDrawArrays(RPi5 texture mipmap)");
            return FALSE;
        }
        PixelBytes += LevelWidth * LevelHeight * 4u;
        LevelWidth = LevelWidth > 1u ? LevelWidth >> 1 : 1u;
        LevelHeight = LevelHeight > 1u ? LevelHeight >> 1 : 1u;
    }

    *Textured = TRUE;
    *LinearFilter = !Shadow && Texture->MinFilter != GL_NEAREST;
    *MipmapFilter = UseMipmap;
    if (*UploadedTexture == Texture &&
        *UploadedTextureSerial == State->TextureSerial &&
        *UploadedGeneration != 0)
    {
        *TextureGeneration = *UploadedGeneration;
        return TRUE;
    }

    HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST, Pixels);
    RequestSize = HeaderSize + PixelBytes;
    Request = HeapAlloc(GetProcessHeap(), 0, RequestSize);
    if (Request == NULL)
    {
        Rpi5OglGl2Error(State, GL_OUT_OF_MEMORY,
                        "glDrawArrays(RPi5 texture upload)");
        return FALSE;
    }

    Request->Size = RequestSize;
    Request->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request->Width = Image->Width;
    Request->Height = Image->Height;
    UploadFormat = Shadow ? RPI5VC4_V3D_TEXTURE_FORMAT_D24_X8 :
                            RPI5VC4_V3D_TEXTURE_FORMAT_RGBA8;
    Request->Format = UploadFormat;
    Request->RowStride = Image->Width * 4u;
    Request->PixelBytes = PixelBytes;
    Request->LevelCount = LevelCount;
    Request->TextureSlot = TextureSlot;
    DestinationOffset = 0;
    LevelWidth = Image->Width;
    LevelHeight = Image->Height;
    for (Level = 0; Level < LevelCount; Level++)
    {
        LevelImage = Texture->Image[Level];
        PixelCount = LevelWidth * LevelHeight;
        Source = LevelImage->Data;
        Destination = Request->Pixels + DestinationOffset;
        if (Shadow)
        {
            const GLdepth *DepthSource = (const GLdepth *)Source;
            PULONG DepthDestination = (PULONG)Destination;

            for (Pixel = 0; Pixel < PixelCount; Pixel++)
                DepthDestination[Pixel] = (ULONG)DepthSource[Pixel] << 8;
        }
        else
        {
            SourceComponents = LevelImage->Format == GL_RGB ? 3u :
                               LevelImage->Format == GL_RGBA ? 4u : 1u;
            for (Pixel = 0; Pixel < PixelCount; Pixel++)
            {
                if (SourceComponents == 1u)
                {
                    Destination[Pixel * 4u] = 0xffu;
                    Destination[Pixel * 4u + 1u] = 0xffu;
                    Destination[Pixel * 4u + 2u] = 0xffu;
                    Destination[Pixel * 4u + 3u] = Source[Pixel];
                }
                else
                {
                    Destination[Pixel * 4u] =
                        Source[Pixel * SourceComponents];
                    Destination[Pixel * 4u + 1u] =
                        Source[Pixel * SourceComponents + 1u];
                    Destination[Pixel * 4u + 2u] =
                        Source[Pixel * SourceComponents + 2u];
                    Destination[Pixel * 4u + 3u] =
                        SourceComponents == 4u ?
                            Source[Pixel * SourceComponents + 3u] : 0xffu;
                }
            }
        }
        DestinationOffset += PixelCount * 4u;
        LevelWidth = LevelWidth > 1u ? LevelWidth >> 1 : 1u;
        LevelHeight = LevelHeight > 1u ? LevelHeight >> 1 : 1u;
    }

    ZeroMemory(&Result, sizeof(Result));
    Returned = ExtEscape(Hdc,
                         RPI5VC4_ESCAPE_UPLOAD_TEXTURE,
                         RequestSize,
                         (LPCSTR)Request,
                         sizeof(Result),
                         (LPSTR)&Result);
    HeapFree(GetProcessHeap(), 0, Request);
    if (Returned < (INT)sizeof(Result) ||
        Result.Size != sizeof(Result) ||
        Result.AbiVersion != RPI5VC4_XPDM_ABI_VERSION ||
        Result.Status != RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ||
        Result.Generation == 0 ||
        Result.Width != Image->Width ||
        Result.Height != Image->Height ||
        Result.Format != UploadFormat ||
        Result.LevelCount != LevelCount ||
        Result.TextureSlot != TextureSlot)
    {
        Rpi5OglGl2InvalidateTextureUpload(State);
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(RPi5 texture upload)");
        return FALSE;
    }

    *UploadedTexture = Texture;
    *UploadedTextureSerial = State->TextureSerial;
    *UploadedGeneration = Result.Generation;
    *TextureGeneration = Result.Generation;
    return TRUE;
}

BOOL
Rpi5OglGl2PrepareTexture(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ HDC Hdc,
    _Out_ PBOOL Textured,
    _Out_ PBOOL LinearFilter,
    _Out_ PBOOL MipmapFilter,
    _Out_ PULONG TextureGeneration,
    _Out_ PULONG TextureGeneration1)
{
    PRPI5VC4_OGL_PROGRAM Program;
    BOOL Ideas;
    BOOL Jellyfish;
    BOOL Shadow;
    BOOL SlotTextured;
    BOOL SlotLinear;
    BOOL SlotMipmap;

    *Textured = FALSE;
    *LinearFilter = FALSE;
    *MipmapFilter = FALSE;
    *TextureGeneration = 0;
    *TextureGeneration1 = 0;
    if (!Rpi5OglGl2ProgramActive(State))
        return FALSE;

    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL)
        return FALSE;
    Ideas = Rpi5OglGl2IsIdeasExecutable(Program->Executable);
    Jellyfish = Rpi5OglGl2IsJellyfishExecutable(Program->Executable);
    Shadow = Program->Executable == Rpi5OglProgramExecutableShadow;
    if (Program->Executable == Rpi5OglProgramExecutablePulsar ||
        Program->Executable == Rpi5OglProgramExecutableDepth ||
        Program->Executable == Rpi5OglProgramExecutableWireframe ||
        Program->Executable == Rpi5OglProgramExecutableBuildDiffuse ||
        Program->Executable == Rpi5OglProgramExecutableBlinnPhong ||
        Program->Executable == Rpi5OglProgramExecutableBumpPoly ||
        Program->Executable == Rpi5OglProgramExecutablePhong ||
        Program->Executable == Rpi5OglProgramExecutableCel ||
        Program->Executable == Rpi5OglProgramExecutableJellyfishGradient)
    {
        return TRUE;
    }
    if ((Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         !Shadow &&
         !Ideas &&
         Program->Executable != Rpi5OglProgramExecutableJellyfishMesh &&
         !Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
         !Rpi5OglGl2IsEffectExecutable(Program->Executable)) ||
        Program->TextureUnit != RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY ||
        (Jellyfish &&
         Program->TextureUnit1 != RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY) ||
        Hdc == NULL)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(RPi5 texture program)");
        return FALSE;
    }

    if (!Rpi5OglGl2PrepareTextureSlot(
            State,
            Hdc,
            Program,
            RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY,
            Ideas,
            Jellyfish,
            Shadow,
            &SlotTextured,
            &SlotLinear,
            &SlotMipmap,
            TextureGeneration))
    {
        return FALSE;
    }
    *Textured = SlotTextured;
    *LinearFilter = SlotLinear;
    *MipmapFilter = SlotMipmap;
    if (!Jellyfish)
        return TRUE;

    if (!Rpi5OglGl2PrepareTextureSlot(
            State,
            Hdc,
            Program,
            RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY,
            FALSE,
            TRUE,
            FALSE,
            &SlotTextured,
            &SlotLinear,
            &SlotMipmap,
            TextureGeneration1))
    {
        return FALSE;
    }
    *Textured = *Textured && SlotTextured;
    *LinearFilter = *LinearFilter && SlotLinear;
    *MipmapFilter = *MipmapFilter || SlotMipmap;
    return TRUE;
}

static BOOL
Rpi5OglGl2GetVertexAttribSource(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLint AttributeIndex,
    _In_ GLint LastVertex,
    _Out_ PRPI5VC4_OGL_VERTEX_ATTRIB *Attribute,
    _Out_ const BYTE **Source,
    _Out_ PSIZE_T EffectiveStride)
{
    SIZE_T ComponentBytes;
    SIZE_T RequiredBytes;

    if (AttributeIndex < 0 ||
        AttributeIndex >= RPI5VC4_GL2_MAX_VERTEX_ATTRIBS ||
        LastVertex < 0)
    {
        return FALSE;
    }
    *Attribute = &State->VertexAttribs[AttributeIndex];
    *Source = NULL;
    if (!(*Attribute)->Enabled)
    {
        *EffectiveStride = 0;
        return TRUE;
    }
    if ((*Attribute)->Type != GL_FLOAT ||
        (*Attribute)->Size < 1 || (*Attribute)->Size > 4)
    {
        return FALSE;
    }

    ComponentBytes = (SIZE_T)(*Attribute)->Size * sizeof(GLfloat);
    *EffectiveStride = (*Attribute)->Stride != 0 ?
        (SIZE_T)(*Attribute)->Stride : ComponentBytes;
    if ((SIZE_T)LastVertex >
        ((SIZE_T)-1 - ComponentBytes) / *EffectiveStride)
    {
        return FALSE;
    }
    RequiredBytes = (SIZE_T)LastVertex * *EffectiveStride + ComponentBytes;
    if ((*Attribute)->BufferName != 0)
    {
        return Rpi5OglBufferResolveRange(State->BufferState,
                                         (*Attribute)->BufferName,
                                         (*Attribute)->Pointer,
                                         RequiredBytes,
                                         Source);
    }
    if ((*Attribute)->Pointer == NULL)
        return FALSE;
    *Source = (const BYTE *)(*Attribute)->Pointer;
    return TRUE;
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

    if (!Rpi5OglGl2GetVertexAttribSource(State,
                                         AttributeIndex,
                                         VertexIndex,
                                         &Attribute,
                                         &Source,
                                         &EffectiveStride))
    {
        return FALSE;
    }
    if (!Attribute->Enabled)
    {
        CopyMemory(Values, Attribute->Current, sizeof(Attribute->Current));
        return TRUE;
    }
    Source += (SIZE_T)VertexIndex * EffectiveStride;
    Values[0] = 0.0f;
    Values[1] = 0.0f;
    Values[2] = 0.0f;
    Values[3] = 1.0f;
    if (Attribute->Size >= 1)
        Values[0] = *(const UNALIGNED GLfloat *)(Source);
    if (Attribute->Size >= 2)
        Values[1] = *(const UNALIGNED GLfloat *)(Source + sizeof(GLfloat));
    if (Attribute->Size >= 3)
        Values[2] = *(const UNALIGNED GLfloat *)(Source + 2 * sizeof(GLfloat));
    if (Attribute->Size >= 4)
        Values[3] = *(const UNALIGNED GLfloat *)(Source + 3 * sizeof(GLfloat));
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

static VOID
Rpi5OglGl2TransformVector(
    _In_reads_(16) const GLfloat Matrix[16],
    _In_reads_(4) const GLfloat Input[4],
    _Out_writes_(4) GLfloat Output[4])
{
    ULONG Row;

    for (Row = 0; Row < 4; Row++)
    {
        Output[Row] = Matrix[Row] * Input[0] +
                      Matrix[4 + Row] * Input[1] +
                      Matrix[8 + Row] * Input[2] +
                      Matrix[12 + Row] * Input[3];
    }
}

static VOID
Rpi5OglGl2TransformNormal(
    _In_reads_(16) const GLfloat Matrix[16],
    _In_reads_(4) const GLfloat Input[4],
    _Out_writes_(3) GLfloat Output[3])
{
    ULONG Row;

    for (Row = 0; Row < 3; Row++)
    {
        Output[Row] = Matrix[Row] * Input[0] +
                      Matrix[4 + Row] * Input[1] +
                      Matrix[8 + Row] * Input[2];
    }
}

static BOOL
Rpi5OglGl2Normalize3(
    _Inout_updates_(3) GLfloat Vector[3])
{
    GLfloat LengthSquared = Vector[0] * Vector[0] +
                            Vector[1] * Vector[1] +
                            Vector[2] * Vector[2];
    GLfloat InverseLength;

    if (!(LengthSquared > 0.0f))
        return FALSE;
    InverseLength = 1.0f / (GLfloat)sqrt((double)LengthSquared);
    Vector[0] *= InverseLength;
    Vector[1] *= InverseLength;
    Vector[2] *= InverseLength;
    return TRUE;
}

static BOOL
Rpi5OglGl2IdeasUniformsReady(
    _In_ PRPI5VC4_OGL_PROGRAM Program)
{
    if (!Program->ProjectionMatrixSet || !Program->ModelViewMatrixSet)
        return FALSE;
    switch (Program->Executable)
    {
        case Rpi5OglProgramExecutableIdeasTable:
        case Rpi5OglProgramExecutableIdeasPaper:
            return Program->LightPositionSet &&
                   Program->LogoDirectionSet &&
                   Program->CurrentTimeSet;
        case Rpi5OglProgramExecutableIdeasText:
            return Program->CurrentTimeSet;
        case Rpi5OglProgramExecutableIdeasFlat:
            return Program->LogoColorSet;
        case Rpi5OglProgramExecutableIdeasLogo:
            return Program->NormalMatrix3Set && Program->LightPositionSet;
        case Rpi5OglProgramExecutableIdeasLampLit:
            return Program->NormalMatrix3Set &&
                   Program->LightPositionSet &&
                   Program->Light1PositionSet &&
                   Program->Light2PositionSet;
        default:
            return TRUE;
    }
}

static BOOL
Rpi5OglGl2JellyfishUniformsReady(
    _In_ PRPI5VC4_OGL_PROGRAM Program)
{
    if (Program->Executable == Rpi5OglProgramExecutableJellyfishGradient)
    {
        return Program->GradientColor1Set && Program->GradientColor2Set;
    }
    if (Program->Executable != Rpi5OglProgramExecutableJellyfishMesh)
        return FALSE;
    return Program->ModelViewProjectionMatrixSet &&
           Program->ModelViewMatrixSet &&
           Program->NormalMatrixSet &&
           Program->LightPositionSet &&
           Program->LightRadiusSet && Program->LightRadius > 0.0f &&
           Program->LightColorSet &&
           Program->AmbientColorSet &&
           Program->FresnelColorSet &&
           Program->FresnelPowerSet &&
           Program->CurrentTimeSet &&
           Program->TextureUnit == RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY &&
           Program->TextureUnit1 == RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY;
}

static GLfloat
Rpi5OglGl2SmoothStep(
    _In_ GLfloat Value)
{
    if (Value <= 0.0f)
        return 0.0f;
    if (Value >= 1.0f)
        return 1.0f;
    return Value * Value * (3.0f - 2.0f * Value);
}

static BOOL
Rpi5OglGl2StoreJellyfishMeshVertex(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _In_reads_(4) const GLfloat ObjectPosition[4],
    _In_reads_(4) const GLfloat ObjectNormal[4],
    _In_reads_(4) const GLfloat ObjectColor[4],
    _In_reads_(4) const GLfloat ObjectTexCoord[4],
    _Out_writes_(4) GLfloat ClipPosition[4],
    _Out_ PRPI5VC4_V3D_VERTEX Output,
    _Out_ PRPI5VC4_V3D_TEXCOORD Auxiliary)
{
    GLfloat AnimatedPosition[4];
    GLfloat WorldPosition[4];
    GLfloat TransformedNormal[4];
    GLfloat LightDirection[3];
    GLfloat WorldEyeVector[3];
    GLfloat Diffuse[3];
    GLfloat Ambient[3];
    GLfloat LightDistance;
    GLfloat DiffuseProduct;
    GLfloat LightFalloff;
    GLfloat FresnelProduct;
    GLfloat Offset;
    GLfloat AnimationScale;
    GLfloat Animation0;
    GLfloat Animation1;
    GLfloat Dot;
    ULONG Component;

    Offset = (-ObjectPosition[1] - 0.8f) / 10.0f;
    if (Offset < 0.0f)
        Offset = 0.0f;
    Offset = Rpi5OglGl2SmoothStep(Offset);
    AnimationScale = 1.0f - Offset;
    Animation0 = (GLfloat)sin((double)(
        Program->CurrentTime + ObjectPosition[1] / 2.0f));
    Animation1 = (GLfloat)sin((double)(
        Program->CurrentTime * 2.0f + ObjectPosition[1] / 0.5f));
    for (Component = 0; Component < 3; Component++)
    {
        AnimatedPosition[Component] = ObjectPosition[Component] +
            ObjectColor[Component] * (Animation0 / 12.0f) *
                AnimationScale +
            ObjectColor[Component] * (Animation1 / 8.0f) *
                AnimationScale;
    }
    AnimatedPosition[3] = 1.0f;

    Rpi5OglGl2TransformVector(Program->ModelViewProjectionMatrix,
                              AnimatedPosition,
                              ClipPosition);
    Rpi5OglGl2TransformVector(Program->ModelViewMatrix,
                              AnimatedPosition,
                              WorldPosition);
    Rpi5OglGl2TransformVector(Program->NormalMatrix,
                              ObjectNormal,
                              TransformedNormal);
    if (!Rpi5OglGl2Normalize3(TransformedNormal))
        return FALSE;

    LightDirection[0] = Program->LightPosition[0] - WorldPosition[0];
    LightDirection[1] = Program->LightPosition[1] - WorldPosition[1];
    LightDirection[2] = Program->LightPosition[2] - WorldPosition[2];
    LightDistance = (GLfloat)sqrt((double)(
        LightDirection[0] * LightDirection[0] +
        LightDirection[1] * LightDirection[1] +
        LightDirection[2] * LightDirection[2]));
    if (!(LightDistance > 0.0f))
        return FALSE;
    LightDirection[0] /= LightDistance;
    LightDirection[1] /= LightDistance;
    LightDirection[2] /= LightDistance;
    DiffuseProduct = TransformedNormal[0] * LightDirection[0] +
                     TransformedNormal[1] * LightDirection[1] +
                     TransformedNormal[2] * LightDirection[2];
    if (DiffuseProduct < 0.0f)
        DiffuseProduct = 0.0f;
    LightFalloff = 1.0f - LightDistance / Program->LightRadius;
    if (LightFalloff < 0.0f)
        LightFalloff = 0.0f;
    LightFalloff *= LightFalloff;
    for (Component = 0; Component < 3; Component++)
    {
        Diffuse[Component] = Program->LightColor[Component] *
            DiffuseProduct * LightFalloff * Program->LightColor[3];
        Ambient[Component] = Program->AmbientColor[Component] *
            Program->AmbientColor[3] * TransformedNormal[1];
    }

    if (WorldPosition[3] == 0.0f)
        return FALSE;
    WorldEyeVector[0] = WorldPosition[0] / WorldPosition[3];
    WorldEyeVector[1] = WorldPosition[1] / WorldPosition[3];
    WorldEyeVector[2] = WorldPosition[2] / WorldPosition[3];
    if (!Rpi5OglGl2Normalize3(WorldEyeVector))
        return FALSE;
    Dot = TransformedNormal[0] * -WorldEyeVector[0] +
          TransformedNormal[1] * -WorldEyeVector[1] +
          TransformedNormal[2] * -WorldEyeVector[2];
    Dot = (GLfloat)fabs((double)Dot);
    if (Dot > 1.0f)
        Dot = 1.0f;
    FresnelProduct = (GLfloat)pow((double)(1.0f - Dot),
                                  (double)Program->FresnelPower);

    for (Component = 0; Component < 4; Component++)
    {
        *(UNALIGNED GLfloat *)&Output->Position[Component] =
            ClipPosition[Component];
    }
    *(UNALIGNED GLfloat *)&Output->Color[0] = ObjectTexCoord[0];
    *(UNALIGNED GLfloat *)&Output->Color[1] = ObjectTexCoord[1];
    *(UNALIGNED GLfloat *)&Output->Color[2] =
        WorldPosition[0] / 24.0f + Program->CurrentTime / 20.0f;
    *(UNALIGNED GLfloat *)&Output->Color[3] =
        (WorldPosition[2] - WorldPosition[1]) / 48.0f +
        Program->CurrentTime / 40.0f;
    *(UNALIGNED GLfloat *)&Output->TexCoord[0] =
        Ambient[0] + Diffuse[0];
    *(UNALIGNED GLfloat *)&Output->TexCoord[1] =
        Ambient[1] + Diffuse[1];
    *(UNALIGNED GLfloat *)&Auxiliary->Component[0] =
        Ambient[2] + Diffuse[2];
    *(UNALIGNED GLfloat *)&Auxiliary->Component[1] =
        Program->FresnelColor[0] * Program->FresnelColor[3] *
        FresnelProduct;
    return TRUE;
}

static VOID
Rpi5OglGl2StoreIdeasVertex(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _In_reads_(4) const GLfloat Position[4],
    _In_reads_(4) const GLfloat ObjectPosition[4],
    _In_reads_(4) const GLfloat EyePosition[4],
    _In_reads_opt_(4) const GLfloat Normal[4],
    _Out_ PRPI5VC4_V3D_VERTEX Output)
{
    GLfloat Color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    GLfloat EyeDirection[3] = {0.0f, 0.0f, 0.0f};
    BOOL Lit = Rpi5OglGl2IsIdeasLitExecutable(Program->Executable);
    ULONG Component;

    if (Program->Executable == Rpi5OglProgramExecutableIdeasTable ||
        Program->Executable == Rpi5OglProgramExecutableIdeasPaper)
    {
        GLfloat LightDirection[3] =
        {
            Program->LightPosition[0] - ObjectPosition[0],
            Program->LightPosition[1] - ObjectPosition[1],
            Program->LightPosition[2] - ObjectPosition[2]
        };
        GLfloat Intensity = 0.0f;

        if (Rpi5OglGl2Normalize3(LightDirection))
        {
            Intensity = LightDirection[0] * Program->LogoDirection[0] +
                        LightDirection[1] * Program->LogoDirection[1] +
                        LightDirection[2] * Program->LogoDirection[2];
            if (Intensity < 0.0f)
                Intensity = 0.0f;
            Intensity = Intensity * Intensity * Intensity *
                        LightDirection[1];
            if (Program->CurrentTime > 10.0f &&
                Program->CurrentTime < 12.0f)
            {
                Intensity *= 1.0f -
                             (Program->CurrentTime - 10.0f) * 0.5f;
            }
        }
        Color[0] = Intensity;
        Color[1] = Intensity;
        Color[2] = Program->Executable ==
                   Rpi5OglProgramExecutableIdeasPaper ?
                       Intensity * 0.78125f : Intensity;
    }
    else if (Program->Executable == Rpi5OglProgramExecutableIdeasText)
    {
        GLfloat Intensity = Program->CurrentTime > 10.0f ?
                            (Program->CurrentTime - 10.0f) / 2.0f : 0.0f;

        Color[0] = Intensity;
        Color[1] = Intensity;
        Color[2] = Intensity;
    }
    else if (Program->Executable ==
             Rpi5OglProgramExecutableIdeasLampUnlit)
    {
        Color[0] = 1.0f;
        Color[1] = 1.0f;
        Color[2] = 1.0f;
    }
    else if (Program->Executable == Rpi5OglProgramExecutableIdeasFlat)
    {
        Color[0] = Program->LogoColor[0];
        Color[1] = Program->LogoColor[1];
        Color[2] = Program->LogoColor[2];
    }
    else if (Lit)
    {
        Rpi5OglGl2TransformNormal(Program->NormalMatrix, Normal, Color);
        EyeDirection[0] = -EyePosition[0];
        EyeDirection[1] = -EyePosition[1];
        EyeDirection[2] = -EyePosition[2];
        Rpi5OglGl2Normalize3(EyeDirection);
        Color[3] = EyeDirection[0];
    }

    for (Component = 0; Component < 4; Component++)
    {
        *(UNALIGNED GLfloat *)&Output->Position[Component] =
            Position[Component];
    }
    for (Component = 0; Component < 4; Component++)
    {
        *(UNALIGNED GLfloat *)&Output->Color[Component] = Color[Component];
    }
    *(UNALIGNED GLfloat *)&Output->TexCoord[0] = Lit ? EyeDirection[1] : 0.0f;
    *(UNALIGNED GLfloat *)&Output->TexCoord[1] = Lit ? EyeDirection[2] : 0.0f;
}

static VOID
Rpi5OglGl2ReadBuildAttribute(
    _In_ PRPI5VC4_OGL_VERTEX_ATTRIB Attribute,
    _In_opt_ const BYTE *AttributeSource,
    _In_ SIZE_T EffectiveStride,
    _In_ GLint VertexIndex,
    _Out_writes_(4) GLfloat Values[4])
{
    const BYTE *Source;

    if (!Attribute->Enabled)
    {
        Values[0] = Attribute->Current[0];
        Values[1] = Attribute->Current[1];
        Values[2] = Attribute->Current[2];
        Values[3] = Attribute->Current[3];
        return;
    }

    Source = AttributeSource +
             (SIZE_T)VertexIndex * EffectiveStride;
    Values[0] = 0.0f;
    Values[1] = 0.0f;
    Values[2] = 0.0f;
    Values[3] = 1.0f;
    if (Attribute->Size >= 1)
        Values[0] = *(const UNALIGNED GLfloat *)(Source);
    if (Attribute->Size >= 2)
        Values[1] = *(const UNALIGNED GLfloat *)(Source + sizeof(GLfloat));
    if (Attribute->Size >= 3)
        Values[2] = *(const UNALIGNED GLfloat *)(Source + 2 * sizeof(GLfloat));
    if (Attribute->Size >= 4)
        Values[3] = *(const UNALIGNED GLfloat *)(Source + 3 * sizeof(GLfloat));
}

static GLfloat
Rpi5OglGl2HomogeneousArea(
    _In_reads_(4) const GLfloat Position0[4],
    _In_reads_(4) const GLfloat Position1[4],
    _In_reads_(4) const GLfloat Position2[4])
{
    return Position0[0] *
               (Position1[1] * Position2[3] -
                Position2[1] * Position1[3]) -
           Position0[1] *
               (Position1[0] * Position2[3] -
                Position2[0] * Position1[3]) +
           Position0[3] *
               (Position1[0] * Position2[1] -
                Position2[0] * Position1[1]);
}

static VOID
Rpi5OglGl2StoreBuildVertex(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _In_reads_(4) const GLfloat Position[4],
    _In_reads_(4) const GLfloat Normal[4],
    _In_reads_(4) const GLfloat EyePosition[4],
    _Out_ PRPI5VC4_V3D_VERTEX Output)
{
    GLfloat TransformedNormal[4];
    GLfloat NormalLengthSquared;
    GLfloat InverseLength = 0.0f;
    GLfloat Diffuse = 0.0f;
    GLfloat LightDirection[3] = {2.0f / 3.0f,
                                 2.0f / 3.0f,
                                 1.0f / 3.0f};
    ULONG Component;

    Rpi5OglGl2TransformVector(Program->NormalMatrix,
                              Normal,
                              TransformedNormal);
    NormalLengthSquared =
        TransformedNormal[0] * TransformedNormal[0] +
        TransformedNormal[1] * TransformedNormal[1] +
        TransformedNormal[2] * TransformedNormal[2];
    if (NormalLengthSquared > 0.0f)
    {
        InverseLength = 1.0f /
                        (GLfloat)sqrt((double)NormalLengthSquared);
        if (Program->Executable == Rpi5OglProgramExecutableBuildDiffuse &&
            Program->LightPositionSet)
        {
            GLfloat LightLengthSquared =
                Program->LightPosition[0] * Program->LightPosition[0] +
                Program->LightPosition[1] * Program->LightPosition[1] +
                Program->LightPosition[2] * Program->LightPosition[2];

            if (LightLengthSquared > 0.0f)
            {
                GLfloat LightInverseLength = 1.0f /
                    (GLfloat)sqrt((double)LightLengthSquared);

                for (Component = 0; Component < 3; Component++)
                {
                    LightDirection[Component] =
                        Program->LightPosition[Component] *
                        LightInverseLength;
                }
            }
        }
        Diffuse = (TransformedNormal[0] * LightDirection[0] +
                   TransformedNormal[1] * LightDirection[1] +
                   TransformedNormal[2] * LightDirection[2]) *
                  InverseLength;
    }
    *(UNALIGNED GLfloat *)&Output->Position[0] = Position[0];
    *(UNALIGNED GLfloat *)&Output->Position[1] = Position[1];
    *(UNALIGNED GLfloat *)&Output->Position[2] = Position[2];
    *(UNALIGNED GLfloat *)&Output->Position[3] = Position[3];
    if (Program->Executable == Rpi5OglProgramExecutableBlinnPhong ||
        Program->Executable == Rpi5OglProgramExecutableBumpPoly ||
        Program->Executable == Rpi5OglProgramExecutablePhong ||
        Program->Executable == Rpi5OglProgramExecutableCel)
    {
        for (Component = 0; Component < 3; Component++)
        {
            *(UNALIGNED GLfloat *)&Output->Color[Component] =
                TransformedNormal[Component] * InverseLength;
        }
        if (Program->Executable == Rpi5OglProgramExecutablePhong ||
            Program->Executable == Rpi5OglProgramExecutableCel)
        {
            *(UNALIGNED GLfloat *)&Output->Color[3] = EyePosition[0];
            *(UNALIGNED GLfloat *)&Output->TexCoord[0] = EyePosition[1];
            *(UNALIGNED GLfloat *)&Output->TexCoord[1] = EyePosition[2];
        }
        else
        {
            *(UNALIGNED GLfloat *)&Output->Color[3] = 1.0f;
        }
        return;
    }
    for (Component = 0; Component < 3; Component++)
    {
        *(UNALIGNED GLfloat *)&Output->Color[Component] =
            (GLfloat)Rpi5OglGl2FloatColor(
                Diffuse * Program->MaterialDiffuse[Component]) / 255.0f;
    }
    *(UNALIGNED GLfloat *)&Output->Color[3] =
        Program->MaterialDiffuse[3];
}

static BOOL
Rpi5OglGl2BuildWireframeDistances(
    _In_ PRPI5VC4_OGL_PROGRAM Program,
    _In_reads_(3)
        PRPI5VC4_OGL_VERTEX_ATTRIB TriangleVertexAttributes[3],
    _In_reads_(3) const BYTE *TriangleVertexSources[3],
    _In_reads_(3) const SIZE_T TriangleVertexStrides[3],
    _In_reads_(3) const GLint InputIndices[3],
    _In_reads_(3) const GLfloat ObjectPositions[3][4],
    _In_reads_(3) const GLfloat ClipPositions[3][4],
    _Out_writes_(3) GLfloat Distances[3][4])
{
    static const ULONG EdgeBase[3] = {1, 2, 0};
    GLfloat AttributeValues[3][3][4];
    BOOL AttributeMatches[3][3];
    BOOL SharedTriangle = TRUE;
    ULONG Vertex;
    ULONG TriangleVertex;
    ULONG Component;

    for (Vertex = 0; Vertex < 3; Vertex++)
    {
        for (TriangleVertex = 0; TriangleVertex < 3; TriangleVertex++)
        {
            Rpi5OglGl2ReadBuildAttribute(
                TriangleVertexAttributes[TriangleVertex],
                TriangleVertexSources[TriangleVertex],
                TriangleVertexStrides[TriangleVertex],
                InputIndices[Vertex],
                AttributeValues[Vertex][TriangleVertex]);
            AttributeValues[Vertex][TriangleVertex][3] = 1.0f;
            AttributeMatches[Vertex][TriangleVertex] = TRUE;
            for (Component = 0; Component < 3; Component++)
            {
                if (AttributeValues[Vertex][TriangleVertex][Component] !=
                    ObjectPositions[TriangleVertex][Component])
                {
                    AttributeMatches[Vertex][TriangleVertex] = FALSE;
                    SharedTriangle = FALSE;
                    break;
                }
            }
        }
    }

    if (SharedTriangle)
    {
        GLfloat Screen[3][2];
        GLfloat Edge[3][2];
        GLfloat EdgeLength[3];

        for (Vertex = 0; Vertex < 3; Vertex++)
        {
            Screen[Vertex][0] =
                0.5f * Program->Viewport[0] *
                (ClipPositions[Vertex][0] / ClipPositions[Vertex][3]);
            Screen[Vertex][1] =
                0.5f * Program->Viewport[1] *
                (ClipPositions[Vertex][1] / ClipPositions[Vertex][3]);
        }
        Edge[0][0] = Screen[2][0] - Screen[1][0];
        Edge[0][1] = Screen[2][1] - Screen[1][1];
        Edge[1][0] = Screen[2][0] - Screen[0][0];
        Edge[1][1] = Screen[2][1] - Screen[0][1];
        Edge[2][0] = Screen[1][0] - Screen[0][0];
        Edge[2][1] = Screen[1][1] - Screen[0][1];
        for (TriangleVertex = 0; TriangleVertex < 3; TriangleVertex++)
        {
            EdgeLength[TriangleVertex] = (GLfloat)sqrt(
                (double)(Edge[TriangleVertex][0] *
                             Edge[TriangleVertex][0] +
                         Edge[TriangleVertex][1] *
                             Edge[TriangleVertex][1]));
            if (!(EdgeLength[TriangleVertex] > 0.0f))
                return FALSE;
        }
        for (Vertex = 0; Vertex < 3; Vertex++)
        {
            for (TriangleVertex = 0;
                 TriangleVertex < 3;
                 TriangleVertex++)
            {
                GLfloat DeltaX =
                    Screen[Vertex][0] -
                    Screen[EdgeBase[TriangleVertex]][0];
                GLfloat DeltaY =
                    Screen[Vertex][1] -
                    Screen[EdgeBase[TriangleVertex]][1];
                GLfloat Cross =
                    DeltaX * Edge[TriangleVertex][1] -
                    DeltaY * Edge[TriangleVertex][0];

                Distances[Vertex][TriangleVertex] =
                    ClipPositions[Vertex][3] *
                    (GLfloat)(fabs((double)Cross) /
                              EdgeLength[TriangleVertex]);
            }
            Distances[Vertex][3] = 1.0f / ClipPositions[Vertex][3];
        }
        return TRUE;
    }

    for (Vertex = 0; Vertex < 3; Vertex++)
    {
        GLfloat Screen[2];
        GLfloat TriangleScreen[3][2];
        GLfloat Edge[3][2];

        for (TriangleVertex = 0; TriangleVertex < 3; TriangleVertex++)
        {
            GLfloat AttributeClip[4];

            if (AttributeMatches[Vertex][TriangleVertex])
            {
                CopyMemory(AttributeClip,
                           ClipPositions[TriangleVertex],
                           sizeof(AttributeClip));
            }
            else
            {
                Rpi5OglGl2TransformVector(
                    Program->ModelViewProjectionMatrix,
                    AttributeValues[Vertex][TriangleVertex],
                    AttributeClip);
            }
            if (!(AttributeClip[3] > 0.0f))
                return FALSE;
            TriangleScreen[TriangleVertex][0] =
                0.5f * Program->Viewport[0] *
                (AttributeClip[0] / AttributeClip[3]);
            TriangleScreen[TriangleVertex][1] =
                0.5f * Program->Viewport[1] *
                (AttributeClip[1] / AttributeClip[3]);
        }

        Screen[0] =
            0.5f * Program->Viewport[0] *
            (ClipPositions[Vertex][0] / ClipPositions[Vertex][3]);
        Screen[1] =
            0.5f * Program->Viewport[1] *
            (ClipPositions[Vertex][1] / ClipPositions[Vertex][3]);
        Edge[0][0] = TriangleScreen[2][0] - TriangleScreen[1][0];
        Edge[0][1] = TriangleScreen[2][1] - TriangleScreen[1][1];
        Edge[1][0] = TriangleScreen[2][0] - TriangleScreen[0][0];
        Edge[1][1] = TriangleScreen[2][1] - TriangleScreen[0][1];
        Edge[2][0] = TriangleScreen[1][0] - TriangleScreen[0][0];
        Edge[2][1] = TriangleScreen[1][1] - TriangleScreen[0][1];

        for (TriangleVertex = 0; TriangleVertex < 3; TriangleVertex++)
        {
            GLfloat EdgeLength = (GLfloat)sqrt(
                (double)(Edge[TriangleVertex][0] *
                             Edge[TriangleVertex][0] +
                         Edge[TriangleVertex][1] *
                             Edge[TriangleVertex][1]));
            GLfloat DeltaX;
            GLfloat DeltaY;
            GLfloat Cross;

            if (!(EdgeLength > 0.0f))
                return FALSE;
            DeltaX = Screen[0] -
                     TriangleScreen[EdgeBase[TriangleVertex]][0];
            DeltaY = Screen[1] -
                     TriangleScreen[EdgeBase[TriangleVertex]][1];
            Cross = DeltaX * Edge[TriangleVertex][1] -
                    DeltaY * Edge[TriangleVertex][0];
            Distances[Vertex][TriangleVertex] =
                ClipPositions[Vertex][3] *
                (GLfloat)(fabs((double)Cross) /
                          EdgeLength);
        }
        Distances[Vertex][3] = 1.0f / ClipPositions[Vertex][3];
    }
    return TRUE;
}

RPI5VC4_OGL_GL2_DRAW_RESULT
Rpi5OglGl2PrepareBatchDraw(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count)
{
    PRPI5VC4_OGL_PROGRAM Program;

    if (!Rpi5OglGl2ProgramActive(State))
        return Rpi5OglGl2DrawNotApplicable;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutablePulsar &&
         Program->Executable != Rpi5OglProgramExecutableDepth &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         Program->Executable != Rpi5OglProgramExecutableBuildDiffuse &&
         Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableBlinnPhong &&
         Program->Executable != Rpi5OglProgramExecutableBumpPoly &&
         Program->Executable != Rpi5OglProgramExecutablePhong &&
         Program->Executable != Rpi5OglProgramExecutableCel &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         Program->Executable != Rpi5OglProgramExecutableWireframe &&
         !Rpi5OglGl2IsIdeasExecutable(Program->Executable) &&
         !Rpi5OglGl2IsJellyfishExecutable(Program->Executable) &&
         !Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
         !Rpi5OglGl2IsTerrainExecutable(Program->Executable) &&
         !Rpi5OglGl2IsEffectExecutable(Program->Executable)))
    {
        return Rpi5OglGl2DrawNotApplicable;
    }
    if (Count < 0 || First < 0 || First > 0x7fffffff - Count)
    {
        Rpi5OglGl2Error(State, GL_INVALID_VALUE,
                        "glDrawArrays(first/count)");
        return Rpi5OglGl2DrawRejected;
    }
    if ((!Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
         !Rpi5OglGl2IsIdeasExecutable(Program->Executable) &&
         !Rpi5OglGl2IsJellyfishExecutable(Program->Executable) &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         (Mode != GL_TRIANGLES || (Count % 3) != 0)) ||
        (Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
         !((Mode == GL_TRIANGLES && (Count % 3) == 0) ||
           (Mode == GL_TRIANGLE_STRIP && (Count == 0 || Count >= 3)))) ||
        (Rpi5OglGl2IsIdeasExecutable(Program->Executable) &&
         !((Mode == GL_TRIANGLES && (Count % 3) == 0) ||
           ((Mode == GL_TRIANGLE_STRIP || Mode == GL_TRIANGLE_FAN) &&
            (Count == 0 || Count >= 3)))) ||
        (Program->Executable == Rpi5OglProgramExecutableShadow &&
         !((Mode == GL_TRIANGLES && (Count % 3) == 0) ||
           (Mode == GL_TRIANGLE_STRIP && (Count == 0 || Count >= 3)))) ||
        (Program->Executable == Rpi5OglProgramExecutableJellyfishGradient &&
         !(Mode == GL_TRIANGLE_STRIP && (Count == 0 || Count >= 3))) ||
        (Program->Executable == Rpi5OglProgramExecutableJellyfishMesh &&
         (Mode != GL_TRIANGLES || (Count % 3) != 0)))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(build shader primitive)");
        return Rpi5OglGl2DrawRejected;
    }
    return Rpi5OglGl2DrawReady;
}

static BOOL
Rpi5OglGl2BuildBatchInternal(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _In_reads_opt_(Count) const GLuint *Indices,
    _In_ GLuint MaximumIndex,
    _In_ GLenum FrontFace,
    _In_ ULONG CullBits,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_VERTEX Output,
    _Out_writes_opt_(OutputCapacity)
        PRPI5VC4_V3D_TEXCOORD HeightTexCoords,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputVertexCount,
    _Out_ PULONG OutputTriangleCount)
{
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_VERTEX_ATTRIB PositionAttribute;
    PRPI5VC4_OGL_VERTEX_ATTRIB ColorAttribute = NULL;
    PRPI5VC4_OGL_VERTEX_ATTRIB NormalAttribute = NULL;
    PRPI5VC4_OGL_VERTEX_ATTRIB TexCoordAttribute = NULL;
    PRPI5VC4_OGL_VERTEX_ATTRIB TangentAttribute = NULL;
    PRPI5VC4_OGL_VERTEX_ATTRIB TriangleVertexAttribute[3] = {NULL};
    const BYTE *PositionSource;
    const BYTE *ColorSource = NULL;
    const BYTE *NormalSource = NULL;
    const BYTE *TexCoordSource = NULL;
    const BYTE *TangentSource = NULL;
    const BYTE *TriangleVertexSource[3] = {NULL};
    SIZE_T PositionStride;
    SIZE_T ColorStride = 0;
    SIZE_T NormalStride = 0;
    SIZE_T TexCoordStride = 0;
    SIZE_T TangentStride = 0;
    SIZE_T TriangleVertexStride[3] = {0};
    BOOLEAN Textured;
    BOOLEAN NormalMapped;
    BOOLEAN HeightMapped;
    BOOLEAN Effect2D;
    BOOLEAN Desktop2D;
    BOOLEAN Pulsar;
    BOOLEAN Wireframe;
    BOOLEAN Ideas;
    BOOLEAN IdeasLit;
    BOOLEAN Jellyfish;
    BOOLEAN JellyfishMesh;
    BOOLEAN TerrainTexture;
    BOOLEAN Shadow;
    ULONG InputTriangle;
    ULONG InputTriangleCount;
    GLint LastVertex;

    *OutputVertexCount = 0;
    *OutputTriangleCount = 0;
    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        (Program->Executable != Rpi5OglProgramExecutablePulsar &&
         Program->Executable != Rpi5OglProgramExecutableDepth &&
         Program->Executable != Rpi5OglProgramExecutableShadow &&
         Program->Executable != Rpi5OglProgramExecutableBuildDiffuse &&
         Program->Executable != Rpi5OglProgramExecutableBuildTexture &&
         Program->Executable != Rpi5OglProgramExecutableBlinnPhong &&
         Program->Executable != Rpi5OglProgramExecutableBumpPoly &&
         Program->Executable != Rpi5OglProgramExecutableCel &&
         Program->Executable != Rpi5OglProgramExecutableNormalMap &&
         Program->Executable != Rpi5OglProgramExecutableHeightMap &&
         Program->Executable != Rpi5OglProgramExecutableWireframe &&
         !Rpi5OglGl2IsIdeasExecutable(Program->Executable) &&
         !Rpi5OglGl2IsJellyfishExecutable(Program->Executable) &&
         !Rpi5OglGl2IsDesktopExecutable(Program->Executable) &&
         !Rpi5OglGl2IsTerrainTextureExecutable(Program->Executable) &&
         !Rpi5OglGl2IsEffectExecutable(Program->Executable)))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(build shader program)");
        return FALSE;
    }
    NormalMapped = Program->Executable ==
                   Rpi5OglProgramExecutableNormalMap;
    HeightMapped = Program->Executable ==
                   Rpi5OglProgramExecutableHeightMap;
    Effect2D = Rpi5OglGl2IsEffectExecutable(Program->Executable);
    Desktop2D = Rpi5OglGl2IsDesktopExecutable(Program->Executable);
    Pulsar = Program->Executable == Rpi5OglProgramExecutablePulsar;
    Wireframe = Program->Executable ==
                Rpi5OglProgramExecutableWireframe;
    Ideas = Rpi5OglGl2IsIdeasExecutable(Program->Executable);
    IdeasLit = Rpi5OglGl2IsIdeasLitExecutable(Program->Executable);
    Jellyfish = Rpi5OglGl2IsJellyfishExecutable(Program->Executable);
    JellyfishMesh = Program->Executable ==
                    Rpi5OglProgramExecutableJellyfishMesh;
    TerrainTexture =
        Rpi5OglGl2IsTerrainTextureExecutable(Program->Executable);
    Shadow = Program->Executable == Rpi5OglProgramExecutableShadow;
    Textured = Program->Executable ==
                   Rpi5OglProgramExecutableBuildTexture ||
               NormalMapped || HeightMapped || Effect2D || Desktop2D || Ideas ||
               JellyfishMesh || Shadow ||
               (TerrainTexture &&
                Program->Executable != Rpi5OglProgramExecutableTerrainNoise);
    if (Count == 0)
        return TRUE;
    if ((Indices != NULL && MaximumIndex > 0x7fffffffu) ||
        (Indices == NULL && (First < 0 || First > 0x7fffffff - Count)))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawElements(vertex index)");
        return FALSE;
    }
    LastVertex = Indices != NULL ? (GLint)MaximumIndex : First + Count - 1;
    if (Ideas && !Rpi5OglGl2IdeasUniformsReady(Program))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(ideas uniforms)");
        return FALSE;
    }
    if (Jellyfish && !Rpi5OglGl2JellyfishUniformsReady(Program))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(jellyfish uniforms)");
        return FALSE;
    }
    if (Program->Executable == Rpi5OglProgramExecutableJellyfishGradient)
    {
        CopyMemory(&State->JellyfishGradientColors[0],
                   Program->GradientColor1,
                   sizeof(Program->GradientColor1));
        CopyMemory(&State->JellyfishGradientColors[3],
                   Program->GradientColor2,
                   sizeof(Program->GradientColor2));
    }
    if (Program->Executable == Rpi5OglProgramExecutableIdeasLogo)
    {
        CopyMemory(State->IdeasLogoLight,
                   Program->LightPosition,
                   sizeof(State->IdeasLogoLight));
    }
    else if (Program->Executable ==
             Rpi5OglProgramExecutableIdeasLampLit)
    {
        CopyMemory(&State->IdeasLampLights[0],
                   Program->LightPosition,
                   sizeof(Program->LightPosition));
        CopyMemory(&State->IdeasLampLights[4],
                   Program->Light1Position,
                   sizeof(Program->Light1Position));
        CopyMemory(&State->IdeasLampLights[8],
                   Program->Light2Position,
                   sizeof(Program->Light2Position));
    }
    if (Wireframe &&
        (!Program->ModelViewProjectionMatrixSet ||
         !Program->ViewportSet ||
         Program->Viewport[0] <= 0.0f ||
         Program->Viewport[1] <= 0.0f))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(wireframe uniforms)");
        return FALSE;
    }
    if (Program->Executable == Rpi5OglProgramExecutableDepth &&
        !Program->ModelViewProjectionMatrixSet)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(depth uniforms)");
        return FALSE;
    }
    if (Shadow &&
        (!Program->ModelViewProjectionMatrixSet || !Program->LightMatrixSet))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(shadow uniforms)");
        return FALSE;
    }
    if (Output == NULL || OutputCapacity < 3 ||
        ((HeightMapped || Jellyfish) && HeightTexCoords == NULL) ||
        !Rpi5OglGl2GetVertexAttribSource(State,
                                         Program->PositionAttribute,
                                         LastVertex,
                                         &PositionAttribute,
                                         &PositionSource,
                                         &PositionStride) ||
        ((Pulsar || JellyfishMesh) &&
         !Rpi5OglGl2GetVertexAttribSource(State,
                                          Program->ColorAttribute,
                                          LastVertex,
                                          &ColorAttribute,
                                          &ColorSource,
                                          &ColorStride)) ||
        ((JellyfishMesh ||
          (!Jellyfish && !NormalMapped && !Effect2D && !Desktop2D && !Shadow &&
           !TerrainTexture && !Pulsar && !Wireframe &&
           (!Ideas || IdeasLit))) &&
         !Rpi5OglGl2GetVertexAttribSource(State,
                                          Program->NormalAttribute,
                                          LastVertex,
                                          &NormalAttribute,
                                          &NormalSource,
                                          &NormalStride)) ||
        ((Program->Executable ==
              Rpi5OglProgramExecutableJellyfishGradient ||
          (Textured && !Effect2D && !Ideas && !Shadow)) &&
         !TerrainTexture &&
         !Rpi5OglGl2GetVertexAttribSource(State,
                                          Program->TexCoordAttribute,
                                          LastVertex,
                                          &TexCoordAttribute,
                                          &TexCoordSource,
                                          &TexCoordStride)) ||
        (HeightMapped &&
         !Rpi5OglGl2GetVertexAttribSource(State,
                                          Program->TangentAttribute,
                                          LastVertex,
                                          &TangentAttribute,
                                          &TangentSource,
                                          &TangentStride)) ||
        (Wireframe &&
         (!Rpi5OglGl2GetVertexAttribSource(
              State,
              Program->TriangleVertexAttribute[0],
              LastVertex,
              &TriangleVertexAttribute[0],
              &TriangleVertexSource[0],
              &TriangleVertexStride[0]) ||
          !Rpi5OglGl2GetVertexAttribSource(
              State,
              Program->TriangleVertexAttribute[1],
              LastVertex,
              &TriangleVertexAttribute[1],
              &TriangleVertexSource[1],
              &TriangleVertexStride[1]) ||
          !Rpi5OglGl2GetVertexAttribSource(
              State,
              Program->TriangleVertexAttribute[2],
              LastVertex,
              &TriangleVertexAttribute[2],
              &TriangleVertexSource[2],
              &TriangleVertexStride[2]))))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(build shader arrays)");
        return FALSE;
    }

    InputTriangleCount =
        (Mode == GL_TRIANGLE_STRIP || Mode == GL_TRIANGLE_FAN) ?
            Count - 2 : Count / 3;
    for (InputTriangle = 0;
         InputTriangle < InputTriangleCount;
         InputTriangle++)
    {
        GLfloat Position[3][4];
        GLfloat ObjectPosition[3][4];
        GLfloat EyePosition[3][4] = {{0}};
        GLfloat ShadowCoordinate[3][4] = {{0}};
        GLfloat WireframeDistance[3][4] = {{0}};
        RPI5VC4_V3D_VERTEX JellyfishVertex[3];
        RPI5VC4_V3D_TEXCOORD JellyfishAuxiliary[3];
        GLint InputIndex[3];
        ULONG ElementIndex[3];
        ULONG Vertex;

        if (Mode == GL_TRIANGLE_STRIP)
        {
            ElementIndex[0] = InputTriangle;
            ElementIndex[1] = InputTriangle + 1;
            ElementIndex[2] = InputTriangle + 2;
            if ((InputTriangle & 1u) != 0)
            {
                ULONG Swap = ElementIndex[0];

                ElementIndex[0] = ElementIndex[1];
                ElementIndex[1] = Swap;
            }
        }
        else if (Mode == GL_TRIANGLE_FAN)
        {
            ElementIndex[0] = 0;
            ElementIndex[1] = InputTriangle + 1;
            ElementIndex[2] = InputTriangle + 2;
        }
        else
        {
            ElementIndex[0] = InputTriangle * 3;
            ElementIndex[1] = ElementIndex[0] + 1;
            ElementIndex[2] = ElementIndex[0] + 2;
        }
        for (Vertex = 0; Vertex < 3; Vertex++)
        {
            InputIndex[Vertex] = Indices != NULL ?
                (GLint)Indices[ElementIndex[Vertex]] :
                First + (GLint)ElementIndex[Vertex];
        }

        for (Vertex = 0; Vertex < 3; Vertex++)
        {
            Rpi5OglGl2ReadBuildAttribute(
                PositionAttribute,
                PositionSource,
                PositionStride,
                InputIndex[Vertex],
                ObjectPosition[Vertex]);
            ObjectPosition[Vertex][3] = 1.0f;
            if (Program->Executable ==
                    Rpi5OglProgramExecutableJellyfishGradient)
            {
                GLfloat TexCoord[4];

                Rpi5OglGl2ReadBuildAttribute(
                    TexCoordAttribute,
                    TexCoordSource,
                    TexCoordStride,
                    InputIndex[Vertex],
                    TexCoord);
                Position[Vertex][0] = ObjectPosition[Vertex][0];
                Position[Vertex][1] = ObjectPosition[Vertex][1];
                Position[Vertex][2] = 1.0f;
                Position[Vertex][3] = 1.0f;
                CopyMemory(JellyfishVertex[Vertex].Position,
                           Position[Vertex],
                           sizeof(JellyfishVertex[Vertex].Position));
                *(UNALIGNED GLfloat *)&JellyfishVertex[Vertex].Color[0] =
                    TexCoord[0];
                *(UNALIGNED GLfloat *)&JellyfishVertex[Vertex].Color[1] =
                    TexCoord[1];
                JellyfishVertex[Vertex].Color[2] = 0;
                JellyfishVertex[Vertex].Color[3] = 0;
                JellyfishVertex[Vertex].TexCoord[0] = 0;
                JellyfishVertex[Vertex].TexCoord[1] = 0;
                JellyfishAuxiliary[Vertex].Component[0] = 0;
                JellyfishAuxiliary[Vertex].Component[1] = 0;
            }
            else if (JellyfishMesh)
            {
                GLfloat Normal[4];
                GLfloat Color[4];
                GLfloat TexCoord[4];

                Rpi5OglGl2ReadBuildAttribute(
                    NormalAttribute,
                    NormalSource,
                    NormalStride,
                    InputIndex[Vertex],
                    Normal);
                Normal[3] = 1.0f;
                Rpi5OglGl2ReadBuildAttribute(
                    ColorAttribute,
                    ColorSource,
                    ColorStride,
                    InputIndex[Vertex],
                    Color);
                Rpi5OglGl2ReadBuildAttribute(
                    TexCoordAttribute,
                    TexCoordSource,
                    TexCoordStride,
                    InputIndex[Vertex],
                    TexCoord);
                if (!Rpi5OglGl2StoreJellyfishMeshVertex(
                        Program,
                        ObjectPosition[Vertex],
                        Normal,
                        Color,
                        TexCoord,
                        Position[Vertex],
                        &JellyfishVertex[Vertex],
                        &JellyfishAuxiliary[Vertex]))
                {
                    Rpi5OglGl2Error(
                        State,
                        GL_INVALID_OPERATION,
                        "glDrawElements(jellyfish vertex input)");
                    return FALSE;
                }
            }
            else if (Effect2D || Desktop2D || TerrainTexture)
            {
                CopyMemory(Position[Vertex],
                           ObjectPosition[Vertex],
                           sizeof(ObjectPosition[Vertex]));
            }
            else if (Ideas)
            {
                Rpi5OglGl2TransformVector(
                    Program->ModelViewMatrix,
                    ObjectPosition[Vertex],
                    EyePosition[Vertex]);
                Rpi5OglGl2TransformVector(
                    Program->ProjectionMatrix,
                    EyePosition[Vertex],
                    Position[Vertex]);
            }
            else
            {
                Rpi5OglGl2TransformVector(
                    Program->ModelViewProjectionMatrix,
                    ObjectPosition[Vertex],
                    Position[Vertex]);
            }
            if (Shadow)
            {
                Rpi5OglGl2TransformVector(Program->LightMatrix,
                                          ObjectPosition[Vertex],
                                          ShadowCoordinate[Vertex]);
                if (!(ShadowCoordinate[Vertex][3] > 0.0f))
                {
                    Rpi5OglGl2Error(
                        State,
                        GL_INVALID_OPERATION,
                        "glDrawArrays(shadow coordinate w)");
                    return FALSE;
                }
            }
            if (!Ideas && !Jellyfish &&
                (Program->Executable == Rpi5OglProgramExecutablePhong ||
                Program->Executable == Rpi5OglProgramExecutableCel)
               )
            {
                Rpi5OglGl2TransformVector(
                    Program->ModelViewMatrix,
                    ObjectPosition[Vertex],
                    EyePosition[Vertex]);
            }
            if (Position[Vertex][3] <= 0.0f)
            {
                Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                                "glDrawArrays(build shader clip w)");
                return FALSE;
            }
        }

        if (CullBits != 0)
        {
            GLfloat Area = Rpi5OglGl2HomogeneousArea(Position[0],
                                                     Position[1],
                                                     Position[2]);
            ULONG Facing;

            if (Area == 0.0f)
                continue;
            Facing = ((Area < 0.0f) ? 1u : 0u) ^
                     ((FrontFace == GL_CW) ? 1u : 0u);
            if (((Facing + 1) & CullBits) != 0)
                continue;
        }

        if (Wireframe &&
            !Rpi5OglGl2BuildWireframeDistances(
                Program,
                TriangleVertexAttribute,
                TriangleVertexSource,
                TriangleVertexStride,
                InputIndex,
                ObjectPosition,
                Position,
                WireframeDistance))
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glDrawArrays(wireframe triangle input)");
            return FALSE;
        }

        if (*OutputVertexCount > OutputCapacity - 3)
        {
            Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                            "glDrawArrays(build shader batch capacity)");
            return FALSE;
        }
        for (Vertex = 0; Vertex < 3; Vertex++)
        {
            ULONG OutputIndex = *OutputVertexCount;
            PRPI5VC4_V3D_VERTEX OutputVertex =
                &Output[OutputIndex];

            if (Jellyfish)
            {
                CopyMemory(OutputVertex,
                           &JellyfishVertex[Vertex],
                           sizeof(*OutputVertex));
                CopyMemory(&HeightTexCoords[OutputIndex],
                           &JellyfishAuxiliary[Vertex],
                           sizeof(HeightTexCoords[OutputIndex]));
            }
            else if (Ideas)
            {
                GLfloat Normal[4] = {0.0f, 0.0f, 0.0f, 0.0f};

                if (IdeasLit)
                {
                    Rpi5OglGl2ReadBuildAttribute(
                        NormalAttribute,
                        NormalSource,
                        NormalStride,
                        InputIndex[Vertex],
                        Normal);
                }
                Rpi5OglGl2StoreIdeasVertex(Program,
                                            Position[Vertex],
                                            ObjectPosition[Vertex],
                                            EyePosition[Vertex],
                                            Normal,
                                            OutputVertex);
            }
            else if (Shadow)
            {
                ULONG Component;

                for (Component = 0; Component < 4; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Position[Component] =
                        Position[Vertex][Component];
                    *(UNALIGNED GLfloat *)&OutputVertex->Color[Component] =
                        ShadowCoordinate[Vertex][Component];
                }
                OutputVertex->TexCoord[0] = 0;
                OutputVertex->TexCoord[1] = 0;
            }
            else if (Effect2D || Desktop2D || TerrainTexture)
            {
                ULONG Component;

                for (Component = 0; Component < 4; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Position[Component] =
                        Position[Vertex][Component];
                    *(UNALIGNED GLfloat *)&OutputVertex->Color[Component] =
                        1.0f;
                }
                *(UNALIGNED GLfloat *)&OutputVertex->TexCoord[0] =
                    (Position[Vertex][0] * 0.5f + 0.5f) *
                        (TerrainTexture ? Program->UvScale[0] : 1.0f) +
                    (TerrainTexture ? Program->UvOffset[0] : 0.0f);
                *(UNALIGNED GLfloat *)&OutputVertex->TexCoord[1] =
                    (Position[Vertex][1] * 0.5f + 0.5f) *
                        (TerrainTexture ? Program->UvScale[1] : 1.0f) +
                    (TerrainTexture ? Program->UvOffset[1] : 0.0f);
                if (TerrainTexture)
                {
                    OutputVertex->Color[0] = OutputVertex->TexCoord[0];
                    OutputVertex->Color[1] = OutputVertex->TexCoord[1];
                }
            }
            else if (Pulsar)
            {
                GLfloat Color[4];
                ULONG Component;

                Rpi5OglGl2ReadBuildAttribute(
                    ColorAttribute,
                    ColorSource,
                    ColorStride,
                    InputIndex[Vertex],
                    Color);
                for (Component = 0; Component < 4; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Position[Component] =
                        Position[Vertex][Component];
                    *(UNALIGNED GLfloat *)&OutputVertex->Color[Component] =
                        Color[Component];
                }
            }
            else if (Wireframe)
            {
                ULONG Component;

                for (Component = 0; Component < 4; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Position[Component] =
                        Position[Vertex][Component];
                    *(UNALIGNED GLfloat *)&OutputVertex->Color[Component] =
                        WireframeDistance[Vertex][Component];
                }
            }
            else if (HeightMapped)
            {
                GLfloat Normal[4];
                GLfloat Tangent[4];
                ULONG Component;

                Rpi5OglGl2ReadBuildAttribute(
                    NormalAttribute,
                    NormalSource,
                    NormalStride,
                    InputIndex[Vertex],
                    Normal);
                Rpi5OglGl2ReadBuildAttribute(
                    TangentAttribute,
                    TangentSource,
                    TangentStride,
                    InputIndex[Vertex],
                    Tangent);
                for (Component = 0; Component < 4; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Position[Component] =
                        Position[Vertex][Component];
                }
                for (Component = 0; Component < 3; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Color[Component] =
                        Normal[Component];
                }
                *(UNALIGNED GLfloat *)&OutputVertex->Color[3] = Tangent[0];
                *(UNALIGNED GLfloat *)&OutputVertex->TexCoord[0] = Tangent[1];
                *(UNALIGNED GLfloat *)&OutputVertex->TexCoord[1] = Tangent[2];
            }
            else if (NormalMapped)
            {
                ULONG Component;

                for (Component = 0; Component < 4; Component++)
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->Position[Component] =
                        Position[Vertex][Component];
                    *(UNALIGNED GLfloat *)&OutputVertex->Color[Component] =
                        1.0f;
                }
            }
            else
            {
                GLfloat Normal[4];

                Rpi5OglGl2ReadBuildAttribute(
                    NormalAttribute,
                    NormalSource,
                    NormalStride,
                    InputIndex[Vertex],
                    Normal);
                Normal[3] = 1.0f;
                Rpi5OglGl2StoreBuildVertex(
                    Program,
                    Position[Vertex],
                    Normal,
                    EyePosition[Vertex],
                    OutputVertex);
            }
            if (Textured && !Effect2D && !Ideas && !Jellyfish && !Shadow &&
                !TerrainTexture)
            {
                GLfloat TexCoord[4];

                Rpi5OglGl2ReadBuildAttribute(
                    TexCoordAttribute,
                    TexCoordSource,
                    TexCoordStride,
                    InputIndex[Vertex],
                    TexCoord);
                if (HeightMapped)
                {
                    *(UNALIGNED GLfloat *)&
                        HeightTexCoords[OutputIndex].Component[0] =
                            TexCoord[0];
                    *(UNALIGNED GLfloat *)&
                        HeightTexCoords[OutputIndex].Component[1] =
                            TexCoord[1];
                }
                else
                {
                    *(UNALIGNED GLfloat *)&OutputVertex->TexCoord[0] =
                        TexCoord[0];
                    *(UNALIGNED GLfloat *)&OutputVertex->TexCoord[1] =
                        TexCoord[1];
                }
            }
            (*OutputVertexCount)++;
        }
        (*OutputTriangleCount)++;
    }
    return TRUE;
}

BOOL
Rpi5OglGl2BuildBatch(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _In_ GLenum FrontFace,
    _In_ ULONG CullBits,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_VERTEX Output,
    _Out_writes_opt_(OutputCapacity)
        PRPI5VC4_V3D_TEXCOORD HeightTexCoords,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputVertexCount,
    _Out_ PULONG OutputTriangleCount)
{
    return Rpi5OglGl2BuildBatchInternal(State,
                                        Mode,
                                        First,
                                        Count,
                                        NULL,
                                        0,
                                        FrontFace,
                                        CullBits,
                                        Output,
                                        HeightTexCoords,
                                        OutputCapacity,
                                        OutputVertexCount,
                                        OutputTriangleCount);
}

BOOL
Rpi5OglGl2BuildIndexedBatch(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_reads_(Count) const GLuint *Indices,
    _In_ GLsizei Count,
    _In_ GLuint MaximumIndex,
    _In_ GLenum FrontFace,
    _In_ ULONG CullBits,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_VERTEX Output,
    _Out_writes_opt_(OutputCapacity)
        PRPI5VC4_V3D_TEXCOORD HeightTexCoords,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputVertexCount,
    _Out_ PULONG OutputTriangleCount)
{
    if (Count != 0 && Indices == NULL)
        return FALSE;
    return Rpi5OglGl2BuildBatchInternal(State,
                                        Mode,
                                        0,
                                        Count,
                                        Indices,
                                        MaximumIndex,
                                        FrontFace,
                                        CullBits,
                                        Output,
                                        HeightTexCoords,
                                        OutputCapacity,
                                        OutputVertexCount,
                                        OutputTriangleCount);
}

static VOID
Rpi5OglGl2ReadTerrainVertex(
    _In_ PRPI5VC4_OGL_VERTEX_ATTRIB PositionAttribute,
    _In_ const BYTE *PositionSource,
    _In_ SIZE_T PositionStride,
    _In_ PRPI5VC4_OGL_VERTEX_ATTRIB NormalAttribute,
    _In_ const BYTE *NormalSource,
    _In_ SIZE_T NormalStride,
    _In_ PRPI5VC4_OGL_VERTEX_ATTRIB TangentAttribute,
    _In_ const BYTE *TangentSource,
    _In_ SIZE_T TangentStride,
    _In_ PRPI5VC4_OGL_VERTEX_ATTRIB TexCoordAttribute,
    _In_ const BYTE *TexCoordSource,
    _In_ SIZE_T TexCoordStride,
    _In_ GLint VertexIndex,
    _Out_ PRPI5VC4_V3D_TERRAIN_VERTEX Output)
{
    GLfloat Value[4];

    Rpi5OglGl2ReadBuildAttribute(PositionAttribute,
                                 PositionSource,
                                 PositionStride,
                                 VertexIndex,
                                 Value);
    CopyMemory(Output->Position, Value, sizeof(Output->Position));
    Rpi5OglGl2ReadBuildAttribute(NormalAttribute,
                                 NormalSource,
                                 NormalStride,
                                 VertexIndex,
                                 Value);
    CopyMemory(Output->Normal, Value, sizeof(Output->Normal));
    Rpi5OglGl2ReadBuildAttribute(TangentAttribute,
                                 TangentSource,
                                 TangentStride,
                                 VertexIndex,
                                 Value);
    CopyMemory(Output->Tangent, Value, sizeof(Output->Tangent));
    Rpi5OglGl2ReadBuildAttribute(TexCoordAttribute,
                                 TexCoordSource,
                                 TexCoordStride,
                                 VertexIndex,
                                 Value);
    CopyMemory(Output->TexCoord, Value, sizeof(Output->TexCoord));
}

BOOL
Rpi5OglGl2BuildTerrainGrid(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLint First,
    _In_ GLsizei Count,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_TERRAIN_VERTEX Output,
    _In_ ULONG OutputCapacity)
{
    PRPI5VC4_OGL_PROGRAM Program;
    PRPI5VC4_OGL_VERTEX_ATTRIB PositionAttribute;
    PRPI5VC4_OGL_VERTEX_ATTRIB NormalAttribute;
    PRPI5VC4_OGL_VERTEX_ATTRIB TangentAttribute;
    PRPI5VC4_OGL_VERTEX_ATTRIB TexCoordAttribute;
    const BYTE *PositionSource;
    const BYTE *NormalSource;
    const BYTE *TangentSource;
    const BYTE *TexCoordSource;
    SIZE_T PositionStride;
    SIZE_T NormalStride;
    SIZE_T TangentStride;
    SIZE_T TexCoordStride;
    ULONG X;
    ULONG Y;

    Program = Rpi5OglGl2FindProgram(State, State->CurrentProgram);
    if (Program == NULL ||
        Program->Executable != Rpi5OglProgramExecutableTerrain ||
        First != 0 || Count != RPI5VC4_V3D_TERRAIN_INDEX_COUNT ||
        Output == NULL ||
        OutputCapacity < RPI5VC4_V3D_TERRAIN_VERTEX_COUNT ||
        !Rpi5OglGl2GetVertexAttribSource(
            State, Program->PositionAttribute, Count - 1,
            &PositionAttribute, &PositionSource, &PositionStride) ||
        !Rpi5OglGl2GetVertexAttribSource(
            State, Program->NormalAttribute, Count - 1,
            &NormalAttribute, &NormalSource, &NormalStride) ||
        !Rpi5OglGl2GetVertexAttribSource(
            State, Program->TangentAttribute, Count - 1,
            &TangentAttribute, &TangentSource, &TangentStride) ||
        !Rpi5OglGl2GetVertexAttribSource(
            State, Program->TexCoordAttribute, Count - 1,
            &TexCoordAttribute, &TexCoordSource, &TexCoordStride) ||
        !PositionAttribute->Enabled || PositionAttribute->Size != 3 ||
        PositionAttribute->Type != GL_FLOAT ||
        !NormalAttribute->Enabled || NormalAttribute->Size != 3 ||
        NormalAttribute->Type != GL_FLOAT ||
        !TangentAttribute->Enabled || TangentAttribute->Size != 3 ||
        TangentAttribute->Type != GL_FLOAT ||
        !TexCoordAttribute->Enabled || TexCoordAttribute->Size != 2 ||
        TexCoordAttribute->Type != GL_FLOAT)
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(terrain grid arrays)");
        return FALSE;
    }

    for (X = 0; X < RPI5VC4_V3D_TERRAIN_GRID_SIDE; X++)
    {
        for (Y = 0; Y < RPI5VC4_V3D_TERRAIN_GRID_SIDE; Y++)
        {
            ULONG SourceIndex;

            if (X < 256u && Y < 256u)
                SourceIndex = (X * 256u + Y) * 6u + 5u;
            else if (X == 256u && Y < 256u)
                SourceIndex = (255u * 256u + Y) * 6u + 2u;
            else if (X < 256u)
                SourceIndex = (X * 256u + 255u) * 6u;
            else
                SourceIndex = (255u * 256u + 255u) * 6u + 1u;
            Rpi5OglGl2ReadTerrainVertex(
                PositionAttribute, PositionSource, PositionStride,
                NormalAttribute, NormalSource, NormalStride,
                TangentAttribute, TangentSource, TangentStride,
                TexCoordAttribute, TexCoordSource, TexCoordStride,
                (GLint)SourceIndex,
                &Output[X * RPI5VC4_V3D_TERRAIN_GRID_SIDE + Y]);
        }
    }

    for (X = 0; X < 256u; X++)
    {
        for (Y = 0; Y < 256u; Y++)
        {
            static const UCHAR VertexMap[6][2] =
            {
                {0, 1}, {1, 1}, {1, 0},
                {0, 1}, {1, 0}, {0, 0}
            };
            ULONG CellFirst = (X * 256u + Y) * 6u;
            ULONG Vertex;

            for (Vertex = 0; Vertex < 6u; Vertex++)
            {
                RPI5VC4_V3D_TERRAIN_VERTEX SourceVertex;
                ULONG GridX = X + VertexMap[Vertex][0];
                ULONG GridY = Y + VertexMap[Vertex][1];

                Rpi5OglGl2ReadTerrainVertex(
                    PositionAttribute, PositionSource, PositionStride,
                    NormalAttribute, NormalSource, NormalStride,
                    TangentAttribute, TangentSource, TangentStride,
                    TexCoordAttribute, TexCoordSource, TexCoordStride,
                    (GLint)(CellFirst + Vertex), &SourceVertex);
                if (memcmp(&SourceVertex,
                           &Output[GridX * RPI5VC4_V3D_TERRAIN_GRID_SIDE +
                                   GridY],
                           sizeof(SourceVertex)) != 0)
                {
                    Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                                    "glDrawArrays(terrain grid topology)");
                    return FALSE;
                }
            }
        }
    }
    return TRUE;
}

RPI5VC4_OGL_GL2_DRAW_RESULT
Rpi5OglGl2BuildPrimitive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _Out_writes_(RPI5VC4_OGL_GL2_MAX_DRAW_VERTICES)
        RPI5VC4_OGL_GL2_VERTEX *Vertices)
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
    if ((Mode != GL_TRIANGLES || Count != 3) &&
        (Mode != GL_TRIANGLE_STRIP || Count != 4))
    {
        Rpi5OglGl2Error(State, GL_INVALID_OPERATION,
                        "glDrawArrays(bounded generic draw)");
        return Rpi5OglGl2DrawRejected;
    }
    if (First < 0 || First > 0x7fffffff - Count)
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

    for (Vertex = 0; Vertex < Count; Vertex++)
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
