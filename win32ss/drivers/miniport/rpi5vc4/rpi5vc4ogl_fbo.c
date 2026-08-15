/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded texture-framebuffer integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Framebuffer color attachments refer directly to Mesa texture objects. This
 * keeps texture upload, rendering, sampling, and readback on one authoritative
 * image instead of maintaining an ICD shadow copy. The extension remains
 * unadvertised until its complete renderbuffer and format contract exists.
 */

#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <context.h>
#include <hash.h>
#include <macros.h>

#include "rpi5vc4ogl_fbo.h"

#define RPI5VC4_OGL_FBO_MAX_FRAMEBUFFERS 16

typedef struct _RPI5VC4_OGL_FRAMEBUFFER
{
    GLuint Name;
    BOOL Object;
    GLuint ColorTexture;
    GLenum ColorTextureTarget;
    GLint ColorTextureLevel;
} RPI5VC4_OGL_FRAMEBUFFER, *PRPI5VC4_OGL_FRAMEBUFFER;

struct _RPI5VC4_OGL_FBO_STATE
{
    GLcontext *Mesa;
    GLuint NextName;
    GLuint CurrentFramebuffer;
    ULONG TargetGeneration;
    RPI5VC4_OGL_FRAMEBUFFER
        Framebuffers[RPI5VC4_OGL_FBO_MAX_FRAMEBUFFERS];
};

static VOID
Rpi5OglFboAdvanceGeneration(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State)
{
    State->TargetGeneration++;
    if (State->TargetGeneration == 0)
        State->TargetGeneration = 1;
}

static VOID
Rpi5OglFboError(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLenum Error,
    _In_z_ PCSTR Function)
{
    if (State != NULL && State->Mesa != NULL)
        gl_error(State->Mesa, Error, Function);
}

static BOOL
Rpi5OglFboCanChangeState(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_z_ PCSTR Function)
{
    if (State == NULL)
        return FALSE;
    if (INSIDE_BEGIN_END(State->Mesa))
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, Function);
        return FALSE;
    }
    return TRUE;
}

static PRPI5VC4_OGL_FRAMEBUFFER
Rpi5OglFboFind(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Framebuffers); Index++)
    {
        if (State->Framebuffers[Index].Name == Name)
            return &State->Framebuffers[Index];
    }
    return NULL;
}

static PRPI5VC4_OGL_FRAMEBUFFER
Rpi5OglFboAllocate(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(State->Framebuffers); Index++)
    {
        if (State->Framebuffers[Index].Name == 0)
        {
            State->Framebuffers[Index].Name = Name;
            return &State->Framebuffers[Index];
        }
    }
    return NULL;
}

static BOOL
Rpi5OglFboValidTarget(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLenum Target,
    _In_z_ PCSTR Function)
{
    if (Target == GL_FRAMEBUFFER_EXT)
        return TRUE;
    Rpi5OglFboError(State, GL_INVALID_ENUM, Function);
    return FALSE;
}

static struct gl_texture_object *
Rpi5OglFboTexture(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint Name)
{
    if (Name == 0)
        return NULL;
    return (struct gl_texture_object *)
        HashLookup(State->Mesa->Shared->TexObjects, Name);
}

static GLenum
Rpi5OglFboStatus(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ PRPI5VC4_OGL_FRAMEBUFFER Framebuffer)
{
    struct gl_texture_object *Texture;
    struct gl_texture_image *Image;

    if (Framebuffer->ColorTexture == 0)
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT;
    Texture = Rpi5OglFboTexture(State, Framebuffer->ColorTexture);
    if (Texture == NULL ||
        Texture->Dimensions !=
            (Framebuffer->ColorTextureTarget == GL_TEXTURE_1D ? 1u : 2u) ||
        Framebuffer->ColorTextureLevel < 0 ||
        Framebuffer->ColorTextureLevel >= MAX_TEXTURE_LEVELS)
    {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
    }
    Image = Texture->Image[Framebuffer->ColorTextureLevel];
    if (Image == NULL || Image->Data == NULL ||
        Image->Border != 0 || Image->Width == 0 || Image->Height == 0 ||
        (Image->Format != GL_RGB && Image->Format != GL_RGBA))
    {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
    }
    return GL_FRAMEBUFFER_COMPLETE_EXT;
}

