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
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <context.h>
#include <hash.h>
#include <macros.h>
#include <teximage.h>
#include <texobj.h>

#include "rpi5vc4ogl_fbo.h"

#define RPI5VC4_OGL_FBO_MAX_FRAMEBUFFERS 16
#define RPI5VC4_OGL_FBO_MAX_RENDERBUFFERS 32
#define RPI5VC4_OGL_FBO_MAX_RENDERBUFFER_SIZE 1024

extern VOID APIENTRY
_mesa_TexImage1D(
    GLenum Target,
    GLint Level,
    GLint InternalFormat,
    GLsizei Width,
    GLint Border,
    GLenum Format,
    GLenum Type,
    const GLvoid *Pixels);

extern VOID APIENTRY
_mesa_TexImage2D(
    GLenum Target,
    GLint Level,
    GLint InternalFormat,
    GLsizei Width,
    GLsizei Height,
    GLint Border,
    GLenum Format,
    GLenum Type,
    const GLvoid *Pixels);

extern VOID APIENTRY
_mesa_TexSubImage2D(
    GLenum Target,
    GLint Level,
    GLint XOffset,
    GLint YOffset,
    GLsizei Width,
    GLsizei Height,
    GLenum Format,
    GLenum Type,
    const GLvoid *Pixels);

extern VOID APIENTRY
_mesa_DrawBuffer(
    GLenum Mode);

extern VOID APIENTRY
_mesa_GetIntegerv(
    GLenum ParameterName,
    GLint *Parameters);

extern VOID APIENTRY
_mesa_ReadBuffer(
    GLenum Mode);

typedef struct _RPI5VC4_OGL_RENDERBUFFER
{
    GLuint Name;
    GLuint ObjectName;
    BOOL Object;
    BOOL DeletePending;
    ULONG References;
    GLenum InternalFormat;
    ULONG Width;
    ULONG Height;
    ULONG RedBits;
    ULONG GreenBits;
    ULONG BlueBits;
    ULONG AlphaBits;
    ULONG DepthBits;
    ULONG StencilBits;
    GLubyte *Color;
    GLdepth *Depth;
    GLstencil *Stencil;
} RPI5VC4_OGL_RENDERBUFFER, *PRPI5VC4_OGL_RENDERBUFFER;

typedef struct _RPI5VC4_OGL_ATTACHMENT
{
    GLenum ObjectType;
    GLuint ObjectName;
    struct gl_texture_object *Texture;
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;
    GLenum TextureTarget;
    GLint TextureLevel;
    GLint TextureZOffset;
} RPI5VC4_OGL_ATTACHMENT, *PRPI5VC4_OGL_ATTACHMENT;

typedef struct _RPI5VC4_OGL_FRAMEBUFFER
{
    GLuint Name;
    BOOL Object;
    GLenum DrawBuffer;
    GLenum ReadBuffer;
    GLframebuffer MesaBuffer;
    RPI5VC4_OGL_ATTACHMENT Color;
    RPI5VC4_OGL_ATTACHMENT Depth;
    RPI5VC4_OGL_ATTACHMENT Stencil;
} RPI5VC4_OGL_FRAMEBUFFER, *PRPI5VC4_OGL_FRAMEBUFFER;

struct _RPI5VC4_OGL_FBO_STATE
{
    GLcontext *Mesa;
    GLframebuffer *DefaultBuffer;
    GLenum DefaultDrawBuffer;
    GLenum DefaultReadBuffer;
    GLuint NextFramebufferName;
    GLuint NextRenderbufferName;
    GLuint CurrentFramebuffer;
    GLuint CurrentRenderbuffer;
    ULONG TargetGeneration;
    RPI5VC4_OGL_FRAMEBUFFER
        Framebuffers[RPI5VC4_OGL_FBO_MAX_FRAMEBUFFERS];
    RPI5VC4_OGL_RENDERBUFFER
        Renderbuffers[RPI5VC4_OGL_FBO_MAX_RENDERBUFFERS];
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
Rpi5OglFboFlushBeforeBindingChange(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint Framebuffer)
{
    if (State->CurrentFramebuffer != Framebuffer &&
        State->Mesa->Driver.Flush != NULL)
    {
        (*State->Mesa->Driver.Flush)(State->Mesa);
    }
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
            State->Framebuffers[Index].DrawBuffer =
                GL_COLOR_ATTACHMENT0_EXT;
            State->Framebuffers[Index].ReadBuffer =
                GL_COLOR_ATTACHMENT0_EXT;
            State->Framebuffers[Index].MesaBuffer.Visual =
                State->Mesa->Visual;
            return &State->Framebuffers[Index];
        }
    }
    return NULL;
}

static PRPI5VC4_OGL_RENDERBUFFER
Rpi5OglFboFindRenderbuffer(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Renderbuffers); Index++)
    {
        if (State->Renderbuffers[Index].Name == Name)
            return &State->Renderbuffers[Index];
    }
    return NULL;
}

static PRPI5VC4_OGL_RENDERBUFFER
Rpi5OglFboAllocateRenderbuffer(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLuint Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(State->Renderbuffers); Index++)
    {
        if (State->Renderbuffers[Index].Name == 0 &&
            !State->Renderbuffers[Index].DeletePending &&
            State->Renderbuffers[Index].References == 0)
        {
            State->Renderbuffers[Index].Name = Name;
            State->Renderbuffers[Index].ObjectName = Name;
            return &State->Renderbuffers[Index];
        }
    }
    return NULL;
}

static VOID
Rpi5OglFboFreeRenderbufferStorage(
    _Inout_ PRPI5VC4_OGL_RENDERBUFFER Renderbuffer)
{
    if (Renderbuffer->Color != NULL)
        HeapFree(GetProcessHeap(), 0, Renderbuffer->Color);
    if (Renderbuffer->Depth != NULL)
        HeapFree(GetProcessHeap(), 0, Renderbuffer->Depth);
    if (Renderbuffer->Stencil != NULL)
        HeapFree(GetProcessHeap(), 0, Renderbuffer->Stencil);
    Renderbuffer->Color = NULL;
    Renderbuffer->Depth = NULL;
    Renderbuffer->Stencil = NULL;
}

static VOID
Rpi5OglFboResetRenderbuffer(
    _Inout_ PRPI5VC4_OGL_RENDERBUFFER Renderbuffer)
{
    Rpi5OglFboFreeRenderbufferStorage(Renderbuffer);
    ZeroMemory(Renderbuffer, sizeof(*Renderbuffer));
}

static PRPI5VC4_OGL_ATTACHMENT
Rpi5OglFboAttachment(
    _In_ PRPI5VC4_OGL_FRAMEBUFFER Framebuffer,
    _In_ GLenum Attachment)
{
    switch (Attachment)
    {
        case GL_COLOR_ATTACHMENT0_EXT:
            return &Framebuffer->Color;
        case GL_DEPTH_ATTACHMENT_EXT:
            return &Framebuffer->Depth;
        case GL_STENCIL_ATTACHMENT_EXT:
            return &Framebuffer->Stencil;
        default:
            return NULL;
    }
}

static VOID
Rpi5OglFboReleaseAttachment(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _Inout_ PRPI5VC4_OGL_ATTACHMENT Attachment,
    _In_ BOOL FreeDeletedTexture)
{
    struct gl_texture_object *Texture = Attachment->Texture;
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer = Attachment->Renderbuffer;

    ZeroMemory(Attachment, sizeof(*Attachment));
    if (Texture != NULL)
    {
        if (Texture->RefCount > 0)
            Texture->RefCount--;
        if (FreeDeletedTexture && Texture->RefCount == 0 &&
            Texture->Name == 0)
        {
            gl_free_texture_object(State->Mesa->Shared, Texture);
        }
    }
    if (Renderbuffer != NULL)
    {
        if (Renderbuffer->References > 0)
            Renderbuffer->References--;
        if (Renderbuffer->DeletePending && Renderbuffer->References == 0)
            Rpi5OglFboResetRenderbuffer(Renderbuffer);
    }
}

