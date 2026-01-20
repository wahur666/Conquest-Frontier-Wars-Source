/*
 * VFX Shape Functions - C++ Implementation
 * Based on reverse-engineered binary format from .shp files
 *
 * File Structure:
 *   [0-3]   Version string ("1.10")
 *   [4-7]   Shape count (U32)
 *   [8+]    Offset table: 2 DWORDs per shape (shape_offset, colors_offset)
 *   [...]   Shape data and color data at specified offsets
 *
 * Shape Header (24 bytes):
 *   [0-3]   bounds (DWORD)
 *   [4-7]   origin (DWORD)
 *   [8-11]  xmin (S32)
 *   [12-15] ymin (S32)
 *   [16-19] xmax (S32)
 *   [20-23] ymax (S32)
 *
 * Color Data at colors_offset:
 *   [0-3]   color_count (U32)
 *   [4+]    VFX_CRGB entries: {color_idx (U8), r (U8), g (U8), b (U8)}
 *
 * RLE Data Format (after shape header):
 *   token = read_byte()
 *   r = token % 2
 *   b = token / 2
 *
 *   if (b==0 && r==1): skip_count = read_byte(); skip skip_count pixels
 *   if (b==0 && r==0): end_of_line
 *   if (r==0):         run_packet - read 1 byte color, repeat b times
 *   if (r==1):         literal_packet - read b bytes as pixel data
 */

#include <string.h>
#include <stdint.h>

// Type definitions
typedef uint8_t  U8;
typedef int32_t  S32;
typedef uint32_t U32;

struct SHAPEHEADER
{
    U32 bounds;
    U32 origin;
    S32 xmin, ymin, xmax, ymax;
};


/*
 * VFX_shape_bounds
 *
 * Returns bounds of the specified shape as packed DWORD:
 * High word = width (xmax - xmin + 1)
 * Low word = height (ymax - ymin + 1)
 *
 * Returns -1 on error
 */
S32 WINAPI VFX_shape_bounds(VFX_SHAPETABLE *shape_table, S32 shape_num)
{
    if (!shape_table || shape_num < 0 || shape_num >= (S32)shape_table->shape_count)
        return -1;

    // Get offset table base (starts after header at offset 8)
    U8 *base = (U8 *)shape_table;
    U32 *offset_table = (U32 *)(base + 8);

    // Each entry is 2 DWORDs: shape_offset, colors_offset
    U32 shape_offset = offset_table[shape_num * 2];

    // Read shape header
    SHAPEHEADER *header = (SHAPEHEADER *)(base + shape_offset);

    S32 width = header->xmax - header->xmin + 1;
    S32 height = header->ymax - header->ymin + 1;

    // Pack as (width << 16) | (height & 0xFFFF)
    return (width << 16) | (height & 0xFFFF);
}


/*
 * VFX_shape_colors
 *
 * Returns the number of colors used in the specified shape.
 * If colors pointer is not NULL, copies the color data there.
 */
S32 WINAPI VFX_shape_colors(VFX_SHAPETABLE *shape_table, S32 shape_num, VFX_CRGB *colors)
{
    if (!shape_table || shape_num < 0 || shape_num >= (S32)shape_table->shape_count)
        return 0;

    // Get offset table base
    U8 *base = (U8 *)shape_table;
    U32 *offset_table = (U32 *)(base + 8);

    // Each entry is 2 DWORDs: shape_offset, colors_offset
    U32 colors_offset = offset_table[shape_num * 2 + 1];

    if (colors_offset == 0)
        return 0;

    // Read color data
    U32 *color_count_ptr = (U32 *)(base + colors_offset);
    U32 color_count = *color_count_ptr;

    if (color_count == 0 || color_count > 256)
        return 0;

    // Copy colors if buffer provided
    if (colors)
    {
        VFX_CRGB *color_data = (VFX_CRGB *)(base + colors_offset + 4);
        memcpy(colors, color_data, color_count * sizeof(VFX_CRGB));
    }

    return (S32)color_count;
}