static VOID APIENTRY
Rpi5OglBindFramebufferEXT(
    _In_ GLenum Target,
    _In_ GLuint Framebuffer)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;

    if (!Rpi5OglFboCanChangeState(State, "glBindFramebufferEXT") ||
        !Rpi5OglFboValidTarget(State, Target, "glBindFramebufferEXT(target)"))
    {
        return;
    }
    if (Framebuffer == 0)
    {
        State->CurrentFramebuffer = 0;
        Rpi5OglFboAdvanceGeneration(State);
        return;
    }

    Entry = Rpi5OglFboFind(State, Framebuffer);
    if (Entry == NULL)
        Entry = Rpi5OglFboAllocate(State, Framebuffer);
    if (Entry == NULL)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, "glBindFramebufferEXT");
        return;
    }
    Entry->Object = TRUE;
    if (Framebuffer >= State->NextName)
    {
        State->NextName = Framebuffer + 1;
        if (State->NextName == 0)
            State->NextName = 1;
    }
    State->CurrentFramebuffer = Framebuffer;
    Rpi5OglFboAdvanceGeneration(State);
}

static VOID APIENTRY
Rpi5OglDeleteFramebuffersEXT(
    _In_ GLsizei Count,
    _In_reads_(Count) const GLuint *Framebuffers)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;
    GLsizei Index;

    if (!Rpi5OglFboCanChangeState(State, "glDeleteFramebuffersEXT"))
        return;
    if (Count < 0)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glDeleteFramebuffersEXT(n)");
        return;
    }
    if (Count != 0 && Framebuffers == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glDeleteFramebuffersEXT(framebuffers)");
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Entry = Rpi5OglFboFind(State, Framebuffers[Index]);
        if (Entry == NULL)
            continue;
        if (State->CurrentFramebuffer == Entry->Name)
        {
            State->CurrentFramebuffer = 0;
            Rpi5OglFboAdvanceGeneration(State);
        }
        ZeroMemory(Entry, sizeof(*Entry));
    }
}

static VOID APIENTRY
Rpi5OglGenFramebuffersEXT(
    _In_ GLsizei Count,
    _Out_writes_(Count) GLuint *Framebuffers)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;
    ULONG Available = 0;
    ULONG Slot;
    ULONG Attempts;
    GLuint Name;
    GLsizei Index;

    if (!Rpi5OglFboCanChangeState(State, "glGenFramebuffersEXT"))
        return;
    if (Count < 0)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glGenFramebuffersEXT(n)");
        return;
    }
    if (Count != 0 && Framebuffers == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glGenFramebuffersEXT(framebuffers)");
        return;
    }
    for (Slot = 0; Slot < RTL_NUMBER_OF(State->Framebuffers); Slot++)
    {
        if (State->Framebuffers[Slot].Name == 0)
            Available++;
    }
    if ((ULONG)Count > Available)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glGenFramebuffersEXT");
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Name = 0;
        for (Attempts = 0;
             Attempts < RPI5VC4_OGL_FBO_MAX_FRAMEBUFFERS + 1;
             Attempts++)
        {
            Name = State->NextName++;
            if (Name != 0 && Rpi5OglFboFind(State, Name) == NULL)
                break;
            Name = 0;
        }
        if (Name == 0 || (Entry = Rpi5OglFboAllocate(State, Name)) == NULL)
        {
            Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                            "glGenFramebuffersEXT");
            return;
        }
        Framebuffers[Index] = Entry->Name;
    }
}

static GLboolean APIENTRY
Rpi5OglIsFramebufferEXT(
    _In_ GLuint Framebuffer)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;

    if (!Rpi5OglFboCanChangeState(State, "glIsFramebufferEXT"))
        return GL_FALSE;
    Entry = Rpi5OglFboFind(State, Framebuffer);
    return Entry != NULL && Entry->Object ? GL_TRUE : GL_FALSE;
}