static VOID
Rpi5OglFboReleaseFramebuffer(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _Inout_ PRPI5VC4_OGL_FRAMEBUFFER Framebuffer)
{
    Rpi5OglFboReleaseAttachment(State, &Framebuffer->Color, TRUE);
    Rpi5OglFboReleaseAttachment(State, &Framebuffer->Depth, TRUE);
    Rpi5OglFboReleaseAttachment(State, &Framebuffer->Stencil, TRUE);
    ZeroMemory(Framebuffer, sizeof(*Framebuffer));
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

static BOOL
Rpi5OglFboAttachmentInfo(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ const RPI5VC4_OGL_ATTACHMENT *Attachment,
    _In_ GLenum AttachmentPoint,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    const struct gl_texture_object *Texture;
    const struct gl_texture_image *Image;
    const RPI5VC4_OGL_RENDERBUFFER *Renderbuffer;
    ULONG Dimensions;

    UNREFERENCED_PARAMETER(State);
    if (Attachment->ObjectType == GL_TEXTURE)
    {
        Texture = Attachment->Texture;
        if (Texture == NULL || Attachment->TextureLevel < 0 ||
            Attachment->TextureLevel >= MAX_TEXTURE_LEVELS)
        {
            return FALSE;
        }
        if (Attachment->TextureTarget == GL_TEXTURE_1D)
            Dimensions = 1;
        else if (Attachment->TextureTarget == GL_TEXTURE_2D)
            Dimensions = 2;
        else if (Attachment->TextureTarget == GL_TEXTURE_3D)
            Dimensions = 3;
        else
            return FALSE;
        if (Texture->Dimensions != Dimensions)
            return FALSE;
        Image = Texture->Image[Attachment->TextureLevel];
        if (Image == NULL || Image->Data == NULL || Image->Border != 0 ||
            Image->Width == 0 || Image->Height == 0)
        {
            return FALSE;
        }
        if ((AttachmentPoint == GL_COLOR_ATTACHMENT0_EXT &&
             Image->Format != GL_RGB && Image->Format != GL_RGBA) ||
            (AttachmentPoint == GL_DEPTH_ATTACHMENT_EXT &&
             Image->Format != GL_DEPTH_COMPONENT) ||
            (AttachmentPoint != GL_COLOR_ATTACHMENT0_EXT &&
             AttachmentPoint != GL_DEPTH_ATTACHMENT_EXT))
        {
            return FALSE;
        }
        *Width = Image->Width;
        *Height = Dimensions == 1 ? 1 : Image->Height;
        return TRUE;
    }

    if (Attachment->ObjectType != GL_RENDERBUFFER_EXT)
        return FALSE;
    Renderbuffer = Attachment->Renderbuffer;
    if (Renderbuffer == NULL || Renderbuffer->InternalFormat == 0 ||
        Renderbuffer->Width == 0 || Renderbuffer->Height == 0)
    {
        return FALSE;
    }
    if ((AttachmentPoint == GL_COLOR_ATTACHMENT0_EXT &&
         Renderbuffer->Color == NULL) ||
        (AttachmentPoint == GL_DEPTH_ATTACHMENT_EXT &&
         Renderbuffer->Depth == NULL) ||
        (AttachmentPoint == GL_STENCIL_ATTACHMENT_EXT &&
         Renderbuffer->Stencil == NULL))
    {
        return FALSE;
    }
    *Width = Renderbuffer->Width;
    *Height = Renderbuffer->Height;
    return TRUE;
}

static GLenum
Rpi5OglFboEffectiveDrawBuffer(
    _In_ const RPI5VC4_OGL_FRAMEBUFFER *Framebuffer)
{
    return Framebuffer->Color.ObjectType == GL_NONE ?
           GL_NONE : Framebuffer->DrawBuffer;
}

static GLenum
Rpi5OglFboEffectiveReadBuffer(
    _In_ const RPI5VC4_OGL_FRAMEBUFFER *Framebuffer)
{
    return Framebuffer->Color.ObjectType == GL_NONE ?
           GL_NONE : Framebuffer->ReadBuffer;
}

static GLenum
Rpi5OglFboStatus(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ PRPI5VC4_OGL_FRAMEBUFFER Framebuffer)
{
    const RPI5VC4_OGL_ATTACHMENT *Attachments[3];
    const GLenum AttachmentPoints[3] =
    {
        GL_COLOR_ATTACHMENT0_EXT,
        GL_DEPTH_ATTACHMENT_EXT,
        GL_STENCIL_ATTACHMENT_EXT
    };
    ULONG Width = 0;
    ULONG Height = 0;
    ULONG AttachmentWidth;
    ULONG AttachmentHeight;
    ULONG Index;
    BOOL Found = FALSE;

    Attachments[0] = &Framebuffer->Color;
    Attachments[1] = &Framebuffer->Depth;
    Attachments[2] = &Framebuffer->Stencil;
    for (Index = 0; Index < RTL_NUMBER_OF(Attachments); Index++)
    {
        if (Attachments[Index]->ObjectType == GL_NONE)
            continue;
        Found = TRUE;
        if (!Rpi5OglFboAttachmentInfo(State, Attachments[Index],
                                      AttachmentPoints[Index],
                                      &AttachmentWidth,
                                      &AttachmentHeight))
        {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
        }
        if (Width == 0)
        {
            Width = AttachmentWidth;
            Height = AttachmentHeight;
        }
        else if (Width != AttachmentWidth || Height != AttachmentHeight)
        {
            return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT;
        }
    }
    if (!Found)
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT;
    if (Rpi5OglFboEffectiveDrawBuffer(Framebuffer) != GL_NONE &&
        (Rpi5OglFboEffectiveDrawBuffer(Framebuffer) !=
             GL_COLOR_ATTACHMENT0_EXT ||
         Framebuffer->Color.ObjectType == GL_NONE))
    {
        return GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT;
    }
    if (Rpi5OglFboEffectiveReadBuffer(Framebuffer) != GL_NONE &&
        (Rpi5OglFboEffectiveReadBuffer(Framebuffer) !=
             GL_COLOR_ATTACHMENT0_EXT ||
         Framebuffer->Color.ObjectType == GL_NONE))
    {
        return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT;
    }
    return GL_FRAMEBUFFER_COMPLETE_EXT;
}

static VOID
Rpi5OglFboApplyBinding(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State)
{
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;
    ULONG Width = 0;
    ULONG Height = 0;

    if (State->CurrentFramebuffer == 0 ||
        (Framebuffer = Rpi5OglFboFind(State,
                                      State->CurrentFramebuffer)) == NULL)
    {
        State->CurrentFramebuffer = 0;
        State->Mesa->Buffer = State->DefaultBuffer;
        State->Mesa->Color.DrawBuffer = State->DefaultDrawBuffer;
        State->Mesa->Pixel.ReadBuffer = State->DefaultReadBuffer;
        if (State->Mesa->Driver.SetBuffer != NULL &&
            State->DefaultDrawBuffer != GL_NONE)
        {
            (*State->Mesa->Driver.SetBuffer)(
                State->Mesa,
                State->DefaultDrawBuffer == GL_BACK ||
                State->DefaultDrawBuffer == GL_BACK_LEFT ?
                    GL_BACK : GL_FRONT);
        }
        if (State->Mesa->Driver.GetBufferSize != NULL)
            gl_ResizeBuffersMESA(State->Mesa);
        else
            State->Mesa->NewState |= NEW_ALL;
        return;
    }

    if (!Rpi5OglFboAttachmentInfo(State, &Framebuffer->Color,
                                  GL_COLOR_ATTACHMENT0_EXT,
                                  &Width, &Height) &&
        !Rpi5OglFboAttachmentInfo(State, &Framebuffer->Depth,
                                  GL_DEPTH_ATTACHMENT_EXT,
                                  &Width, &Height))
    {
        (void)Rpi5OglFboAttachmentInfo(State, &Framebuffer->Stencil,
                                       GL_STENCIL_ATTACHMENT_EXT,
                                       &Width, &Height);
    }
    Framebuffer->MesaBuffer.Visual = State->Mesa->Visual;
    Framebuffer->MesaBuffer.Width = Width;
    Framebuffer->MesaBuffer.Height = Height;
    Framebuffer->MesaBuffer.Xmin = 0;
    Framebuffer->MesaBuffer.Ymin = 0;
    Framebuffer->MesaBuffer.Xmax = Width != 0 ? (GLint)Width - 1 : -1;
    Framebuffer->MesaBuffer.Ymax = Height != 0 ? (GLint)Height - 1 : -1;
    Framebuffer->MesaBuffer.Depth = NULL;
    Framebuffer->MesaBuffer.Stencil = NULL;
    if (Framebuffer->Depth.ObjectType == GL_RENDERBUFFER_EXT &&
        (Renderbuffer = Framebuffer->Depth.Renderbuffer) != NULL)
    {
        Framebuffer->MesaBuffer.Depth = Renderbuffer->Depth;
    }
    else if (Framebuffer->Depth.ObjectType == GL_TEXTURE &&
             Framebuffer->Depth.Texture != NULL)
    {
        struct gl_texture_image *Image =
            Framebuffer->Depth.Texture->Image[
                Framebuffer->Depth.TextureLevel];

        if (Image != NULL && Image->Format == GL_DEPTH_COMPONENT)
            Framebuffer->MesaBuffer.Depth = (GLdepth *)Image->Data;
    }
    if (Framebuffer->Stencil.ObjectType == GL_RENDERBUFFER_EXT &&
        (Renderbuffer = Framebuffer->Stencil.Renderbuffer) != NULL)
    {
        Framebuffer->MesaBuffer.Stencil = Renderbuffer->Stencil;
    }
    State->Mesa->Buffer = &Framebuffer->MesaBuffer;
    State->Mesa->Color.DrawBuffer =
        Rpi5OglFboEffectiveDrawBuffer(Framebuffer);
    State->Mesa->Pixel.ReadBuffer =
        Rpi5OglFboEffectiveReadBuffer(Framebuffer);
    if (State->Mesa->Driver.SetBuffer != NULL)
    {
        (*State->Mesa->Driver.SetBuffer)(
            State->Mesa,
            Rpi5OglFboEffectiveDrawBuffer(Framebuffer));
    }
    State->Mesa->NewState |= NEW_ALL;
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
        Rpi5OglFboFlushBeforeBindingChange(State, Framebuffer);
        State->CurrentFramebuffer = 0;
        Rpi5OglFboAdvanceGeneration(State);
        Rpi5OglFboApplyBinding(State);
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
    if (Framebuffer >= State->NextFramebufferName)
    {
        State->NextFramebufferName = Framebuffer + 1;
        if (State->NextFramebufferName == 0)
            State->NextFramebufferName = 1;
    }
    Rpi5OglFboFlushBeforeBindingChange(State, Framebuffer);
    State->CurrentFramebuffer = Framebuffer;
    Rpi5OglFboAdvanceGeneration(State);
    Rpi5OglFboApplyBinding(State);
}

static VOID APIENTRY
Rpi5OglDeleteFramebuffersEXT(
    _In_ GLsizei Count,
    _In_reads_(Count) const GLuint *Framebuffers)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;
    GLsizei Index;
    BOOL BindingChanged = FALSE;

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
            Rpi5OglFboFlushBeforeBindingChange(State, 0);
            State->CurrentFramebuffer = 0;
            Rpi5OglFboAdvanceGeneration(State);
            BindingChanged = TRUE;
        }
        Rpi5OglFboReleaseFramebuffer(State, Entry);
    }
    if (BindingChanged)
        Rpi5OglFboApplyBinding(State);
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
            Name = State->NextFramebufferName++;
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

