/* $Id: teximage.c,v 1.35 1998/02/07 14:36:41 brianp Exp $ */

/*
 * Mesa 3-D graphics library
 * Version:  2.6
 * Copyright (C) 1995-1997  Brian Paul
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/*
 * $Log: teximage.c,v $
 * Revision 1.35  1998/02/07 14:36:41  brianp
 * fixed bug when passing NULL proxy image to glTexImageXD() (Wes Bethel)
 *
 * Revision 1.34  1998/01/16 03:46:07  brianp
 * fixed a few Windows compilation warnings (Theodore Jump)
 *
 * Revision 1.33  1997/12/31 06:10:03  brianp
 * added Henk Kok's texture validation optimization (AnyDirty flag)
 *
 * Revision 1.32  1997/12/07 17:30:39  brianp
 * added DavidB's patches for v0.21 driver (TexSubImage)
 *
 * Revision 1.31  1997/11/07 03:38:07  brianp
 * added stdio.h include for SunOS 4.x
 *
 * Revision 1.30  1997/11/02 20:20:30  brianp
 * rewrote gl_TexSubImage[123]D()
 *
 * Revision 1.29  1997/10/16 01:14:04  brianp
 * removed teximage Dirty flag
 *
 * Revision 1.28  1997/10/14 00:40:20  brianp
 * added DavidB's v19 fxmesa changes
 *
 * Revision 1.27  1997/09/29 23:28:14  brianp
 * updated for new device driver texture functions
 *
 * Revision 1.26  1997/09/28 15:29:03  brianp
 * initialize image->Depth = 1 in read_color_image() (Hiroki Honda)
 *
 * Revision 1.25  1997/09/27 00:14:39  brianp
 * added GL_EXT_paletted_texture extension
 *
 * Revision 1.24  1997/09/03 13:17:17  brianp
 * added a few pointer casts
 *
 * Revision 1.23  1997/08/23 18:40:46  brianp
 * glTexImage[123]D() with NULL image pointer is correctly handled now
 *
 * Revision 1.22  1997/08/11 01:23:29  brianp
 * added a few pointer casts
 *
 * Revision 1.21  1997/07/24 01:25:34  brianp
 * changed precompiled header symbol from PCH to PC_HEADER
 *
 * Revision 1.20  1997/07/05 16:21:17  brianp
 * fixed unitialized variable bug in gl_TexSubImage1D()
 *
 * Revision 1.19  1997/06/24 01:13:53  brianp
 * call gl_free_image() in gl_TexSubImage[123]D() if ref count==0
 *
 * Revision 1.18  1997/06/04 00:33:14  brianp
 * fixed reference count bug in gl_CopyTexImage1/2D() (Randy Frank)
 *
 * Revision 1.17  1997/05/28 03:26:49  brianp
 * added precompiled header (PCH) support
 *
 * Revision 1.16  1997/05/03 00:52:19  brianp
 * set texture object Dirty flag when changing texture image
 *
 * Revision 1.15  1997/04/20 20:29:11  brianp
 * replaced abort() with gl_problem()
 *
 * Revision 1.14  1997/03/04 19:18:29  brianp
 * added texture image Width2, Height2, and Depth2 fields
 *
 * Revision 1.13  1997/02/27 19:58:08  brianp
 * call gl_problem() instead of gl_warning()
 *
 * Revision 1.12  1997/02/09 18:53:05  brianp
 * added GL_EXT_texture3D support
 *
 * Revision 1.11  1997/01/16 03:35:10  brianp
 * added calls to device driver TexImage() function
 *
 * Revision 1.10  1997/01/09 21:26:46  brianp
 * gl_TexImage[12]D() didn't free the incoming image- a memory leak
 *
 * Revision 1.9  1997/01/09 19:55:52  brianp
 * fixed a few error messages
 *
 * Revision 1.8  1997/01/09 19:49:18  brianp
 * better error checking
 *
 * Revision 1.7  1996/12/02 18:59:54  brianp
 * added code to handle GL_COLOR_INDEX textures, per Randy Frank
 *
 * Revision 1.6  1996/11/07 04:13:24  brianp
 * all new texture image handling, now pixel scale, bias, mapping work
 *
 * Revision 1.5  1996/09/27 01:29:57  brianp
 * removed unused variables, fixed cut&paste bug in color scaling
 *
 * Revision 1.4  1996/09/26 22:35:10  brianp
 * fixed a few compiler warnings from IRIX 6 -n32 and -64 compiler
 *
 * Revision 1.3  1996/09/15 14:18:55  brianp
 * now use GLframebuffer and GLvisual
 *
 * Revision 1.2  1996/09/15 01:48:58  brianp
 * removed #define NULL 0
 *
 * Revision 1.1  1996/09/13 01:38:16  brianp
 * Initial revision
 *
 */


#ifdef PC_HEADER
#include "all.h"
#else
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "context.h"
#include "image.h"
#include "macros.h"
#include "pixel.h"
#include "span.h"
#include "teximage.h"
#include "types.h"
#endif


/*
 * NOTES:
 *
 * The internal texture storage convension is an array of N GLubytes
 * where N = width * height * components.  There is no padding.
 */




/*
 * Compute log base 2 of n.
 * If n isn't an exact power of two return -1.
 * If n<0 return -1.
 */
static int logbase2( int n )
{
   GLint i = 1;
   GLint log2 = 0;

   if (n<0) {
      return -1;
   }

   while ( n > i ) {
      i *= 2;
      log2++;
   }
   if (i != n) {
      return -1;
   }
   else {
      return log2;
   }
}



/*
 * Compute floor(log2(n)).  Zero-sized texture images carry zero here and
 * remain incomplete until a non-empty level zero image is supplied.
 */
static int floor_logbase2( int n )
{
   GLint log2 = 0;

   while (n > 1) {
      n >>= 1;
      log2++;
   }
   return log2;
}



/*
 * Given an internal texture format enum or 1, 2, 3, 4 return the
 * corresponding _base_ internal format:  GL_ALPHA, GL_LUMINANCE,
 * GL_LUMANCE_ALPHA, GL_INTENSITY, GL_RGB, or GL_RGBA.  Return -1 if
 * invalid enum.
 */
static GLint decode_internal_format( GLint format )
{
   switch (format) {
      case GL_ALPHA:
      case GL_ALPHA4:
      case GL_ALPHA8:
      case GL_ALPHA12:
      case GL_ALPHA16:
         return GL_ALPHA;
      case 1:
      case GL_LUMINANCE:
      case GL_LUMINANCE4:
      case GL_LUMINANCE8:
      case GL_LUMINANCE12:
      case GL_LUMINANCE16:
         return GL_LUMINANCE;
      case 2:
      case GL_LUMINANCE_ALPHA:
      case GL_LUMINANCE4_ALPHA4:
      case GL_LUMINANCE6_ALPHA2:
      case GL_LUMINANCE8_ALPHA8:
      case GL_LUMINANCE12_ALPHA4:
      case GL_LUMINANCE12_ALPHA12:
      case GL_LUMINANCE16_ALPHA16:
         return GL_LUMINANCE_ALPHA;
      case GL_INTENSITY:
      case GL_INTENSITY4:
      case GL_INTENSITY8:
      case GL_INTENSITY12:
      case GL_INTENSITY16:
         return GL_INTENSITY;
      case 3:
      case GL_RGB:
      case GL_R3_G3_B2:
      case GL_RGB4:
      case GL_RGB5:
      case GL_RGB8:
      case GL_RGB10:
      case GL_RGB12:
      case GL_RGB16:
         return GL_RGB;
      case 4:
      case GL_RGBA:
      case GL_RGBA2:
      case GL_RGBA4:
      case GL_RGB5_A1:
      case GL_RGBA8:
      case GL_RGB10_A2:
      case GL_RGBA12:
      case GL_RGBA16:
         return GL_RGBA;
      case GL_COLOR_INDEX1_EXT:
      case GL_COLOR_INDEX2_EXT:
      case GL_COLOR_INDEX4_EXT:
      case GL_COLOR_INDEX8_EXT:
      case GL_COLOR_INDEX12_EXT:
      case GL_COLOR_INDEX16_EXT:
         return GL_COLOR_INDEX;
      default:
         return -1;  /* error */
   }
}



/*
 * Given an internal texture format enum or 1, 2, 3, 4 return the
 * corresponding _base_ internal format:  GL_ALPHA, GL_LUMINANCE,
 * GL_LUMANCE_ALPHA, GL_INTENSITY, GL_RGB, or GL_RGBA.  Return the
 * number of components for the format.  Return -1 if invalid enum.
 */
static GLint components_in_intformat( GLint format )
{
   switch (format) {
      case GL_ALPHA:
      case GL_ALPHA4:
      case GL_ALPHA8:
      case GL_ALPHA12:
      case GL_ALPHA16:
         return 1;
      case 1:
      case GL_LUMINANCE:
      case GL_LUMINANCE4:
      case GL_LUMINANCE8:
      case GL_LUMINANCE12:
      case GL_LUMINANCE16:
         return 1;
      case 2:
      case GL_LUMINANCE_ALPHA:
      case GL_LUMINANCE4_ALPHA4:
      case GL_LUMINANCE6_ALPHA2:
      case GL_LUMINANCE8_ALPHA8:
      case GL_LUMINANCE12_ALPHA4:
      case GL_LUMINANCE12_ALPHA12:
      case GL_LUMINANCE16_ALPHA16:
         return 2;
      case GL_INTENSITY:
      case GL_INTENSITY4:
      case GL_INTENSITY8:
      case GL_INTENSITY12:
      case GL_INTENSITY16:
         return 1;
      case 3:
      case GL_RGB:
      case GL_R3_G3_B2:
      case GL_RGB4:
      case GL_RGB5:
      case GL_RGB8:
      case GL_RGB10:
      case GL_RGB12:
      case GL_RGB16:
         return 3;
      case 4:
      case GL_RGBA:
      case GL_RGBA2:
      case GL_RGBA4:
      case GL_RGB5_A1:
      case GL_RGBA8:
      case GL_RGB10_A2:
      case GL_RGBA12:
      case GL_RGBA16:
         return 4;
      case GL_COLOR_INDEX1_EXT:
      case GL_COLOR_INDEX2_EXT:
      case GL_COLOR_INDEX4_EXT:
      case GL_COLOR_INDEX8_EXT:
      case GL_COLOR_INDEX12_EXT:
      case GL_COLOR_INDEX16_EXT:
         return 1;
      default:
         return -1;  /* error */
   }
}



struct gl_texture_image *gl_alloc_texture_image( void )
{
   return (struct gl_texture_image *) calloc( 1, sizeof(struct gl_texture_image) );
}



void gl_free_texture_image( struct gl_texture_image *teximage )
{
   if (teximage->Data) {
      free( teximage->Data );
   }
   free( teximage );
}



/*
 * Given a gl_image, apply the pixel transfer scale, bias, and mapping
 * to produce a gl_texture_image.  Convert image data to GLubytes.
 * Input:  image - the incoming gl_image
 *         internalFormat - desired format of resultant texture
 *         border - texture border width (0 or 1)
 * Return:  pointer to a gl_texture_image or NULL if an error occurs.
 */