static GLenum APIENTRY
Rpi5OglCheckFramebufferStatusEXT(
    _In_ GLenum Target)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;

    if (!Rpi5OglFboCanChangeState(State, "glCheckFramebufferStatusEXT") ||
        !Rpi5OglFboValidTarget(State, Target,
                               "glCheckFramebufferStatusEXT(target)"))
    {
        return 0;
    }
    if (State->CurrentFramebuffer == 0)
        return GL_FRAMEBUFFER_COMPLETE_EXT;
    Entry = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    if (Entry == NULL || !Entry->Object)
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT;
    return Rpi5OglFboStatus(State, Entry);
}

static VOID APIENTRY
Rpi5OglFramebufferTexture2DEXT(
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum TextureTarget,
    _In_ GLuint TextureName,
    _In_ GLint Level)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;
    struct gl_texture_object *Texture;

    if (!Rpi5OglFboCanChangeState(State, "glFramebufferTexture2DEXT") ||
        !Rpi5OglFboValidTarget(State, Target,
                               "glFramebufferTexture2DEXT(target)"))
    {
        return;
    }
    if (Attachment != GL_COLOR_ATTACHMENT0_EXT ||
        TextureTarget != GL_TEXTURE_2D)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glFramebufferTexture2DEXT(attachment/target)");
        return;
    }
    if (Level < 0 || Level >= MAX_TEXTURE_LEVELS)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glFramebufferTexture2DEXT(level)");
        return;
    }
    if (State->CurrentFramebuffer == 0 ||
        (Entry = Rpi5OglFboFind(State, State->CurrentFramebuffer)) == NULL ||
        !Entry->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glFramebufferTexture2DEXT(default)");
        return;
    }
    Texture = Rpi5OglFboTexture(State, TextureName);
    if (TextureName != 0 &&
        (Texture == NULL || Texture->Dimensions != 2))
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glFramebufferTexture2DEXT(texture)");
        return;
    }
    Entry->ColorTexture = TextureName;
    Entry->ColorTextureTarget = TextureName != 0 ? TextureTarget : 0;
    Entry->ColorTextureLevel = TextureName != 0 ? Level : 0;
    Rpi5OglFboAdvanceGeneration(State);
}

static VOID APIENTRY
Rpi5OglGetFramebufferAttachmentParameterivEXT(
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;

    if (!Rpi5OglFboCanChangeState(
            State, "glGetFramebufferAttachmentParameterivEXT") ||
        !Rpi5OglFboValidTarget(
            State, Target,
            "glGetFramebufferAttachmentParameterivEXT(target)"))
    {
        return;
    }
    if (Attachment != GL_COLOR_ATTACHMENT0_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glGetFramebufferAttachmentParameterivEXT(attachment)");
        return;
    }
    if (Parameters == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glGetFramebufferAttachmentParameterivEXT(params)");
        return;
    }
    if (State->CurrentFramebuffer == 0 ||
        (Entry = Rpi5OglFboFind(State, State->CurrentFramebuffer)) == NULL ||
        !Entry->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glGetFramebufferAttachmentParameterivEXT(default)");
        return;
    }

    switch (ParameterName)
    {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT:
            *Parameters = Entry->ColorTexture != 0 ? GL_TEXTURE : GL_NONE;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT:
            *Parameters = Entry->ColorTexture;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT:
            *Parameters = Entry->ColorTextureLevel;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT:
            *Parameters = 0;
            break;
        default:
            Rpi5OglFboError(
                State, GL_INVALID_ENUM,
                "glGetFramebufferAttachmentParameterivEXT(pname)");
            break;
    }
}