static VOID
Rpi5OglFboSetTextureAttachment(
    _In_ PCSTR Function,
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum TextureTarget,
    _In_ GLuint TextureName,
    _In_ GLint Level,
    _In_ GLint ZOffset,
    _In_ ULONG Dimensions)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;
    PRPI5VC4_OGL_ATTACHMENT Destination;
    struct gl_texture_object *Texture;

    if (!Rpi5OglFboCanChangeState(State, Function) ||
        !Rpi5OglFboValidTarget(State, Target, Function))
    {
        return;
    }
    if (Attachment != GL_COLOR_ATTACHMENT0_EXT &&
        Attachment != GL_DEPTH_ATTACHMENT_EXT &&
        Attachment != GL_STENCIL_ATTACHMENT_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM, Function);
        return;
    }
    if ((Dimensions == 1 && TextureTarget != GL_TEXTURE_1D) ||
        (Dimensions == 2 && TextureTarget != GL_TEXTURE_2D) ||
        (Dimensions == 3 && TextureTarget != GL_TEXTURE_3D))
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM, Function);
        return;
    }
    if (State->CurrentFramebuffer == 0 ||
        (Entry = Rpi5OglFboFind(State, State->CurrentFramebuffer)) == NULL ||
        !Entry->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, Function);
        return;
    }
    Destination = Rpi5OglFboAttachment(Entry, Attachment);
    if (Level < 0 || Level >= MAX_TEXTURE_LEVELS || ZOffset < 0)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE, Function);
        return;
    }
    Texture = Rpi5OglFboTexture(State, TextureName);
    if (TextureName != 0 &&
        (Texture == NULL || Texture->Dimensions != Dimensions))
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, Function);
        return;
    }
    Rpi5OglFboReleaseAttachment(State, Destination, TRUE);
    if (TextureName != 0)
    {
        Destination->ObjectType = GL_TEXTURE;
        Destination->ObjectName = TextureName;
        Destination->Texture = Texture;
        Destination->TextureTarget = TextureTarget;
        Destination->TextureLevel = Level;
        Destination->TextureZOffset = ZOffset;
        Texture->RefCount++;
    }
    Rpi5OglFboAdvanceGeneration(State);
    Rpi5OglFboApplyBinding(State);
}

static VOID APIENTRY
Rpi5OglFramebufferTexture1DEXT(
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum TextureTarget,
    _In_ GLuint TextureName,
    _In_ GLint Level)
{
    Rpi5OglFboSetTextureAttachment("glFramebufferTexture1DEXT",
                                   Target, Attachment, TextureTarget,
                                   TextureName, Level, 0, 1);
}

static VOID APIENTRY
Rpi5OglFramebufferTexture2DEXT(
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum TextureTarget,
    _In_ GLuint TextureName,
    _In_ GLint Level)
{
    Rpi5OglFboSetTextureAttachment("glFramebufferTexture2DEXT",
                                   Target, Attachment, TextureTarget,
                                   TextureName, Level, 0, 2);
}

static VOID APIENTRY
Rpi5OglFramebufferTexture3DEXT(
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum TextureTarget,
    _In_ GLuint TextureName,
    _In_ GLint Level,
    _In_ GLint ZOffset)
{
    Rpi5OglFboSetTextureAttachment("glFramebufferTexture3DEXT",
                                   Target, Attachment, TextureTarget,
                                   TextureName, Level, ZOffset, 3);
}

static VOID APIENTRY
Rpi5OglFramebufferRenderbufferEXT(
    _In_ GLenum Target,
    _In_ GLenum Attachment,
    _In_ GLenum RenderbufferTarget,
    _In_ GLuint RenderbufferName)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Entry;
    PRPI5VC4_OGL_ATTACHMENT Destination;
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;

    if (!Rpi5OglFboCanChangeState(State,
                                  "glFramebufferRenderbufferEXT") ||
        !Rpi5OglFboValidTarget(State, Target,
                               "glFramebufferRenderbufferEXT(target)"))
    {
        return;
    }
    if (RenderbufferTarget != GL_RENDERBUFFER_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glFramebufferRenderbufferEXT(target)");
        return;
    }
    if (State->CurrentFramebuffer == 0 ||
        (Entry = Rpi5OglFboFind(State, State->CurrentFramebuffer)) == NULL ||
        !Entry->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glFramebufferRenderbufferEXT(default)");
        return;
    }
    Destination = Rpi5OglFboAttachment(Entry, Attachment);
    if (Destination == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glFramebufferRenderbufferEXT(attachment)");
        return;
    }
    Renderbuffer = Rpi5OglFboFindRenderbuffer(State, RenderbufferName);
    if (RenderbufferName != 0 &&
        (Renderbuffer == NULL || !Renderbuffer->Object))
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glFramebufferRenderbufferEXT(renderbuffer)");
        return;
    }
    Rpi5OglFboReleaseAttachment(State, Destination, TRUE);
    if (RenderbufferName != 0)
    {
        Destination->ObjectType = GL_RENDERBUFFER_EXT;
        Destination->ObjectName = RenderbufferName;
        Destination->Renderbuffer = Renderbuffer;
        Renderbuffer->References++;
    }
    Rpi5OglFboAdvanceGeneration(State);
    Rpi5OglFboApplyBinding(State);
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
    PRPI5VC4_OGL_ATTACHMENT Source;

    if (!Rpi5OglFboCanChangeState(
            State, "glGetFramebufferAttachmentParameterivEXT") ||
        !Rpi5OglFboValidTarget(
            State, Target,
            "glGetFramebufferAttachmentParameterivEXT(target)"))
    {
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
    Source = Rpi5OglFboAttachment(Entry, Attachment);
    if (Source == NULL)
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
    switch (ParameterName)
    {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT:
            *Parameters = Source->ObjectType;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT:
            *Parameters = Source->ObjectName;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT:
            if (Source->ObjectType != GL_TEXTURE)
                goto TextureParameterError;
            *Parameters = Source->TextureLevel;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT:
            if (Source->ObjectType != GL_TEXTURE)
                goto TextureParameterError;
            *Parameters = 0;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT:
            if (Source->ObjectType != GL_TEXTURE)
                goto TextureParameterError;
            *Parameters = Source->TextureZOffset;
            break;
        default:
            Rpi5OglFboError(
                State, GL_INVALID_ENUM,
                "glGetFramebufferAttachmentParameterivEXT(pname)");
            break;
    }
    return;

TextureParameterError:
    Rpi5OglFboError(State, GL_INVALID_ENUM,
                    "glGetFramebufferAttachmentParameterivEXT(texture)");
}

static BOOL
Rpi5OglFboValidRenderbufferTarget(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLenum Target,
    _In_z_ PCSTR Function)
{
    if (Target == GL_RENDERBUFFER_EXT)
        return TRUE;
    Rpi5OglFboError(State, GL_INVALID_ENUM, Function);
    return FALSE;
}

static VOID APIENTRY
Rpi5OglBindRenderbufferEXT(
    _In_ GLenum Target,
    _In_ GLuint RenderbufferName)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;

    if (!Rpi5OglFboCanChangeState(State, "glBindRenderbufferEXT") ||
        !Rpi5OglFboValidRenderbufferTarget(
            State, Target, "glBindRenderbufferEXT(target)"))
    {
        return;
    }
    if (RenderbufferName == 0)
    {
        State->CurrentRenderbuffer = 0;
        return;
    }
    Renderbuffer = Rpi5OglFboFindRenderbuffer(State, RenderbufferName);
    if (Renderbuffer == NULL)
    {
        Renderbuffer = Rpi5OglFboAllocateRenderbuffer(State,
                                                       RenderbufferName);
    }
    if (Renderbuffer == NULL)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glBindRenderbufferEXT");
        return;
    }
    Renderbuffer->Object = TRUE;
    if (RenderbufferName >= State->NextRenderbufferName)
    {
        State->NextRenderbufferName = RenderbufferName + 1;
        if (State->NextRenderbufferName == 0)
            State->NextRenderbufferName = 1;
    }
    State->CurrentRenderbuffer = RenderbufferName;
}