static struct gl_texture_image *
image_to_texture( GLcontext *ctx, const struct gl_image *image,
                  GLenum internalFormat, GLint border )
{
   GLint components;
   struct gl_texture_image *texImage;
   size_t numPixels, pixel;
   GLboolean scaleOrBias;

   assert(image);
   assert(image->Width>0);
   assert(image->Height>0);
   assert(image->Depth>0);

   /*   internalFormat = decode_internal_format(internalFormat);*/
   components = components_in_intformat(internalFormat);
   numPixels = (size_t) image->Width * image->Height * image->Depth;

   texImage = gl_alloc_texture_image();
   if (!texImage)
      return NULL;

   texImage->Format = decode_internal_format(internalFormat);
   texImage->IntFormat = internalFormat;
   texImage->Border = border;
   texImage->Width = image->Width;
   texImage->Height = image->Height;
   texImage->Depth = image->Depth;
   texImage->Width2 = image->Width - 2*border;
   texImage->WidthLog2 = floor_logbase2(texImage->Width2);
   if (image->Height==1)  /* 1-D texture */
      texImage->HeightLog2 = 0;
   else {
      texImage->Height2 = image->Height - 2*border;
      texImage->HeightLog2 = floor_logbase2(texImage->Height2);
   }
   if (image->Height==1)
      texImage->Height2 = 1;
   if (image->Depth==1) {
      texImage->Depth2 = 1;
      texImage->DepthLog2 = 0;
   }
   else {
      texImage->Depth2 = image->Depth - 2*border;
      texImage->DepthLog2 = floor_logbase2(texImage->Depth2);
   }
   texImage->MaxLog2 = MAX2( texImage->WidthLog2,
                             MAX2( texImage->HeightLog2,
                                   texImage->DepthLog2 ) );
   texImage->Data = (GLubyte *) malloc( numPixels * components );

   if (!texImage->Data) {
      /* out of memory */
      gl_free_texture_image( texImage );
      return NULL;
   }

   /* Determine if scaling and/or biasing is needed */
   if (ctx->Pixel.RedScale!=1.0F   || ctx->Pixel.RedBias!=0.0F ||
       ctx->Pixel.GreenScale!=1.0F || ctx->Pixel.GreenBias!=0.0F ||
       ctx->Pixel.BlueScale!=1.0F  || ctx->Pixel.BlueBias!=0.0F ||
       ctx->Pixel.AlphaScale!=1.0F || ctx->Pixel.AlphaBias!=0.0F) {
      scaleOrBias = GL_TRUE;
   }
   else {
      scaleOrBias = GL_FALSE;
   }

   switch (image->Type) {
      case GL_BITMAP:
         {
            GLint shift = ctx->Pixel.IndexShift;
            GLint offset = ctx->Pixel.IndexOffset;
            /* MapIto[RGBA]Size must be powers of two */
            GLint rMask = ctx->Pixel.MapItoRsize-1;
            GLint gMask = ctx->Pixel.MapItoGsize-1;
            GLint bMask = ctx->Pixel.MapItoBsize-1;
            GLint aMask = ctx->Pixel.MapItoAsize-1;
            GLint i, j, d;
            GLubyte *srcPtr = (GLubyte *) image->Data;

            assert( image->Format==GL_COLOR_INDEX );

            for (d=0; d<image->Depth; d++) {
               for (j=0; j<image->Height; j++) {
                  GLubyte bitMask = 128;
                  for (i=0; i<image->Width; i++) {
                  GLint index;
                  GLubyte red, green, blue, alpha;

                  /* Fetch image color index */
                  index = (*srcPtr & bitMask) ? 1 : 0;
                  bitMask = bitMask >> 1;
                  if (bitMask==0) {
                     bitMask = 128;
                     srcPtr++;
                  }
                  /* apply index shift and offset */
                  if (shift>=0) {
                     index = (index << shift) + offset;
                  }
                  else {
                     index = (index >> -shift) + offset;
                  }
                  /* convert index to RGBA */
                  red   = (GLint) (ctx->Pixel.MapItoR[index & rMask] * 255.0F);
                  green = (GLint) (ctx->Pixel.MapItoG[index & gMask] * 255.0F);
                  blue  = (GLint) (ctx->Pixel.MapItoB[index & bMask] * 255.0F);
                  alpha = (GLint) (ctx->Pixel.MapItoA[index & aMask] * 255.0F);

                  /* store texel (components are GLubytes in [0,255]) */
                  pixel = ((size_t) d * image->Height + j) *
                          image->Width + i;
                  switch (texImage->Format) {
                     case GL_ALPHA:
                        texImage->Data[pixel] = alpha;
                        break;
                     case GL_LUMINANCE:
                        texImage->Data[pixel] = red;
                        break;
                     case GL_LUMINANCE_ALPHA:
                        texImage->Data[pixel*2+0] = red;
                        texImage->Data[pixel*2+1] = alpha;
                        break;
                     case GL_INTENSITY:
                        texImage->Data[pixel] = red;
                        break;
                     case GL_RGB:
                        texImage->Data[pixel*3+0] = red;
                        texImage->Data[pixel*3+1] = green;
                        texImage->Data[pixel*3+2] = blue;
                        break;
                     case GL_RGBA:
                        texImage->Data[pixel*4+0] = red;
                        texImage->Data[pixel*4+1] = green;
                        texImage->Data[pixel*4+2] = blue;
                        texImage->Data[pixel*4+3] = alpha;
                        break;
                     default:
                        gl_problem(ctx,"Bad format in image_to_texture");
                        return NULL;
                  }
                  }
                  if (bitMask!=128) {
                     srcPtr++;
                  }
               }
            }
         }
         break;

      case GL_UNSIGNED_BYTE:
         for (pixel=0; pixel<numPixels; pixel++) {
            GLubyte red, green, blue, alpha;
            switch (image->Format) {
               case GL_COLOR_INDEX:
                  if (decode_internal_format(internalFormat)==GL_COLOR_INDEX) {
                     /* a paletted texture */
                     GLint index = ((GLubyte*)image->Data)[pixel];
                     red = index;
                  }
                  else {
                     /* convert color index to RGBA */
                     GLint index = ((GLubyte*)image->Data)[pixel];
                     red   = 255.0F * ctx->Pixel.MapItoR[index];
                     green = 255.0F * ctx->Pixel.MapItoG[index];
                     blue  = 255.0F * ctx->Pixel.MapItoB[index];
                     alpha = 255.0F * ctx->Pixel.MapItoA[index];
                  }
                  break;
               case GL_RGB:
                  /* Fetch image RGBA values */
                  red   = ((GLubyte*) image->Data)[pixel*3+0];
                  green = ((GLubyte*) image->Data)[pixel*3+1];
                  blue  = ((GLubyte*) image->Data)[pixel*3+2];
                  alpha = 255;
                  break;
               case GL_BGR_EXT:
                   blue  = ((GLubyte*) image->Data)[pixel*3+0];
                   green = ((GLubyte*) image->Data)[pixel*3+1];
                   red   = ((GLubyte*) image->Data)[pixel*3+2];
                   alpha = 255;
                 break;
               case GL_RGBA:
                  red   = ((GLubyte*) image->Data)[pixel*4+0];
                  green = ((GLubyte*) image->Data)[pixel*4+1];
                  blue  = ((GLubyte*) image->Data)[pixel*4+2];
                  alpha = ((GLubyte*) image->Data)[pixel*4+3];
                  break;
               case GL_BGRA_EXT:
                  blue  = ((GLubyte*) image->Data)[pixel*4+0];
                  green = ((GLubyte*) image->Data)[pixel*4+1];
                  red   = ((GLubyte*) image->Data)[pixel*4+2];
                  alpha = ((GLubyte*) image->Data)[pixel*4+3];
                  break;
               case GL_RED:
                  red   = ((GLubyte*) image->Data)[pixel];
                  green = 0;
                  blue  = 0;
                  alpha = 255;
                  break;
               case GL_GREEN:
                  red   = 0;
                  green = ((GLubyte*) image->Data)[pixel];
                  blue  = 0;
                  alpha = 255;
                  break;
               case GL_BLUE:
                  red   = 0;
                  green = 0;
                  blue  = ((GLubyte*) image->Data)[pixel];
                  alpha = 255;
                  break;
               case GL_ALPHA:
                  red   = 0;
                  green = 0;
                  blue  = 0;
                  alpha = ((GLubyte*) image->Data)[pixel];
                  break;
               case GL_LUMINANCE: 
                  red   = ((GLubyte*) image->Data)[pixel];
                  green = red;
                  blue  = red;
                  alpha = 255;
                  break;
              case GL_LUMINANCE_ALPHA:
                  red   = ((GLubyte*) image->Data)[pixel*2+0];
                  green = red;
                  blue  = red;
                  alpha = ((GLubyte*) image->Data)[pixel*2+1];
                  break;
              default:
                 gl_problem(ctx,"Bad format (2) in image_to_texture");
                 return NULL;
            }
            
            if (scaleOrBias || ctx->Pixel.MapColorFlag) {
               /* Apply RGBA scale and bias */
               GLfloat r = red   * (1.0F/255.0F);
               GLfloat g = green * (1.0F/255.0F);
               GLfloat b = blue  * (1.0F/255.0F);
               GLfloat a = alpha * (1.0F/255.0F);
               if (scaleOrBias) {
                  /* r,g,b,a now in [0,1] */
                  r = r * ctx->Pixel.RedScale   + ctx->Pixel.RedBias;
                  g = g * ctx->Pixel.GreenScale + ctx->Pixel.GreenBias;
                  b = b * ctx->Pixel.BlueScale  + ctx->Pixel.BlueBias;
                  a = a * ctx->Pixel.AlphaScale + ctx->Pixel.AlphaBias;
                  r = CLAMP( r, 0.0F, 1.0F );
                  g = CLAMP( g, 0.0F, 1.0F );
                  b = CLAMP( b, 0.0F, 1.0F );
                  a = CLAMP( a, 0.0F, 1.0F );
               }
               /* Apply pixel maps */
               if (ctx->Pixel.MapColorFlag) {
                  GLint ir = (GLint) (r*ctx->Pixel.MapRtoRsize);
                  GLint ig = (GLint) (g*ctx->Pixel.MapGtoGsize);
                  GLint ib = (GLint) (b*ctx->Pixel.MapBtoBsize);
                  GLint ia = (GLint) (a*ctx->Pixel.MapAtoAsize);
                  r = ctx->Pixel.MapRtoR[ir];
                  g = ctx->Pixel.MapGtoG[ig];
                  b = ctx->Pixel.MapBtoB[ib];
                  a = ctx->Pixel.MapAtoA[ia];
               }
               red   = (GLint) (r * 255.0F);
               green = (GLint) (g * 255.0F);
               blue  = (GLint) (b * 255.0F);
               alpha = (GLint) (a * 255.0F);
            }

            /* store texel (components are GLubytes in [0,255]) */
            switch (texImage->Format) {
               case GL_COLOR_INDEX:
                  texImage->Data[pixel] = red; /* really an index */
                  break;
               case GL_ALPHA:
                  texImage->Data[pixel] = alpha;
                  break;
               case GL_LUMINANCE:
                  texImage->Data[pixel] = red;
                  break;
               case GL_LUMINANCE_ALPHA:
                  texImage->Data[pixel*2+0] = red;
                  texImage->Data[pixel*2+1] = alpha;
                  break;
               case GL_INTENSITY:
                  texImage->Data[pixel] = red;
                  break;
               case GL_RGB:
                  texImage->Data[pixel*3+0] = red;
                  texImage->Data[pixel*3+1] = green;
                  texImage->Data[pixel*3+2] = blue;
                  break;
               case GL_RGBA:
                  texImage->Data[pixel*4+0] = red;
                  texImage->Data[pixel*4+1] = green;
                  texImage->Data[pixel*4+2] = blue;
                  texImage->Data[pixel*4+3] = alpha;
                  break;
               default:
                  gl_problem(ctx,"Bad format (3) in image_to_texture");
                  return NULL;
            }
         }
         break;

      case GL_FLOAT:
         for (pixel=0; pixel<numPixels; pixel++) {
            GLfloat red, green, blue, alpha;
            switch (texImage->Format) {
               case GL_COLOR_INDEX:
                  if (decode_internal_format(internalFormat)==GL_COLOR_INDEX) {
                     /* a paletted texture */
                     GLint index = (GLint) ((GLfloat*) image->Data)[pixel];
                     red = index;
                  }
                  else {
                     GLint shift = ctx->Pixel.IndexShift;
                     GLint offset = ctx->Pixel.IndexOffset;
                     /* MapIto[RGBA]Size must be powers of two */
                     GLint rMask = ctx->Pixel.MapItoRsize-1;
                     GLint gMask = ctx->Pixel.MapItoGsize-1;
                     GLint bMask = ctx->Pixel.MapItoBsize-1;
                     GLint aMask = ctx->Pixel.MapItoAsize-1;
                     /* Fetch image color index */
                     GLint index = (GLint) ((GLfloat*) image->Data)[pixel];
                     /* apply index shift and offset */
                     if (shift>=0) {
                        index = (index << shift) + offset;
                     }
                     else {
                        index = (index >> -shift) + offset;
                     }
                     /* convert index to RGBA */
                     red   = ctx->Pixel.MapItoR[index & rMask];
                     green = ctx->Pixel.MapItoG[index & gMask];
                     blue  = ctx->Pixel.MapItoB[index & bMask];
                     alpha = ctx->Pixel.MapItoA[index & aMask];
                  }
                  break;
               case GL_RGB:
                  /* Fetch image RGBA values */
                  red   = ((GLfloat*) image->Data)[pixel*3+0];
                  green = ((GLfloat*) image->Data)[pixel*3+1];
                  blue  = ((GLfloat*) image->Data)[pixel*3+2];
                  alpha = 1.0;
                  break;
               case GL_BGR_EXT:
                  blue  = ((GLfloat*) image->Data)[pixel*3+0];
                  green = ((GLfloat*) image->Data)[pixel*3+1];
                  red   = ((GLfloat*) image->Data)[pixel*3+2];
                  alpha = 1.0;
                  break;
               case GL_RGBA:
                  red   = ((GLfloat*) image->Data)[pixel*4+0];
                  green = ((GLfloat*) image->Data)[pixel*4+1];
                  blue  = ((GLfloat*) image->Data)[pixel*4+2];
                  alpha = ((GLfloat*) image->Data)[pixel*4+3];
                  break;
               case GL_BGRA_EXT:
                  blue  = ((GLfloat*) image->Data)[pixel*4+0];
                  green = ((GLfloat*) image->Data)[pixel*4+1];
                  red   = ((GLfloat*) image->Data)[pixel*4+2];
                  alpha = ((GLfloat*) image->Data)[pixel*4+3];
                  break;
               case GL_RED:
                  red   = ((GLfloat*) image->Data)[pixel];
                  green = 0.0;
                  blue  = 0.0;
                  alpha = 1.0;
                  break;
               case GL_GREEN:
                  red   = 0.0;
                  green = ((GLfloat*) image->Data)[pixel];
                  blue  = 0.0;
                  alpha = 1.0;
                  break;
               case GL_BLUE:
                  red   = 0.0;
                  green = 0.0;
                  blue  = ((GLfloat*) image->Data)[pixel];
                  alpha = 1.0;
                  break;
               case GL_ALPHA:
                  red   = 0.0;
                  green = 0.0;
                  blue  = 0.0;
                  alpha = ((GLfloat*) image->Data)[pixel];
                  break;
               case GL_LUMINANCE: 
                  red   = ((GLfloat*) image->Data)[pixel];
                  green = red;
                  blue  = red;
                  alpha = 1.0;
                  break;
              case GL_LUMINANCE_ALPHA:
                  red   = ((GLfloat*) image->Data)[pixel*2+0];
                  green = red;
                  blue  = red;
                  alpha = ((GLfloat*) image->Data)[pixel*2+1];
                  break;
               default:
                  gl_problem(ctx,"Bad format (4) in image_to_texture");
                  return NULL;
            }
            
            if (image->Format!=GL_COLOR_INDEX) {
               /* Apply RGBA scale and bias */
               if (scaleOrBias) {
                  red   = red   * ctx->Pixel.RedScale   + ctx->Pixel.RedBias;
                  green = green * ctx->Pixel.GreenScale + ctx->Pixel.GreenBias;
                  blue  = blue  * ctx->Pixel.BlueScale  + ctx->Pixel.BlueBias;
                  alpha = alpha * ctx->Pixel.AlphaScale + ctx->Pixel.AlphaBias;
                  red   = CLAMP( red,    0.0F, 1.0F );
                  green = CLAMP( green,  0.0F, 1.0F );
                  blue  = CLAMP( blue,   0.0F, 1.0F );
                  alpha = CLAMP( alpha,  0.0F, 1.0F );
               }
               /* Apply pixel maps */
               if (ctx->Pixel.MapColorFlag) {
                  GLint ir = (GLint) (red  *ctx->Pixel.MapRtoRsize);
                  GLint ig = (GLint) (green*ctx->Pixel.MapGtoGsize);
                  GLint ib = (GLint) (blue *ctx->Pixel.MapBtoBsize);
                  GLint ia = (GLint) (alpha*ctx->Pixel.MapAtoAsize);
                  red   = ctx->Pixel.MapRtoR[ir];
                  green = ctx->Pixel.MapGtoG[ig];
                  blue  = ctx->Pixel.MapBtoB[ib];
                  alpha = ctx->Pixel.MapAtoA[ia];
               }
            }

            /* store texel (components are GLubytes in [0,255]) */
            switch (texImage->Format) {
               case GL_COLOR_INDEX:
                  /* a paletted texture */
                  texImage->Data[pixel] = (GLint) (red * 255.0F);
                  break;
               case GL_ALPHA:
                  texImage->Data[pixel] = (GLint) (alpha * 255.0F);
                  break;
               case GL_LUMINANCE:
                  texImage->Data[pixel] = (GLint) (red * 255.0F);
                  break;
               case GL_LUMINANCE_ALPHA:
                  texImage->Data[pixel*2+0] = (GLint) (red * 255.0F);
                  texImage->Data[pixel*2+1] = (GLint) (alpha * 255.0F);
                  break;
               case GL_INTENSITY:
                  texImage->Data[pixel] = (GLint) (red * 255.0F);
                  break;
               case GL_RGB:
                  texImage->Data[pixel*3+0] = (GLint) (red   * 255.0F);
                  texImage->Data[pixel*3+1] = (GLint) (green * 255.0F);
                  texImage->Data[pixel*3+2] = (GLint) (blue  * 255.0F);
                  break;
               case GL_RGBA:
                  texImage->Data[pixel*4+0] = (GLint) (red   * 255.0F);
                  texImage->Data[pixel*4+1] = (GLint) (green * 255.0F);
                  texImage->Data[pixel*4+2] = (GLint) (blue  * 255.0F);
                  texImage->Data[pixel*4+3] = (GLint) (alpha * 255.0F);
                  break;
               default:
                  gl_problem(ctx,"Bad format (5) in image_to_texture");
                  return NULL;
            }
         }
         break;

      default:
         gl_problem(ctx, "Bad image type in image_to_texture");
         return NULL;
   }

   return texImage;
}