/*
 * Decompress RLE data into buffer
 * Returns number of pixels written, or -1 on error
 */
static S32 decompress_rle(const U8 *rle_data, S32 rle_size,
                          S32 width, S32 height,
                          U8 *output_buffer)
{
    if (!rle_data || !output_buffer || width <= 0 || height <= 0)
        return -1;

    const U8 *src = rle_data;
    const U8 *src_end = rle_data + rle_size;
    U8 *dst = output_buffer;
    S32 pix_pos = 0;
    S32 line = 0;

    while (src < src_end && line < height)
    {
        U8 token = *src++;

        S32 r = token % 2;  // bit 0
        S32 b = token / 2;  // bits 1-7

        if (b == 0 && r == 1)
        {
            // Skip packet
            if (src >= src_end) break;
            U8 skip_count = *src++;
            for (S32 i = 0; i < skip_count && pix_pos < width; i++, pix_pos++)
            {
                *dst++ = 0;  // Background index
            }
        }
        else if (b == 0 && r == 0)
        {
            // End of line
            line++;
            pix_pos = 0;
        }
        else if (r == 0)
        {
            // Run packet - repeat color b times
            if (src >= src_end) break;
            U8 color = *src++;
            for (S32 i = 0; i < b && pix_pos < width; i++, pix_pos++)
            {
                *dst++ = color;
            }
        }
        else  // r == 1
        {
            // Literal packet - read b bytes
            for (S32 i = 0; i < b && pix_pos < width; i++, pix_pos++)
            {
                if (src >= src_end) break;
                *dst++ = *src++;
            }
        }
    }

    return (S32)(dst - output_buffer);
}


/*
 * VFX_shape_draw_unclipped8
 *
 * Draw shape directly into pane buffer without clipping
 * Used internally and for fully-visible shapes
 */
void WINAPI VFX_shape_draw_unclipped8(PANE *pane, VFX_SHAPETABLE *shape_table,
                                       S32 shape_number, S32 hotX, S32 hotY)
{
    if (!pane || !pane->window || !shape_table || !pane->window->buffer)
        return;

    if (shape_number < 0 || shape_number >= (S32)shape_table->shape_count)
        return;

    // Get offset table base
    U8 *base = (U8 *)shape_table;
    U32 *offset_table = (U32 *)(base + 8);

    U32 shape_offset = offset_table[shape_number * 2];

    // Read shape header
    SHAPEHEADER *header = (SHAPEHEADER *)(base + shape_offset);

    S32 width = header->xmax - header->xmin + 1;
    S32 height = header->ymax - header->ymin + 1;

    if (width <= 0 || height <= 0)
        return;

    // RLE data starts after header (24 bytes)
    U8 *rle_data = (U8 *)(header + 1);

    // Determine RLE data size
    S32 rle_size = 0;
    if (shape_number < (S32)shape_table->shape_count - 1)
    {
        U32 next_shape_offset = offset_table[(shape_number + 1) * 2];
        rle_size = next_shape_offset - shape_offset - 24;
    }
    else
    {
        rle_size = 65536;  // Large default
    }

    // Allocate temporary buffer for decompressed data
    U8 *decomp_buffer = (U8 *)malloc(width * height);
    if (!decomp_buffer)
        return;

    // Decompress RLE
    S32 pixels_written = decompress_rle(rle_data, rle_size, width, height, decomp_buffer);

    if (pixels_written <= 0)
    {
        free(decomp_buffer);
        return;
    }

    // Calculate screen position
    S32 screen_x = hotX + header->xmin;
    S32 screen_y = hotY + header->ymin;

    // Get pane and window dimensions
    VFX_WINDOW *window = pane->window;
    S32 pane_width = pane->x1 - pane->x0 + 1;
    S32 pane_height = pane->y1 - pane->y0 + 1;

    // Copy to window buffer using pixel_pitch and bytes_per_pixel
    U8 *src = decomp_buffer;

    for (S32 y = 0; y < height; y++)
    {
        S32 window_y = screen_y + y - pane->y0;

        if (window_y < 0 || window_y > window->y_max)
        {
            src += width;
            continue;
        }

        for (S32 x = 0; x < width; x++)
        {
            S32 window_x = screen_x + x - pane->x0;

            if (window_x >= 0 && window_x <= window->x_max)
            {
                // Calculate buffer offset using pixel_pitch
                U8 *pixel_ptr = (U8 *)window->buffer +
                               (window_y * window->pixel_pitch) +
                               (window_x * window->bytes_per_pixel);

                // For 8-bit indexed mode, just copy the index
                *pixel_ptr = *src;
            }

            src++;
        }
    }

    free(decomp_buffer);
}