static VOID APIENTRY
Rpi5OglDeleteRenderbuffersEXT(
    _In_ GLsizei Count,
    _In_reads_(Count) const GLuint *Renderbuffers)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;
    PRPI5VC4_OGL_ATTACHMENT Attachments[3];
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;
    GLsizei Index;
    ULONG AttachmentIndex;
    BOOL Changed;

    if (!Rpi5OglFboCanChangeState(State, "glDeleteRenderbuffersEXT"))
        return;
    if (Count < 0)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glDeleteRenderbuffersEXT(n)");
        return;
    }
    if (Count != 0 && Renderbuffers == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glDeleteRenderbuffersEXT(renderbuffers)");
        return;
    }

    Changed = FALSE;
    Framebuffer = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    if (Framebuffer != NULL)
    {
        Attachments[0] = &Framebuffer->Color;
        Attachments[1] = &Framebuffer->Depth;
        Attachments[2] = &Framebuffer->Stencil;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Renderbuffer = Rpi5OglFboFindRenderbuffer(State,
                                                  Renderbuffers[Index]);
        if (Renderbuffer == NULL)
            continue;
        if (State->CurrentRenderbuffer == Renderbuffer->Name)
            State->CurrentRenderbuffer = 0;
        Renderbuffer->Name = 0;
        Renderbuffer->Object = FALSE;
        Renderbuffer->DeletePending = TRUE;
        if (Framebuffer != NULL)
        {
            for (AttachmentIndex = 0;
                 AttachmentIndex < RTL_NUMBER_OF(Attachments);
                 AttachmentIndex++)
            {
                if (Attachments[AttachmentIndex]->Renderbuffer ==
                    Renderbuffer)
                {
                    Rpi5OglFboReleaseAttachment(
                        State, Attachments[AttachmentIndex], TRUE);
                    Changed = TRUE;
                }
            }
        }
        if (Renderbuffer->References == 0 &&
            Renderbuffer->DeletePending)
        {
            Rpi5OglFboResetRenderbuffer(Renderbuffer);
        }
    }
    if (Changed)
    {
        Rpi5OglFboAdvanceGeneration(State);
        Rpi5OglFboApplyBinding(State);
    }
}

static VOID APIENTRY
Rpi5OglGenRenderbuffersEXT(
    _In_ GLsizei Count,
    _Out_writes_(Count) GLuint *Renderbuffers)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_RENDERBUFFER Entry;
    ULONG Available = 0;
    ULONG Slot;
    ULONG Attempts;
    GLuint Name;
    GLsizei Index;

    if (!Rpi5OglFboCanChangeState(State, "glGenRenderbuffersEXT"))
        return;
    if (Count < 0)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glGenRenderbuffersEXT(n)");
        return;
    }
    if (Count != 0 && Renderbuffers == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glGenRenderbuffersEXT(renderbuffers)");
        return;
    }
    for (Slot = 0; Slot < RTL_NUMBER_OF(State->Renderbuffers); Slot++)
    {
        if (State->Renderbuffers[Slot].Name == 0 &&
            !State->Renderbuffers[Slot].DeletePending &&
            State->Renderbuffers[Slot].References == 0)
        {
            Available++;
        }
    }
    if ((ULONG)Count > Available)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glGenRenderbuffersEXT");
        return;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Name = 0;
        for (Attempts = 0;
             Attempts < RPI5VC4_OGL_FBO_MAX_RENDERBUFFERS + 1;
             Attempts++)
        {
            Name = State->NextRenderbufferName++;
            if (Name != 0 &&
                Rpi5OglFboFindRenderbuffer(State, Name) == NULL)
            {
                break;
            }
            Name = 0;
        }
        Entry = Name != 0 ?
            Rpi5OglFboAllocateRenderbuffer(State, Name) : NULL;
        if (Entry == NULL)
        {
            Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                            "glGenRenderbuffersEXT");
            return;
        }
        Renderbuffers[Index] = Entry->Name;
    }
}

static GLboolean APIENTRY
Rpi5OglIsRenderbufferEXT(
    _In_ GLuint RenderbufferName)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;

    if (!Rpi5OglFboCanChangeState(State, "glIsRenderbufferEXT"))
        return GL_FALSE;
    Renderbuffer = Rpi5OglFboFindRenderbuffer(State, RenderbufferName);
    return Renderbuffer != NULL && Renderbuffer->Object ?
           GL_TRUE : GL_FALSE;
}

static BOOL
Rpi5OglFboRenderbufferFormat(
    _In_ GLenum InternalFormat,
    _Out_ PBOOL Color,
    _Out_ PBOOL Depth,
    _Out_ PBOOL Stencil,
    _Out_writes_(6) PULONG Bits)
{
    *Color = FALSE;
    *Depth = FALSE;
    *Stencil = FALSE;
    ZeroMemory(Bits, 6 * sizeof(*Bits));
    switch (InternalFormat)
    {
        case GL_RGB:
        case GL_RGB8:
            *Color = TRUE;
            Bits[0] = Bits[1] = Bits[2] = 8;
            return TRUE;
        case GL_RGB5:
            *Color = TRUE;
            Bits[0] = Bits[1] = Bits[2] = 5;
            return TRUE;
        case GL_RGB565:
            *Color = TRUE;
            Bits[0] = 5;
            Bits[1] = 6;
            Bits[2] = 5;
            return TRUE;
        case GL_RGBA:
        case GL_RGBA8:
            *Color = TRUE;
            Bits[0] = Bits[1] = Bits[2] = Bits[3] = 8;
            return TRUE;
        case GL_RGBA4:
            *Color = TRUE;
            Bits[0] = Bits[1] = Bits[2] = Bits[3] = 4;
            return TRUE;
        case GL_RGB5_A1:
            *Color = TRUE;
            Bits[0] = Bits[1] = Bits[2] = 5;
            Bits[3] = 1;
            return TRUE;
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_COMPONENT24:
            *Depth = TRUE;
            Bits[4] = 24;
            return TRUE;
        case GL_DEPTH_COMPONENT16:
            *Depth = TRUE;
            Bits[4] = 16;
            return TRUE;
        case GL_DEPTH_COMPONENT32:
            *Depth = TRUE;
            Bits[4] = 32;
            return TRUE;
        case GL_STENCIL_INDEX:
        case GL_STENCIL_INDEX8_EXT:
            *Stencil = TRUE;
            Bits[5] = 8;
            return TRUE;
        case GL_DEPTH24_STENCIL8_EXT:
            *Depth = TRUE;
            *Stencil = TRUE;
            Bits[4] = 24;
            Bits[5] = 8;
            return TRUE;
        default:
            return FALSE;
    }
}

static VOID APIENTRY
Rpi5OglRenderbufferStorageEXT(
    _In_ GLenum Target,
    _In_ GLenum InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;
    GLubyte *ColorData = NULL;
    GLdepth *DepthData = NULL;
    GLstencil *StencilData = NULL;
    ULONG Bits[6];
    ULONGLONG Pixels;
    BOOL Color;
    BOOL Depth;
    BOOL Stencil;

    if (!Rpi5OglFboCanChangeState(State,
                                  "glRenderbufferStorageEXT") ||
        !Rpi5OglFboValidRenderbufferTarget(
            State, Target, "glRenderbufferStorageEXT(target)"))
    {
        return;
    }
    if (Width < 0 || Height < 0 ||
        Width > RPI5VC4_OGL_FBO_MAX_RENDERBUFFER_SIZE ||
        Height > RPI5VC4_OGL_FBO_MAX_RENDERBUFFER_SIZE)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glRenderbufferStorageEXT(size)");
        return;
    }
    if (!Rpi5OglFboRenderbufferFormat(InternalFormat, &Color, &Depth,
                                      &Stencil, Bits))
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glRenderbufferStorageEXT(format)");
        return;
    }
    Renderbuffer = Rpi5OglFboFindRenderbuffer(
        State, State->CurrentRenderbuffer);
    if (Renderbuffer == NULL || !Renderbuffer->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glRenderbufferStorageEXT(binding)");
        return;
    }

    Pixels = (ULONGLONG)(ULONG)Width * (ULONG)Height;
    if (Pixels > 0xFFFFFFFFULL / sizeof(GLdepth))
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glRenderbufferStorageEXT(allocation)");
        return;
    }
    if (Pixels != 0 && Color)
        ColorData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              (SIZE_T)Pixels * 4);
    if (Pixels != 0 && Depth)
        DepthData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              (SIZE_T)Pixels * sizeof(*DepthData));
    if (Pixels != 0 && Stencil)
        StencilData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                (SIZE_T)Pixels * sizeof(*StencilData));
    if (Pixels != 0 && ((Color && ColorData == NULL) ||
                        (Depth && DepthData == NULL) ||
                        (Stencil && StencilData == NULL)))
    {
        if (ColorData != NULL)
            HeapFree(GetProcessHeap(), 0, ColorData);
        if (DepthData != NULL)
            HeapFree(GetProcessHeap(), 0, DepthData);
        if (StencilData != NULL)
            HeapFree(GetProcessHeap(), 0, StencilData);
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glRenderbufferStorageEXT(allocation)");
        return;
    }

    Rpi5OglFboFreeRenderbufferStorage(Renderbuffer);
    Renderbuffer->InternalFormat = InternalFormat;
    Renderbuffer->Width = Width;
    Renderbuffer->Height = Height;
    Renderbuffer->RedBits = Bits[0];
    Renderbuffer->GreenBits = Bits[1];
    Renderbuffer->BlueBits = Bits[2];
    Renderbuffer->AlphaBits = Bits[3];
    Renderbuffer->DepthBits = Bits[4];
    Renderbuffer->StencilBits = Bits[5];
    Renderbuffer->Color = ColorData;
    Renderbuffer->Depth = DepthData;
    Renderbuffer->Stencil = StencilData;
    Rpi5OglFboAdvanceGeneration(State);
    Rpi5OglFboApplyBinding(State);
}