/*
 * glTexImage[123]D can accept a NULL image pointer.  In this case we
 * create a texture image with unspecified image contents per the OpenGL
 * spec.
 */
static struct gl_texture_image *
make_null_texture( GLcontext *ctx, GLenum internalFormat,
                   GLsizei width, GLsizei height, GLsizei depth, GLint border )
{
   GLint components;
   struct gl_texture_image *texImage;
   size_t numPixels;

   /*internalFormat = decode_internal_format(internalFormat);*/
   components = components_in_intformat(internalFormat);
   numPixels = (size_t) width * height * depth;

   texImage = gl_alloc_texture_image();
   if (!texImage)
      return NULL;

   texImage->Format = decode_internal_format(internalFormat);
   texImage->IntFormat = internalFormat;
   texImage->Border = border;
   texImage->Width = width;
   texImage->Height = height;
   texImage->Depth = depth;
   texImage->Width2 = width - 2*border;
   texImage->WidthLog2 = floor_logbase2(texImage->Width2);
   if (height==1)  /* 1-D texture */
      texImage->HeightLog2 = 0;
   else {
      texImage->Height2 = height - 2*border;
      texImage->HeightLog2 = floor_logbase2(texImage->Height2);
   }
   if (height==1)
      texImage->Height2 = 1;
   if (depth==1) {
      texImage->Depth2 = 1;
      texImage->DepthLog2 = 0;
   }
   else {
      texImage->Depth2 = depth - 2*border;
      texImage->DepthLog2 = floor_logbase2(texImage->Depth2);
   }
   texImage->MaxLog2 = MAX2( texImage->WidthLog2,
                             MAX2( texImage->HeightLog2,
                                   texImage->DepthLog2 ) );

   /* XXX should we really allocate memory for the image or let it be NULL? */
   /*texImage->Data = NULL;*/

   texImage->Data = numPixels > 0 ?
                    (GLubyte *) malloc( numPixels * components ) : NULL;

   /*
    * Let's see if anyone finds this.  If glTexImage2D() is called with
    * a NULL image pointer then load the texture image with something
    * interesting instead of leaving it indeterminate.
    */
   if (texImage->Data) {
      char message[8][32] = {
         "   X   X  XXXXX   XXX     X    ",
         "   XX XX  X      X   X   X X   ",
         "   X X X  X      X      X   X  ",
         "   X   X  XXXX    XXX   XXXXX  ",
         "   X   X  X          X  X   X  ",
         "   X   X  X      X   X  X   X  ",
         "   X   X  XXXXX   XXX   X   X  ",
         "                               "
      };

      GLubyte *imgPtr = texImage->Data;
      GLint d, i, j, k;
      for (d=0;d<depth;d++) {
         for (i=0;i<height;i++) {
            GLint srcRow = 7 - i % 8;
            for (j=0;j<width;j++) {
               GLint srcCol = j % 32;
               GLubyte texel = (message[srcRow][srcCol]=='X') ? 255 : 70;
               for (k=0;k<components;k++) {
                  *imgPtr++ = texel;
               }
            }
         }
      }
   }

   return texImage;
}



static GLboolean packed_type_error_check( GLcontext *ctx, GLenum format,
                                          GLenum type, const char *where )
{
   if (gl_sizeof_packed_type(type) &&
       !gl_packed_type_matches_format(type, format)) {
      gl_error(ctx, GL_INVALID_OPERATION, where);
      return GL_TRUE;
   }
   return GL_FALSE;
}




/*
 * Test glTexImagee1D() parameters for errors.
 * Return:  GL_TRUE = an error was detected, GL_FALSE = no errors
 */
