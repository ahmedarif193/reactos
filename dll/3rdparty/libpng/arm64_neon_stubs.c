/*
 * ARM64 NEON stubs for libpng
 * TODO: Implement actual NEON optimizations
 */

#include "pngpriv.h"

/* Stub implementations that return 0 to indicate non-NEON path should be used */

void png_riffle_palette_neon(png_structrp png_ptr)
{
    /* TODO: Implement NEON optimized version */
    /* For now, do nothing - used for palette riffling */
    PNG_UNUSED(png_ptr)
}

int png_do_expand_palette_rgba8_neon(png_structrp png_ptr,
                                     png_row_infop row_info,
                                     png_const_bytep row,
                                     const png_bytepp sp,
                                     const png_bytepp dp)
{
    /* TODO: Implement NEON optimized version */
    /* Return 0 to indicate non-NEON path should be used */
    PNG_UNUSED(png_ptr)
    PNG_UNUSED(row_info)
    PNG_UNUSED(row)
    PNG_UNUSED(sp)
    PNG_UNUSED(dp)
    return 0;
}

int png_do_expand_palette_rgb8_neon(png_structrp png_ptr,
                                    png_row_infop row_info,
                                    png_const_bytep row,
                                    const png_bytepp sp,
                                    const png_bytepp dp)
{
    /* TODO: Implement NEON optimized version */
    /* Return 0 to indicate non-NEON path should be used */
    PNG_UNUSED(png_ptr)
    PNG_UNUSED(row_info)
    PNG_UNUSED(row)
    PNG_UNUSED(sp)
    PNG_UNUSED(dp)
    return 0;
}
void png_init_filter_functions_neon(png_structp png_ptr, unsigned int bpp)
{
    /* TODO: Implement NEON filter initialization */
    /* For now, do nothing - fallback to non-NEON filters */
    PNG_UNUSED(png_ptr)
    PNG_UNUSED(bpp)
}
