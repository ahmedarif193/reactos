/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Unicode Conversion Routines
 * FILE:              lib/rtl/unicode_vista.c
 * PROGRAMMER:        Alex Ionescu (alex@relsoft.net)
 *                    Emanuele Aliberti
 *                    Gunnar Dalsnes
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#include <wine/unicode.h>

/* FUNCTIONS *****************************************************************/

/*
 * RtlCompareUnicodeStrings lives in the base rtl library (unicode.c) so that
 * ntoskrnl, which links base rtl rather than rtl_vista, exports it too as
 * Windows does.
 */