static GLboolean texture_1d_error_check( GLcontext *ctx, GLenum target,
                                         GLint level, GLenum internalFormat,
                                         GLenum format, GLenum type,
                                         GLint width, GLint border )
{
   GLint iformat;
   if (target!=GL_TEXTURE_1D && target!=GL_PROXY_TEXTURE_1D) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexImage1D" );
      return GL_TRUE;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexImage1D(level)" );
      return GL_TRUE;
   }
   iformat = decode_internal_format( internalFormat );
   if (iformat<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexImage1D(internalFormat)" );
      return GL_TRUE;
   }
   if (border!=0 && border!=1) {
      if (target!=GL_PROXY_TEXTURE_1D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage1D(border)" );
      }
      return GL_TRUE;
   }
   if (width<2*border
       || width>(MAX_TEXTURE_SIZE >> level)+2*border) {
      if (target!=GL_PROXY_TEXTURE_1D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage1D(width)" );
      }
      return GL_TRUE;
   }
   if (!ctx->AllowNpotTextures && logbase2( width-2*border )<0) {
      gl_error( ctx, GL_INVALID_VALUE,
               "glTexImage1D(width != 2^k + 2*border)");
      return GL_TRUE;
   }
   switch (format) {
      case GL_COLOR_INDEX:
      case GL_RED:
      case GL_GREEN:
      case GL_BLUE:
      case GL_ALPHA:
      case GL_RGB:
      case GL_BGR_EXT:
      case GL_RGBA:
      case GL_BGRA_EXT:
      case GL_LUMINANCE:
      case GL_LUMINANCE_ALPHA:
         /* OK */
         break;
      default:
         gl_error( ctx, GL_INVALID_ENUM, "glTexImage1D(format)" );
         return GL_TRUE;
   }
   switch (type) {
      case GL_UNSIGNED_BYTE:
      case GL_BYTE:
      case GL_UNSIGNED_SHORT:
      case GL_SHORT:
      case GL_UNSIGNED_INT:
      case GL_INT:
      case GL_FLOAT:
      case GL_BITMAP:
      case GL_UNSIGNED_BYTE_3_3_2:
      case GL_UNSIGNED_BYTE_2_3_3_REV:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_5_6_5_REV:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_4_4_4_4_REV:
      case GL_UNSIGNED_SHORT_5_5_5_1:
      case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      case GL_UNSIGNED_INT_8_8_8_8:
      case GL_UNSIGNED_INT_8_8_8_8_REV:
      case GL_UNSIGNED_INT_10_10_10_2:
      case GL_UNSIGNED_INT_2_10_10_10_REV:
         /* OK */
         break;
      default:
         gl_error( ctx, GL_INVALID_ENUM, "glTexImage1D(type)" );
         return GL_TRUE;
   }
   if (type==GL_BITMAP && format!=GL_COLOR_INDEX) {
      gl_error(ctx, GL_INVALID_ENUM, "glTexImage1D(format/type)");
      return GL_TRUE;
   }
   if (packed_type_error_check(ctx, format, type,
                               "glTexImage1D(format/type)"))
      return GL_TRUE;
   return GL_FALSE;
}


/*
 * Test glTexImagee2D() parameters for errors.
 * Return:  GL_TRUE = an error was detected, GL_FALSE = no errors
 */
static GLboolean texture_2d_error_check( GLcontext *ctx, GLenum target,
                                         GLint level, GLenum internalFormat,
                                         GLenum format, GLenum type,
                                         GLint width, GLint height,
                                         GLint border )
{
   GLint iformat;
   if (target!=GL_TEXTURE_2D && target!=GL_PROXY_TEXTURE_2D) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexImage2D(target)" );
      return GL_TRUE;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexImage2D(level)" );
      return GL_TRUE;
   }
   iformat = decode_internal_format( internalFormat );
   if (iformat<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexImage2D(internalFormat)" );
      return GL_TRUE;
   }
   if (border!=0 && border!=1) {
      if (target!=GL_PROXY_TEXTURE_2D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage2D(border)" );
      }
      return GL_TRUE;
   }
   if (width<2*border
       || width>(MAX_TEXTURE_SIZE >> level)+2*border) {
      if (target!=GL_PROXY_TEXTURE_2D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage2D(width)" );
      }
      return GL_TRUE;
   }
   if (height<2*border
       || height>(MAX_TEXTURE_SIZE >> level)+2*border) {
      if (target!=GL_PROXY_TEXTURE_2D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage2D(height)" );
      }
      return GL_TRUE;
   }
   if (!ctx->AllowNpotTextures && logbase2( width-2*border )<0) {
      gl_error( ctx,GL_INVALID_VALUE,
               "glTexImage2D(width != 2^k + 2*border)");
      return GL_TRUE;
   }
   if (!ctx->AllowNpotTextures && logbase2( height-2*border )<0) {
      gl_error( ctx,GL_INVALID_VALUE,
               "glTexImage2D(height != 2^k + 2*border)");
      return GL_TRUE;
   }
   switch (format) {
      case GL_COLOR_INDEX:
      case GL_RED:
      case GL_GREEN:
      case GL_BLUE:
      case GL_ALPHA:
      case GL_RGB:
      case GL_BGR_EXT:
      case GL_RGBA:
      case GL_BGRA_EXT:
      case GL_LUMINANCE:
      case GL_LUMINANCE_ALPHA:
         /* OK */
         break;
      default:
         gl_error( ctx, GL_INVALID_ENUM, "glTexImage2D(format)" );
         return GL_TRUE;
   }
   switch (type) {
      case GL_UNSIGNED_BYTE:
      case GL_BYTE:
      case GL_UNSIGNED_SHORT:
      case GL_SHORT:
      case GL_UNSIGNED_INT:
      case GL_INT:
      case GL_FLOAT:
      case GL_BITMAP:
      case GL_UNSIGNED_BYTE_3_3_2:
      case GL_UNSIGNED_BYTE_2_3_3_REV:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_5_6_5_REV:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_4_4_4_4_REV:
      case GL_UNSIGNED_SHORT_5_5_5_1:
      case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      case GL_UNSIGNED_INT_8_8_8_8:
      case GL_UNSIGNED_INT_8_8_8_8_REV:
      case GL_UNSIGNED_INT_10_10_10_2:
      case GL_UNSIGNED_INT_2_10_10_10_REV:
         /* OK */
         break;
      default:
         gl_error( ctx, GL_INVALID_ENUM, "glTexImage2D(type)" );
         return GL_TRUE;
   }
   if (type==GL_BITMAP && format!=GL_COLOR_INDEX) {
      gl_error(ctx, GL_INVALID_ENUM, "glTexImage2D(format/type)");
      return GL_TRUE;
   }
   if (packed_type_error_check(ctx, format, type,
                               "glTexImage2D(format/type)"))
      return GL_TRUE;
   return GL_FALSE;
}



/*
 * Test glTexImage3D() parameters for errors.
 * Return:  GL_TRUE = an error was detected, GL_FALSE = no errors
 */
static GLboolean texture_3d_error_check( GLcontext *ctx, GLenum target,
                                         GLint level, GLenum internalFormat,
                                         GLenum format, GLenum type,
                                         GLint width, GLint height,
                                         GLint depth, GLint border )
{
   GLint iformat;

   if (target!=GL_TEXTURE_3D && target!=GL_PROXY_TEXTURE_3D) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexImage3D(target)" );
      return GL_TRUE;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexImage3D(level)" );
      return GL_TRUE;
   }
   iformat = decode_internal_format( internalFormat );
   if (iformat<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexImage3D(internalFormat)" );
      return GL_TRUE;
   }
   if (border!=0 && border!=1) {
      if (target!=GL_PROXY_TEXTURE_3D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage3D(border)" );
      }
      return GL_TRUE;
   }
   if (width<2*border
       || width>(MAX_TEXTURE_SIZE >> level)+2*border) {
      if (target!=GL_PROXY_TEXTURE_3D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage3D(width)" );
      }
      return GL_TRUE;
   }
   if (height<2*border
       || height>(MAX_TEXTURE_SIZE >> level)+2*border) {
      if (target!=GL_PROXY_TEXTURE_3D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage3D(height)" );
      }
      return GL_TRUE;
   }
   if (depth<2*border
       || depth>(MAX_TEXTURE_SIZE >> level)+2*border) {
      if (target!=GL_PROXY_TEXTURE_3D) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexImage3D(depth)" );
      }
      return GL_TRUE;
   }
   if (!ctx->AllowNpotTextures && logbase2( width-2*border )<0) {
      gl_error( ctx, GL_INVALID_VALUE,
                "glTexImage3D(width != 2^k + 2*border)" );
      return GL_TRUE;
   }
   if (!ctx->AllowNpotTextures && logbase2( height-2*border )<0) {
      gl_error( ctx, GL_INVALID_VALUE,
                "glTexImage3D(height != 2^k + 2*border)" );
      return GL_TRUE;
   }
   if (!ctx->AllowNpotTextures && logbase2( depth-2*border )<0) {
      gl_error( ctx, GL_INVALID_VALUE,
                "glTexImage3D(depth != 2^k + 2*border)" );
      return GL_TRUE;
   }
   switch (format) {
      case GL_COLOR_INDEX:
      case GL_RED:
      case GL_GREEN:
      case GL_BLUE:
      case GL_ALPHA:
      case GL_RGB:
      case GL_BGR_EXT:
      case GL_RGBA:
      case GL_BGRA_EXT:
      case GL_LUMINANCE:
      case GL_LUMINANCE_ALPHA:
         break;
      default:
         gl_error( ctx, GL_INVALID_ENUM, "glTexImage3D(format)" );
         return GL_TRUE;
   }
   switch (type) {
      case GL_UNSIGNED_BYTE:
      case GL_BYTE:
      case GL_UNSIGNED_SHORT:
      case GL_SHORT:
      case GL_UNSIGNED_INT:
      case GL_INT:
      case GL_FLOAT:
      case GL_BITMAP:
      case GL_UNSIGNED_BYTE_3_3_2:
      case GL_UNSIGNED_BYTE_2_3_3_REV:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_5_6_5_REV:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_4_4_4_4_REV:
      case GL_UNSIGNED_SHORT_5_5_5_1:
      case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      case GL_UNSIGNED_INT_8_8_8_8:
      case GL_UNSIGNED_INT_8_8_8_8_REV:
      case GL_UNSIGNED_INT_10_10_10_2:
      case GL_UNSIGNED_INT_2_10_10_10_REV:
         break;
      default:
         gl_error( ctx, GL_INVALID_ENUM, "glTexImage3D(type)" );
         return GL_TRUE;
   }
   if (type==GL_BITMAP && format!=GL_COLOR_INDEX) {
      gl_error(ctx, GL_INVALID_ENUM, "glTexImage3D(format/type)");
      return GL_TRUE;
   }
   if (packed_type_error_check(ctx, format, type,
                               "glTexImage3D(format/type)"))
      return GL_TRUE;
   return GL_FALSE;
}


/*
 * Called from the API.  Note that width includes the border.
 */
void gl_TexImage1D( GLcontext *ctx,
                    GLenum target, GLint level, GLint internalformat,
		    GLsizei width, GLint border, GLenum format,
		    GLenum type, struct gl_image *image )
{
   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexImage1D" );
      return;
   }

   if (target==GL_TEXTURE_1D) {
      struct gl_texture_image *teximage;
      if (texture_1d_error_check( ctx, target, level, internalformat,
                                  format, type, width, border )) {
         /* error in texture image was detected */
         return;
      }

      /* free current texture image, if any */
      if (ctx->Texture.Current1D->Image[level]) {
         gl_free_texture_image( ctx->Texture.Current1D->Image[level] );
      }

      /* make new texture from source image */
      if (image) {
         teximage = image_to_texture(ctx, image, internalformat, border);
      }
      else {
         teximage = make_null_texture(ctx, internalformat,
                                      width, 1, 1, border);
      }

      /* install new texture image */
      ctx->Texture.Current1D->Image[level] = teximage;
      ctx->Texture.Current1D->Dirty = GL_TRUE;
      ctx->Texture.AnyDirty = GL_TRUE;
      ctx->NewState |= NEW_TEXTURING;

      /* free the source image */
      if (image && image->RefCount==0) {
         /* if RefCount>0 then image must be in a display list */
         gl_free_image(image);
      }

      /* tell driver about change */
      if (ctx->Driver.TexImage) {
         (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_1D,
                                  ctx->Texture.Current1D,
                                  level, internalformat, teximage );
      }
   }
   else if (target==GL_PROXY_TEXTURE_1D) {
      /* Proxy texture: check for errors and update proxy state */
      if (texture_1d_error_check( ctx, target, level, internalformat,
                                  format, type, width, border )) {
         if (level>=0 && level<MAX_TEXTURE_LEVELS) {
            MEMSET( ctx->Texture.Proxy1D->Image[level], 0,
                    sizeof(struct gl_texture_image) );
         }
      }
      else {
         ctx->Texture.Proxy1D->Image[level]->Format = internalformat;
         ctx->Texture.Proxy1D->Image[level]->Border = border;
         ctx->Texture.Proxy1D->Image[level]->Width = width;
         ctx->Texture.Proxy1D->Image[level]->Height = 1;
      }
      if (image && image->RefCount==0) {
         /* if RefCount>0 then image must be in a display list */
         gl_free_image(image);
      }
   }
   else {
      gl_error( ctx, GL_INVALID_ENUM, "glTexImage1D(target)" );
      return;
   }
}