static BOOL
Rpi5OglFboReadTexel(
    _In_ const struct gl_texture_image *Image,
    _In_ ULONG Pixel,
    _Out_writes_(4) GLubyte Color[4])
{
    const GLubyte *Source;

    switch (Image->Format)
    {
        case GL_RGBA:
            Source = Image->Data + Pixel * 4;
            Color[0] = Source[0];
            Color[1] = Source[1];
            Color[2] = Source[2];
            Color[3] = Source[3];
            return TRUE;
        case GL_RGB:
            Source = Image->Data + Pixel * 3;
            Color[0] = Source[0];
            Color[1] = Source[1];
            Color[2] = Source[2];
            Color[3] = 255;
            return TRUE;
        case GL_ALPHA:
            Color[0] = 0;
            Color[1] = 0;
            Color[2] = 0;
            Color[3] = Image->Data[Pixel];
            return TRUE;
        case GL_LUMINANCE:
            Color[0] = Image->Data[Pixel];
            Color[1] = Color[0];
            Color[2] = Color[0];
            Color[3] = 255;
            return TRUE;
        case GL_LUMINANCE_ALPHA:
            Source = Image->Data + Pixel * 2;
            Color[0] = Source[0];
            Color[1] = Source[0];
            Color[2] = Source[0];
            Color[3] = Source[1];
            return TRUE;
        case GL_INTENSITY:
            Color[0] = Image->Data[Pixel];
            Color[1] = Color[0];
            Color[2] = Color[0];
            Color[3] = Color[0];
            return TRUE;
        default:
            return FALSE;
    }
}

static ULONG
Rpi5OglFboPackStride(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ ULONG Width,
    _In_ ULONG BytesPerPixel)
{
    ULONG RowLength;
    ULONG Alignment;
    ULONGLONG RowBytes;

    RowLength = State->Mesa->Pack.RowLength > 0 ?
                State->Mesa->Pack.RowLength : Width;
    Alignment = State->Mesa->Pack.Alignment > 0 ?
                State->Mesa->Pack.Alignment : 1;
    RowBytes = (ULONGLONG)RowLength * BytesPerPixel;
    if (RowBytes > 0xFFFFFFFFULL ||
        RowBytes + Alignment - 1 > 0xFFFFFFFFULL)
    {
        return 0;
    }
    return ((ULONG)RowBytes + Alignment - 1) & ~(Alignment - 1);
}