static VOID APIENTRY
Rpi5OglGetRenderbufferParameterivEXT(
    _In_ GLenum Target,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_RENDERBUFFER Renderbuffer;

    if (!Rpi5OglFboCanChangeState(
            State, "glGetRenderbufferParameterivEXT") ||
        !Rpi5OglFboValidRenderbufferTarget(
            State, Target, "glGetRenderbufferParameterivEXT(target)"))
    {
        return;
    }
    if (Parameters == NULL)
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glGetRenderbufferParameterivEXT(params)");
        return;
    }
    Renderbuffer = Rpi5OglFboFindRenderbuffer(
        State, State->CurrentRenderbuffer);
    if (Renderbuffer == NULL || !Renderbuffer->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glGetRenderbufferParameterivEXT(binding)");
        return;
    }
    switch (ParameterName)
    {
        case GL_RENDERBUFFER_WIDTH_EXT:
            *Parameters = Renderbuffer->Width;
            break;
        case GL_RENDERBUFFER_HEIGHT_EXT:
            *Parameters = Renderbuffer->Height;
            break;
        case GL_RENDERBUFFER_INTERNAL_FORMAT_EXT:
            *Parameters = Renderbuffer->InternalFormat;
            break;
        case GL_RENDERBUFFER_RED_SIZE_EXT:
            *Parameters = Renderbuffer->RedBits;
            break;
        case GL_RENDERBUFFER_GREEN_SIZE_EXT:
            *Parameters = Renderbuffer->GreenBits;
            break;
        case GL_RENDERBUFFER_BLUE_SIZE_EXT:
            *Parameters = Renderbuffer->BlueBits;
            break;
        case GL_RENDERBUFFER_ALPHA_SIZE_EXT:
            *Parameters = Renderbuffer->AlphaBits;
            break;
        case GL_RENDERBUFFER_DEPTH_SIZE_EXT:
            *Parameters = Renderbuffer->DepthBits;
            break;
        case GL_RENDERBUFFER_STENCIL_SIZE_EXT:
            *Parameters = Renderbuffer->StencilBits;
            break;
        default:
            Rpi5OglFboError(State, GL_INVALID_ENUM,
                            "glGetRenderbufferParameterivEXT(pname)");
            break;
    }
}

VOID APIENTRY
Rpi5OglFboDrawBuffer(
    _In_ GLenum Mode)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;

    if (!Rpi5OglFboCanChangeState(State, "glDrawBuffer"))
        return;
    if (State->CurrentFramebuffer == 0)
    {
        _mesa_DrawBuffer(Mode);
        State->DefaultDrawBuffer = State->Mesa->Color.DrawBuffer;
        return;
    }
    if (Mode != GL_NONE && Mode != GL_COLOR_ATTACHMENT0_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM, "glDrawBuffer");
        return;
    }
    Framebuffer = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    if (Framebuffer == NULL || !Framebuffer->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, "glDrawBuffer");
        return;
    }
    Framebuffer->DrawBuffer = Mode;
    State->Mesa->Color.DrawBuffer = Mode;
    State->Mesa->Buffer->Alpha = NULL;
    if (State->Mesa->Driver.SetBuffer != NULL)
        (*State->Mesa->Driver.SetBuffer)(State->Mesa, Mode);
    State->Mesa->NewState |= NEW_RASTER_OPS;
}

VOID APIENTRY
Rpi5OglFboReadBuffer(
    _In_ GLenum Mode)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;

    if (!Rpi5OglFboCanChangeState(State, "glReadBuffer"))
        return;
    if (State->CurrentFramebuffer == 0)
    {
        _mesa_ReadBuffer(Mode);
        State->DefaultReadBuffer = State->Mesa->Pixel.ReadBuffer;
        return;
    }
    if (Mode != GL_NONE && Mode != GL_COLOR_ATTACHMENT0_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM, "glReadBuffer");
        return;
    }
    Framebuffer = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    if (Framebuffer == NULL || !Framebuffer->Object)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, "glReadBuffer");
        return;
    }
    Framebuffer->ReadBuffer = Mode;
    State->Mesa->Pixel.ReadBuffer = Mode;
    State->Mesa->NewState |= NEW_RASTER_OPS;
}

VOID APIENTRY
Rpi5OglFboGetIntegerv(
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();

    if (State == NULL)
        return;
    switch (ParameterName)
    {
        case GL_FRAMEBUFFER_BINDING_EXT:
            *Parameters = State->CurrentFramebuffer;
            break;
        case GL_RENDERBUFFER_BINDING_EXT:
            *Parameters = State->CurrentRenderbuffer;
            break;
        case GL_MAX_COLOR_ATTACHMENTS_EXT:
            *Parameters = 1;
            break;
        case GL_MAX_RENDERBUFFER_SIZE_EXT:
            *Parameters = RPI5VC4_OGL_FBO_MAX_RENDERBUFFER_SIZE;
            break;
        default:
            _mesa_GetIntegerv(ParameterName, Parameters);
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
Rpi5OglFboByteSwap32(
    _In_ ULONG Value)
{
    return (Value >> 24) |
           ((Value >> 8) & 0x0000FF00) |
           ((Value << 8) & 0x00FF0000) |
           (Value << 24);
}

static BOOL
Rpi5OglFboPacked8888Type(
    _In_ GLenum Type)
{
    return Type == GL_UNSIGNED_INT_8_8_8_8 ||
           Type == GL_UNSIGNED_INT_8_8_8_8_REV;
}

static VOID
Rpi5OglFboSetTightUnpack(
    _Inout_ struct gl_pixelstore_attrib *Unpack)
{
    Unpack->Alignment = 1;
    Unpack->RowLength = 0;
    Unpack->SkipPixels = 0;
    Unpack->SkipRows = 0;
    Unpack->SwapBytes = GL_FALSE;
    Unpack->LsbFirst = GL_FALSE;
}

static VOID
Rpi5OglFboCallTexImage1DUbyte(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_opt_ const GLvoid *Pixels)
{
    struct gl_pixelstore_attrib SavedUnpack = State->Mesa->Unpack;

    Rpi5OglFboSetTightUnpack(&State->Mesa->Unpack);
    _mesa_TexImage1D(Target, Level, InternalFormat, Width, Border,
                     Format, GL_UNSIGNED_BYTE, Pixels);
    State->Mesa->Unpack = SavedUnpack;
}

static VOID
Rpi5OglFboCallTexImage2DUbyte(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_opt_ const GLvoid *Pixels)
{
    struct gl_pixelstore_attrib SavedUnpack = State->Mesa->Unpack;

    Rpi5OglFboSetTightUnpack(&State->Mesa->Unpack);
    _mesa_TexImage2D(Target, Level, InternalFormat, Width, Height,
                     Border, Format, GL_UNSIGNED_BYTE, Pixels);
    State->Mesa->Unpack = SavedUnpack;
}

static VOID
Rpi5OglFboCallTexSubImage2DUbyte(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint XOffset,
    _In_ GLint YOffset,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_opt_ const GLvoid *Pixels)
{
    struct gl_pixelstore_attrib SavedUnpack = State->Mesa->Unpack;

    Rpi5OglFboSetTightUnpack(&State->Mesa->Unpack);
    _mesa_TexSubImage2D(Target, Level, XOffset, YOffset,
                        Width, Height, Format, GL_UNSIGNED_BYTE, Pixels);
    State->Mesa->Unpack = SavedUnpack;
}

static BOOL
Rpi5OglFboGenerateMipmapLevel(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _Inout_ struct gl_texture_object *Texture,
    _In_ GLenum Target,
    _In_ GLint Level)
{
    const struct gl_texture_image *Source = Texture->Image[Level - 1];
    GLubyte *Pixels;
    GLubyte Color[4];
    ULONG Sum[4];
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG Width;
    ULONG Height;
    ULONG X;
    ULONG Y;
    ULONG SampleX;
    ULONG SampleY;
    ULONG Samples;
    ULONG Component;
    ULONGLONG Bytes;

    if (Source == NULL || Source->Data == NULL || Source->Border != 0)
        return FALSE;
    SourceWidth = Source->Width;
    SourceHeight = Target == GL_TEXTURE_1D ? 1 : Source->Height;
    Width = SourceWidth > 1 ? SourceWidth / 2 : 1;
    Height = SourceHeight > 1 ? SourceHeight / 2 : 1;
    Bytes = (ULONGLONG)Width * Height * 4;
    if (Bytes == 0 || Bytes > 0xFFFFFFFFULL)
        return FALSE;
    Pixels = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Bytes);
    if (Pixels == NULL)
        return FALSE;

    for (Y = 0; Y < Height; Y++)
    {
        for (X = 0; X < Width; X++)
        {
            ZeroMemory(Sum, sizeof(Sum));
            Samples = 0;
            for (SampleY = 0;
                 SampleY < (SourceHeight > 1 ? 2u : 1u);
                 SampleY++)
            {
                for (SampleX = 0;
                     SampleX < (SourceWidth > 1 ? 2u : 1u);
                     SampleX++)
                {
                    if (!Rpi5OglFboReadTexel(
                            Source,
                            (Y * 2 + SampleY) * SourceWidth +
                                X * 2 + SampleX,
                            Color))
                    {
                        HeapFree(GetProcessHeap(), 0, Pixels);
                        return FALSE;
                    }
                    for (Component = 0; Component < 4; Component++)
                        Sum[Component] += Color[Component];
                    Samples++;
                }
            }
            for (Component = 0; Component < 4; Component++)
            {
                Pixels[(Y * Width + X) * 4 + Component] =
                    (GLubyte)((Sum[Component] + Samples / 2) / Samples);
            }
        }
    }

    if (Target == GL_TEXTURE_1D)
    {
        Rpi5OglFboCallTexImage1DUbyte(State, Target, Level,
                                      Source->Format, Width, 0,
                                      GL_RGBA, Pixels);
    }
    else
    {
        Rpi5OglFboCallTexImage2DUbyte(State, Target, Level,
                                      Source->Format, Width, Height, 0,
                                      GL_RGBA, Pixels);
    }
    HeapFree(GetProcessHeap(), 0, Pixels);
    return Texture->Image[Level] != NULL &&
           Texture->Image[Level]->Data != NULL;
}

static VOID APIENTRY
Rpi5OglGenerateMipmapEXT(
    _In_ GLenum Target)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    struct gl_texture_object *Texture;
    struct gl_texture_image *Base;
    ULONG Width;
    ULONG Height;
    GLint Level;

    if (!Rpi5OglFboCanChangeState(State, "glGenerateMipmapEXT"))
        return;
    if (Target == GL_TEXTURE_1D)
        Texture = State->Mesa->Texture.Current1D;
    else if (Target == GL_TEXTURE_2D)
        Texture = State->Mesa->Texture.Current2D;
    else
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glGenerateMipmapEXT(target)");
        return;
    }
    Base = Texture != NULL ? Texture->Image[0] : NULL;
    if (Base == NULL || Base->Data == NULL || Base->Border != 0)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glGenerateMipmapEXT(base level)");
        return;
    }
    if (Rpi5OglRecordGraphTextureMipmap(Texture))
        return;
    Rpi5OglMaterializeTextureClear(Texture);
    Width = Base->Width;
    Height = Target == GL_TEXTURE_1D ? 1 : Base->Height;
    for (Level = 1;
         (Width > 1 || Height > 1) && Level < MAX_TEXTURE_LEVELS;
         Level++)
    {
        if (!Rpi5OglFboGenerateMipmapLevel(State, Texture,
                                           Target, Level))
        {
            Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                            "glGenerateMipmapEXT(level)");
            return;
        }
        Width = Width > 1 ? Width / 2 : 1;
        Height = Height > 1 ? Height / 2 : 1;
    }
    Texture->Dirty = GL_TRUE;
    gl_test_texture_object_completeness(Texture);
    (VOID)Rpi5OglRecordGraphTextureMipmap(Texture);
}