/*
 * Called by the API or display list executor.
 * Note that width and height include the border.
 */
void gl_TexImage2D( GLcontext *ctx,
                    GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLint border,
                    GLenum format, GLenum type,
                    struct gl_image *image )
{
   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexImage2D" );
      return;
   }

   if (target==GL_TEXTURE_2D) {
      struct gl_texture_image *teximage;
      if (texture_2d_error_check( ctx, target, level, internalformat,
                                  format, type, width, height, border )) {
         /* error in texture image was detected */
         return;
      }

      /* free current texture image, if any */
      if (ctx->Texture.Current2D->Image[level]) {
         gl_free_texture_image( ctx->Texture.Current2D->Image[level] );
      }

      /* make new texture from source image */
      if (image) {
         teximage = image_to_texture(ctx, image, internalformat, border);
      }
      else {
         teximage = make_null_texture(ctx, internalformat,
                                      width, height, 1, border);
      }

      /* install new texture image */
      ctx->Texture.Current2D->Image[level] = teximage;
      ctx->Texture.Current2D->Dirty = GL_TRUE;
      ctx->Texture.AnyDirty = GL_TRUE;
      ctx->NewState |= NEW_TEXTURING;

      /* free the source image */
      if (image && image->RefCount==0) {
         /* if RefCount>0 then image must be in a display list */
         gl_free_image(image);
      }

      /* tell driver about change */
      if (ctx->Driver.TexImage) {
         (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_2D,
                                  ctx->Texture.Current2D,
                                  level, internalformat, teximage );
      }
   }
   else if (target==GL_PROXY_TEXTURE_2D) {
      /* Proxy texture: check for errors and update proxy state */
      if (texture_2d_error_check( ctx, target, level, internalformat,
                                  format, type, width, height, border )) {
         if (level>=0 && level<MAX_TEXTURE_LEVELS) {
            MEMSET( ctx->Texture.Proxy2D->Image[level], 0,
                    sizeof(struct gl_texture_image) );
         }
      }
      else {
         ctx->Texture.Proxy2D->Image[level]->Format = internalformat;
         ctx->Texture.Proxy2D->Image[level]->Border = border;
         ctx->Texture.Proxy2D->Image[level]->Width = width;
         ctx->Texture.Proxy2D->Image[level]->Height = height;
      }
      if (image && image->RefCount==0) {
         /* if RefCount>0 then image must be in a display list */
         gl_free_image(image);
      }
   }
   else {
      gl_error( ctx, GL_INVALID_ENUM, "glTexImage2D(target)" );
      return;
   }
}



/*
 * Called by the API or display list executor.
 * Width, height, and depth include the border.
 */
void gl_TexImage3D( GLcontext *ctx,
                    GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLsizei depth,
                    GLint border, GLenum format, GLenum type,
                    struct gl_image *image )
{
   struct gl_texture_image *teximage;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexImage3D" );
      if (image && image->RefCount==0)
         gl_free_image(image);
      return;
   }

   if (texture_3d_error_check( ctx, target, level, internalformat,
                               format, type, width, height, depth, border )) {
      if (target==GL_PROXY_TEXTURE_3D &&
          level>=0 && level<MAX_TEXTURE_LEVELS) {
         MEMSET( ctx->Texture.Proxy3D->Image[level], 0,
                 sizeof(struct gl_texture_image) );
      }
      if (image && image->RefCount==0)
         gl_free_image(image);
      return;
   }

   if (target==GL_TEXTURE_3D) {
      if (image) {
         teximage = image_to_texture(ctx, image, internalformat, border);
      }
      else {
         teximage = make_null_texture(ctx, internalformat,
                                      width, height, depth, border);
      }

      if (!teximage ||
          (width>0 && height>0 && depth>0 && !teximage->Data)) {
         if (teximage)
            gl_free_texture_image(teximage);
         if (image && image->RefCount==0)
            gl_free_image(image);
         gl_error( ctx, GL_OUT_OF_MEMORY, "glTexImage3D" );
         return;
      }

      if (ctx->Texture.Current3D->Image[level]) {
         gl_free_texture_image( ctx->Texture.Current3D->Image[level] );
      }
      ctx->Texture.Current3D->Image[level] = teximage;
      ctx->Texture.Current3D->Dirty = GL_TRUE;
      ctx->Texture.AnyDirty = GL_TRUE;
      ctx->NewState |= NEW_TEXTURING;

      if (image && image->RefCount==0)
         gl_free_image(image);

      if (ctx->Driver.TexImage) {
         (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_3D,
                                  ctx->Texture.Current3D,
                                  level, internalformat, teximage );
      }
   }
   else {
      struct gl_texture_image *proxy = ctx->Texture.Proxy3D->Image[level];

      MEMSET( proxy, 0, sizeof(*proxy) );
      proxy->Format = decode_internal_format(internalformat);
      proxy->IntFormat = internalformat;
      proxy->Border = border;
      proxy->Width = width;
      proxy->Height = height;
      proxy->Depth = depth;
      proxy->Width2 = width - 2*border;
      proxy->Height2 = height - 2*border;
      proxy->Depth2 = depth - 2*border;

      if (image && image->RefCount==0)
         gl_free_image(image);
   }
}



static GLboolean texture_image_texel( const struct gl_texture_image *image,
                                      size_t pixel, GLfloat rgba[4] )
{
   const GLubyte *source;

   switch (image->Format) {
      case GL_ALPHA:
         rgba[0] = rgba[1] = rgba[2] = 0.0F;
         rgba[3] = UBYTE_TO_FLOAT(image->Data[pixel]);
         return GL_TRUE;
      case GL_LUMINANCE:
         rgba[0] = UBYTE_TO_FLOAT(image->Data[pixel]);
         rgba[1] = rgba[2] = 0.0F;
         rgba[3] = 1.0F;
         return GL_TRUE;
      case GL_LUMINANCE_ALPHA:
         source = image->Data + pixel * 2;
         rgba[0] = UBYTE_TO_FLOAT(source[0]);
         rgba[1] = rgba[2] = 0.0F;
         rgba[3] = UBYTE_TO_FLOAT(source[1]);
         return GL_TRUE;
      case GL_INTENSITY:
         rgba[0] = UBYTE_TO_FLOAT(image->Data[pixel]);
         rgba[1] = rgba[2] = 0.0F;
         rgba[3] = 1.0F;
         return GL_TRUE;
      case GL_RGB:
         source = image->Data + pixel * 3;
         rgba[0] = UBYTE_TO_FLOAT(source[0]);
         rgba[1] = UBYTE_TO_FLOAT(source[1]);
         rgba[2] = UBYTE_TO_FLOAT(source[2]);
         rgba[3] = 1.0F;
         return GL_TRUE;
      case GL_RGBA:
         source = image->Data + pixel * 4;
         rgba[0] = UBYTE_TO_FLOAT(source[0]);
         rgba[1] = UBYTE_TO_FLOAT(source[1]);
         rgba[2] = UBYTE_TO_FLOAT(source[2]);
         rgba[3] = UBYTE_TO_FLOAT(source[3]);
         return GL_TRUE;
      default:
         return GL_FALSE;
   }
}



static GLint texture_output_components( GLenum format, const GLfloat rgba[4],
                                        GLfloat output[4] )
{
   switch (format) {
      case GL_RED:
         output[0] = rgba[0];
         return 1;
      case GL_GREEN:
         output[0] = rgba[1];
         return 1;
      case GL_BLUE:
         output[0] = rgba[2];
         return 1;
      case GL_ALPHA:
         output[0] = rgba[3];
         return 1;
      case GL_LUMINANCE:
         output[0] = CLAMP(rgba[0] + rgba[1] + rgba[2], 0.0F, 1.0F);
         return 1;
      case GL_LUMINANCE_ALPHA:
         output[0] = CLAMP(rgba[0] + rgba[1] + rgba[2], 0.0F, 1.0F);
         output[1] = rgba[3];
         return 2;
      case GL_RGB:
         output[0] = rgba[0];
         output[1] = rgba[1];
         output[2] = rgba[2];
         return 3;
      case GL_BGR_EXT:
         output[0] = rgba[2];
         output[1] = rgba[1];
         output[2] = rgba[0];
         return 3;
      case GL_RGBA:
         output[0] = rgba[0];
         output[1] = rgba[1];
         output[2] = rgba[2];
         output[3] = rgba[3];
         return 4;
      case GL_BGRA_EXT:
         output[0] = rgba[2];
         output[1] = rgba[1];
         output[2] = rgba[0];
         output[3] = rgba[3];
         return 4;
      default:
         return 0;
   }
}



static GLuint texture_packed_component( GLfloat value, GLuint bits )
{
   value = CLAMP(value, 0.0F, 1.0F);
   return (GLuint) (value * (GLfloat) ((1U << bits) - 1U));
}



static void texture_store_scalar( GLenum type, GLfloat value,
                                  GLboolean swapBytes, GLvoid *destination )
{
   GLushort word;
   GLuint dword;

   value = CLAMP(value, 0.0F, 1.0F);
   switch (type) {
      case GL_UNSIGNED_BYTE:
         *(GLubyte *) destination = FLOAT_TO_UBYTE(value);
         break;
      case GL_BYTE:
      {
         GLbyte byte = (GLbyte) FLOAT_TO_BYTE(value);
         MEMCPY(destination, &byte, sizeof(byte));
         break;
      }
      case GL_UNSIGNED_SHORT:
         word = FLOAT_TO_USHORT(value);
         if (swapBytes)
            word = (word >> 8) | (word << 8);
         MEMCPY(destination, &word, sizeof(word));
         break;
      case GL_SHORT:
      {
         GLshort shortValue = (GLshort) FLOAT_TO_SHORT(value);
         MEMCPY(&word, &shortValue, sizeof(word));
         if (swapBytes)
            word = (word >> 8) | (word << 8);
         MEMCPY(destination, &word, sizeof(word));
         break;
      }
      case GL_UNSIGNED_INT:
         dword = FLOAT_TO_UINT(value);
         if (swapBytes) {
            dword = (dword >> 24) |
                    ((dword >> 8) & 0x0000ff00) |
                    ((dword << 8) & 0x00ff0000) |
                    (dword << 24);
         }
         MEMCPY(destination, &dword, sizeof(dword));
         break;
      case GL_INT:
      {
         GLint intValue = FLOAT_TO_INT(value);
         MEMCPY(&dword, &intValue, sizeof(dword));
         if (swapBytes) {
            dword = (dword >> 24) |
                    ((dword >> 8) & 0x0000ff00) |
                    ((dword << 8) & 0x00ff0000) |
                    (dword << 24);
         }
         MEMCPY(destination, &dword, sizeof(dword));
         break;
      }
      case GL_FLOAT:
         MEMCPY(&dword, &value, sizeof(dword));
         if (swapBytes) {
            dword = (dword >> 24) |
                    ((dword >> 8) & 0x0000ff00) |
                    ((dword << 8) & 0x00ff0000) |
                    (dword << 24);
         }
         MEMCPY(destination, &dword, sizeof(dword));
         break;
   }
}