VOID APIENTRY
Rpi5OglFboGetTexImage(
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _Out_ GLvoid *Pixels)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    struct gl_texture_object *Texture;
    struct gl_texture_image *Image;
    GLubyte Components[4];
    GLubyte Ordered[4];
    GLubyte *Destination;
    ULONG ComponentCount;
    ULONG BytesPerPixel;
    ULONG Stride;
    ULONG Row;
    ULONG Column;
    ULONG Pixel;
    ULONG Word;

    if (!Rpi5OglFboCanChangeState(State, "glGetTexImage"))
        return;
    if (Target == GL_TEXTURE_1D)
        Texture = State->Mesa->Texture.Current1D;
    else if (Target == GL_TEXTURE_2D)
        Texture = State->Mesa->Texture.Current2D;
    else
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM, "glGetTexImage(target)");
        return;
    }
    if (Level < 0 || Level >= MAX_TEXTURE_LEVELS)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE, "glGetTexImage(level)");
        return;
    }
    Image = Texture != NULL ? Texture->Image[Level] : NULL;
    if (Image == NULL || Image->Data == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, "glGetTexImage(image)");
        return;
    }
    if (Pixels == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE, "glGetTexImage(pixels)");
        return;
    }

    switch (Format)
    {
        case GL_RGBA:
        case GL_BGRA_EXT:
            ComponentCount = 4;
            break;
        case GL_RGB:
        case GL_BGR_EXT:
            ComponentCount = 3;
            break;
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:
            ComponentCount = 1;
            break;
        case GL_LUMINANCE_ALPHA:
            ComponentCount = 2;
            break;
        default:
            Rpi5OglFboError(State, GL_INVALID_ENUM, "glGetTexImage(format)");
            return;
    }
    if (Type == GL_UNSIGNED_BYTE)
        BytesPerPixel = ComponentCount;
    else if ((Type == GL_UNSIGNED_INT_8_8_8_8 ||
              Type == GL_UNSIGNED_INT_8_8_8_8_REV) &&
             ComponentCount == 4)
        BytesPerPixel = sizeof(ULONG);
    else
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM, "glGetTexImage(type)");
        return;
    }

    Stride = Rpi5OglFboPackStride(State, Image->Width, BytesPerPixel);
    if (Stride == 0 || State->Mesa->Pack.SkipRows < 0 ||
        State->Mesa->Pack.SkipPixels < 0 ||
        (ULONGLONG)State->Mesa->Pack.SkipRows * Stride > 0xFFFFFFFFULL ||
        (ULONGLONG)State->Mesa->Pack.SkipPixels * BytesPerPixel >
            0xFFFFFFFFULL)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glGetTexImage(pack state)");
        return;
    }
    Destination = (GLubyte *)Pixels +
                  State->Mesa->Pack.SkipRows * Stride +
                  State->Mesa->Pack.SkipPixels * BytesPerPixel;

    for (Row = 0; Row < Image->Height; Row++)
    {
        for (Column = 0; Column < Image->Width; Column++)
        {
            Pixel = Row * Image->Width + Column;
            if (!Rpi5OglFboReadTexel(Image, Pixel, Components))
            {
                Rpi5OglFboError(State, GL_INVALID_OPERATION,
                                "glGetTexImage(internal format)");
                return;
            }
            switch (Format)
            {
                case GL_RGBA:
                    Ordered[0] = Components[0];
                    Ordered[1] = Components[1];
                    Ordered[2] = Components[2];
                    Ordered[3] = Components[3];
                    break;
                case GL_BGRA_EXT:
                    Ordered[0] = Components[2];
                    Ordered[1] = Components[1];
                    Ordered[2] = Components[0];
                    Ordered[3] = Components[3];
                    break;
                case GL_RGB:
                    Ordered[0] = Components[0];
                    Ordered[1] = Components[1];
                    Ordered[2] = Components[2];
                    break;
                case GL_BGR_EXT:
                    Ordered[0] = Components[2];
                    Ordered[1] = Components[1];
                    Ordered[2] = Components[0];
                    break;
                case GL_RED:
                    Ordered[0] = Components[0];
                    break;
                case GL_GREEN:
                    Ordered[0] = Components[1];
                    break;
                case GL_BLUE:
                    Ordered[0] = Components[2];
                    break;
                case GL_ALPHA:
                    Ordered[0] = Components[3];
                    break;
                case GL_LUMINANCE:
                    Ordered[0] = Components[0];
                    break;
                case GL_LUMINANCE_ALPHA:
                    Ordered[0] = Components[0];
                    Ordered[1] = Components[3];
                    break;
            }

            if (Type == GL_UNSIGNED_BYTE)
            {
                CopyMemory(Destination + Column * BytesPerPixel,
                           Ordered,
                           BytesPerPixel);
            }
            else if (Type == GL_UNSIGNED_INT_8_8_8_8_REV)
            {
                Word = (ULONG)Ordered[0] |
                       ((ULONG)Ordered[1] << 8) |
                       ((ULONG)Ordered[2] << 16) |
                       ((ULONG)Ordered[3] << 24);
                if (State->Mesa->Pack.SwapBytes)
                {
                    Word = (Word >> 24) |
                           ((Word >> 8) & 0x0000FF00) |
                           ((Word << 8) & 0x00FF0000) |
                           (Word << 24);
                }
                CopyMemory(Destination + Column * sizeof(Word),
                           &Word,
                           sizeof(Word));
            }
            else
            {
                Word = ((ULONG)Ordered[0] << 24) |
                       ((ULONG)Ordered[1] << 16) |
                       ((ULONG)Ordered[2] << 8) |
                       Ordered[3];
                if (State->Mesa->Pack.SwapBytes)
                {
                    Word = (Word >> 24) |
                           ((Word >> 8) & 0x0000FF00) |
                           ((Word << 8) & 0x00FF0000) |
                           (Word << 24);
                }
                CopyMemory(Destination + Column * sizeof(Word),
                           &Word,
                           sizeof(Word));
            }
        }
        Destination += Stride;
    }
}

typedef struct _RPI5VC4_OGL_FBO_PROC
{
    PCSTR Name;
    PROC Procedure;
} RPI5VC4_OGL_FBO_PROC, *PRPI5VC4_OGL_FBO_PROC;