static GLubyte *
Rpi5OglFboUnpack8888(
    _In_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_reads_bytes_opt_(1) const GLvoid *Pixels,
    _In_z_ PCSTR Function)
{
    const struct gl_pixelstore_attrib *Unpack = &State->Mesa->Unpack;
    const GLubyte *Source;
    GLubyte *Converted;
    GLubyte *Destination;
    ULONGLONG RowBytes;
    ULONGLONG Stride;
    ULONGLONG SourceOffset;
    ULONGLONG SourceEnd;
    ULONGLONG ConvertedBytes;
    ULONG RowPixels;
    ULONG Alignment;
    ULONG Row;
    ULONG Column;
    ULONG Word;
    GLubyte Components[4];

    if (Pixels == NULL)
        return NULL;
    if (Width <= 0 || Height <= 0)
        return NULL;

    if (Unpack->RowLength < 0 || Unpack->SkipRows < 0 ||
        Unpack->SkipPixels < 0)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION, Function);
        return NULL;
    }

    RowPixels = Unpack->RowLength > 0 ?
                Unpack->RowLength : (ULONG)Width;
    Alignment = Unpack->Alignment > 0 ? Unpack->Alignment : 1;
    RowBytes = (ULONGLONG)RowPixels * sizeof(ULONG);
    if ((Alignment != 1 && Alignment != 2 &&
         Alignment != 4 && Alignment != 8) ||
        RowBytes > 0xFFFFFFFFULL - (Alignment - 1))
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, Function);
        return NULL;
    }
    Stride = (RowBytes + Alignment - 1) & ~(ULONGLONG)(Alignment - 1);
    ConvertedBytes = (ULONGLONG)(ULONG)Width * (ULONG)Height * 4;
    if (ConvertedBytes == 0 || ConvertedBytes > 0xFFFFFFFFULL ||
        (Unpack->SkipRows != 0 &&
         Stride > 0xFFFFFFFFULL / (ULONG)Unpack->SkipRows))
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, Function);
        return NULL;
    }
    SourceOffset = (ULONGLONG)Unpack->SkipRows * Stride;
    if ((ULONGLONG)Unpack->SkipPixels * sizeof(ULONG) >
        0xFFFFFFFFULL - SourceOffset)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, Function);
        return NULL;
    }
    SourceOffset += (ULONGLONG)Unpack->SkipPixels * sizeof(ULONG);
    if ((ULONG)Height > 1 &&
        Stride > (0xFFFFFFFFULL - SourceOffset) /
            ((ULONG)Height - 1))
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, Function);
        return NULL;
    }
    SourceEnd = SourceOffset +
                (ULONGLONG)((ULONG)Height - 1) * Stride;
    if ((ULONGLONG)(ULONG)Width * sizeof(ULONG) >
        0xFFFFFFFFULL - SourceEnd)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, Function);
        return NULL;
    }

    Converted = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)ConvertedBytes);
    if (Converted == NULL)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY, Function);
        return NULL;
    }

    Source = (const GLubyte *)Pixels + (ULONG)SourceOffset;
    Destination = Converted;
    for (Row = 0; Row < (ULONG)Height; Row++)
    {
        for (Column = 0; Column < (ULONG)Width; Column++)
        {
            CopyMemory(&Word,
                       Source + Row * (ULONG)Stride +
                           Column * sizeof(Word),
                       sizeof(Word));
            if (Unpack->SwapBytes)
                Word = Rpi5OglFboByteSwap32(Word);
            if (Type == GL_UNSIGNED_INT_8_8_8_8_REV)
            {
                Components[0] = (GLubyte)Word;
                Components[1] = (GLubyte)(Word >> 8);
                Components[2] = (GLubyte)(Word >> 16);
                Components[3] = (GLubyte)(Word >> 24);
            }
            else
            {
                Components[0] = (GLubyte)(Word >> 24);
                Components[1] = (GLubyte)(Word >> 16);
                Components[2] = (GLubyte)(Word >> 8);
                Components[3] = (GLubyte)Word;
            }
            Destination[0] = Format == GL_BGRA_EXT ?
                             Components[2] : Components[0];
            Destination[1] = Components[1];
            Destination[2] = Format == GL_BGRA_EXT ?
                             Components[0] : Components[2];
            Destination[3] = Components[3];
            Destination += 4;
        }
    }
    return Converted;
}

static BOOL
Rpi5OglFboDepthInternalFormat(
    _In_ GLint InternalFormat)
{
    return InternalFormat == GL_DEPTH_COMPONENT ||
           InternalFormat == GL_DEPTH_COMPONENT16 ||
           InternalFormat == GL_DEPTH_COMPONENT24 ||
           InternalFormat == GL_DEPTH_COMPONENT32;
}

static ULONG
Rpi5OglFboFloorLog2(
    _In_ ULONG Value)
{
    ULONG Log2 = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Log2++;
    }
    return Log2;
}