static void texture_store_packed( GLenum type, const GLfloat component[4],
                                  GLboolean swapBytes, GLvoid *destination )
{
   GLubyte byte;
   GLushort word;
   GLuint dword;

   switch (type) {
      case GL_UNSIGNED_BYTE_3_3_2:
         byte = (texture_packed_component(component[0], 3) << 5) |
                (texture_packed_component(component[1], 3) << 2) |
                texture_packed_component(component[2], 2);
         MEMCPY(destination, &byte, sizeof(byte));
         return;
      case GL_UNSIGNED_BYTE_2_3_3_REV:
         byte = texture_packed_component(component[0], 3) |
                (texture_packed_component(component[1], 3) << 3) |
                (texture_packed_component(component[2], 2) << 6);
         MEMCPY(destination, &byte, sizeof(byte));
         return;
      case GL_UNSIGNED_SHORT_5_6_5:
         word = (texture_packed_component(component[0], 5) << 11) |
                (texture_packed_component(component[1], 6) << 5) |
                texture_packed_component(component[2], 5);
         break;
      case GL_UNSIGNED_SHORT_5_6_5_REV:
         word = texture_packed_component(component[0], 5) |
                (texture_packed_component(component[1], 6) << 5) |
                (texture_packed_component(component[2], 5) << 11);
         break;
      case GL_UNSIGNED_SHORT_4_4_4_4:
         word = (texture_packed_component(component[0], 4) << 12) |
                (texture_packed_component(component[1], 4) << 8) |
                (texture_packed_component(component[2], 4) << 4) |
                texture_packed_component(component[3], 4);
         break;
      case GL_UNSIGNED_SHORT_4_4_4_4_REV:
         word = texture_packed_component(component[0], 4) |
                (texture_packed_component(component[1], 4) << 4) |
                (texture_packed_component(component[2], 4) << 8) |
                (texture_packed_component(component[3], 4) << 12);
         break;
      case GL_UNSIGNED_SHORT_5_5_5_1:
         word = (texture_packed_component(component[0], 5) << 11) |
                (texture_packed_component(component[1], 5) << 6) |
                (texture_packed_component(component[2], 5) << 1) |
                texture_packed_component(component[3], 1);
         break;
      case GL_UNSIGNED_SHORT_1_5_5_5_REV:
         word = texture_packed_component(component[0], 5) |
                (texture_packed_component(component[1], 5) << 5) |
                (texture_packed_component(component[2], 5) << 10) |
                (texture_packed_component(component[3], 1) << 15);
         break;
      case GL_UNSIGNED_INT_8_8_8_8:
         dword = (texture_packed_component(component[0], 8) << 24) |
                 (texture_packed_component(component[1], 8) << 16) |
                 (texture_packed_component(component[2], 8) << 8) |
                 texture_packed_component(component[3], 8);
         goto store_dword;
      case GL_UNSIGNED_INT_8_8_8_8_REV:
         dword = texture_packed_component(component[0], 8) |
                 (texture_packed_component(component[1], 8) << 8) |
                 (texture_packed_component(component[2], 8) << 16) |
                 (texture_packed_component(component[3], 8) << 24);
         goto store_dword;
      case GL_UNSIGNED_INT_10_10_10_2:
         dword = (texture_packed_component(component[0], 10) << 22) |
                 (texture_packed_component(component[1], 10) << 12) |
                 (texture_packed_component(component[2], 10) << 2) |
                 texture_packed_component(component[3], 2);
         goto store_dword;
      default:
         dword = texture_packed_component(component[0], 10) |
                 (texture_packed_component(component[1], 10) << 10) |
                 (texture_packed_component(component[2], 10) << 20) |
                 (texture_packed_component(component[3], 2) << 30);
store_dword:
         if (swapBytes) {
            dword = (dword >> 24) |
                    ((dword >> 8) & 0x0000ff00) |
                    ((dword << 8) & 0x00ff0000) |
                    (dword << 24);
         }
         MEMCPY(destination, &dword, sizeof(dword));
         return;
   }

   if (swapBytes)
      word = (word >> 8) | (word << 8);
   MEMCPY(destination, &word, sizeof(word));
}



void gl_GetTexImage( GLcontext *ctx, GLenum target, GLint level, GLenum format,
                     GLenum type, GLvoid *pixels )
{
   struct gl_texture_object *texture;
   struct gl_texture_image *image;
   struct gl_pixelstore_attrib pack;
   GLint components;
   GLint packedSize;
   GLint scalarSize;
   GLint img, row, column;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error(ctx, GL_INVALID_OPERATION, "glGetTexImage");
      return;
   }

   switch (target) {
      case GL_TEXTURE_1D:
         texture = ctx->Texture.Current1D;
         break;
      case GL_TEXTURE_2D:
         texture = ctx->Texture.Current2D;
         break;
      case GL_TEXTURE_3D:
         texture = ctx->Texture.Current3D;
         break;
      default:
         gl_error(ctx, GL_INVALID_ENUM, "glGetTexImage(target)");
         return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error(ctx, GL_INVALID_VALUE, "glGetTexImage(level)");
      return;
   }

   components = gl_components_in_format(format);
   if (components<0 || format==GL_COLOR_INDEX ||
       format==GL_STENCIL_INDEX || format==GL_DEPTH_COMPONENT) {
      gl_error(ctx, GL_INVALID_ENUM, "glGetTexImage(format)");
      return;
   }

   packedSize = gl_sizeof_packed_type(type);
   scalarSize = gl_sizeof_type(type);
   if (!packedSize && scalarSize<=0) {
      gl_error(ctx, GL_INVALID_ENUM, "glGetTexImage(type)");
      return;
   }
   if (packedSize && !gl_packed_type_matches_format(type, format)) {
      gl_error(ctx, GL_INVALID_OPERATION, "glGetTexImage(format/type)");
      return;
   }

   image = texture ? texture->Image[level] : NULL;
   if (!image || !image->Data || !pixels)
      return;

   pack = ctx->Pack;
   if (target!=GL_TEXTURE_3D) {
      pack.ImageHeight = 0;
      pack.SkipImages = 0;
   }

   for (img=0; img<(GLint)image->Depth; img++) {
      for (row=0; row<(GLint)image->Height; row++) {
         for (column=0; column<(GLint)image->Width; column++) {
            GLfloat rgba[4];
            GLfloat output[4];
            size_t pixel = ((size_t)img * image->Height + row) *
                           image->Width + column;
            GLvoid *destination = gl_pixel_addr_in_image(
               &pack, pixels, image->Width, image->Height, format, type,
               img, row, column);
            GLint component;

            if (!destination || !texture_image_texel(image, pixel, rgba)) {
               gl_problem(ctx, "Bad texture image in glGetTexImage");
               return;
            }
            components = texture_output_components(format, rgba, output);
            if (packedSize) {
               texture_store_packed(type, output, pack.SwapBytes,
                                    destination);
            }
            else {
               for (component=0; component<components; component++) {
                  texture_store_scalar(type, output[component],
                                       pack.SwapBytes,
                                       (GLubyte *)destination +
                                       component * scalarSize);
               }
            }
         }
      }
   }
}




/*
 * Unpack the image data given to glTexSubImage[123]D.
 * This function is just a wrapper for gl_unpack_image() but it does
 * some extra error checking.
 */
struct gl_image *
gl_unpack_texsubimage( GLcontext *ctx, GLint width, GLint height, GLint depth,
                       GLenum format, GLenum type, const GLvoid *pixels )
{
   if (type==GL_BITMAP && format!=GL_COLOR_INDEX) {
      return NULL;
   }

   if (format==GL_STENCIL_INDEX || format==GL_DEPTH_COMPONENT){
      return NULL;
   }

   if (gl_sizeof_type(type)<0 && !gl_sizeof_packed_type(type)) {
      return NULL;
   }

   if (gl_sizeof_packed_type(type) &&
       !gl_packed_type_matches_format(type, format)) {
      return NULL;
   }

   if (width <= 0 || height <= 0 || depth <= 0 || pixels == NULL) {
      return NULL;
   }

   return gl_unpack_image3D( ctx, width, height, depth,
                             format, type, pixels );
}



void gl_TexSubImage1D( GLcontext *ctx,
                       GLenum target, GLint level, GLint xoffset,
                       GLsizei width, GLenum format, GLenum type,
                       struct gl_image *image )
{
   struct gl_texture_image *destTex;

   if (target!=GL_TEXTURE_1D) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage1D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage1D(level)" );
      return;
   }

   destTex = ctx->Texture.Current1D->Image[level];
   if (!destTex) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexSubImage1D" );
      return;
   }

   if (xoffset < -((GLint)destTex->Border)) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage1D(xoffset)" );
      return;
   }
   if (xoffset + width > destTex->Width + destTex->Border) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage1D(xoffset+width)" );
      return;
   }

   if (image) {
      /* unpacking must have been error-free */
      GLint texcomponents = components_in_intformat(destTex->Format);

      if (image->Type==GL_UNSIGNED_BYTE && texcomponents==image->Components) {
         /* Simple case, just byte copy image data into texture image */
         /* row by row. */
         GLubyte *dst = destTex->Data + texcomponents * xoffset;
         GLubyte *src = (GLubyte *) image->Data;
         MEMCPY( dst, src, width * texcomponents );
      }
      else {
         /* General case, convert image pixels into texels, scale, bias, etc */
         struct gl_texture_image *subTexImg = image_to_texture(ctx, image,
                                        destTex->IntFormat, destTex->Border);
         GLubyte *dst = destTex->Data + texcomponents * xoffset;
         GLubyte *src = subTexImg->Data;
         MEMCPY( dst, src, width * texcomponents );
         gl_free_texture_image(subTexImg);
      }

      /* if the image's reference count is zero, delete it now */
      if (image->RefCount==0) {
         gl_free_image(image);
      }

      ctx->Texture.Current1D->Dirty = GL_TRUE;
      ctx->Texture.AnyDirty = GL_TRUE;

      /* tell driver about change */
      if (ctx->Driver.TexSubImage) {
	(*ctx->Driver.TexSubImage)( ctx, GL_TEXTURE_1D,
				    ctx->Texture.Current1D, level,
				    xoffset,0,width,1,
				    ctx->Texture.Current1D->Image[level]->IntFormat,
				    destTex );
      }
      else {
	if (ctx->Driver.TexImage) {
	  (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_1D,
				   ctx->Texture.Current1D,
				   level, ctx->Texture.Current1D->Image[level]->IntFormat,
				   destTex );
	}
      }
   }
   else {
      /* if no image, an error must have occured, do more testing now */
      GLint components, size;

      if (width<0) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage1D(width)" );
         return;
      }
      if (type==GL_BITMAP && format!=GL_COLOR_INDEX) {
         gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage1D(format)" );
         return;
      }
      components = gl_components_in_format( format );
      if (components<0 || format==GL_STENCIL_INDEX
          || format==GL_DEPTH_COMPONENT){
         gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage1D(format)" );
         return;
      }
      size = gl_sizeof_type( type );
      if (size<0 && !gl_sizeof_packed_type(type)) {
         gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage1D(type)" );
         return;
      }
      if (gl_sizeof_packed_type(type) &&
          !gl_packed_type_matches_format(type, format)) {
         gl_error( ctx, GL_INVALID_OPERATION,
                   "glTexSubImage1D(format/type)" );
         return;
      }
      if (width==0)
         return;
      /* if we get here, probably ran out of memory during unpacking */
      gl_error( ctx, GL_OUT_OF_MEMORY, "glTexSubImage1D" );
   }
}