/*
 * VFX_shape_draw8
 *
 * Draw shape with clipping to pane boundaries
 */
void WINAPI VFX_shape_draw8(PANE *pane, VFX_SHAPETABLE *shape_table,
                             S32 shape_number, S32 hotX, S32 hotY)
{
    if (!pane || !pane->window || !shape_table)
        return;

    if (shape_number < 0 || shape_number >= (S32)shape_table->shape_count)
        return;

    // Get offset table base
    U8 *base = (U8 *)shape_table;
    U32 *offset_table = (U32 *)(base + 8);

    U32 shape_offset = offset_table[shape_number * 2];

    // Read shape header
    SHAPEHEADER *header = (SHAPEHEADER *)(base + shape_offset);

    S32 width = header->xmax - header->xmin + 1;
    S32 height = header->ymax - header->ymin + 1;

    if (width <= 0 || height <= 0)
        return;

    // Calculate screen bounds
    S32 screen_x = hotX + header->xmin;
    S32 screen_y = hotY + header->ymin;

    // Get window dimensions
    VFX_WINDOW *window = pane->window;
    S32 pane_width = pane->x1 - pane->x0 + 1;
    S32 pane_height = pane->y1 - pane->y0 + 1;

    // Quick bounds check - if completely outside pane, skip
    if (screen_x + width < pane->x0 || screen_x > pane->x1 ||
        screen_y + height < pane->y0 || screen_y > pane->y1)
    {
        return;
    }

    // If fully inside pane, use unclipped version
    if (screen_x >= pane->x0 && screen_x + width <= pane->x0 + pane_width &&
        screen_y >= pane->y0 && screen_y + height <= pane->y0 + pane_height)
    {
        VFX_shape_draw_unclipped8(pane, shape_table, shape_number, hotX, hotY);
        return;
    }

    // Otherwise, perform clipped draw
    // RLE data starts after header (24 bytes)
    U8 *rle_data = (U8 *)(header + 1);

    // Determine RLE data size
    S32 rle_size = 0;
    if (shape_number < (S32)shape_table->shape_count - 1)
    {
        U32 next_shape_offset = offset_table[(shape_number + 1) * 2];
        rle_size = next_shape_offset - shape_offset - 24;
    }
    else
    {
        rle_size = 65536;  // Large default
    }

    // Allocate temporary buffer for decompressed data
    U8 *decomp_buffer = (U8 *)malloc(width * height);
    if (!decomp_buffer)
        return;

    // Decompress RLE
    S32 pixels_written = decompress_rle(rle_data, rle_size, width, height, decomp_buffer);

    if (pixels_written <= 0)
    {
        free(decomp_buffer);
        return;
    }

    // Copy with clipping
    for (S32 y = 0; y < height; y++)
    {
        S32 window_y = screen_y + y - pane->y0;

        if (window_y < 0 || window_y > window->y_max)
            continue;

        for (S32 x = 0; x < width; x++)
        {
            S32 window_x = screen_x + x - pane->x0;

            if (window_x >= 0 && window_x <= window->x_max)
            {
                U8 pixel = decomp_buffer[y * width + x];

                // Calculate buffer offset using pixel_pitch
                U8 *pixel_ptr = (U8 *)window->buffer +
                               (window_y * window->pixel_pitch) +
                               (window_x * window->bytes_per_pixel);

                *pixel_ptr = pixel;
            }
        }
    }

    free(decomp_buffer);
}