static BOOL
Rpi5OglFboUnpackDepth(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Type,
    _In_reads_bytes_opt_(1) const GLvoid *Pixels,
    _Out_writes_((ULONG)Width * (ULONG)Height) GLdepth *Depth)
{
    const struct gl_pixelstore_attrib *Unpack = &State->Mesa->Unpack;
    ULONG BytesPerPixel;
    ULONG RowPixels;
    ULONG Alignment;
    ULONGLONG RowBytes;
    ULONGLONG Stride;
    ULONGLONG SourceOffset;
    const GLubyte *Source;
    ULONG Row;
    ULONG Column;

    if (Pixels == NULL)
    {
        ZeroMemory(Depth,
                   (SIZE_T)(ULONG)Width * (ULONG)Height * sizeof(*Depth));
        return TRUE;
    }
    if (Type == GL_UNSIGNED_SHORT)
        BytesPerPixel = sizeof(USHORT);
    else if (Type == GL_UNSIGNED_INT || Type == GL_FLOAT)
        BytesPerPixel = sizeof(ULONG);
    else
        return FALSE;

    if (Unpack->RowLength < 0 || Unpack->SkipRows < 0 ||
        Unpack->SkipPixels < 0)
    {
        return FALSE;
    }
    RowPixels = Unpack->RowLength > 0 ?
                (ULONG)Unpack->RowLength : (ULONG)Width;
    Alignment = Unpack->Alignment > 0 ? (ULONG)Unpack->Alignment : 1;
    RowBytes = (ULONGLONG)RowPixels * BytesPerPixel;
    if ((Alignment != 1 && Alignment != 2 &&
         Alignment != 4 && Alignment != 8) ||
        RowBytes > 0xFFFFFFFFULL - (Alignment - 1))
    {
        return FALSE;
    }
    Stride = (RowBytes + Alignment - 1) & ~(ULONGLONG)(Alignment - 1);
    SourceOffset = (ULONGLONG)(ULONG)Unpack->SkipRows * Stride +
                   (ULONGLONG)(ULONG)Unpack->SkipPixels * BytesPerPixel;
    if (SourceOffset > 0xFFFFFFFFULL ||
        ((ULONG)Height > 1 &&
         ((ULONGLONG)(ULONG)Height - 1) * Stride >
             0xFFFFFFFFULL - SourceOffset) ||
        (ULONGLONG)(ULONG)Width * BytesPerPixel >
            0xFFFFFFFFULL - SourceOffset -
            ((ULONGLONG)(ULONG)Height - 1) * Stride)
    {
        return FALSE;
    }

    Source = (const GLubyte *)Pixels + (ULONG)SourceOffset;
    for (Row = 0; Row < (ULONG)Height; Row++)
    {
        for (Column = 0; Column < (ULONG)Width; Column++)
        {
            const GLubyte *Texel = Source + Row * (ULONG)Stride +
                                    Column * BytesPerPixel;
            ULONG Value;

            if (Type == GL_UNSIGNED_SHORT)
            {
                USHORT ShortValue;

                CopyMemory(&ShortValue, Texel, sizeof(ShortValue));
                if (Unpack->SwapBytes)
                {
                    ShortValue = (USHORT)((ShortValue >> 8) |
                                          (ShortValue << 8));
                }
                Value = (ULONG)(((ULONGLONG)ShortValue * MAX_DEPTH +
                                 0x7FFFu) / 0xFFFFu);
            }
            else
            {
                CopyMemory(&Value, Texel, sizeof(Value));
                if (Unpack->SwapBytes)
                    Value = Rpi5OglFboByteSwap32(Value);
                if (Type == GL_FLOAT)
                {
                    GLfloat FloatValue;

                    CopyMemory(&FloatValue, &Value, sizeof(FloatValue));
                    if (FloatValue <= 0.0F)
                        Value = 0;
                    else if (FloatValue >= 1.0F)
                        Value = MAX_DEPTH;
                    else
                        Value = (ULONG)(FloatValue * MAX_DEPTH + 0.5F);
                }
                else
                {
                    Value = (ULONG)(((ULONGLONG)Value * MAX_DEPTH +
                                     0x7FFFFFFFULL) / 0xFFFFFFFFULL);
                }
            }
            Depth[Row * (ULONG)Width + Column] = (GLdepth)Value;
        }
    }
    return TRUE;
}

static BOOL
Rpi5OglFboDefineDepthTexture2D(
    _Inout_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_reads_bytes_opt_(1) const GLvoid *Pixels)
{
    struct gl_texture_object *Texture;
    struct gl_texture_image *Image;
    ULONGLONG Bytes;

    if (Level < 0 || Level >= MAX_TEXTURE_LEVELS || Border != 0 ||
        Width <= 0 || Height <= 0 ||
        Width > (RPI5VC4_OGL_FBO_MAX_RENDERBUFFER_SIZE >> Level) ||
        Height > (RPI5VC4_OGL_FBO_MAX_RENDERBUFFER_SIZE >> Level))
    {
        Rpi5OglFboError(State, GL_INVALID_VALUE,
                        "glTexImage2D(depth dimensions)");
        return FALSE;
    }
    if (!Rpi5OglFboDepthInternalFormat(InternalFormat) ||
        Format != GL_DEPTH_COMPONENT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glTexImage2D(depth format)");
        return FALSE;
    }
    if (Type != GL_UNSIGNED_SHORT && Type != GL_UNSIGNED_INT &&
        Type != GL_FLOAT)
    {
        Rpi5OglFboError(State, GL_INVALID_ENUM,
                        "glTexImage2D(depth type)");
        return FALSE;
    }

    Texture = State->Mesa->Texture.Current2D;
    if (Texture == NULL || Texture->Dimensions != 2)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glTexImage2D(depth texture)");
        return FALSE;
    }
    Bytes = (ULONGLONG)(ULONG)Width * (ULONG)Height * sizeof(GLdepth);
    if (Bytes == 0 || Bytes > 0xFFFFFFFFULL)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glTexImage2D(depth size)");
        return FALSE;
    }

    Image = gl_alloc_texture_image();
    if (Image == NULL)
    {
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glTexImage2D(depth image)");
        return FALSE;
    }
    Image->Data = malloc((SIZE_T)Bytes);
    if (Image->Data == NULL)
    {
        gl_free_texture_image(Image);
        Rpi5OglFboError(State, GL_OUT_OF_MEMORY,
                        "glTexImage2D(depth data)");
        return FALSE;
    }
    if (!Rpi5OglFboUnpackDepth(State, Width, Height, Type, Pixels,
                               (GLdepth *)Image->Data))
    {
        gl_free_texture_image(Image);
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glTexImage2D(depth unpack)");
        return FALSE;
    }

    Image->Format = GL_DEPTH_COMPONENT;
    Image->IntFormat = (GLenum)InternalFormat;
    Image->Border = 0;
    Image->Width = (GLuint)Width;
    Image->Height = (GLuint)Height;
    Image->Width2 = (GLuint)Width;
    Image->Height2 = (GLuint)Height;
    Image->WidthLog2 = Rpi5OglFboFloorLog2((ULONG)Width);
    Image->HeightLog2 = Rpi5OglFboFloorLog2((ULONG)Height);
    Image->MaxLog2 = MAX2(Image->WidthLog2, Image->HeightLog2);

    if (Texture->Image[Level] != NULL)
        gl_free_texture_image(Texture->Image[Level]);
    Texture->Image[Level] = Image;
    Texture->Dirty = GL_TRUE;
    State->Mesa->Texture.AnyDirty = GL_TRUE;
    State->Mesa->NewState |= NEW_TEXTURING;
    gl_test_texture_object_completeness(Texture);
    if (State->Mesa->Driver.TexImage != NULL)
    {
        (*State->Mesa->Driver.TexImage)(State->Mesa,
                                        GL_TEXTURE_2D,
                                        Texture,
                                        Level,
                                        InternalFormat,
                                        Image);
    }
    return TRUE;
}

VOID APIENTRY
Rpi5OglFboTexImage2D(
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    GLubyte *Converted;

    if (Target == GL_TEXTURE_2D &&
        (Rpi5OglFboDepthInternalFormat(InternalFormat) ||
         Format == GL_DEPTH_COMPONENT))
    {
        if (!Rpi5OglFboCanChangeState(State, "glTexImage2D"))
            return;
        (void)Rpi5OglFboDefineDepthTexture2D(State, Level,
                                             InternalFormat,
                                             Width, Height, Border,
                                             Format, Type, Pixels);
        return;
    }

    if (!Rpi5OglFboPacked8888Type(Type))
    {
        _mesa_TexImage2D(Target, Level, InternalFormat, Width, Height,
                         Border, Format, Type, Pixels);
        return;
    }
    if (!Rpi5OglFboCanChangeState(State, "glTexImage2D"))
        return;
    if (Format != GL_RGBA && Format != GL_BGRA_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glTexImage2D(packed format)");
        return;
    }
    if (Width <= 0 || Height <= 0 || Pixels == NULL)
    {
        Rpi5OglFboCallTexImage2DUbyte(State, Target, Level,
                                      InternalFormat, Width, Height,
                                      Border, GL_RGBA, NULL);
        return;
    }

    Converted = Rpi5OglFboUnpack8888(State, Width, Height, Format, Type,
                                     Pixels, "glTexImage2D(unpack)");
    if (Converted == NULL)
        return;
    Rpi5OglFboCallTexImage2DUbyte(State, Target, Level, InternalFormat,
                                  Width, Height, Border, GL_RGBA,
                                  Converted);
    HeapFree(GetProcessHeap(), 0, Converted);
}