void gl_TexSubImage2D( GLcontext *ctx,
                       GLenum target, GLint level,
                       GLint xoffset, GLint yoffset,
                       GLsizei width, GLsizei height,
                       GLenum format, GLenum type,
                       struct gl_image *image )
{
   struct gl_texture_image *destTex;

   if (target!=GL_TEXTURE_2D) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage2D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage2D(level)" );
      return;
   }

   destTex = ctx->Texture.Current2D->Image[level];
   if (!destTex) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexSubImage2D" );
      return;
   }

   if (xoffset < -((GLint)destTex->Border)) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage2D(xoffset)" );
      return;
   }
   if (yoffset < -((GLint)destTex->Border)) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage2D(yoffset)" );
      return;
   }
   if (xoffset + width > destTex->Width + destTex->Border) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage2D(xoffset+width)" );
      return;
   }
   if (yoffset + height > destTex->Height + destTex->Border) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage2D(yoffset+height)" );
      return;
   }

   if (image) {
      /* unpacking must have been error-free */
      GLint texcomponents = components_in_intformat(destTex->Format);

      if (image->Type==GL_UNSIGNED_BYTE && texcomponents==image->Components) {
         /* Simple case, just byte copy image data into texture image */
         /* row by row. */
         GLubyte *dst = destTex->Data 
                      + (yoffset * destTex->Width + xoffset) * texcomponents;
         GLubyte *src = (GLubyte *) image->Data;
         GLint  j;
         for (j=0;j<height;j++) {
            MEMCPY( dst, src, width * texcomponents );
            dst += destTex->Width * texcomponents * sizeof(GLubyte);
            src += width * texcomponents * sizeof(GLubyte);
         }
      }
      else {
         /* General case, convert image pixels into texels, scale, bias, etc */
         struct gl_texture_image *subTexImg = image_to_texture(ctx, image,
                                        destTex->IntFormat, destTex->Border);
         GLubyte *dst = destTex->Data
                  + (yoffset * destTex->Width + xoffset) * texcomponents;
         GLubyte *src = subTexImg->Data;
         GLint j;
         for (j=0;j<height;j++) {
            MEMCPY( dst, src, width * texcomponents );
            dst += destTex->Width * texcomponents * sizeof(GLubyte);
            src += width * texcomponents * sizeof(GLubyte);
         }
         gl_free_texture_image(subTexImg);
      }

      /* if the image's reference count is zero, delete it now */
      if (image->RefCount==0) {
         gl_free_image(image);
      }

      ctx->Texture.Current2D->Dirty = GL_TRUE;
      ctx->Texture.AnyDirty = GL_TRUE;

      /* tell driver about change */
      if (ctx->Driver.TexSubImage) {
	(*ctx->Driver.TexSubImage)( ctx, GL_TEXTURE_2D, ctx->Texture.Current2D, level,
				    xoffset, yoffset, width, height,
				    ctx->Texture.Current2D->Image[level]->IntFormat,
				    destTex );
      }
      else {
	if (ctx->Driver.TexImage) {
	  (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_2D, ctx->Texture.Current2D,
				   level, ctx->Texture.Current2D->Image[level]->IntFormat,
				   destTex );
	}
      }
   }
   else {
      /* if no image, an error must have occured, do more testing now */
      GLint components, size;

      if (width<0) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage2D(width)" );
         return;
      }
      if (height<0) {
         gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage2D(height)" );
         return;
      }
      if (type==GL_BITMAP && format!=GL_COLOR_INDEX) {
         gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage1D(format)" );
         return;
      }
      components = gl_components_in_format( format );
      if (components<0 || format==GL_STENCIL_INDEX
          || format==GL_DEPTH_COMPONENT){
         gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage2D(format)" );
         return;
      }
      size = gl_sizeof_type( type );
      if (size<0 && !gl_sizeof_packed_type(type)) {
         gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage2D(type)" );
         return;
      }
      if (gl_sizeof_packed_type(type) &&
          !gl_packed_type_matches_format(type, format)) {
         gl_error( ctx, GL_INVALID_OPERATION,
                   "glTexSubImage2D(format/type)" );
         return;
      }
      if (width==0 || height==0)
         return;
      /* if we get here, probably ran out of memory during unpacking */
      gl_error( ctx, GL_OUT_OF_MEMORY, "glTexSubImage2D" );
   }
}



void gl_TexSubImage3D( GLcontext *ctx,
                       GLenum target, GLint level,
                       GLint xoffset, GLint yoffset, GLint zoffset,
                       GLsizei width, GLsizei height, GLsizei depth,
                       GLenum format, GLenum type,
                       struct gl_image *image )
{
   struct gl_texture_image *destTex;
   GLint texcomponents;
   GLint srccomponents;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexSubImage3D" );
      goto release_image;
   }
   if (target!=GL_TEXTURE_3D) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage3D(target)" );
      goto release_image;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage3D(level)" );
      goto release_image;
   }
   if (width<0 || height<0 || depth<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage3D(size)" );
      goto release_image;
   }

   srccomponents = gl_components_in_format(format);
   if (srccomponents<0 || format==GL_STENCIL_INDEX ||
       format==GL_DEPTH_COMPONENT ||
       (type==GL_BITMAP && format!=GL_COLOR_INDEX)) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage3D(format)" );
      goto release_image;
   }
   if (gl_sizeof_type(type)<0 && !gl_sizeof_packed_type(type)) {
      gl_error( ctx, GL_INVALID_ENUM, "glTexSubImage3D(type)" );
      goto release_image;
   }
   if (gl_sizeof_packed_type(type) &&
       !gl_packed_type_matches_format(type, format)) {
      gl_error( ctx, GL_INVALID_OPERATION,
                "glTexSubImage3D(format/type)" );
      goto release_image;
   }

   destTex = ctx->Texture.Current3D->Image[level];
   if (!destTex) {
      gl_error( ctx, GL_INVALID_OPERATION, "glTexSubImage3D" );
      goto release_image;
   }
   if (xoffset < -((GLint)destTex->Border) ||
       yoffset < -((GLint)destTex->Border) ||
       zoffset < -((GLint)destTex->Border) ||
       width > (GLint)destTex->Width - (GLint)destTex->Border - xoffset ||
       height > (GLint)destTex->Height - (GLint)destTex->Border - yoffset ||
       depth > (GLint)destTex->Depth - (GLint)destTex->Border - zoffset) {
      gl_error( ctx, GL_INVALID_VALUE, "glTexSubImage3D(offset+size)" );
      goto release_image;
   }

   if (width==0 || height==0 || depth==0)
      goto release_image;
   if (!image) {
      gl_error( ctx, GL_OUT_OF_MEMORY, "glTexSubImage3D" );
      return;
   }

   texcomponents = components_in_intformat(destTex->Format);
   if (image->Type==GL_UNSIGNED_BYTE &&
       texcomponents==image->Components) {
      GLubyte *srcBase = (GLubyte *) image->Data;
      GLint k, j;

      for (k=0; k<depth; k++) {
         GLubyte *dst = destTex->Data +
            ((((zoffset+(GLint)destTex->Border)+k) * destTex->Height +
              yoffset+(GLint)destTex->Border) * destTex->Width +
             xoffset+(GLint)destTex->Border) * texcomponents;
         GLubyte *src = srcBase +
            (size_t)k * width * height * texcomponents;

         for (j=0; j<height; j++) {
            MEMCPY( dst, src, (size_t)width * texcomponents );
            dst += (size_t)destTex->Width * texcomponents;
            src += (size_t)width * texcomponents;
         }
      }
   }
   else {
      struct gl_texture_image *subTexImg =
         image_to_texture(ctx, image, destTex->IntFormat, 0);
      GLubyte *srcBase;
      GLint k, j;

      if (!subTexImg) {
         gl_error( ctx, GL_OUT_OF_MEMORY, "glTexSubImage3D" );
         goto release_image;
      }
      srcBase = subTexImg->Data;
      for (k=0; k<depth; k++) {
         GLubyte *dst = destTex->Data +
            ((((zoffset+(GLint)destTex->Border)+k) * destTex->Height +
              yoffset+(GLint)destTex->Border) * destTex->Width +
             xoffset+(GLint)destTex->Border) * texcomponents;
         GLubyte *src = srcBase +
            (size_t)k * width * height * texcomponents;

         for (j=0; j<height; j++) {
            MEMCPY( dst, src, (size_t)width * texcomponents );
            dst += (size_t)destTex->Width * texcomponents;
            src += (size_t)width * texcomponents;
         }
      }
      gl_free_texture_image(subTexImg);
   }

   ctx->Texture.Current3D->Dirty = GL_TRUE;
   ctx->Texture.AnyDirty = GL_TRUE;
   ctx->NewState |= NEW_TEXTURING;

   if (ctx->Driver.TexImage) {
      (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_3D,
                               ctx->Texture.Current3D, level,
                               destTex->IntFormat, destTex );
   }

release_image:
   if (image && image->RefCount==0)
      gl_free_image(image);
}


/*
 * Read an RGBA image from the frame buffer.
 * Input:  ctx - the context
 *         x, y - lower left corner
 *         width, height - size of region to read
 *         format - one of GL_RED, GL_RGB, GL_LUMINANCE, etc.
 * Return: gl_image pointer or NULL if out of memory
 */
static struct gl_image *read_color_image( GLcontext *ctx, GLint x, GLint y,
                                          GLsizei width, GLsizei height,
                                          GLint format )
{
   struct gl_image *image;
   GLubyte *imgptr;
   GLint components;
   GLint i, j;

   components = components_in_intformat( format );

   /*
    * Allocate image struct and image data buffer
    */
   image = (struct gl_image *) malloc( sizeof(struct gl_image) );
   if (image) {
      image->Width = width;
      image->Height = height;
      image->Depth = 1;
      image->Components = components;
      image->Format = format;
      image->Type = GL_UNSIGNED_BYTE;
      image->RefCount = 0;
      image->Data = (GLubyte *) malloc( width * height * components );
      if (!image->Data) {
         free(image);
         return NULL;
      }
   }
   else {
      return NULL;
   }

   imgptr = (GLubyte *) image->Data;

   /* Select buffer to read from */
   (void) (*ctx->Driver.SetBuffer)( ctx, ctx->Pixel.ReadBuffer );

   for (j=0;j<height;j++) {
      GLubyte red[MAX_WIDTH], green[MAX_WIDTH];
      GLubyte blue[MAX_WIDTH], alpha[MAX_WIDTH];
      gl_read_color_span( ctx, width, x, y+j, red, green, blue, alpha );

      if (!ctx->Visual->EightBitColor) {
         /* scale red, green, blue, alpha values to range [0,255] */
         GLfloat rscale = 255.0f * ctx->Visual->InvRedScale;
         GLfloat gscale = 255.0f * ctx->Visual->InvGreenScale;
         GLfloat bscale = 255.0f * ctx->Visual->InvBlueScale;
         GLfloat ascale = 255.0f * ctx->Visual->InvAlphaScale;
         for (i=0;i<width;i++) {
            red[i]   = (GLubyte) (GLint) (red[i]   * rscale);
            green[i] = (GLubyte) (GLint) (green[i] * gscale);
            blue[i]  = (GLubyte) (GLint) (blue[i]  * bscale);
            alpha[i] = (GLubyte) (GLint) (alpha[i] * ascale);
         }
      }

      switch (format) {
         case GL_ALPHA:
            for (i=0;i<width;i++) {
               *imgptr++ = alpha[i];
            }
            break;
         case GL_LUMINANCE:
            for (i=0;i<width;i++) {
               *imgptr++ = red[i];
            }
            break;
         case GL_LUMINANCE_ALPHA:
            for (i=0;i<width;i++) {
               *imgptr++ = red[i];
               *imgptr++ = alpha[i];
            }
            break;
         case GL_INTENSITY:
            for (i=0;i<width;i++) {
               *imgptr++ = red[i];
            }
            break;
         case GL_RGB:
            for (i=0;i<width;i++) {
               *imgptr++ = red[i];
               *imgptr++ = green[i];
               *imgptr++ = blue[i];
            }
            break;
         case GL_RGBA:
            for (i=0;i<width;i++) {
               *imgptr++ = red[i];
               *imgptr++ = green[i];
               *imgptr++ = blue[i];
               *imgptr++ = alpha[i];
            }
            break;
      } /*switch*/

   } /*for*/         

   /* Restore drawing buffer */
   (void) (*ctx->Driver.SetBuffer)( ctx, ctx->Color.DrawBuffer );

   return image;
}




