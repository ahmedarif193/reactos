/*
 * PROJECT:     ReactOS Raspberry Pi 5 OpenGL test harness
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise incremental OpenGL 3.0 driver contracts
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <GL/gl.h>
#include <GL/glext.h>

static LONG Failures;

static VOID
TestPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
    fputs(Buffer, stdout);
    fflush(stdout);
}

static VOID
Check(
    _In_ BOOL Condition,
    _In_z_ PCSTR Name)
{
    if (!Condition)
    {
        InterlockedIncrement(&Failures);
        TestPrint("RPI5_GL3_FAIL check=%s error=0x%04x\n",
                  Name, glGetError());
    }
}

static VOID
CheckError(
    _In_ GLenum Expected,
    _In_z_ PCSTR Name)
{
    GLenum Actual = glGetError();

    if (Actual != Expected)
    {
        InterlockedIncrement(&Failures);
        TestPrint("RPI5_GL3_FAIL check=%s expected=0x%04x actual=0x%04x\n",
                  Name, Expected, Actual);
    }
}

static LRESULT CALLBACK
WindowProcedure(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
CreateContext(
    _Out_ HWND *Window,
    _Out_ HDC *DeviceContext,
    _Out_ HGLRC *RenderingContext)
{
    static const WCHAR ClassName[] = L"Rpi5Gl3ContractTest";
    static const PIXELFORMATDESCRIPTOR Format =
    {
        sizeof(Format),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,
        32,
        0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0,
        24, 8, 0,
        PFD_MAIN_PLANE,
        0, 0, 0, 0
    };
    WNDCLASSW Class;
    INT PixelFormat;

    ZeroMemory(&Class, sizeof(Class));
    Class.style = CS_OWNDC;
    Class.lpfnWndProc = WindowProcedure;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    if (!RegisterClassW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;

    *Window = CreateWindowW(ClassName, ClassName, WS_POPUP,
                            0, 0, 64, 64, NULL, NULL,
                            Class.hInstance, NULL);
    if (*Window == NULL)
        return FALSE;
    *DeviceContext = GetDC(*Window);
    if (*DeviceContext == NULL)
        return FALSE;
    PixelFormat = ChoosePixelFormat(*DeviceContext, &Format);
    if (PixelFormat == 0 ||
        !SetPixelFormat(*DeviceContext, PixelFormat, &Format))
    {
        return FALSE;
    }
    *RenderingContext = wglCreateContext(*DeviceContext);
    return *RenderingContext != NULL &&
           wglMakeCurrent(*DeviceContext, *RenderingContext);
}

static VOID
DrainErrors(VOID)
{
    while (glGetError() != GL_NO_ERROR)
        ;
}

static VOID
Gl30Failure(
    _In_z_ PCSTR Name,
    _In_ GLenum Error)
{
    InterlockedIncrement(&Failures);
    TestPrint("RPI5_GL30_FAIL check=%s error=0x%04x\n", Name, Error);
}

static BOOL
ParseVersion(
    _In_opt_z_ const GLubyte *String,
    _Out_ ULONG *Major,
    _Out_ ULONG *Minor)
{
    *Major = 0;
    *Minor = 0;
    return String != NULL &&
           sscanf((const char *)String, "%lu.%lu", Major, Minor) == 2;
}

static VOID
CheckGl30Limit(
    _In_ GLenum Name,
    _In_ GLint Minimum,
    _In_z_ PCSTR Label)
{
    GLenum Error;
    GLint Value = 0;

    DrainErrors();
    glGetIntegerv(Name, &Value);
    Error = glGetError();
    TestPrint("RPI5_GL30_LIMIT name=%s actual=%d required=%d error=0x%04x\n",
              Label, Value, Minimum, Error);
    if (Error != GL_NO_ERROR || Value < Minimum)
        Gl30Failure(Label, Error);
}

static VOID
TestGl30ShaderCompiler(VOID)
{
    static const GLchar VertexSource[] =
        "#version 130\n"
        "in vec4 position;\n"
        "uniform float x_offset;\n"
        "out vec4 color;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = position + vec4(x_offset, 0.0, 0.0, 0.0);\n"
        "    color = vec4(0.25, 0.5, 0.75, 1.0);\n"
        "}\n";
    static const GLchar FragmentSource[] =
        "#version 130\n"
        "in vec4 color;\n"
        "out vec4 output_color;\n"
        "void main()\n"
        "{\n"
        "    output_color = color * vec4(1.0);\n"
        "}\n";
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
    PFNGLBINDFRAGDATALOCATIONPROC BindFragDataLocation;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLDELETESHADERPROC DeleteShader;
    const GLchar *Source;
    GLuint VertexShader = 0;
    GLuint FragmentShader = 0;
    GLuint Program = 0;
    GLint VertexStatus = GL_FALSE;
    GLint FragmentStatus = GL_FALSE;
    GLint LinkStatus = GL_FALSE;
    GLsizei Length = 0;
    GLchar Log[512];

    CreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    ShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    CompileShader = (PFNGLCOMPILESHADERPROC)
        wglGetProcAddress("glCompileShader");
    GetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    GetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)
        wglGetProcAddress("glGetShaderInfoLog");
    CreateProgram = (PFNGLCREATEPROGRAMPROC)
        wglGetProcAddress("glCreateProgram");
    AttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    BindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)
        wglGetProcAddress("glBindAttribLocation");
    BindFragDataLocation = (PFNGLBINDFRAGDATALOCATIONPROC)
        wglGetProcAddress("glBindFragDataLocation");
    LinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    GetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    GetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)
        wglGetProcAddress("glGetProgramInfoLog");
    DeleteProgram = (PFNGLDELETEPROGRAMPROC)
        wglGetProcAddress("glDeleteProgram");
    DeleteShader = (PFNGLDELETESHADERPROC)
        wglGetProcAddress("glDeleteShader");
    if (!CreateShader || !ShaderSource || !CompileShader || !GetShaderiv ||
        !GetShaderInfoLog || !CreateProgram || !AttachShader ||
        !BindAttribLocation || !BindFragDataLocation || !LinkProgram ||
        !GetProgramiv || !GetProgramInfoLog || !DeleteProgram || !DeleteShader)
    {
        Gl30Failure("arbitrary_glsl130_procs", GL_NO_ERROR);
        return;
    }

    VertexShader = CreateShader(GL_VERTEX_SHADER);
    FragmentShader = CreateShader(GL_FRAGMENT_SHADER);
    if (!VertexShader || !FragmentShader)
    {
        Gl30Failure("arbitrary_glsl130_shader_create", glGetError());
        goto Cleanup;
    }

    Source = VertexSource;
    ShaderSource(VertexShader, 1, &Source, NULL);
    CompileShader(VertexShader);
    GetShaderiv(VertexShader, GL_COMPILE_STATUS, &VertexStatus);
    if (VertexStatus != GL_TRUE)
    {
        Log[0] = '\0';
        GetShaderInfoLog(VertexShader, sizeof(Log) - 1, &Length, Log);
        Log[Length >= 0 && Length < sizeof(Log) ? Length : sizeof(Log) - 1] =
            '\0';
        TestPrint("RPI5_GL30_SHADER_LOG stage=vertex log=%s\n", Log);
        Gl30Failure("arbitrary_glsl130_vertex_compile", glGetError());
    }

    Source = FragmentSource;
    ShaderSource(FragmentShader, 1, &Source, NULL);
    CompileShader(FragmentShader);
    GetShaderiv(FragmentShader, GL_COMPILE_STATUS, &FragmentStatus);
    if (FragmentStatus != GL_TRUE)
    {
        Log[0] = '\0';
        GetShaderInfoLog(FragmentShader, sizeof(Log) - 1, &Length, Log);
        Log[Length >= 0 && Length < sizeof(Log) ? Length : sizeof(Log) - 1] =
            '\0';
        TestPrint("RPI5_GL30_SHADER_LOG stage=fragment log=%s\n", Log);
        Gl30Failure("arbitrary_glsl130_fragment_compile", glGetError());
    }
    if (VertexStatus != GL_TRUE || FragmentStatus != GL_TRUE)
        goto Cleanup;

    Program = CreateProgram();
    if (!Program)
    {
        Gl30Failure("arbitrary_glsl130_program_create", glGetError());
        goto Cleanup;
    }
    AttachShader(Program, VertexShader);
    AttachShader(Program, FragmentShader);
    BindAttribLocation(Program, 0, "position");
    BindFragDataLocation(Program, 0, "output_color");
    LinkProgram(Program);
    GetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
    if (LinkStatus != GL_TRUE)
    {
        Log[0] = '\0';
        GetProgramInfoLog(Program, sizeof(Log) - 1, &Length, Log);
        Log[Length >= 0 && Length < sizeof(Log) ? Length : sizeof(Log) - 1] =
            '\0';
        TestPrint("RPI5_GL30_SHADER_LOG stage=link log=%s\n", Log);
        Gl30Failure("arbitrary_glsl130_program_link", glGetError());
    }

Cleanup:
    if (Program)
        DeleteProgram(Program);
    if (VertexShader)
        DeleteShader(VertexShader);
    if (FragmentShader)
        DeleteShader(FragmentShader);
}

static VOID
TestGl30TextureArray(VOID)
{
    PFNGLTEXIMAGE3DPROC TexImage3D;
    GLuint Texture = 0;
    GLint Width = 0;
    GLint Height = 0;
    GLint Depth = 0;
    GLenum Error;

    TexImage3D = (PFNGLTEXIMAGE3DPROC)wglGetProcAddress("glTexImage3D");
    if (!TexImage3D)
    {
        Gl30Failure("texture_2d_array_proc", GL_NO_ERROR);
        return;
    }

    DrainErrors();
    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Texture);
    TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 2, 2, 2, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Error = glGetError();
    if (Error == GL_NO_ERROR)
    {
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0,
                                 GL_TEXTURE_WIDTH, &Width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0,
                                 GL_TEXTURE_HEIGHT, &Height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0,
                                 GL_TEXTURE_DEPTH, &Depth);
        Error = glGetError();
    }
    TestPrint("RPI5_GL30_TEXTURE_ARRAY size=%dx%dx%d error=0x%04x\n",
              Width, Height, Depth, Error);
    if (Error != GL_NO_ERROR || Width != 2 || Height != 2 || Depth != 2)
        Gl30Failure("texture_2d_array_allocation", Error);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    if (Texture)
        glDeleteTextures(1, &Texture);
}

static VOID
TestGl30Conformance(VOID)
{
    const GLubyte *VersionString;
    const GLubyte *ShadingLanguageString;
    ULONG Major;
    ULONG Minor;
    ULONG ShadingMajor;
    ULONG ShadingMinor;
    BOOL VersionValid;
    BOOL ShadingVersionValid;
    LONG InitialFailures = Failures;

    VersionString = glGetString(GL_VERSION);
    ShadingLanguageString = glGetString(GL_SHADING_LANGUAGE_VERSION);
    VersionValid = ParseVersion(VersionString, &Major, &Minor) &&
                   (Major > 3 || (Major == 3 && Minor >= 0));
    ShadingVersionValid = ParseVersion(ShadingLanguageString,
                                       &ShadingMajor, &ShadingMinor) &&
                          (ShadingMajor > 1 ||
                           (ShadingMajor == 1 && ShadingMinor >= 30));
    TestPrint("RPI5_GL30_VERSION api=%s glsl=%s required=3.0/1.30\n",
              VersionString ? (const char *)VersionString : "missing",
              ShadingLanguageString ?
                  (const char *)ShadingLanguageString : "missing");
    if (!VersionValid)
        Gl30Failure("advertised_opengl_version", glGetError());
    if (!ShadingVersionValid)
        Gl30Failure("advertised_glsl_version", glGetError());

    CheckGl30Limit(GL_MAX_ARRAY_TEXTURE_LAYERS, 256,
                   "max_array_texture_layers");
    CheckGl30Limit(GL_MAX_VERTEX_ATTRIBS, 16, "max_vertex_attribs");
    CheckGl30Limit(GL_MAX_VERTEX_UNIFORM_COMPONENTS, 1024,
                   "max_vertex_uniform_components");
    CheckGl30Limit(GL_MAX_VARYING_COMPONENTS, 64,
                   "max_varying_components");
    CheckGl30Limit(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 16,
                   "max_combined_texture_image_units");
    CheckGl30Limit(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, 16,
                   "max_vertex_texture_image_units");
    CheckGl30Limit(GL_MAX_TEXTURE_IMAGE_UNITS, 16,
                   "max_texture_image_units");
    CheckGl30Limit(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, 1024,
                   "max_fragment_uniform_components");
    CheckGl30Limit(GL_MAX_DRAW_BUFFERS, 8, "max_draw_buffers");
    CheckGl30Limit(GL_MAX_COLOR_ATTACHMENTS, 8, "max_color_attachments");
    CheckGl30Limit(GL_MAX_SAMPLES, 4, "max_samples");
    CheckGl30Limit(GL_MAX_TEXTURE_SIZE, 1024, "max_texture_size");
    CheckGl30Limit(GL_MAX_3D_TEXTURE_SIZE, 256, "max_3d_texture_size");
    CheckGl30Limit(GL_MAX_RENDERBUFFER_SIZE, 1024,
                   "max_renderbuffer_size");
    TestGl30ShaderCompiler();
    TestGl30TextureArray();
    TestPrint("RPI5_GL30_CONFORMANCE_%s added_failures=%ld\n",
              Failures == InitialFailures ? "PASS" : "FAIL",
              Failures - InitialFailures);
}

static VOID
TestMapBufferRange(VOID)
{
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLGETBUFFERSUBDATAPROC GetBufferSubData;
    PFNGLGETBUFFERPARAMETERIVPROC GetBufferParameteriv;
    PFNGLGETBUFFERPOINTERVPROC GetBufferPointerv;
    PFNGLMAPBUFFERRANGEPROC MapBufferRange;
    PFNGLFLUSHMAPPEDBUFFERRANGEPROC FlushMappedBufferRange;
    PFNGLUNMAPBUFFERPROC UnmapBuffer;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    GLubyte Initial[32];
    GLubyte Readback[32];
    GLubyte *Mapping;
    GLvoid *MapPointer = NULL;
    GLuint Buffer = 0;
    GLint Value;
    ULONG Index;

    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    BufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    GetBufferSubData = (PFNGLGETBUFFERSUBDATAPROC)
        wglGetProcAddress("glGetBufferSubData");
    GetBufferParameteriv = (PFNGLGETBUFFERPARAMETERIVPROC)
        wglGetProcAddress("glGetBufferParameteriv");
    GetBufferPointerv = (PFNGLGETBUFFERPOINTERVPROC)
        wglGetProcAddress("glGetBufferPointerv");
    MapBufferRange = (PFNGLMAPBUFFERRANGEPROC)
        wglGetProcAddress("glMapBufferRange");
    FlushMappedBufferRange = (PFNGLFLUSHMAPPEDBUFFERRANGEPROC)
        wglGetProcAddress("glFlushMappedBufferRange");
    UnmapBuffer = (PFNGLUNMAPBUFFERPROC)
        wglGetProcAddress("glUnmapBuffer");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)
        wglGetProcAddress("glDeleteBuffers");
    Check(GenBuffers && BindBuffer && BufferData && GetBufferSubData &&
          GetBufferParameteriv && GetBufferPointerv && MapBufferRange &&
          FlushMappedBufferRange && UnmapBuffer && DeleteBuffers,
          "map_buffer_range_procs");
    if (Failures != 0)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(Initial); Index++)
        Initial[Index] = (GLubyte)Index;
    GenBuffers(1, &Buffer);
    BindBuffer(GL_ARRAY_BUFFER, Buffer);
    BufferData(GL_ARRAY_BUFFER, sizeof(Initial), Initial, GL_DYNAMIC_DRAW);
    Check(glGetError() == GL_NO_ERROR, "buffer_setup");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS, &Value);
    Check(Value == GL_READ_WRITE, "buffer_initial_access");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &Value);
    Check(Value == 0, "buffer_initial_access_flags");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &Value);
    Check(Value == 0, "buffer_initial_map_offset");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &Value);
    Check(Value == 0, "buffer_initial_map_length");
    GetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &MapPointer);
    Check(MapPointer == NULL, "buffer_initial_map_pointer");

    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 8, 8,
                             GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
    Check(Mapping != NULL && Mapping[0] == 8 && Mapping[7] == 15,
          "mapped_subrange_contents");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &Value);
    Check(Value == (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT),
          "buffer_access_flags");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &Value);
    Check(Value == 8, "buffer_map_offset");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &Value);
    Check(Value == 8, "buffer_map_length");
    GetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &MapPointer);
    Check(MapPointer == Mapping, "buffer_map_pointer");
    Mapping[0] = 0xa8;
    Mapping[7] = 0xaf;
    Check(UnmapBuffer(GL_ARRAY_BUFFER), "unmap_read_write_range");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &Value);
    Check(Value == 0, "unmap_access_flags_reset");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &Value);
    Check(Value == 0, "unmap_offset_reset");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &Value);
    Check(Value == 0, "unmap_length_reset");
    GetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &MapPointer);
    Check(MapPointer == NULL, "unmap_pointer_reset");
    GetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Readback), Readback);
    Check(Readback[8] == 0xa8 && Readback[15] == 0xaf,
          "mapped_subrange_writeback");

    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 4, 4,
                             GL_MAP_WRITE_BIT |
                             GL_MAP_FLUSH_EXPLICIT_BIT);
    Check(Mapping != NULL, "map_explicit_flush");
    if (Mapping != NULL)
    {
        Mapping[1] = 0x51;
        Mapping[2] = 0x52;
        FlushMappedBufferRange(GL_ARRAY_BUFFER, 1, 2);
        Check(glGetError() == GL_NO_ERROR, "flush_mapped_subrange");
        Check(UnmapBuffer(GL_ARRAY_BUFFER), "unmap_explicit_flush");
    }

    DrainErrors();
    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 0, 0, GL_MAP_WRITE_BIT);
    Check(Mapping == NULL, "reject_zero_length_pointer");
    CheckError(GL_INVALID_OPERATION, "reject_zero_length");
    DrainErrors();
    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 0, 4, 0x80000000);
    Check(Mapping == NULL, "reject_unknown_access_pointer");
    CheckError(GL_INVALID_VALUE, "reject_unknown_access_bit");
    DrainErrors();
    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 0, 4,
                             GL_MAP_READ_BIT |
                             GL_MAP_INVALIDATE_RANGE_BIT);
    Check(Mapping == NULL, "reject_read_invalidate_pointer");
    CheckError(GL_INVALID_OPERATION, "reject_read_invalidate");
    DrainErrors();
    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 31, 2, GL_MAP_READ_BIT);
    Check(Mapping == NULL, "reject_out_of_range_pointer");
    CheckError(GL_INVALID_VALUE, "reject_out_of_range");

    DrainErrors();
    FlushMappedBufferRange(GL_ARRAY_BUFFER, 40, 1);
    CheckError(GL_INVALID_OPERATION, "reject_flush_unmapped");
    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 0, 4, GL_MAP_WRITE_BIT);
    Check(Mapping != NULL, "map_without_explicit_flush");
    FlushMappedBufferRange(GL_ARRAY_BUFFER, 0, 1);
    CheckError(GL_INVALID_OPERATION, "reject_flush_without_explicit");
    Check(UnmapBuffer(GL_ARRAY_BUFFER), "unmap_without_explicit_flush");
    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 8, 4,
                             GL_MAP_WRITE_BIT |
                             GL_MAP_FLUSH_EXPLICIT_BIT);
    Check(Mapping != NULL, "map_for_flush_range_validation");
    FlushMappedBufferRange(GL_ARRAY_BUFFER, 3, 2);
    CheckError(GL_INVALID_VALUE, "reject_flush_out_of_range");
    Check(UnmapBuffer(GL_ARRAY_BUFFER), "unmap_after_flush_range_error");

    Mapping = MapBufferRange(GL_ARRAY_BUFFER, 0, 4, GL_MAP_READ_BIT);
    Check(Mapping != NULL, "map_before_store_reset");
    Check(UnmapBuffer(GL_ARRAY_BUFFER), "unmap_before_store_reset");
    BufferData(GL_ARRAY_BUFFER, sizeof(Initial), Initial, GL_STREAM_DRAW);
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS, &Value);
    Check(Value == GL_READ_WRITE, "store_reset_access");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &Value);
    Check(Value == 0, "store_reset_access_flags");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &Value);
    Check(Value == 0, "store_reset_map_offset");
    GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &Value);
    Check(Value == 0, "store_reset_map_length");

    DeleteBuffers(1, &Buffer);
    TestPrint("RPI5_GL3_MAP_BUFFER_RANGE_%s\n",
              Failures == 0 ? "PASS" : "FAIL");
}

static VOID
TestIndexedBufferBindings(VOID)
{
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLBINDBUFFERBASEPROC BindBufferBase;
    PFNGLBINDBUFFERRANGEPROC BindBufferRange;
    PFNGLGETINTEGERI_VPROC GetIntegeri;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    GLuint Buffer = 0;
    GLint Value = -1;
    LONG InitialFailures = Failures;

    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    BufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    BindBufferBase = (PFNGLBINDBUFFERBASEPROC)
        wglGetProcAddress("glBindBufferBase");
    BindBufferRange = (PFNGLBINDBUFFERRANGEPROC)
        wglGetProcAddress("glBindBufferRange");
    GetIntegeri = (PFNGLGETINTEGERI_VPROC)
        wglGetProcAddress("glGetIntegeri_v");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)
        wglGetProcAddress("glDeleteBuffers");
    Check(GenBuffers && BindBuffer && BufferData && BindBufferBase &&
          BindBufferRange && GetIntegeri && DeleteBuffers,
          "indexed_buffer_procs");
    if (Failures != InitialFailures)
        return;

    GenBuffers(1, &Buffer);
    BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, Buffer);
    BufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 128, NULL, GL_STREAM_COPY);
    BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, Buffer);
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0, &Value);
    Check(Value == (GLint)Buffer, "indexed_buffer_base_binding");
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_START, 0, &Value);
    Check(Value == 0, "indexed_buffer_base_start");
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE, 0, &Value);
    Check(Value == 128, "indexed_buffer_base_size");

    BindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 1, Buffer, 16, 32);
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 1, &Value);
    Check(Value == (GLint)Buffer, "indexed_buffer_range_binding");
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_START, 1, &Value);
    Check(Value == 16, "indexed_buffer_range_start");
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE, 1, &Value);
    Check(Value == 32, "indexed_buffer_range_size");
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, &Value);
    Check(Value == (GLint)Buffer, "generic_transform_feedback_binding");

    DrainErrors();
    BindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 4, Buffer, 0, 16);
    CheckError(GL_INVALID_VALUE, "reject_indexed_buffer_index");
    BindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 1, Buffer, 2, 16);
    CheckError(GL_INVALID_VALUE, "reject_indexed_buffer_alignment");
    BindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 1, Buffer, 112, 32);
    CheckError(GL_INVALID_VALUE, "reject_indexed_buffer_range");

    DeleteBuffers(1, &Buffer);
    GetIntegeri(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0, &Value);
    Check(Value == 0, "delete_indexed_buffer_binding");
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, &Value);
    Check(Value == 0, "delete_generic_transform_feedback_binding");
    TestPrint("RPI5_GL3_INDEXED_BUFFER_%s\n",
              Failures == InitialFailures ? "PASS" : "FAIL");
}

static VOID
TestIndexedRasterAndClearState(VOID)
{
    PFNGLCOLORMASKIPROC ColorMaski;
    PFNGLENABLEIPROC Enablei;
    PFNGLDISABLEIPROC Disablei;
    PFNGLISENABLEDIPROC IsEnabledi;
    PFNGLGETBOOLEANI_VPROC GetBooleani;
    PFNGLCLAMPCOLORPROC ClampColor;
    PFNGLCLEARBUFFERFVPROC ClearBufferfv;
    GLboolean Mask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
    GLfloat SavedClear[4];
    const GLfloat Clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    GLubyte Pixel[4] = {0, 0, 0, 0};
    GLint Value = 0;
    LONG InitialFailures = Failures;

    ColorMaski = (PFNGLCOLORMASKIPROC)wglGetProcAddress("glColorMaski");
    Enablei = (PFNGLENABLEIPROC)wglGetProcAddress("glEnablei");
    Disablei = (PFNGLDISABLEIPROC)wglGetProcAddress("glDisablei");
    IsEnabledi = (PFNGLISENABLEDIPROC)wglGetProcAddress("glIsEnabledi");
    GetBooleani = (PFNGLGETBOOLEANI_VPROC)
        wglGetProcAddress("glGetBooleani_v");
    ClampColor = (PFNGLCLAMPCOLORPROC)wglGetProcAddress("glClampColor");
    ClearBufferfv = (PFNGLCLEARBUFFERFVPROC)
        wglGetProcAddress("glClearBufferfv");
    Check(ColorMaski && Enablei && Disablei && IsEnabledi && GetBooleani &&
          ClampColor && ClearBufferfv, "indexed_raster_procs");
    if (Failures != InitialFailures)
        return;

    ColorMaski(0, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    GetBooleani(GL_COLOR_WRITEMASK, 0, Mask);
    Check(Mask[0] && !Mask[1] && Mask[2] && !Mask[3],
          "indexed_color_mask");
    ColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    Enablei(GL_BLEND, 0);
    Check(IsEnabledi(GL_BLEND, 0), "indexed_blend_enable");
    Disablei(GL_BLEND, 0);
    Check(!IsEnabledi(GL_BLEND, 0), "indexed_blend_disable");

    ClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
    glGetIntegerv(GL_CLAMP_READ_COLOR, &Value);
    Check(Value == GL_FALSE, "clamp_read_color_state");
    ClampColor(GL_CLAMP_READ_COLOR, GL_FIXED_ONLY);

    glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, SavedClear);
    ClearBufferfv(GL_COLOR, 0, Clear);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    Check(Pixel[0] >= 63 && Pixel[0] <= 65 &&
          Pixel[1] >= 127 && Pixel[1] <= 129 &&
          Pixel[2] >= 190 && Pixel[2] <= 192 && Pixel[3] == 255,
          "clear_buffer_color_readback");
    glGetFloatv(GL_COLOR_CLEAR_VALUE, SavedClear);
    Check(SavedClear[0] > 0.09f && SavedClear[0] < 0.11f &&
          SavedClear[3] > 0.39f && SavedClear[3] < 0.41f,
          "clear_buffer_preserves_clear_state");

    DrainErrors();
    ColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    CheckError(GL_INVALID_VALUE, "reject_color_mask_index");
    Enablei(GL_DEPTH_TEST, 0);
    CheckError(GL_INVALID_ENUM, "reject_indexed_enable_cap");
    ClearBufferfv(GL_COLOR, 1, Clear);
    CheckError(GL_INVALID_VALUE, "reject_clear_drawbuffer");
    TestPrint("RPI5_GL3_INDEXED_RASTER_%s\n",
              Failures == InitialFailures ? "PASS" : "FAIL");
}

static VOID
TestCombinedClear(VOID)
{
    PFNGLCLEARBUFFERFIPROC ClearBufferfi;
    GLubyte StencilPixel[4] = {0, 0, 0, 0};
    GLfloat DepthPixel = 0.0f;
    GLdouble SavedDepth = 0.0;
    GLint SavedStencil = 0;
    GLint StencilBits = 0;
    LONG InitialFailures = Failures;

    ClearBufferfi = (PFNGLCLEARBUFFERFIPROC)
        wglGetProcAddress("glClearBufferfi");
    Check(ClearBufferfi != NULL, "combined_clear_buffer_proc");
    glGetIntegerv(GL_STENCIL_BITS, &StencilBits);
    Check(StencilBits >= 8, "combined_clear_stencil_buffer");
    if (Failures != InitialFailures)
        return;

    DrainErrors();
    glViewport(0, 0, 64, 64);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glStencilMask(0xff);
    glClearColor(0.125f, 0.25f, 0.5f, 1.0f);
    glClearDepth(1.0);
    glClearStencil(0x5a);
    glClear(GL_COLOR_BUFFER_BIT |
            GL_DEPTH_BUFFER_BIT |
            GL_STENCIL_BUFFER_BIT);
    glReadPixels(0, 0, 1, 1, GL_STENCIL_INDEX,
                 GL_UNSIGNED_BYTE, StencilPixel);
    Check(StencilPixel[0] == 0x5a, "combined_clear_stencil_value");
    Check(glGetError() == GL_NO_ERROR, "combined_clear_errors");

    glClearDepth(0.75);
    glClearStencil(0x12);
    ClearBufferfi(GL_DEPTH_STENCIL, 0, 0.25f, 0x33);
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT,
                 GL_FLOAT, &DepthPixel);
    glReadPixels(0, 0, 1, 1, GL_STENCIL_INDEX,
                 GL_UNSIGNED_BYTE, StencilPixel);
    Check(DepthPixel > 0.249f && DepthPixel < 0.251f,
          "clear_bufferfi_depth_value");
    Check(StencilPixel[0] == 0x33, "clear_bufferfi_stencil_value");
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &SavedDepth);
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &SavedStencil);
    Check(SavedDepth > 0.749 && SavedDepth < 0.751,
          "clear_bufferfi_preserves_depth_state");
    Check(SavedStencil == 0x12,
          "clear_bufferfi_preserves_stencil_state");
    Check(glGetError() == GL_NO_ERROR, "clear_bufferfi_errors");

    glDisable(GL_DEPTH_TEST);
    glClearDepth(1.0);
    glClearStencil(0);
    TestPrint("RPI5_GL3_COMBINED_CLEAR_%s stencil=0x%02x depth=%.3f\n",
              Failures == InitialFailures ? "PASS" : "FAIL",
              StencilPixel[0], DepthPixel);
}

static VOID
TestTransformFeedback(VOID)
{
    static const GLchar VertexSource[] =
        "#version 130\n"
        "in vec4 pos;in vec4 color;out vec4 out_color;"
        "void main(){gl_Position=pos;out_color=color;}";
    static const GLchar FragmentSource[] =
        "#version 130\n"
        "in vec4 out_color;out vec4 frag_color;"
        "void main(){frag_color=out_color;}";
    static const GLchar *Varyings[] = {"out_color"};
    static const GLfloat Vertices[24] =
    {
        -0.5f, -0.5f, 0.0f, 1.0f, 0.1f, 0.2f, 0.3f, 1.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 0.4f, 0.5f, 0.6f, 1.0f,
         0.0f,  0.5f, 0.0f, 1.0f, 0.7f, 0.8f, 0.9f, 1.0f
    };
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
    PFNGLBINDFRAGDATALOCATIONPROC BindFragDataLocation;
    PFNGLTRANSFORMFEEDBACKVARYINGSPROC TransformFeedbackVaryings;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETFRAGDATALOCATIONPROC GetFragDataLocation;
    PFNGLGETTRANSFORMFEEDBACKVARYINGPROC GetTransformFeedbackVarying;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLGETBUFFERSUBDATAPROC GetBufferSubData;
    PFNGLBINDBUFFERBASEPROC BindBufferBase;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLBEGINTRANSFORMFEEDBACKPROC BeginTransformFeedback;
    PFNGLENDTRANSFORMFEEDBACKPROC EndTransformFeedback;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLDELETESHADERPROC DeleteShader;
    GLfloat Captured[12];
    GLchar Name[32];
    GLuint VertexShader;
    GLuint FragmentShader;
    GLuint Program;
    GLuint Buffers[2] = {0, 0};
    GLint Status;
    GLsizei Length;
    GLsizei Size;
    GLenum Type;
    ULONG Index;
    LONG InitialFailures = Failures;
    const GLchar *Source;

    CreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    ShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    CompileShader = (PFNGLCOMPILESHADERPROC)
        wglGetProcAddress("glCompileShader");
    GetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    CreateProgram = (PFNGLCREATEPROGRAMPROC)
        wglGetProcAddress("glCreateProgram");
    AttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    BindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)
        wglGetProcAddress("glBindAttribLocation");
    BindFragDataLocation = (PFNGLBINDFRAGDATALOCATIONPROC)
        wglGetProcAddress("glBindFragDataLocation");
    TransformFeedbackVaryings = (PFNGLTRANSFORMFEEDBACKVARYINGSPROC)
        wglGetProcAddress("glTransformFeedbackVaryings");
    LinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    GetProgramiv = (PFNGLGETPROGRAMIVPROC)
        wglGetProcAddress("glGetProgramiv");
    GetFragDataLocation = (PFNGLGETFRAGDATALOCATIONPROC)
        wglGetProcAddress("glGetFragDataLocation");
    GetTransformFeedbackVarying = (PFNGLGETTRANSFORMFEEDBACKVARYINGPROC)
        wglGetProcAddress("glGetTransformFeedbackVarying");
    UseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    BufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    GetBufferSubData = (PFNGLGETBUFFERSUBDATAPROC)
        wglGetProcAddress("glGetBufferSubData");
    BindBufferBase = (PFNGLBINDBUFFERBASEPROC)
        wglGetProcAddress("glBindBufferBase");
    VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)
        wglGetProcAddress("glVertexAttribPointer");
    EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)
        wglGetProcAddress("glEnableVertexAttribArray");
    BeginTransformFeedback = (PFNGLBEGINTRANSFORMFEEDBACKPROC)
        wglGetProcAddress("glBeginTransformFeedback");
    EndTransformFeedback = (PFNGLENDTRANSFORMFEEDBACKPROC)
        wglGetProcAddress("glEndTransformFeedback");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)
        wglGetProcAddress("glDeleteBuffers");
    DeleteProgram = (PFNGLDELETEPROGRAMPROC)
        wglGetProcAddress("glDeleteProgram");
    DeleteShader = (PFNGLDELETESHADERPROC)
        wglGetProcAddress("glDeleteShader");
    Check(CreateShader && ShaderSource && CompileShader && GetShaderiv &&
          CreateProgram && AttachShader && BindAttribLocation &&
          BindFragDataLocation && TransformFeedbackVaryings && LinkProgram &&
          GetProgramiv && GetFragDataLocation &&
          GetTransformFeedbackVarying && UseProgram && GenBuffers &&
          BindBuffer && BufferData && GetBufferSubData && BindBufferBase &&
          VertexAttribPointer && EnableVertexAttribArray &&
          BeginTransformFeedback && EndTransformFeedback && DeleteBuffers &&
          DeleteProgram && DeleteShader, "transform_feedback_procs");
    if (Failures != InitialFailures)
        return;

    VertexShader = CreateShader(GL_VERTEX_SHADER);
    FragmentShader = CreateShader(GL_FRAGMENT_SHADER);
    Source = VertexSource;
    ShaderSource(VertexShader, 1, &Source, NULL);
    Source = FragmentSource;
    ShaderSource(FragmentShader, 1, &Source, NULL);
    CompileShader(VertexShader);
    CompileShader(FragmentShader);
    GetShaderiv(VertexShader, GL_COMPILE_STATUS, &Status);
    Check(Status == GL_TRUE, "glsl130_vertex_compile");
    GetShaderiv(FragmentShader, GL_COMPILE_STATUS, &Status);
    Check(Status == GL_TRUE, "glsl130_fragment_compile");

    Program = CreateProgram();
    AttachShader(Program, VertexShader);
    AttachShader(Program, FragmentShader);
    BindAttribLocation(Program, 0, "pos");
    BindAttribLocation(Program, 1, "color");
    BindFragDataLocation(Program, 0, "frag_color");
    TransformFeedbackVaryings(Program, 1, Varyings, GL_INTERLEAVED_ATTRIBS);
    LinkProgram(Program);
    GetProgramiv(Program, GL_LINK_STATUS, &Status);
    Check(Status == GL_TRUE, "glsl130_program_link");
    Check(GetFragDataLocation(Program, "frag_color") == 0,
          "fragment_output_location");
    GetProgramiv(Program, GL_TRANSFORM_FEEDBACK_VARYINGS, &Status);
    Check(Status == 1, "transform_feedback_varying_count");
    GetProgramiv(Program, GL_TRANSFORM_FEEDBACK_BUFFER_MODE, &Status);
    Check(Status == GL_INTERLEAVED_ATTRIBS, "transform_feedback_buffer_mode");
    GetTransformFeedbackVarying(Program, 0, sizeof(Name), &Length,
                                &Size, &Type, Name);
    Check(Length == 9 && Size == 1 && Type == GL_FLOAT_VEC4 &&
          lstrcmpA(Name, "out_color") == 0,
          "transform_feedback_varying_query");

    GenBuffers(2, Buffers);
    BindBuffer(GL_ARRAY_BUFFER, Buffers[0]);
    BufferData(GL_ARRAY_BUFFER, sizeof(Vertices), Vertices, GL_STATIC_DRAW);
    VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), NULL);
    VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (const GLvoid *)(4 * sizeof(GLfloat)));
    EnableVertexAttribArray(0);
    EnableVertexAttribArray(1);
    BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, Buffers[1]);
    BufferData(GL_TRANSFORM_FEEDBACK_BUFFER, sizeof(Captured), NULL,
               GL_STREAM_COPY);
    BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, Buffers[1]);
    UseProgram(Program);
    glEnable(GL_RASTERIZER_DISCARD);
    BeginTransformFeedback(GL_TRIANGLES);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    EndTransformFeedback();
    glDisable(GL_RASTERIZER_DISCARD);
    GetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                     sizeof(Captured), Captured);
    for (Index = 0; Index < RTL_NUMBER_OF(Captured); Index++)
    {
        Check(Captured[Index] == Vertices[(Index / 4) * 8 + 4 + Index % 4],
              "transform_feedback_data");
    }
    Check(glGetError() == GL_NO_ERROR, "transform_feedback_errors");

    UseProgram(0);
    DeleteBuffers(2, Buffers);
    DeleteProgram(Program);
    DeleteShader(VertexShader);
    DeleteShader(FragmentShader);
    TestPrint("RPI5_GL3_TRANSFORM_FEEDBACK_%s\n",
              Failures == InitialFailures ? "PASS" : "FAIL");
}

static VOID
TestVertexArrays(VOID)
{
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLISVERTEXARRAYPROC IsVertexArray;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLGETVERTEXATTRIBFVPROC GetVertexAttribfv;
    GLuint Arrays[2] = {0, 0};
    GLuint Buffers[2] = {0, 0};
    GLint Binding;
    GLfloat Attribute;

    GenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)
        wglGetProcAddress("glGenVertexArrays");
    BindVertexArray = (PFNGLBINDVERTEXARRAYPROC)
        wglGetProcAddress("glBindVertexArray");
    DeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)
        wglGetProcAddress("glDeleteVertexArrays");
    IsVertexArray = (PFNGLISVERTEXARRAYPROC)
        wglGetProcAddress("glIsVertexArray");
    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)
        wglGetProcAddress("glDeleteBuffers");
    VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)
        wglGetProcAddress("glVertexAttribPointer");
    EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)
        wglGetProcAddress("glEnableVertexAttribArray");
    GetVertexAttribfv = (PFNGLGETVERTEXATTRIBFVPROC)
        wglGetProcAddress("glGetVertexAttribfv");
    Check(GenVertexArrays && BindVertexArray && DeleteVertexArrays &&
          IsVertexArray && GenBuffers && BindBuffer && DeleteBuffers &&
          VertexAttribPointer && EnableVertexAttribArray &&
          GetVertexAttribfv, "vertex_array_procs");
    if (Failures != 0)
        return;

    GenVertexArrays(2, Arrays);
    Check(Arrays[0] != 0 && Arrays[1] != 0 && Arrays[0] != Arrays[1],
          "vertex_array_names");
    Check(!IsVertexArray(Arrays[0]), "vertex_array_unbound_name");
    BindVertexArray(Arrays[0]);
    Check(IsVertexArray(Arrays[0]), "vertex_array_object_created");
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Binding);
    Check(Binding == (GLint)Arrays[0], "vertex_array_binding_a");

    GenBuffers(2, Buffers);
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, Buffers[0]);
    BindBuffer(GL_ARRAY_BUFFER, Buffers[1]);
    VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 16,
                        (const GLvoid *)(ULONG_PTR)32);
    EnableVertexAttribArray(2);

    BindVertexArray(Arrays[1]);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &Binding);
    Check(Binding == 0, "vertex_array_b_element_binding");
    GetVertexAttribfv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &Attribute);
    Check(Attribute == GL_FALSE, "vertex_array_b_attribute_enable");
    GetVertexAttribfv(2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &Attribute);
    Check(Attribute == 0.0f, "vertex_array_b_attribute_buffer");

    BindVertexArray(Arrays[0]);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &Binding);
    Check(Binding == (GLint)Buffers[0], "vertex_array_a_element_binding");
    GetVertexAttribfv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &Attribute);
    Check(Attribute == GL_TRUE, "vertex_array_a_attribute_enable");
    GetVertexAttribfv(2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &Attribute);
    Check(Attribute == (GLfloat)Buffers[1],
          "vertex_array_a_attribute_buffer");

    DeleteBuffers(2, Buffers);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &Binding);
    Check(Binding == 0, "vertex_array_deleted_element_buffer");
    GetVertexAttribfv(2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &Attribute);
    Check(Attribute == 0.0f, "vertex_array_deleted_attribute_buffer");

    DrainErrors();
    BindVertexArray(0x7fffffffu);
    CheckError(GL_INVALID_OPERATION, "reject_unknown_vertex_array");
    DeleteVertexArrays(2, Arrays);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Binding);
    Check(Binding == 0, "delete_bound_vertex_array_unbinds");
    Check(!IsVertexArray(Arrays[0]), "deleted_vertex_array_absent");
    TestPrint("RPI5_GL3_VERTEX_ARRAY_%s\n",
              Failures == 0 ? "PASS" : "FAIL");
}

static VOID
TestCoreFramebufferObjects(VOID)
{
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
    PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
    PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC StorageMultisample;
    PFNGLGETRENDERBUFFERPARAMETERIVPROC GetRenderbufferParameteriv;
    PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;
    GLuint Framebuffers[2] = {0, 0};
    GLuint Renderbuffer = 0;
    GLint Value;

    GenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)
        wglGetProcAddress("glGenFramebuffers");
    BindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)
        wglGetProcAddress("glBindFramebuffer");
    CheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)
        wglGetProcAddress("glCheckFramebufferStatus");
    DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)
        wglGetProcAddress("glDeleteFramebuffers");
    GenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)
        wglGetProcAddress("glGenRenderbuffers");
    BindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)
        wglGetProcAddress("glBindRenderbuffer");
    StorageMultisample = (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC)
        wglGetProcAddress("glRenderbufferStorageMultisample");
    GetRenderbufferParameteriv = (PFNGLGETRENDERBUFFERPARAMETERIVPROC)
        wglGetProcAddress("glGetRenderbufferParameteriv");
    DeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)
        wglGetProcAddress("glDeleteRenderbuffers");
    Check(GenFramebuffers && BindFramebuffer && CheckFramebufferStatus &&
          DeleteFramebuffers && GenRenderbuffers && BindRenderbuffer &&
          StorageMultisample && GetRenderbufferParameteriv &&
          DeleteRenderbuffers, "core_framebuffer_procs");
    if (Failures != 0)
        return;

    GenFramebuffers(2, Framebuffers);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, Framebuffers[0]);
    BindFramebuffer(GL_READ_FRAMEBUFFER, Framebuffers[1]);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &Value);
    Check(Value == (GLint)Framebuffers[0], "draw_framebuffer_binding");
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &Value);
    Check(Value == (GLint)Framebuffers[1], "read_framebuffer_binding");
    Check(CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) ==
              GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
          "core_framebuffer_status");

    GenRenderbuffers(1, &Renderbuffer);
    BindRenderbuffer(GL_RENDERBUFFER, Renderbuffer);
    StorageMultisample(GL_RENDERBUFFER, 0, GL_RGBA8, 8, 8);
    Check(glGetError() == GL_NO_ERROR, "zero_sample_renderbuffer_storage");
    GetRenderbufferParameteriv(GL_RENDERBUFFER,
                               GL_RENDERBUFFER_SAMPLES, &Value);
    Check(Value == 0, "renderbuffer_samples");
    DrainErrors();
    StorageMultisample(GL_RENDERBUFFER, 1, GL_RGBA8, 8, 8);
    CheckError(GL_INVALID_VALUE, "reject_unsupported_sample_count");
    glGetIntegerv(GL_MAX_SAMPLES, &Value);
    Check(Value == 0, "max_samples_truthful");

    BindFramebuffer(GL_FRAMEBUFFER, 0);
    DeleteRenderbuffers(1, &Renderbuffer);
    DeleteFramebuffers(2, Framebuffers);
    TestPrint("RPI5_GL3_CORE_FBO_%s\n",
              Failures == 0 ? "PASS" : "FAIL");
}

static VOID
TestIntegerVertexAttributes(VOID)
{
    static const PCSTR RequiredProcedures[] =
    {
        "glGetVertexAttribdv",
        "glGetVertexAttribiv",
        "glGetVertexAttribIiv",
        "glGetVertexAttribIuiv",
        "glVertexAttrib1d",
        "glVertexAttrib1dv",
        "glVertexAttrib1s",
        "glVertexAttrib1sv",
        "glVertexAttrib2d",
        "glVertexAttrib2dv",
        "glVertexAttrib2s",
        "glVertexAttrib2sv",
        "glVertexAttrib3d",
        "glVertexAttrib3dv",
        "glVertexAttrib3s",
        "glVertexAttrib3sv",
        "glVertexAttrib4Nbv",
        "glVertexAttrib4Niv",
        "glVertexAttrib4Nsv",
        "glVertexAttrib4Nub",
        "glVertexAttrib4Nubv",
        "glVertexAttrib4Nuiv",
        "glVertexAttrib4Nusv",
        "glVertexAttrib4bv",
        "glVertexAttrib4d",
        "glVertexAttrib4dv",
        "glVertexAttrib4iv",
        "glVertexAttrib4s",
        "glVertexAttrib4sv",
        "glVertexAttrib4ubv",
        "glVertexAttrib4uiv",
        "glVertexAttrib4usv",
        "glVertexAttribI1i",
        "glVertexAttribI2i",
        "glVertexAttribI3i",
        "glVertexAttribI4i",
        "glVertexAttribI1ui",
        "glVertexAttribI2ui",
        "glVertexAttribI3ui",
        "glVertexAttribI4ui",
        "glVertexAttribI1iv",
        "glVertexAttribI2iv",
        "glVertexAttribI3iv",
        "glVertexAttribI4iv",
        "glVertexAttribI1uiv",
        "glVertexAttribI2uiv",
        "glVertexAttribI3uiv",
        "glVertexAttribI4uiv",
        "glVertexAttribI4bv",
        "glVertexAttribI4sv",
        "glVertexAttribI4ubv",
        "glVertexAttribI4usv",
        "glVertexAttribIPointer"
    };
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLVERTEXATTRIBIPOINTERPROC VertexAttribIPointer;
    PFNGLVERTEXATTRIBI4IPROC VertexAttribI4i;
    PFNGLVERTEXATTRIBI4UIPROC VertexAttribI4ui;
    PFNGLVERTEXATTRIB4NUBVPROC VertexAttrib4Nubv;
    PFNGLGETVERTEXATTRIBIVPROC GetVertexAttribiv;
    PFNGLGETVERTEXATTRIBIIVPROC GetVertexAttribIiv;
    PFNGLGETVERTEXATTRIBIUIVPROC GetVertexAttribIuiv;
    GLshort Data[4] = {-32768, -1, 1, 32767};
    GLubyte Normalized[4] = {0, 64, 128, 255};
    GLint Signed[4];
    GLuint Unsigned[4];
    GLuint Buffer = 0;
    GLint Value;
    GLfloat Current[4];
    ULONG Index;
    LONG InitialFailures = Failures;

    for (Index = 0; Index < RTL_NUMBER_OF(RequiredProcedures); Index++)
    {
        Check(wglGetProcAddress(RequiredProcedures[Index]) != NULL,
              RequiredProcedures[Index]);
    }
    if (Failures != InitialFailures)
        return;

    GenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    BindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    BufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    DeleteBuffers = (PFNGLDELETEBUFFERSPROC)
        wglGetProcAddress("glDeleteBuffers");
    VertexAttribIPointer = (PFNGLVERTEXATTRIBIPOINTERPROC)
        wglGetProcAddress("glVertexAttribIPointer");
    VertexAttribI4i = (PFNGLVERTEXATTRIBI4IPROC)
        wglGetProcAddress("glVertexAttribI4i");
    VertexAttribI4ui = (PFNGLVERTEXATTRIBI4UIPROC)
        wglGetProcAddress("glVertexAttribI4ui");
    VertexAttrib4Nubv = (PFNGLVERTEXATTRIB4NUBVPROC)
        wglGetProcAddress("glVertexAttrib4Nubv");
    GetVertexAttribiv = (PFNGLGETVERTEXATTRIBIVPROC)
        wglGetProcAddress("glGetVertexAttribiv");
    GetVertexAttribIiv = (PFNGLGETVERTEXATTRIBIIVPROC)
        wglGetProcAddress("glGetVertexAttribIiv");
    GetVertexAttribIuiv = (PFNGLGETVERTEXATTRIBIUIVPROC)
        wglGetProcAddress("glGetVertexAttribIuiv");

    GenBuffers(1, &Buffer);
    BindBuffer(GL_ARRAY_BUFFER, Buffer);
    BufferData(GL_ARRAY_BUFFER, sizeof(Data), Data, GL_STATIC_DRAW);
    VertexAttribIPointer(3, 4, GL_SHORT, 0, NULL);
    GetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &Value);
    Check(Value == GL_TRUE, "integer_attribute_array_flag");
    GetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_TYPE, &Value);
    Check(Value == GL_SHORT, "integer_attribute_array_type");
    GetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_SIZE, &Value);
    Check(Value == 4, "integer_attribute_array_size");

    VertexAttribI4i(4, -7, 2, 99, -1024);
    GetVertexAttribIiv(4, GL_CURRENT_VERTEX_ATTRIB, Signed);
    Check(Signed[0] == -7 && Signed[1] == 2 &&
          Signed[2] == 99 && Signed[3] == -1024,
          "signed_integer_current_attribute");
    VertexAttribI4ui(4, 1, 0x80000000u, 17, 0xffffffffu);
    GetVertexAttribIuiv(4, GL_CURRENT_VERTEX_ATTRIB, Unsigned);
    Check(Unsigned[0] == 1 && Unsigned[1] == 0x80000000u &&
          Unsigned[2] == 17 && Unsigned[3] == 0xffffffffu,
          "unsigned_integer_current_attribute");

    VertexAttrib4Nubv(5, Normalized);
    glGetFloatv(GL_CURRENT_COLOR, Current);
    GetVertexAttribiv(5, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &Value);
    Check(Value == GL_FALSE, "float_attribute_array_flag");
    ((PFNGLGETVERTEXATTRIBFVPROC)wglGetProcAddress("glGetVertexAttribfv"))(
        5, GL_CURRENT_VERTEX_ATTRIB, Current);
    Check(Current[0] == 0.0f && Current[3] == 1.0f &&
          Current[1] > 0.250f && Current[1] < 0.252f &&
          Current[2] > 0.501f && Current[2] < 0.503f,
          "normalized_current_attribute");
    Check(glGetError() == GL_NO_ERROR, "integer_attribute_state_errors");

    DeleteBuffers(1, &Buffer);
    TestPrint("RPI5_GL3_INTEGER_VERTEX_%s\n",
              Failures == InitialFailures ? "PASS" : "FAIL");
}

static VOID
TestRemainingCoreContracts(VOID)
{
    static const PCSTR RequiredProcedures[] =
    {
        "glCompressedTexImage1D",
        "glCompressedTexImage2D",
        "glCompressedTexImage3D",
        "glCompressedTexSubImage1D",
        "glCompressedTexSubImage2D",
        "glCompressedTexSubImage3D",
        "glGetCompressedTexImage",
        "glGetStringi",
        "glGetTexParameterIiv",
        "glGetTexParameterIuiv",
        "glGetUniformfv",
        "glGetUniformiv",
        "glGetUniformuiv",
        "glMultiDrawArrays",
        "glMultiDrawElements",
        "glPointParameterf",
        "glPointParameterfv",
        "glPointParameteri",
        "glPointParameteriv",
        "glSampleCoverage",
        "glStencilFuncSeparate",
        "glStencilMaskSeparate",
        "glStencilOpSeparate",
        "glTexParameterIiv",
        "glTexParameterIuiv",
        "glUniform1fv",
        "glUniform1iv",
        "glUniform1ui",
        "glUniform1uiv",
        "glUniform2f",
        "glUniform2i",
        "glUniform2iv",
        "glUniform2ui",
        "glUniform2uiv",
        "glUniform3f",
        "glUniform3i",
        "glUniform3iv",
        "glUniform3ui",
        "glUniform3uiv",
        "glUniform4f",
        "glUniform4i",
        "glUniform4iv",
        "glUniform4ui",
        "glUniform4uiv",
        "glUniformMatrix2fv",
        "glUniformMatrix2x3fv",
        "glUniformMatrix2x4fv",
        "glUniformMatrix3x2fv",
        "glUniformMatrix3x4fv",
        "glUniformMatrix4x2fv",
        "glUniformMatrix4x3fv"
    };
    PFNGLGETSTRINGIPROC GetStringi;
    PFNGLSAMPLECOVERAGEPROC SampleCoverage;
    PFNGLPOINTPARAMETERFVPROC PointParameterfv;
    PFNGLPOINTPARAMETERIPROC PointParameteri;
    PFNGLTEXPARAMETERIIVPROC TexParameterIiv;
    PFNGLTEXPARAMETERIUIVPROC TexParameterIuiv;
    PFNGLGETTEXPARAMETERIIVPROC GetTexParameterIiv;
    PFNGLGETTEXPARAMETERIUIVPROC GetTexParameterIuiv;
    PFNGLCOMPRESSEDTEXIMAGE2DPROC CompressedTexImage2D;
    PFNGLGETCOMPRESSEDTEXIMAGEPROC GetCompressedTexImage;
    PFNGLSTENCILFUNCSEPARATEPROC StencilFuncSeparate;
    PFNGLSTENCILMASKSEPARATEPROC StencilMaskSeparate;
    PFNGLSTENCILOPSEPARATEPROC StencilOpSeparate;
    PFNGLMULTIDRAWARRAYSPROC MultiDrawArrays;
    PFNGLMULTIDRAWELEMENTSPROC MultiDrawElements;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
    PFNGLGETUNIFORMFVPROC GetUniformfv;
    const GLvoid *ElementIndices[2] = {NULL, NULL};
    const GLsizei Counts[2] = {0, 0};
    const GLint Firsts[2] = {0, 0};
    const GLint SignedBorder[4] = {-7, 2, 99, -1024};
    const GLuint UnsignedBorder[4] =
        {1, 0x80000000u, 17, 0xffffffffu};
    const GLint ZeroBorder[4] = {0, 0, 0, 0};
    GLint SignedResult[4] = {0, 0, 0, 0};
    GLuint UnsignedResult[4] = {0, 0, 0, 0};
    GLfloat Attenuation[3] = {1.0f, 2.0f, 3.0f};
    GLfloat FloatValues[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLfloat Matrix[16];
    GLuint Texture = 0;
    GLint Value;
    GLboolean BooleanValue;
    ULONG Index;
    LONG InitialFailures = Failures;

    for (Index = 0; Index < RTL_NUMBER_OF(RequiredProcedures); Index++)
    {
        Check(wglGetProcAddress(RequiredProcedures[Index]) != NULL,
              RequiredProcedures[Index]);
    }
    if (Failures != InitialFailures)
        return;

    GetStringi = (PFNGLGETSTRINGIPROC)wglGetProcAddress("glGetStringi");
    SampleCoverage = (PFNGLSAMPLECOVERAGEPROC)
        wglGetProcAddress("glSampleCoverage");
    PointParameterfv = (PFNGLPOINTPARAMETERFVPROC)
        wglGetProcAddress("glPointParameterfv");
    PointParameteri = (PFNGLPOINTPARAMETERIPROC)
        wglGetProcAddress("glPointParameteri");
    TexParameterIiv = (PFNGLTEXPARAMETERIIVPROC)
        wglGetProcAddress("glTexParameterIiv");
    TexParameterIuiv = (PFNGLTEXPARAMETERIUIVPROC)
        wglGetProcAddress("glTexParameterIuiv");
    GetTexParameterIiv = (PFNGLGETTEXPARAMETERIIVPROC)
        wglGetProcAddress("glGetTexParameterIiv");
    GetTexParameterIuiv = (PFNGLGETTEXPARAMETERIUIVPROC)
        wglGetProcAddress("glGetTexParameterIuiv");
    CompressedTexImage2D = (PFNGLCOMPRESSEDTEXIMAGE2DPROC)
        wglGetProcAddress("glCompressedTexImage2D");
    GetCompressedTexImage = (PFNGLGETCOMPRESSEDTEXIMAGEPROC)
        wglGetProcAddress("glGetCompressedTexImage");
    StencilFuncSeparate = (PFNGLSTENCILFUNCSEPARATEPROC)
        wglGetProcAddress("glStencilFuncSeparate");
    StencilMaskSeparate = (PFNGLSTENCILMASKSEPARATEPROC)
        wglGetProcAddress("glStencilMaskSeparate");
    StencilOpSeparate = (PFNGLSTENCILOPSEPARATEPROC)
        wglGetProcAddress("glStencilOpSeparate");
    MultiDrawArrays = (PFNGLMULTIDRAWARRAYSPROC)
        wglGetProcAddress("glMultiDrawArrays");
    MultiDrawElements = (PFNGLMULTIDRAWELEMENTSPROC)
        wglGetProcAddress("glMultiDrawElements");
    UniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)
        wglGetProcAddress("glUniformMatrix4fv");
    GetUniformfv = (PFNGLGETUNIFORMFVPROC)
        wglGetProcAddress("glGetUniformfv");

    glGetIntegerv(GL_NUM_EXTENSIONS, &Value);
    Check(Value > 0, "extension_count");
    for (Index = 0; Index < (ULONG)Value; Index++)
        Check(GetStringi(GL_EXTENSIONS, Index) != NULL, "extension_string");
    DrainErrors();
    Check(GetStringi(GL_VERSION, 0) == NULL, "reject_stringi_name_result");
    CheckError(GL_INVALID_ENUM, "reject_stringi_name");
    Check(GetStringi(GL_EXTENSIONS, (GLuint)Value) == NULL,
          "reject_stringi_index_result");
    CheckError(GL_INVALID_VALUE, "reject_stringi_index");

    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, FloatValues);
    Check(FloatValues[0] == 1.0f, "sample_coverage_default_value");
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &BooleanValue);
    Check(BooleanValue == GL_FALSE, "sample_coverage_default_invert");
    SampleCoverage(2.0f, GL_TRUE);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, FloatValues);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &BooleanValue);
    Check(FloatValues[0] == 1.0f && BooleanValue == GL_TRUE,
          "sample_coverage_clamp_and_invert");
    glEnable(GL_SAMPLE_COVERAGE);
    Check(glIsEnabled(GL_SAMPLE_COVERAGE), "sample_coverage_enable");
    glDisable(GL_SAMPLE_COVERAGE);

    glGetFloatv(GL_POINT_SIZE_MIN, FloatValues);
    Check(FloatValues[0] == 0.0f, "point_size_min_default");
    glGetFloatv(GL_POINT_SIZE_MAX, FloatValues);
    Check(FloatValues[0] == 1.0f, "point_size_max_default");
    PointParameterfv(GL_POINT_DISTANCE_ATTENUATION, Attenuation);
    ZeroMemory(FloatValues, sizeof(FloatValues));
    glGetFloatv(GL_POINT_DISTANCE_ATTENUATION, FloatValues);
    Check(FloatValues[0] == 1.0f && FloatValues[1] == 2.0f &&
          FloatValues[2] == 3.0f, "point_attenuation_round_trip");
    PointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT);
    glGetIntegerv(GL_POINT_SPRITE_COORD_ORIGIN, &Value);
    Check(Value == GL_LOWER_LEFT, "point_sprite_origin_round_trip");

    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    TexParameterIiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, SignedBorder);
    GetTexParameterIiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, SignedResult);
    Check(memcmp(SignedBorder, SignedResult, sizeof(SignedBorder)) == 0,
          "signed_integer_border_color");
    TexParameterIuiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, UnsignedBorder);
    GetTexParameterIuiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                        UnsignedResult);
    Check(memcmp(UnsignedBorder, UnsignedResult,
                 sizeof(UnsignedBorder)) == 0,
          "unsigned_integer_border_color");
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, ZeroBorder);
    FillMemory(SignedResult, sizeof(SignedResult), 0x5a);
    GetTexParameterIiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, SignedResult);
    Check(memcmp(ZeroBorder, SignedResult, sizeof(ZeroBorder)) == 0,
          "legacy_border_replaces_integer_border");
    Value = GL_CLAMP_TO_EDGE;
    TexParameterIiv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &Value);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &Value);
    Check(Value == GL_CLAMP_TO_EDGE, "integer_texture_scalar_parameter");

    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &Value);
    Check(Value == 0, "compressed_format_count");
    DrainErrors();
    CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA,
                         4, 4, 0, -1, NULL);
    CheckError(GL_INVALID_VALUE, "reject_negative_compressed_size");
    CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA,
                         4, 4, 0, 0, NULL);
    CheckError(GL_INVALID_ENUM, "reject_unavailable_compressed_format");
    GetCompressedTexImage(GL_TEXTURE_2D, 0, NULL);
    CheckError(GL_INVALID_OPERATION, "reject_uncompressed_readback");
    glDeleteTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, 0);
    TexParameterIuiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, UnsignedBorder);
    ZeroMemory(UnsignedResult, sizeof(UnsignedResult));
    GetTexParameterIuiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                        UnsignedResult);
    Check(memcmp(UnsignedBorder, UnsignedResult,
                 sizeof(UnsignedBorder)) == 0,
          "default_texture_integer_border_color");

    StencilFuncSeparate(GL_FRONT, GL_LESS, 3, 0x0f);
    StencilFuncSeparate(GL_BACK, GL_GREATER, 5, 0xf0);
    StencilMaskSeparate(GL_FRONT, 0xaa);
    StencilMaskSeparate(GL_BACK, 0x55);
    StencilOpSeparate(GL_FRONT, GL_REPLACE, GL_INCR, GL_DECR);
    StencilOpSeparate(GL_BACK, GL_INVERT, GL_KEEP, GL_INCR_WRAP);
    glGetIntegerv(GL_STENCIL_FUNC, &Value);
    Check(Value == GL_LESS, "front_stencil_function");
    glGetIntegerv(GL_STENCIL_REF, &Value);
    Check(Value == 3, "front_stencil_reference");
    glGetIntegerv(GL_STENCIL_WRITEMASK, &Value);
    Check(Value == 0xaa, "front_stencil_write_mask");
    glGetIntegerv(GL_STENCIL_BACK_FUNC, &Value);
    Check(Value == GL_GREATER, "back_stencil_function");
    glGetIntegerv(GL_STENCIL_BACK_REF, &Value);
    Check(Value == 5, "back_stencil_reference");
    glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &Value);
    Check(Value == 0x55, "back_stencil_write_mask");
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &Value);
    Check(Value == GL_INCR_WRAP, "back_stencil_depth_pass");
    glStencilFunc(GL_ALWAYS, 7, 0xff);
    glGetIntegerv(GL_STENCIL_BACK_FUNC, &Value);
    Check(Value == GL_ALWAYS, "legacy_stencil_updates_back_function");
    glGetIntegerv(GL_STENCIL_BACK_REF, &Value);
    Check(Value == 7, "legacy_stencil_updates_back_reference");

    for (Index = 0; Index < RTL_NUMBER_OF(Matrix); Index++)
        Matrix[Index] = (GLfloat)Index;
    UniformMatrix4fv(-1, 1, GL_TRUE, Matrix);
    Check(glGetError() == GL_NO_ERROR, "uniform_matrix_transpose_ignored");
    MultiDrawArrays(GL_TRIANGLES, Firsts, Counts, 2);
    MultiDrawElements(GL_TRIANGLES, Counts, GL_UNSIGNED_SHORT,
                      ElementIndices, 2);
    Check(glGetError() == GL_NO_ERROR, "zero_count_multi_draw");
    DrainErrors();
    GetUniformfv(0x7fffffffu, 0, FloatValues);
    CheckError(GL_INVALID_VALUE, "reject_unknown_uniform_program");

    TestPrint("RPI5_GL3_REMAINING_CORE_%s\n",
              Failures == InitialFailures ? "PASS" : "FAIL");
}

static VOID
TestSeparateStencilPixels(VOID)
{
    PFNGLSTENCILFUNCSEPARATEPROC StencilFuncSeparate;
    PFNGLSTENCILMASKSEPARATEPROC StencilMaskSeparate;
    PFNGLSTENCILOPSEPARATEPROC StencilOpSeparate;
    GLubyte FrontPixel[4] = {0, 0, 0, 0};
    GLubyte BackPixel[4] = {0, 0, 0, 0};
    GLubyte IncrementWrapPixel[4] = {0, 0, 0, 0};
    GLubyte DecrementWrapPixel[4] = {0, 0, 0, 0};
    LONG InitialFailures = Failures;

    StencilFuncSeparate = (PFNGLSTENCILFUNCSEPARATEPROC)
        wglGetProcAddress("glStencilFuncSeparate");
    StencilMaskSeparate = (PFNGLSTENCILMASKSEPARATEPROC)
        wglGetProcAddress("glStencilMaskSeparate");
    StencilOpSeparate = (PFNGLSTENCILOPSEPARATEPROC)
        wglGetProcAddress("glStencilOpSeparate");
    Check(StencilFuncSeparate && StencilMaskSeparate && StencilOpSeparate,
          "separate_stencil_pixel_procs");
    if (Failures != InitialFailures)
        return;

    DrainErrors();
    glViewport(0, 0, 64, 64);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    StencilMaskSeparate(GL_FRONT, 0xff);
    StencilMaskSeparate(GL_BACK, 0xff);
    StencilFuncSeparate(GL_FRONT, GL_ALWAYS, 0x11, 0xff);
    StencilFuncSeparate(GL_BACK, GL_ALWAYS, 0x22, 0xff);
    StencilOpSeparate(GL_FRONT, GL_REPLACE, GL_REPLACE, GL_REPLACE);
    StencilOpSeparate(GL_BACK, GL_REPLACE, GL_REPLACE, GL_REPLACE);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.8f);
    glVertex2f(-0.1f, -0.8f);
    glVertex2f(-0.5f, 0.8f);
    glVertex2f(0.1f, -0.8f);
    glVertex2f(0.5f, 0.8f);
    glVertex2f(0.9f, -0.8f);
    glEnd();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 0x11, 0xff);
    glColor4ub(255, 0, 0, 255);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    glStencilFunc(GL_EQUAL, 0x22, 0xff);
    glColor4ub(0, 255, 0, 255);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();

    glReadPixels(16, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, FrontPixel);
    glReadPixels(48, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, BackPixel);
    Check(FrontPixel[0] > 240 && FrontPixel[1] < 16 &&
          FrontPixel[2] < 16,
          "front_stencil_pixel");
    Check(BackPixel[0] < 16 && BackPixel[1] > 240 &&
          BackPixel[2] < 16,
          "back_stencil_pixel");

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(0xff);
    glClearStencil(0xff);
    glClear(GL_STENCIL_BUFFER_BIT);
    StencilFuncSeparate(GL_FRONT, GL_ALWAYS, 0, 0xff);
    StencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.8f);
    glVertex2f(-0.1f, -0.8f);
    glVertex2f(-0.5f, 0.8f);
    glEnd();
    glReadPixels(16, 24, 1, 1, GL_STENCIL_INDEX,
                 GL_UNSIGNED_BYTE, IncrementWrapPixel);
    Check(IncrementWrapPixel[0] == 0, "stencil_increment_wrap_pixel");

    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    StencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.8f);
    glVertex2f(-0.1f, -0.8f);
    glVertex2f(-0.5f, 0.8f);
    glEnd();
    glReadPixels(16, 24, 1, 1, GL_STENCIL_INDEX,
                 GL_UNSIGNED_BYTE, DecrementWrapPixel);
    Check(DecrementWrapPixel[0] == 0xff,
          "stencil_decrement_wrap_pixel");
    Check(glGetError() == GL_NO_ERROR, "separate_stencil_pixel_errors");

    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    TestPrint("RPI5_GL3_STENCIL_PIXELS_%s front=%u,%u,%u back=%u,%u,%u "
              "wrap=%u,%u\n",
              Failures == InitialFailures ? "PASS" : "FAIL",
              FrontPixel[0], FrontPixel[1], FrontPixel[2],
              BackPixel[0], BackPixel[1], BackPixel[2],
              IncrementWrapPixel[0], DecrementWrapPixel[0]);
}

int
main(VOID)
{
    HWND Window = NULL;
    HDC DeviceContext = NULL;
    HGLRC RenderingContext = NULL;

    TestPrint("RPI5_GL3_TEST_BEGIN\n");
    if (!CreateContext(&Window, &DeviceContext, &RenderingContext))
    {
        TestPrint("RPI5_GL3_TEST_FAIL create_context=%lu\n", GetLastError());
        Failures++;
        goto Cleanup;
    }
    TestPrint("RPI5_GL3_INFO version=%s renderer=%s\n",
              glGetString(GL_VERSION), glGetString(GL_RENDERER));
    if (GetEnvironmentVariableW(L"RPI5_GL3_RUN_SMOKE", NULL, 0) != 0)
    {
        TestMapBufferRange();
        TestIndexedBufferBindings();
        TestIndexedRasterAndClearState();
        TestCombinedClear();
        TestTransformFeedback();
        TestVertexArrays();
        TestIntegerVertexAttributes();
        TestCoreFramebufferObjects();
        TestRemainingCoreContracts();
        TestSeparateStencilPixels();
        TestPrint("RPI5_GL3_INCREMENTAL_%s failures=%ld\n",
                  Failures == 0 ? "PASS" : "FAIL", Failures);
    }
    TestGl30Conformance();

Cleanup:
    wglMakeCurrent(NULL, NULL);
    if (RenderingContext != NULL)
        wglDeleteContext(RenderingContext);
    if (DeviceContext != NULL && Window != NULL)
        ReleaseDC(Window, DeviceContext);
    if (Window != NULL)
        DestroyWindow(Window);
    TestPrint("RPI5_GL3_TEST_%s failures=%ld\n",
              Failures == 0 ? "PASS" : "FAIL", Failures);
    return Failures == 0 ? 0 : 1;
}