VOID APIENTRY
Rpi5OglFboTexSubImage2D(
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint XOffset,
    _In_ GLint YOffset,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels)
{
    PRPI5VC4_OGL_FBO_STATE State = Rpi5OglCurrentFboState();
    struct gl_texture_object *Texture = NULL;
    GLubyte *Converted;

    if (State != NULL)
    {
        if (Target == GL_TEXTURE_1D)
            Texture = State->Mesa->Texture.Current1D;
        else if (Target == GL_TEXTURE_2D)
            Texture = State->Mesa->Texture.Current2D;
    }
    Rpi5OglMaterializeTextureClear(Texture);

    if (!Rpi5OglFboPacked8888Type(Type))
    {
        _mesa_TexSubImage2D(Target, Level, XOffset, YOffset,
                            Width, Height, Format, Type, Pixels);
        return;
    }
    if (!Rpi5OglFboCanChangeState(State, "glTexSubImage2D"))
        return;
    if (Format != GL_RGBA && Format != GL_BGRA_EXT)
    {
        Rpi5OglFboError(State, GL_INVALID_OPERATION,
                        "glTexSubImage2D(packed format)");
        return;
    }
    if (Width == 0 || Height == 0)
        return;
    if (Width < 0 || Height < 0 || Pixels == NULL)
    {
        Rpi5OglFboCallTexSubImage2DUbyte(State, Target, Level,
                                         XOffset, YOffset, Width, Height,
                                         GL_RGBA, NULL);
        return;
    }

    Converted = Rpi5OglFboUnpack8888(State, Width, Height, Format, Type,
                                     Pixels, "glTexSubImage2D(unpack)");
    if (Converted == NULL)
        return;
    Rpi5OglFboCallTexSubImage2DUbyte(State, Target, Level,
                                     XOffset, YOffset, Width, Height,
                                     GL_RGBA, Converted);
    HeapFree(GetProcessHeap(), 0, Converted);
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
    Rpi5OglMaterializeTextureClear(Texture);
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
                    Word = Rpi5OglFboByteSwap32(Word);
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
                    Word = Rpi5OglFboByteSwap32(Word);
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
    {"glBindRenderbufferEXT", (PROC)Rpi5OglBindRenderbufferEXT},
    {"glCheckFramebufferStatusEXT",
     (PROC)Rpi5OglCheckFramebufferStatusEXT},
    {"glDeleteFramebuffersEXT", (PROC)Rpi5OglDeleteFramebuffersEXT},
    {"glDeleteRenderbuffersEXT", (PROC)Rpi5OglDeleteRenderbuffersEXT},
    {"glFramebufferRenderbufferEXT",
     (PROC)Rpi5OglFramebufferRenderbufferEXT},
    {"glFramebufferTexture1DEXT",
     (PROC)Rpi5OglFramebufferTexture1DEXT},
    {"glFramebufferTexture2DEXT",
     (PROC)Rpi5OglFramebufferTexture2DEXT},
    {"glFramebufferTexture3DEXT",
     (PROC)Rpi5OglFramebufferTexture3DEXT},
    {"glGenFramebuffersEXT", (PROC)Rpi5OglGenFramebuffersEXT},
    {"glGenerateMipmapEXT", (PROC)Rpi5OglGenerateMipmapEXT},
    {"glGenRenderbuffersEXT", (PROC)Rpi5OglGenRenderbuffersEXT},
    {"glGetFramebufferAttachmentParameterivEXT",
     (PROC)Rpi5OglGetFramebufferAttachmentParameterivEXT},
    {"glGetRenderbufferParameterivEXT",
     (PROC)Rpi5OglGetRenderbufferParameterivEXT},
    {"glIsFramebufferEXT", (PROC)Rpi5OglIsFramebufferEXT},
    {"glIsRenderbufferEXT", (PROC)Rpi5OglIsRenderbufferEXT},
    {"glRenderbufferStorageEXT", (PROC)Rpi5OglRenderbufferStorageEXT},
};

BOOL
Rpi5OglFboInitialize(
    _Outptr_ PRPI5VC4_OGL_FBO_STATE *State,
    _In_ GLcontext *Mesa,
    _In_ GLframebuffer *DefaultBuffer)
{
    PRPI5VC4_OGL_FBO_STATE NewState;

    *State = NULL;
    NewState = HeapAlloc(GetProcessHeap(),
                         HEAP_ZERO_MEMORY,
                         sizeof(*NewState));
    if (NewState == NULL)
        return FALSE;
    NewState->Mesa = Mesa;
    NewState->DefaultBuffer = DefaultBuffer;
    NewState->DefaultDrawBuffer = Mesa->Color.DrawBuffer;
    NewState->DefaultReadBuffer = Mesa->Pixel.ReadBuffer;
    NewState->NextFramebufferName = 1;
    NewState->NextRenderbufferName = 1;
    NewState->TargetGeneration = 1;
    *State = NewState;
    return TRUE;
}

VOID
Rpi5OglFboCleanup(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State)
{
    ULONG Index;

    if (State == NULL)
        return;
    State->Mesa->Buffer = State->DefaultBuffer;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Framebuffers); Index++)
    {
        if (State->Framebuffers[Index].Name != 0 ||
            State->Framebuffers[Index].Color.ObjectType != GL_NONE ||
            State->Framebuffers[Index].Depth.ObjectType != GL_NONE ||
            State->Framebuffers[Index].Stencil.ObjectType != GL_NONE)
        {
            Rpi5OglFboReleaseFramebuffer(State,
                                         &State->Framebuffers[Index]);
        }
    }
    for (Index = 0; Index < RTL_NUMBER_OF(State->Renderbuffers); Index++)
        Rpi5OglFboResetRenderbuffer(&State->Renderbuffers[Index]);
    HeapFree(GetProcessHeap(), 0, State);
}

GLuint
Rpi5OglFboCurrentName(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State)
{
    return State != NULL ? State->CurrentFramebuffer : 0;
}

BOOL
Rpi5OglFboCurrentComplete(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State)
{
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;

    if (State == NULL || State->CurrentFramebuffer == 0)
        return TRUE;
    Framebuffer = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    return Framebuffer != NULL && Framebuffer->Object &&
           Rpi5OglFboStatus(State, Framebuffer) ==
               GL_FRAMEBUFFER_COMPLETE_EXT;
}

BOOL
Rpi5OglFboValidateCurrent(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_z_ PCSTR Function)
{
    if (Rpi5OglFboCurrentComplete(State))
        return TRUE;
    Rpi5OglFboError(State, GL_INVALID_FRAMEBUFFER_OPERATION_EXT,
                    Function);
    return FALSE;
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
    ZeroMemory(Target, sizeof(*Target));
    Target->Width = Framebuffer->MesaBuffer.Width;
    Target->Height = Framebuffer->MesaBuffer.Height;
    Target->Framebuffer = Framebuffer->Name;
    Target->Generation = State->TargetGeneration;
    if (Framebuffer->Color.ObjectType == GL_TEXTURE)
    {
        Texture = Framebuffer->Color.Texture;
        Image = Texture->Image[Framebuffer->Color.TextureLevel];
        Target->Texture = Texture;
        Target->Data = Image->Data;
        Target->Format = Image->Format;
        Target->HasColor = TRUE;
    }
    else if (Framebuffer->Color.ObjectType == GL_RENDERBUFFER_EXT)
    {
        Target->Data = Framebuffer->Color.Renderbuffer->Color;
        Target->Format = GL_RGBA;
        Target->HasColor = TRUE;
    }
    if (Framebuffer->Depth.ObjectType == GL_TEXTURE)
    {
        Texture = Framebuffer->Depth.Texture;
        Image = Texture->Image[Framebuffer->Depth.TextureLevel];
        Target->DepthTexture = Texture;
        Target->DepthData = (GLdepth *)Image->Data;
        Target->DepthFormat = Image->Format;
        Target->HasDepth = TRUE;
    }
    else if (Framebuffer->Depth.ObjectType == GL_RENDERBUFFER_EXT)
    {
        Target->DepthData = Framebuffer->Depth.Renderbuffer->Depth;
        Target->DepthFormat = GL_DEPTH_COMPONENT;
        Target->HasDepth = TRUE;
    }
    return TRUE;
}

VOID
Rpi5OglFboTextureChanged(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ struct gl_texture_object *Texture)
{
    ULONG Index;

    if (State == NULL || Texture == NULL)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Framebuffers); Index++)
    {
        if (State->Framebuffers[Index].Name != 0 &&
            (State->Framebuffers[Index].Color.Texture == Texture ||
             State->Framebuffers[Index].Depth.Texture == Texture ||
             State->Framebuffers[Index].Stencil.Texture == Texture))
        {
            Rpi5OglFboAdvanceGeneration(State);
            if (State->CurrentFramebuffer ==
                State->Framebuffers[Index].Name)
            {
                Rpi5OglFboApplyBinding(State);
            }
            return;
        }
    }
}

VOID
Rpi5OglFboTextureDeleted(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _Inout_ struct gl_texture_object *Texture)
{
    PRPI5VC4_OGL_FRAMEBUFFER Framebuffer;
    PRPI5VC4_OGL_ATTACHMENT Attachments[3];
    ULONG Index;
    BOOL Changed = FALSE;

    if (State == NULL || Texture == NULL || Texture->Name == 0)
        return;
    HashRemove(State->Mesa->Shared->TexObjects, Texture->Name);
    Texture->Name = 0;
    Framebuffer = Rpi5OglFboFind(State, State->CurrentFramebuffer);
    if (Framebuffer != NULL)
    {
        Attachments[0] = &Framebuffer->Color;
        Attachments[1] = &Framebuffer->Depth;
        Attachments[2] = &Framebuffer->Stencil;
        for (Index = 0; Index < RTL_NUMBER_OF(Attachments); Index++)
        {
            if (Attachments[Index]->Texture == Texture)
            {
                Rpi5OglFboReleaseAttachment(State,
                                             Attachments[Index], FALSE);
                Changed = TRUE;
            }
        }
    }
    if (Changed)
    {
        Rpi5OglFboAdvanceGeneration(State);
        Rpi5OglFboApplyBinding(State);
    }
}

VOID
Rpi5OglFboRestoreBinding(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State)
{
    if (State != NULL)
        Rpi5OglFboApplyBinding(State);
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