void gl_CopyTexImage1D( GLcontext *ctx,
                        GLenum target, GLint level,
                        GLenum internalformat,
                        GLint x, GLint y,
                        GLsizei width, GLint border )
{
   GLint format;
   struct gl_image *teximage;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexImage1D" );
      return;
   }
   if (target!=GL_TEXTURE_1D) {
      gl_error( ctx, GL_INVALID_ENUM, "glCopyTexImage1D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage1D(level)" );
      return;
   }
   if (border!=0 && border!=1) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage1D(border)" );
      return;
   }
   if (width<2*border
       || width>(MAX_TEXTURE_SIZE >> level)+2*border) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage1D(width)" );
      return;
   }
   format = decode_internal_format( internalformat );
   if (format<0 || (internalformat>=1 && internalformat<=4)) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage1D(format)" );
      return;
   }

   if (width==0) {
      gl_TexImage1D( ctx, target, level, internalformat, width,
                     border, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
      return;
   }

   teximage = read_color_image( ctx, x, y, width, 1, format );
   if (!teximage) {
      gl_error( ctx, GL_OUT_OF_MEMORY, "glCopyTexImage1D" );
      return;
   }

   gl_TexImage1D( ctx, target, level, internalformat, width,
                  border, GL_RGBA, GL_UNSIGNED_BYTE, teximage );

   /* teximage was freed in gl_TexImage1D */
}



void gl_CopyTexImage2D( GLcontext *ctx,
                        GLenum target, GLint level, GLenum internalformat,
                        GLint x, GLint y, GLsizei width, GLsizei height,
                        GLint border )
{
   GLint format;
   struct gl_image *teximage;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexImage2D" );
      return;
   }
   if (target!=GL_TEXTURE_2D) {
      gl_error( ctx, GL_INVALID_ENUM, "glCopyTexImage2D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage2D(level)" );
      return;
   }
   if (border!=0 && border!=1) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage2D(border)" );
      return;
   }
   if (width<2*border
       || width>(MAX_TEXTURE_SIZE >> level)+2*border) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage2D(width)" );
      return;
   }
   if (height<2*border
       || height>(MAX_TEXTURE_SIZE >> level)+2*border) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage2D(height)" );
      return;
   }
   format = decode_internal_format( internalformat );
   if (format<0 || (internalformat>=1 && internalformat<=4)) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexImage2D(format)" );
      return;
   }

   if (width==0 || height==0) {
      gl_TexImage2D( ctx, target, level, internalformat, width, height,
                     border, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
      return;
   }

   teximage = read_color_image( ctx, x, y, width, height, format );
   if (!teximage) {
      gl_error( ctx, GL_OUT_OF_MEMORY, "glCopyTexImage2D" );
      return;
   }

   gl_TexImage2D( ctx, target, level, internalformat, width, height,
                  border, GL_RGBA, GL_UNSIGNED_BYTE, teximage );

   /* teximage was freed in gl_TexImage2D */
}




/*
 * Do the work of glCopyTexSubImage[123]D.
 * TODO: apply pixel bias scale and mapping.
 */
static void copy_tex_sub_image( GLcontext *ctx, struct gl_texture_image *dest,
                                GLint width, GLint height,
                                GLint srcx, GLint srcy,
                                GLint dstx, GLint dsty, GLint dstz )
{
   GLint i, j;
   GLint format, components;

   format = dest->Format;
   components = components_in_intformat( format );

   (void) (*ctx->Driver.SetBuffer)( ctx, ctx->Pixel.ReadBuffer );

   for (j=0;j<height;j++) {
      GLubyte red[MAX_WIDTH], green[MAX_WIDTH];
      GLubyte blue[MAX_WIDTH], alpha[MAX_WIDTH];
      GLubyte *texptr;

      gl_read_color_span( ctx, width, srcx, srcy+j, red, green, blue, alpha );

      if (!ctx->Visual->EightBitColor) {
         /* scale red, green, blue, alpha values to range [0,255] */
         GLfloat rscale = 255.0f * ctx->Visual->InvRedScale;
         GLfloat gscale = 255.0f * ctx->Visual->InvGreenScale;
         GLfloat bscale = 255.0f * ctx->Visual->InvBlueScale;
         GLfloat ascale = 255.0f * ctx->Visual->InvAlphaScale;
         for (i=0;i<width;i++) {
            red[i]   = (GLubyte) (GLint) (red[i]   * rscale);
            green[i] = (GLubyte) (GLint) (green[i] * gscale);
            blue[i]  = (GLubyte) (GLint) (blue[i]  * bscale);
            alpha[i] = (GLubyte) (GLint) (alpha[i] * ascale);
         }
      }

      texptr = dest->Data +
               (((dstz * dest->Height + dsty+j) * dest->Width) + dstx)
               * components;

      switch (format) {
         case GL_ALPHA:
            for (i=0;i<width;i++) {
               *texptr++ = alpha[i];
            }
            break;
         case GL_LUMINANCE:
            for (i=0;i<width;i++) {
               *texptr++ = red[i];
            }
            break;
         case GL_LUMINANCE_ALPHA:
            for (i=0;i<width;i++) {
               *texptr++ = red[i];
               *texptr++ = alpha[i];
            }
            break;
         case GL_INTENSITY:
            for (i=0;i<width;i++) {
               *texptr++ = red[i];
            }
            break;
         case GL_RGB:
            for (i=0;i<width;i++) {
               *texptr++ = red[i];
               *texptr++ = green[i];
               *texptr++ = blue[i];
            }
            break;
         case GL_RGBA:
            for (i=0;i<width;i++) {
               *texptr++ = red[i];
               *texptr++ = green[i];
               *texptr++ = blue[i];
               *texptr++ = alpha[i];
            }
            break;
      } /*switch*/
   } /*for*/         

   (void) (*ctx->Driver.SetBuffer)( ctx, ctx->Color.DrawBuffer );
}




void gl_CopyTexSubImage1D( GLcontext *ctx,
                              GLenum target, GLint level,
                              GLint xoffset, GLint x, GLint y, GLsizei width )
{
   struct gl_texture_image *teximage;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage1D" );
      return;
   }
   if (target!=GL_TEXTURE_1D) {
      gl_error( ctx, GL_INVALID_ENUM, "glCopyTexSubImage1D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage1D(level)" );
      return;
   }
   if (width<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage1D(width)" );
      return;
   }

   teximage = ctx->Texture.Current1D->Image[level];

   if (teximage) {
      if (xoffset < -((GLint)teximage->Border)) {
         gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage1D(xoffset)" );
         return;
      }
      if (width > (GLint)teximage->Width - (GLint)teximage->Border - xoffset) {
         gl_error( ctx, GL_INVALID_VALUE,
                   "glCopyTexSubImage1D(xoffset+width)" );
         return;
      }
      if (teximage->Data) {
         copy_tex_sub_image( ctx, teximage, width, 1, x, y,
                             xoffset+(GLint)teximage->Border, 0, 0 );
         ctx->Texture.Current1D->Dirty = GL_TRUE;
         ctx->Texture.AnyDirty = GL_TRUE;
      }
   }
   else {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage1D" );
   }
}



void gl_CopyTexSubImage2D( GLcontext *ctx,
                              GLenum target, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLint x, GLint y, GLsizei width, GLsizei height )
{
   struct gl_texture_image *teximage;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage2D" );
      return;
   }
   if (target!=GL_TEXTURE_2D) {
      gl_error( ctx, GL_INVALID_ENUM, "glCopyTexSubImage2D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage2D(level)" );
      return;
   }
   if (width<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage2D(width)" );
      return;
   }
   if (height<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage2D(height)" );
      return;
   }

   teximage = ctx->Texture.Current2D->Image[level];

   if (teximage) {
      if (xoffset < -((GLint)teximage->Border)) {
         gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage2D(xoffset)" );
         return;
      }
      if (yoffset < -((GLint)teximage->Border)) {
         gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage2D(yoffset)" );
         return;
      }
      if (width > (GLint)teximage->Width - (GLint)teximage->Border - xoffset) {
         gl_error( ctx, GL_INVALID_VALUE,
                   "glCopyTexSubImage2D(xoffset+width)" );
         return;
      }
      if (height > (GLint)teximage->Height - (GLint)teximage->Border - yoffset) {
         gl_error( ctx, GL_INVALID_VALUE,
                   "glCopyTexSubImage2D(yoffset+height)" );
         return;
      }

      if (teximage->Data) {
         copy_tex_sub_image( ctx, teximage, width, height,
                             x, y,
                             xoffset+(GLint)teximage->Border,
                             yoffset+(GLint)teximage->Border, 0 );
         ctx->Texture.Current2D->Dirty = GL_TRUE;
         ctx->Texture.AnyDirty = GL_TRUE;
      }
   }
   else {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage2D" );
   }
}



void gl_CopyTexSubImage3D( GLcontext *ctx,
                           GLenum target, GLint level,
                           GLint xoffset, GLint yoffset, GLint zoffset,
                           GLint x, GLint y,
                           GLsizei width, GLsizei height )
{
   struct gl_texture_image *teximage;

   if (INSIDE_BEGIN_END(ctx)) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage3D" );
      return;
   }
   if (target!=GL_TEXTURE_3D) {
      gl_error( ctx, GL_INVALID_ENUM, "glCopyTexSubImage3D(target)" );
      return;
   }
   if (level<0 || level>=MAX_TEXTURE_LEVELS) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage3D(level)" );
      return;
   }
   if (width<0 || height<0) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage3D(size)" );
      return;
   }

   teximage = ctx->Texture.Current3D->Image[level];
   if (!teximage) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage3D" );
      return;
   }
   if (xoffset < -((GLint)teximage->Border) ||
       yoffset < -((GLint)teximage->Border) ||
       zoffset < -((GLint)teximage->Border) ||
       width > (GLint)teximage->Width - (GLint)teximage->Border - xoffset ||
       height > (GLint)teximage->Height - (GLint)teximage->Border - yoffset ||
       zoffset >= (GLint)teximage->Depth - (GLint)teximage->Border) {
      gl_error( ctx, GL_INVALID_VALUE, "glCopyTexSubImage3D(offset+size)" );
      return;
   }

   if (width==0 || height==0)
      return;
   if (!teximage->Data) {
      gl_error( ctx, GL_INVALID_OPERATION, "glCopyTexSubImage3D(storage)" );
      return;
   }

   copy_tex_sub_image( ctx, teximage, width, height, x, y,
                       xoffset+(GLint)teximage->Border,
                       yoffset+(GLint)teximage->Border,
                       zoffset+(GLint)teximage->Border );
   ctx->Texture.Current3D->Dirty = GL_TRUE;
   ctx->Texture.AnyDirty = GL_TRUE;
   ctx->NewState |= NEW_TEXTURING;

   if (ctx->Driver.TexImage) {
      (*ctx->Driver.TexImage)( ctx, GL_TEXTURE_3D,
                               ctx->Texture.Current3D, level,
                               teximage->IntFormat, teximage );
   }
}