static const RPI5VC4_OGL_FBO_PROC Rpi5OglFboProcedures[] =
{
    {"glBindFramebufferEXT", (PROC)Rpi5OglBindFramebufferEXT},
    {"glCheckFramebufferStatusEXT",
     (PROC)Rpi5OglCheckFramebufferStatusEXT},
    {"glDeleteFramebuffersEXT", (PROC)Rpi5OglDeleteFramebuffersEXT},
    {"glFramebufferTexture2DEXT",
     (PROC)Rpi5OglFramebufferTexture2DEXT},
    {"glGenFramebuffersEXT", (PROC)Rpi5OglGenFramebuffersEXT},
    {"glGetFramebufferAttachmentParameterivEXT",
     (PROC)Rpi5OglGetFramebufferAttachmentParameterivEXT},
    {"glIsFramebufferEXT", (PROC)Rpi5OglIsFramebufferEXT},
};

BOOL
Rpi5OglFboInitialize(
    _Outptr_ PRPI5VC4_OGL_FBO_STATE *State,
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_FBO_STATE NewState;

    *State = NULL;
    NewState = HeapAlloc(GetProcessHeap(),
                         HEAP_ZERO_MEMORY,
                         sizeof(*NewState));
    if (NewState == NULL)
        return FALSE;
    NewState->Mesa = Mesa;
    NewState->NextName = 1;
    NewState->TargetGeneration = 1;
    *State = NewState;
    return TRUE;
}

VOID
Rpi5OglFboCleanup(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State)
{
    if (State != NULL)
        HeapFree(GetProcessHeap(), 0, State);
}

GLuint
Rpi5OglFboCurrentName(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State)
{
    return State != NULL ? State->CurrentFramebuffer : 0;
}

BOOL
Rpi5OglFboGetColorTarget(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _Out_ PRPI5VC4_OGL_FBO_COLOR_TARGET Target)
{
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;
    struct gl_texture_object *Texture;
    struct gl_texture_image *Image;

    if (State == NULL || Target == NULL || State->CurrentFramebuffer == 0)
        return FALSE;
    Framebuffer = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    if (Framebuffer == NULL || !Framebuffer->Object ||
        Rpi5OglFboStatus(State, Framebuffer) != GL_FRAMEBUFFER_COMPLETE_EXT)
    {
        return FALSE;
    }
    Texture = Rpi5OglFboTexture(State, Framebuffer->ColorTexture);
    Image = Texture->Image[Framebuffer->ColorTextureLevel];
    Target->Data = Image->Data;
    Target->Width = Image->Width;
    Target->Height = Image->Height;
    Target->Format = Image->Format;
    Target->Framebuffer = Framebuffer->Name;
    Target->Generation = State->TargetGeneration;
    return TRUE;
}

VOID
Rpi5OglFboTextureChanged(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint TextureName)
{
    ULONG Index;

    if (State == NULL || TextureName == 0)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Framebuffers); Index++)
    {
        if (State->Framebuffers[Index].Name != 0 &&
            State->Framebuffers[Index].ColorTexture == TextureName)
        {
            Rpi5OglFboAdvanceGeneration(State);
            return;
        }
    }
}

VOID
Rpi5OglFboTextureDeleted(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint TextureName)
{
    ULONG Index;
    BOOL Changed = FALSE;

    if (State == NULL || TextureName == 0)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Framebuffers); Index++)
    {
        if (State->Framebuffers[Index].Name == 0 ||
            State->Framebuffers[Index].ColorTexture != TextureName)
        {
            continue;
        }
        State->Framebuffers[Index].ColorTexture = 0;
        State->Framebuffers[Index].ColorTextureTarget = 0;
        State->Framebuffers[Index].ColorTextureLevel = 0;
        Changed = TRUE;
    }
    if (Changed)
        Rpi5OglFboAdvanceGeneration(State);
}

PROC
Rpi5OglFboGetProcAddress(
    _In_z_ LPCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Rpi5OglFboProcedures); Index++)
    {
        if (lstrcmpA(Name, Rpi5OglFboProcedures[Index].Name) == 0)
            return Rpi5OglFboProcedures[Index].Procedure;
    }
    return NULL;
}
