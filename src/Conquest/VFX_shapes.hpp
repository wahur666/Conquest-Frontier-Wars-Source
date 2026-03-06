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

struct SHAPEHEADER
{
    U32 bounds;
    U32 origin;
    S32 xmin, ymin, xmax, ymax;
};

// Constants
#define NO_COLOR  (-1)
#define LEFTOF    0x08   // 1000b
#define RIGHTOF   0x04   // 0100b

// Global translation table (256 native pixel values, one per 8-bit palette index)
static uint32_t shape_translate[256];
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
void WINAPI VFX_shape_draw_unclipped8(PANE *pane, VFX_SHAPETABLE *shape_table, S32 shape_number, S32 hotX, S32 hotY)
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

int WINAPI VFX_pixel_read(const PANE *panep, S32 x, S32 y)
{
    VFX_WINDOW *window = panep->window;

    // CLIP_PANE_TO_WINDOW macro translation:

    // Validate window dimensions (x_max and y_max are treated as inclusive maximums)
    int CP_W  = window->x_max + 1;   // window width in pixels
    if (CP_W <= 0)
        return -1; // Bad window

    int CP_BW = CP_W * window->pixel_pitch;  // window width in bytes

    int win_height = window->y_max + 1;
    if (win_height <= 0)
        return -1; // Bad window

    // Save pane origins (used later to convert pane coords -> window coords)
    int CP_CX = panep->x0;
    int CP_CY = panep->y0;

    // Clip pane rect into window rect
    int CP_L = (panep->x0 > 0)              ? panep->x0 : 0;
    int CP_T = (panep->y0 > 0)              ? panep->y0 : 0;
    int CP_R = (panep->x1 < CP_W - 1)       ? panep->x1 : CP_W - 1;
    int CP_B = (panep->y1 < win_height - 1) ? panep->y1 : win_height - 1;

    // Validate clipped pane (also catches pane completely off window)
    if (CP_R < CP_L || CP_B < CP_T)
        return -2; // Bad pane

    void *CP_A = window->buffer;
    int pixel_pitch    = window->pixel_pitch;
    int bytes_per_pixel = window->bytes_per_pixel;

    // CONVERT_REG_PAIR_PANE_TO_WINDOW:
    // Convert pane-relative (x, y) to window coordinates
    int wx = x + CP_CX;
    int wy = y + CP_CY;

    // Clip pixel to pane bounds (in window coordinates)
    if (wx < CP_L || wx > CP_R || wy < CP_T || wy > CP_B)
        return -3; // Off pane

    // GET_WINDOW_ADDRESS:
    // address = buffer + (wy * CP_BW) + (wx * pixel_pitch)
    uint8_t *addr = (uint8_t *)CP_A
                  + (wy * CP_BW)
                  + (wx * pixel_pitch);

    // Read pixel (up to 4 bytes)
    int pixel = 0;
    memcpy(&pixel, addr, bytes_per_pixel);

    return pixel;
}

#define NO_COLOR (-1)

int WINAPI VFX_pane_copy(const PANE *source, S32 Sx, S32 Sy,
                  PANE *target, S32  Tx, S32  Ty, S32  fill)
{
    //
    // CLIP_PANE_TO_WINDOW source, SCP
    //

    VFX_WINDOW *src_win = source->window;

    int SCP_W = src_win->x_max + 1;
    if (SCP_W <= 0) return -1;

    int SCP_BW = SCP_W * src_win->pixel_pitch;

    int src_win_h = src_win->y_max + 1;
    if (src_win_h <= 0) return -1;

    int SCP_CX = source->x0;
    int SCP_CY = source->y0;

    int SCP_L = (source->x0 > 0)             ? source->x0 : 0;
    int SCP_T = (source->y0 > 0)             ? source->y0 : 0;
    int SCP_R = (source->x1 < SCP_W - 1)     ? source->x1 : SCP_W - 1;
    int SCP_B = (source->y1 < src_win_h - 1) ? source->y1 : src_win_h - 1;

    if (SCP_R < SCP_L || SCP_B < SCP_T) return -2;

    uint8_t *SCP_A           = (uint8_t *)src_win->buffer;
    int      src_pixel_pitch = src_win->pixel_pitch;

    //
    // CONVERT_QUAD_WINDOW_TO_PANE source_x0..y1, SCP
    // (subtract CX/CY to go from window coords back to pane coords)
    //

    int source_x0 = SCP_L - SCP_CX;
    int source_y0 = SCP_T - SCP_CY;
    int source_x1 = SCP_R - SCP_CX;
    int source_y1 = SCP_B - SCP_CY;

    //
    // CLIP_PANE_TO_WINDOW target, CP
    //
    // NOTE: This overwrites the local pixel_pitch with the target's value.
    // GET_WINDOW_ADDRESS uses this local, so when called for the source
    // pointer it will use the target's pixel_pitch. This matches the
    // original assembly's behavior faithfully (likely a latent bug).
    //

    VFX_WINDOW *tgt_win = target->window;

    int CP_W = tgt_win->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * tgt_win->pixel_pitch;

    int tgt_win_h = tgt_win->y_max + 1;
    if (tgt_win_h <= 0) return -1;

    int CP_CX = target->x0;
    int CP_CY = target->y0;

    int CP_L = (target->x0 > 0)             ? target->x0 : 0;
    int CP_T = (target->y0 > 0)             ? target->y0 : 0;
    int CP_R = (target->x1 < CP_W - 1)      ? target->x1 : CP_W - 1;
    int CP_B = (target->y1 < tgt_win_h - 1) ? target->y1 : tgt_win_h - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A            = (uint8_t *)tgt_win->buffer;
    int      pixel_pitch     = tgt_win->pixel_pitch; // overwrites src value — matches asm
    int      bytes_per_pixel = tgt_win->bytes_per_pixel;

    //
    // CONVERT_QUAD_WINDOW_TO_PANE target_x0..y1, CP
    //

    int target_x0 = CP_L - CP_CX;
    int target_y0 = CP_T - CP_CY;
    int target_x1 = CP_R - CP_CX;
    int target_y1 = CP_B - CP_CY;

    //
    // Calculate delta: source_pane_coord = target_pane_coord + delta
    //

    int delta_x = Sx - Tx;
    int delta_y = Sy - Ty;

    //
    // Clip source pane rect to target pane rect (in pane coords)
    //

    int p_x0 = (source_x0 > target_x0 + delta_x) ? source_x0 : target_x0 + delta_x;
    int p_y0 = (source_y0 > target_y0 + delta_y) ? source_y0 : target_y0 + delta_y;
    int p_x1 = (source_x1 < target_x1 + delta_x) ? source_x1 : target_x1 + delta_x;
    int p_y1 = (source_y1 < target_y1 + delta_y) ? source_y1 : target_y1 + delta_y;

    if (p_x1 < p_x0 || p_y1 < p_y0) return -3;

    //
    // Clip target pane rect to source pane rect (in pane coords)
    //

    int q_x0 = (target_x0 > source_x0 - delta_x) ? target_x0 : source_x0 - delta_x;
    int q_y0 = (target_y0 > source_y0 - delta_y) ? target_y0 : source_y0 - delta_y;
    int q_x1 = (target_x1 < source_x1 - delta_x) ? target_x1 : source_x1 - delta_x;
    int q_y1 = (target_y1 < source_y1 - delta_y) ? target_y1 : source_y1 - delta_y;

    //
    // copyXsize is in bytes (assembly doubles pixel count: "add eax,eax ;X2 for 16bpp")
    // We use pixel_pitch (target's, per the overwrite noted above) to generalize.
    //

    int copyXsize = (p_x1 + 1 - p_x0) * pixel_pitch; // width in bytes
    int copyYsize =  p_y1 + 1 - p_y0;                 // height in rows

    //
    // GET_WINDOW_ADDRESS SCP_CX, SCP_CY, SCP  =>  base of source pane origin
    // GET_WINDOW_ADDRESS CP_CX,  CP_CY,  CP   =>  base of target pane origin
    //
    // Formula: buffer + (y * BW) + (x * pixel_pitch)
    // Note: pixel_pitch here is the target's (already overwritten), matching asm.
    //

    uint8_t *p = SCP_A + (SCP_CY * SCP_BW) + (SCP_CX * pixel_pitch);
    uint8_t *q = CP_A  + (CP_CY  * CP_BW)  + (CP_CX  * pixel_pitch);

    //
    // Choose Y direction to avoid overwriting source data before it is read
    //

    int sourceYstep, targetYstep;

    if (p_y0 > q_y0)
    {
        // top-to-bottom
        p += p_y0 * SCP_BW;
        q += q_y0 * CP_BW;
        sourceYstep = SCP_BW;   // MOVE sourceYstep, eax, SCP_BW
        targetYstep = CP_BW;
    }
    else
    {
        // bottom-to-top
        p += p_y1 * SCP_BW;
        q += q_y1 * CP_BW;
        sourceYstep = -SCP_BW;
        targetYstep = -CP_BW;
    }

    //
    // Choose X direction for the same reason
    //

    if (p_x0 > q_x0)
    {
        // left-to-right
        p += p_x0 * pixel_pitch;
        q += q_x0 * pixel_pitch;
        sourceYstep -= copyXsize;
        targetYstep -= copyXsize;
    }
    else
    {
        // right-to-left: point to last byte of rightmost pixel
        // assembly adds 1 ("pre-add bytes_per_pixel - 1" for 16bpp = +1)
        p += p_x1 * pixel_pitch + (pixel_pitch - 1);
        q += q_x1 * pixel_pitch + (pixel_pitch - 1);
        sourceYstep += copyXsize;
        targetYstep += copyXsize;
    }

    //
    // Copy or fill
    //

    if (fill == NO_COLOR)
    {
        // copyLoop: handle trailing bytes then dwords, matching:
        //   mov ecx,copyXsize / and ecx,11B / rep movsb
        //   mov ecx,copyXsize / shr ecx,2  / rep movsd

        for (int row = 0; row < copyYsize; row++)
        {
            if (p_x0 > q_x0)
            {
                // forward: rep movsb for remainder, rep movsd for bulk
                uint8_t *src = p;
                uint8_t *dst = q;
                int rem = copyXsize & 3;
                for (int i = 0; i < rem; i++)
                    *dst++ = *src++;
                int dwords = copyXsize >> 2;
                uint32_t *src32 = (uint32_t *)dst; // after movsb, esi/edi advanced
                // In the asm, after the movsb the adjust (0) is subtracted,
                // so movsd starts right where movsb left off.
                src32 = (uint32_t *)src;
                uint32_t *dst32 = (uint32_t *)dst;
                for (int i = 0; i < dwords; i++)
                    *dst32++ = *src32++;
            }
            else
            {
                // backward: esi/edi point to last byte; subtract adjust (3)
                // to align to dword boundary before movsd, then add back after
                uint8_t *src = p;
                uint8_t *dst = q;
                int rem = copyXsize & 3;
                for (int i = 0; i < rem; i++)
                    *dst-- = *src--;
                // adjust = 3: back up 3 more bytes to reach dword boundary
                src -= 3;
                dst -= 3;
                int dwords = copyXsize >> 2;
                uint32_t *src32 = (uint32_t *)src;
                uint32_t *dst32 = (uint32_t *)dst;
                for (int i = 0; i < dwords; i++)
                    *--dst32 = *--src32;
            }

            p += sourceYstep;
            q += targetYstep;
        }
    }
    else
    {
        // fillLoop: matches:
        //   mov ecx,copyXsize / and ecx,11B / shr ecx,1 / rep stosw
        //   mov ecx,copyXsize / shr ecx,2             / rep stosd

        // Replicate 16-bit color into both halves of a dword
        // ("shl eax,16 / mov ax,dx" in the asm)
        uint16_t color16 = (uint16_t)(fill & 0xFFFF);
        uint32_t color32 = ((uint32_t)color16 << 16) | color16;

        for (int row = 0; row < copyYsize; row++)
        {
            if (p_x0 > q_x0)
            {
                // forward stosw then stosd
                // adjust_w = 0: no pointer adjustment needed
                uint8_t *dst = q;
                int words  = (copyXsize & 3) >> 1;
                for (int i = 0; i < words; i++)
                    { *(uint16_t *)dst = color16; dst += 2; }
                int dwords = copyXsize >> 2;
                for (int i = 0; i < dwords; i++)
                    { *(uint32_t *)dst = color32; dst += 4; }
            }
            else
            {
                // backward stosw then stosd
                // adjust_w = 1: "sub edi,1" before stosw, "add edi,1" after
                uint8_t *dst = q;
                int words  = (copyXsize & 3) >> 1;
                dst -= 1; // adjust_w
                for (int i = 0; i < words; i++)
                    { dst -= 2; *(uint16_t *)dst = color16; }
                dst += 1; // restore adjust_w
                // adjust = 3: "sub edi,3" before stosd
                dst -= 3;
                int dwords = copyXsize >> 2;
                for (int i = 0; i < dwords; i++)
                    { dst -= 4; *(uint32_t *)dst = color32; }
                // "add edi,3" after stosd handled by targetYstep
            }

            q += targetYstep;
        }
    }

    return 0;
}

//----------------------------------------------------------------------------
// DrawShapeUnclipped - private helper, draws with no clipping
//----------------------------------------------------------------------------
static int WINAPI DrawShapeUnclipped(PANE *panep, void *shape, int hotX, int hotY)
{
    VFX_WINDOW *window = panep->window;

    // Convert hotspot to window coordinates
    hotX += panep->x0;
    hotY += panep->y0;

    // CP_BW = (x_max + 1) * 2  (16bpp: 2 bytes per pixel)
    int CP_BW = (window->x_max + 1) * 2;
    if (CP_BW <= 0)
        return 0;

    uint8_t  *esi = (uint8_t *)shape;
    SHAPEHEADER *hdr = (SHAPEHEADER *)esi;

    // edi = buffer + (minX + hotX)*2 + (minY + hotY) * CP_BW
    uint8_t *buf = (uint8_t *)window->buffer;
    int startX = hdr->xmin + hotX;
    int startY = hdr->ymin + hotY;

    uint8_t *line_ptr = buf + startY * CP_BW + startX * 2;

    int line_count = hdr->ymax + 1 - hdr->ymin;
    if (line_count <= 0)
        return 0;

    esi += sizeof(SHAPEHEADER);

    while (line_count > 0)
    {
        uint8_t *edi = line_ptr;

        // Packet decode loop for one line
        while (1)
        {
            uint8_t token = *esi++;
            uint8_t count = token >> 1;
            int     carry = token & 1;

            if (count > 0 && !carry)
            {
                // Run packet: replicate one source byte as 16bpp pixels
                int ecx = count;
                uint8_t idx = *esi++;
                uint16_t pixel = (uint16_t)shape_translate[idx];
                uint32_t pixel32 = ((uint32_t)pixel << 16) | pixel;
                int dwords = ecx >> 1;
                int words  = ecx & 1;
                uint32_t *d32 = (uint32_t *)edi;
                while (dwords--) *d32++ = pixel32;
                edi = (uint8_t *)d32;
                if (words) { *(uint16_t *)edi = pixel; edi += 2; }
                goto next_token_run;
            }
            else if (count > 0 && carry)
            {
                // String packet: copy source bytes, translating each to 16bpp
                int ecx = count;
                while (ecx--) {
                    uint8_t idx = *esi++;
                    *(uint16_t *)edi = (uint16_t)shape_translate[idx];
                    edi += 2;
                }
                goto next_token_string;
            }
            else if (carry)
            {
                // Skip packet: advance destination pointer
                uint8_t skip = *esi++;
                edi += skip * 2;
                continue;
            }
            else
            {
                // End packet: end of line
                break;
            }

            next_token_run:
            next_token_string:;
            // loop continues to read next token
            // (the gotos above are just to re-enter the token read)
            // Actually restructure as a proper loop below
        }

        line_ptr += CP_BW;
        line_count--;
    }

    return 0;
}

void WINAPI VFX_shape_palette2(VFX_SHAPETABLE  *SHP_buffer, S32 Shape_number, uint32_t *Palette)
{
    // Skip 8-byte header to reach the offset table
    uint8_t *base = (uint8_t *)SHP_buffer;
    uint8_t *esi  = base + 8;

    // Each shape entry is 2 longs (8 bytes):
    //   [0] = shape offset
    //   [4] = palette offset
    // Skip to this shape's entry, then skip past the shape offset to palette offset
    esi += Shape_number * 8;
    esi += 4;

    // Read palette offset ptr; if null, no palette for this shape
    uint32_t palette_offset = *(uint32_t *)esi;
    if (palette_offset == 0)
        return;

    // Palette data is at base + palette_offset
    esi = base + palette_offset;

    // First dword is the color count
    uint32_t color_count = *(uint32_t *)esi;
    esi += 4;

    // Read each color entry
    for (uint32_t i = 0; i < color_count; i++)
    {
        uint32_t ecx = *(uint32_t *)esi;
        esi += 4;

        // bl = low byte of ecx = palette index
        uint8_t index = (uint8_t)(ecx & 0xFF);

        // Mask out top 2 bits of each byte component, then shift right 6
        // Input format is 6-bit components packed as 00bbggrr (2 bits padding per byte)
        // After: "and ecx, 3F3F3F3F / shr ecx, 6" yields 8-8-8 in 00bbggrr layout
        ecx &= 0x3F3F3F3Fu;
        ecx >>= 6;

        // Convert the 00bbggrr colorref to a pixel value for the current display mode
        Palette[index] = ColorRefToPixel(ecx);
    }
}

// Helper: write 'count' pixels of a run (one color) to edi, 16bpp
static inline void write_run(uint8_t **edi, uint8_t *esi_color_byte, int count)
{
    uint8_t  idx     = *esi_color_byte;
    uint16_t pixel   = (uint16_t)shape_translate[idx];
    uint32_t pixel32 = ((uint32_t)pixel << 16) | pixel;
    uint32_t *d      = (uint32_t *)*edi;
    int dwords = count >> 1;
    while (dwords--) *d++ = pixel32;
    *edi = (uint8_t *)d;
    if (count & 1) { *(uint16_t *)*edi = pixel; *edi += 2; }
}

// Helper: write 'count' pixels from a string (each byte looked up), advancing esi
static inline void write_string(uint8_t **edi, uint8_t **esi, int count)
{
    while (count--)
    {
        *(uint16_t *)*edi = (uint16_t)shape_translate[**esi];
        (*esi)++;
        *edi += 2;
    }
}


//----------------------------------------------------------------------------
// VFX_shape_draw - clips and draws a shape to a pane
//----------------------------------------------------------------------------
int WINAPI VFX_shape_draw(PANE *panep, VFX_SHAPETABLE *shape_table, S32 shape_number, S32 hotX, S32 hotY)
{
    //
    // CLIP_PANE_TO_WINDOW panep
    //

    VFX_WINDOW *window = panep->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * 2;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return -1;

    int CP_CX = panep->x0;
    int CP_CY = panep->y0;

    int CP_L = (panep->x0 > 0)        ? panep->x0 : 0;
    int CP_T = (panep->y0 > 0)        ? panep->y0 : 0;
    int CP_R = (panep->x1 < CP_W - 1) ? panep->x1 : CP_W - 1;
    int CP_B = (panep->y1 < CP_H - 1) ? panep->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A = (uint8_t *)window->buffer;

    // Convert hotspot: pane -> window coords
    hotX += CP_CX;
    hotY += CP_CY;

    // Locate shape data in table
    uint8_t *table_base = (uint8_t *)shape_table;
    uint8_t *entry      = table_base + sizeof(SHAPEHEADER) + shape_number * 8;
    uint32_t shape_off  = *(uint32_t *)(entry + 0);
    uint32_t pal_off    = *(uint32_t *)(entry + 4);
    uint8_t *shape      = table_base + shape_off;

    // Set up palette translation table
    if (pal_off != 0)
        VFX_shape_palette2(shape_table, shape_number, shape_translate);

    // Read shape bounding box, offset by hotspot
    SHAPEHEADER *hdr = (SHAPEHEADER *)shape;
    int minX = hdr->xmin + hotX;
    int minY = hdr->ymin + hotY;
    int maxX = hdr->xmax + hotX;
    int maxY = hdr->ymax + hotY;

    if (maxX < minX || maxY < minY) return -4;

    // Build clip flags (reproduces the asm shl/adc bit-packing)
    // dl = min-corner flags, dh = max-corner flags
    // bit3=left-of, bit2=right-of, bit1=above, bit0=below
    uint8_t dl = 0, dh = 0;
    dl = (dl << 1) | (uint8_t)((uint32_t)(minX - CP_L) >> 31);
    dl = (dl << 1) | (uint8_t)((uint32_t)(CP_R - minX) >> 31);
    dl = (dl << 1) | (uint8_t)((uint32_t)(minY - CP_T) >> 31);
    dl = (dl << 1) | (uint8_t)((uint32_t)(CP_B - minY) >> 31);
    dh = (dh << 1) | (uint8_t)((uint32_t)(maxX - CP_L) >> 31);
    dh = (dh << 1) | (uint8_t)((uint32_t)(CP_R - maxX) >> 31);
    dh = (dh << 1) | (uint8_t)((uint32_t)(maxY - CP_T) >> 31);
    dh = (dh << 1) | (uint8_t)((uint32_t)(CP_B - maxY) >> 31);

    uint16_t clip_flags = ((uint16_t)dh << 8) | dl;

    // Completely outside pane on a single edge
    if (dl & dh) return -3;

    // Fully inside pane — fast unclipped path
    if (!(dl | dh))
    {
        hotX -= CP_CX;
        hotY -= CP_CY;
        return DrawShapeUnclipped(panep, shape, hotX, hotY);
    }

    // Clipped draw path
    uint8_t *line_ptr = CP_A + minY * CP_BW + minX * 2;
    uint8_t *adr0     = line_ptr - minX * 2 + CP_L * 2;  // left  clip boundary
    uint8_t *adr1     = line_ptr - minX * 2 + CP_R * 2;  // right clip boundary

    uint8_t *esi  = shape + sizeof(SHAPEHEADER);
    int      lineY = minY;

    //
    // Skip top-clipped lines
    //

    while (lineY < CP_T)
    {
        for (;;)
        {
            uint8_t raw   = *esi++;
            uint8_t count = raw >> 1;
            int     carry = raw & 1;

            if      (count == 0 && !carry) break;     // End
            else if (count == 0 &&  carry) esi++;     // Skip: consume count byte
            else if (!carry)               esi++;     // Run:  consume color byte
            else                           esi += count; // String: consume pixels
        }
        line_ptr += CP_BW;
        adr0     += CP_BW;
        adr1     += CP_BW;
        lineY++;
    }

    //
    // Main draw loop
    //

    int left_clip  = (clip_flags & LEFTOF)         != 0;
    int right_clip = (clip_flags & (RIGHTOF << 8)) != 0;

    while (lineY <= maxY)
    {
        if (lineY > CP_B) break;

        uint8_t *edi = line_ptr;

        // case tracks which clipping region edi is currently in:
        //   1 = inside pane, no right clip pending
        //   2 = left of left edge
        //   3 = inside pane, right clip pending
        //   4 = past right edge
        int cas = left_clip ? 2 : (right_clip ? 3 : 1);

        for (;;)
        {
            uint8_t raw   = *esi++;
            uint8_t count = raw >> 1;
            int     carry = raw & 1;

            // End packet
            if (count == 0 && !carry) break;

            // Skip packet
            if (count == 0 && carry)
            {
                uint8_t skip = *esi++;
                edi += (int)skip * 2;
                // entering skip can push us past right edge in case 3
                if (cas == 3 && edi > adr1) cas = 4;
                continue;
            }

            // Run or String packet — handle per case
            int is_run = !carry;
            int ecx    = count;

            if (cas == 1)
            {
                if (is_run)
                {
                    write_run(&edi, esi, ecx);
                    esi++;
                }
                else
                {
                    write_string(&edi, &esi, ecx);
                }
            }
            else if (cas == 2)
            {
                // How many pixels until the left edge?
                int delta = (int)(adr0 - edi) >> 1;

                if (delta >= ecx)
                {
                    // Entire packet is still left of pane — skip it
                    edi += ecx * 2;
                    if (is_run) esi++;
                    else        esi += ecx;
                }
                else
                {
                    // Advance past the invisible portion
                    if (delta > 0)
                    {
                        edi += delta * 2;
                        ecx -= delta;
                        if (!is_run) esi += delta;
                    }

                    // We've crossed the left edge; switch to case 1 or 3
                    cas = right_clip ? 3 : 1;

                    if (cas == 1)
                    {
                        if (is_run) { write_run(&edi, esi, ecx); esi++; }
                        else          write_string(&edi, &esi, ecx);
                    }
                    else
                    {
                        // Fall through into case 3 logic for the remainder
                        // (no goto needed — just inline case 3 here)
                        int end_addr = (int)((intptr_t)edi + ecx * 2 - 2);
                        int over     = end_addr - (int)(intptr_t)adr1;
                        int clipped  = (over > 0) ? (over >> 1) : 0;
                        int draw     = ecx - clipped;

                        if (is_run)
                        {
                            write_run(&edi, esi, draw);
                            esi++;
                        }
                        else
                        {
                            write_string(&edi, &esi, draw);
                            esi += clipped;
                        }
                        edi += clipped * 2;
                        if (clipped > 0) cas = 4;
                    }
                }
            }
            else if (cas == 3)
            {
                // Inside pane, right clip is upcoming
                int end_addr = (int)((intptr_t)edi + ecx * 2 - 2);
                int over     = end_addr - (int)(intptr_t)adr1;
                int clipped  = (over > 0) ? (over >> 1) : 0;
                int draw     = ecx - clipped;

                if (edi > adr1)
                {
                    // Already past right edge — skip entire packet
                    cas = 4;
                    edi += ecx * 2;
                    if (is_run) esi++;
                    else        esi += ecx;
                }
                else
                {
                    if (is_run)
                    {
                        write_run(&edi, esi, draw);
                        esi++;
                    }
                    else
                    {
                        write_string(&edi, &esi, draw);
                        esi += clipped;
                    }
                    edi += clipped * 2;
                    if (clipped > 0) cas = 4;
                }
            }
            else // cas == 4
            {
                // Past right clip — skip everything
                edi += ecx * 2;
                if (is_run) esi++;
                else        esi += ecx;
            }
        }

        line_ptr += CP_BW;
        adr0     += CP_BW;
        adr1     += CP_BW;
        lineY++;
    }

    return 0;
}

// Packet type constants
#define INIT_   0
#define STRING_ 1
#define RUN_    2
#define SKIP_   3
#define END_    4
#define NONE_   5

// SHP_ globals (shared between VFX_shape_scan8, ScanLine, FlushPacket)
static int       SHP_building;   // non-zero if actually writing shape data
static int       SHP_skipCount;  // accumulated transparent pixels not yet written
static uint8_t  *SHP_LinePtr;    // start of current scanline in source window
static uint8_t  *SHP_ScanPtr;    // current read position within scanline
static uint8_t  *SHP_ShapePtr;   // current write position in output shape buffer
static uint8_t  *SHP_FlushPtr;   // lags SHP_ShapePtr, used to flush completed packets
static int32_t   SHP_minX, SHP_minY, SHP_maxX, SHP_maxY;

//----------------------------------------------------------------------------
// FlushPacket — writes a completed packet segment to the shape buffer.
// 'keep' is the number of trailing bytes to leave unflushed.
// CP_L is the window x-coordinate of the left edge of the pane.
//----------------------------------------------------------------------------
static void FlushPacket(int packetType, int keep, int CP_L)
{
    uint8_t *esi = SHP_FlushPtr;
    uint8_t *edi = SHP_ShapePtr;

    switch (packetType)
    {
    case INIT_:
        // Initialise: reset skip count and flush pointer
        SHP_skipCount = 0;
        SHP_FlushPtr  = SHP_ScanPtr;
        goto exit;

    case SKIP_:
        // Accumulate skip count — don't write anything yet
        SHP_skipCount = (int)(SHP_ScanPtr - esi) - keep;
        goto exit;

    case RUN_:
    {
        // Write any accumulated skip packets first (max 255 pixels each)
        int skip = SHP_skipCount;
        while (skip > 0)
        {
            int n = (skip < 255) ? skip : 255;
            skip -= n;
            if (SHP_building) { *edi++ = 1; *edi++ = (uint8_t)n; }
            else               edi += 2;
            esi += n;
        }
        SHP_skipCount = 0;

        // length = SHP_ScanPtr - SHP_FlushPtr - keep
        int length = (int)(SHP_ScanPtr - esi) - keep;

        // Update SHP_minX / SHP_maxX
        int x = CP_L + (int)(esi - SHP_LinePtr);
        if (x < SHP_minX) SHP_minX = x;
        int xend = x + length - 1;
        if (xend > SHP_maxX) SHP_maxX = xend;

        // Write run packets (max 127 pixels each)
        while (length > 0)
        {
            int n = (length < 127) ? length : 127;
            if (SHP_building) { *edi++ = (uint8_t)(n * 2); *edi++ = *esi; }
            else               edi += 2;
            esi    += n;
            length -= n;
        }
        break;
    }

    case STRING_:
    {
        // Write any accumulated skip packets first
        int skip = SHP_skipCount;
        while (skip > 0)
        {
            int n = (skip < 255) ? skip : 255;
            skip -= n;
            if (SHP_building) { *edi++ = 1; *edi++ = (uint8_t)n; }
            else               edi += 2;
            esi += n;
        }
        SHP_skipCount = 0;

        // length = SHP_ScanPtr - SHP_FlushPtr - keep
        int length = (int)(SHP_ScanPtr - esi) - keep;

        // Update SHP_minX / SHP_maxX
        int x = CP_L + (int)(esi - SHP_LinePtr);
        if (x < SHP_minX) SHP_minX = x;
        int xend = x + length - 1;
        if (xend > SHP_maxX) SHP_maxX = xend;

        // Write string packets (max 127 pixels each)
        while (length > 0)
        {
            int n = (length < 127) ? length : 127;
            if (SHP_building)
            {
                *edi++ = (uint8_t)(n * 2 + 1);
                memcpy(edi, esi, n);
                edi += n;
            }
            else
            {
                edi += 1 + n;
            }
            esi    += n;
            length -= n;
        }
        break;
    }

    case END_:
        // Write a single zero end-of-line byte
        if (SHP_building) *edi = 0;
        edi++;
        break;

    default:
        goto exit;
    }

exit:
    SHP_ShapePtr = edi;
    SHP_FlushPtr = esi;
}

//----------------------------------------------------------------------------
// ScanLine — scans one source line and encodes it as VFX shape packets.
// 'count'      = number of pixels on this line
// 'background' = transparent color index
// 'CP_L'       = window x-coord of left edge (passed through to FlushPacket)
//----------------------------------------------------------------------------
static void ScanLine(int count, int background, int CP_L)
{
    SHP_ScanPtr = SHP_LinePtr;
    FlushPacket(INIT_, 0, CP_L);

    int packetType = NONE_;

    if (count == 0)
    {
        FlushPacket(NONE_, 0, CP_L);
        FlushPacket(END_,  0, CP_L);
        return;
    }

    // Read first pixel to seed the state machine
    uint8_t previous = *SHP_ScanPtr++;
    count--;

    int state = (previous == (uint8_t)background) ? SKIP_ : STRING_;
    if (state == STRING_) packetType = STRING_;
    if (state == SKIP_)   packetType = SKIP_;

    while (count > 0 || state != NONE_)
    {
        switch (state)
        {
        case STRING_:
        {
            packetType = STRING_;

            if (count == 0) { state = NONE_; break; }

            uint8_t pixel = *SHP_ScanPtr++;
            count--;
            uint8_t same = pixel ^ previous;
            previous     = same ? pixel : previous;  // previous = new pixel

            if (same == 0)
            {
                // Two identical — start looking for run (need a third)
                if (count == 0) { state = NONE_; break; }

                pixel    = *SHP_ScanPtr++;
                count--;
                same     = pixel ^ previous;
                previous = same ? pixel : previous;

                if (previous == (uint8_t)background)
                {
                    FlushPacket(STRING_, 1, CP_L);
                    state = SKIP_;
                    packetType = SKIP_;
                    break;
                }

                if (same != 0)
                {
                    // Back to stringing — different pixel
                    break; // stay in STRING_
                }

                // Three identical — check a third to confirm run
                if (count == 0) { state = NONE_; break; }

                pixel    = *SHP_ScanPtr++;
                count--;
                same     = pixel ^ previous;
                previous = same ? pixel : previous;

                if (previous == (uint8_t)background)
                {
                    FlushPacket(STRING_, 1, CP_L);
                    state = SKIP_;
                    packetType = SKIP_;
                    break;
                }

                if (same != 0)
                {
                    break; // still different — stay in STRING_
                }

                // Three in a row confirmed — flush string, switch to run
                FlushPacket(STRING_, 3, CP_L);
                state = RUN_;
                packetType = RUN_;
                break;
            }

            // Different pixel
            previous = pixel;
            if (previous == (uint8_t)background)
            {
                FlushPacket(STRING_, 1, CP_L);
                state = SKIP_;
                packetType = SKIP_;
            }
            // else stay in STRING_
            break;
        }

        case RUN_:
        {
            packetType = RUN_;

            if (count == 0) { state = NONE_; break; }

            uint8_t pixel = *SHP_ScanPtr++;
            count--;
            uint8_t same  = pixel ^ previous;

            if (same == 0)
            {
                // Same pixel — stay in run
                break;
            }

            previous = pixel;
            FlushPacket(RUN_, 1, CP_L);

            if (previous != (uint8_t)background)
            {
                state = STRING_;
                packetType = STRING_;
            }
            else
            {
                state = SKIP_;
                packetType = SKIP_;
            }
            break;
        }

        case SKIP_:
        {
            packetType = SKIP_;

            if (count == 0) { state = NONE_; break; }

            uint8_t pixel = *SHP_ScanPtr++;
            count--;
            uint8_t same  = pixel ^ previous;

            if (same == 0)
            {
                // Still transparent — stay in skip
                break;
            }

            previous = pixel;
            FlushPacket(SKIP_, 1, CP_L);
            state = STRING_;
            packetType = STRING_;
            break;
        }

        default:
            state = NONE_;
            break;
        }

        if (count == 0 && state != NONE_)
        {
            state = NONE_;
        }
    }

    FlushPacket(packetType, 0, CP_L);
    FlushPacket(END_,       0, CP_L);
}

//----------------------------------------------------------------------------
// VFX_shape_scan8 — scans an 8bpp pane and encodes it as a VFX shape.
//
// panep            : pane to scan
// transparentColor : palette index treated as background/transparent
// hotX, hotY       : origin of shape within the pane (pane coords)
// buffer           : output buffer for the shape table, or NULL to
//                    query the required size
//
// Returns: total bytes written (or required) including the 16-byte table header
//----------------------------------------------------------------------------
int WINAPI VFX_shape_scan8 (PANE *panep, U32 transparentColor, S32 hotX, S32 hotY, VFX_SHAPETABLE *buffer)
{
    //
    // CLIP_PANE_TO_WINDOW panep
    //

    VFX_WINDOW *window = panep->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return -1;

    int CP_CX = panep->x0;
    int CP_CY = panep->y0;

    int CP_L = (panep->x0 > 0)        ? panep->x0 : 0;
    int CP_T = (panep->y0 > 0)        ? panep->y0 : 0;
    int CP_R = (panep->x1 < CP_W - 1) ? panep->x1 : CP_W - 1;
    int CP_B = (panep->y1 < CP_H - 1) ? panep->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A       = (uint8_t *)window->buffer;
    int      pixel_pitch = window->pixel_pitch;

    // SHP_building: true if buffer was provided, false = size-query only
    SHP_building = (buffer != NULL) ? 1 : 0;

    uint8_t *out = (uint8_t *)buffer;

    //
    // Write the shape table header (16 bytes) if building
    //
    // Table header layout:
    //   [0..3]  version string "1.10"
    //   [4..7]  shape count = 1
    //   [8..11] offset to shape data = 16 (immediately after header)
    //   [12..15] palette offset = 0 (no palette)
    //
    // Followed by a provisional null SHAPEHEADER:
    //   bounds  = (pane_width << 16) | pane_height
    //   origin  = (hotX << 16) | hotY
    //   xmin=0, ymin=0, xmax=-1, ymax=-1
    //

    if (SHP_building)
    {
        // Table header
        *out++ = '1'; *out++ = '.'; *out++ = '1'; *out++ = '0';
        *(uint32_t *)out = 1;   out += 4;  // shape count
        *(uint32_t *)out = 16;  out += 4;  // offset to shape
        *(uint32_t *)out = 0;   out += 4;  // no palette

        // Provisional null shape header
        uint32_t bounds = (uint32_t)((panep->x1 - panep->x0) << 16)
                        | (uint16_t)(panep->y1 - panep->y0);
        *(uint32_t *)out = bounds;                        out += 4; // bounds
        *(uint32_t *)out = ((uint32_t)hotX << 16) | (uint16_t)hotY; out += 4; // origin
        *(uint32_t *)out = 0;  out += 4;  // xmin = 0
        *(uint32_t *)out = 0;  out += 4;  // ymin = 0
        *(uint32_t *)out = (uint32_t)-1; out += 4; // xmax = -1
        *(uint32_t *)out = (uint32_t)-1; out += 4; // ymax = -1
    }

    // Convert hotspot from pane to window coordinates
    hotX += CP_CX;
    hotY += CP_CY;

    int paneWidth  = CP_R - CP_L + 1;
    // Note: assembly computes paneHeight the same way as paneWidth (appears to
    // be a copy-paste in the original), but it's only paneWidth that's used in
    // the scan loops below.

    //
    // Pass 1: find the bounding box of non-transparent pixels
    //

    SHP_minX =  0x7FFFFFFF;
    SHP_minY =  0x7FFFFFFF;
    SHP_maxX = -0x80000000;  // INT32_MIN
    SHP_maxY = -0x80000000;

    // SHP_LinePtr = address of top-left pixel of clipped pane
    SHP_LinePtr = CP_A + CP_T * CP_BW + CP_L * pixel_pitch;

    for (int lineY = CP_T; lineY <= CP_B; lineY++)
    {
        // Scan left-to-right for first non-transparent pixel
        uint8_t *scan = SHP_LinePtr;
        int       rem  = paneWidth;
        while (rem > 0 && *scan == (uint8_t)transparentColor) { scan++; rem--; }

        if (rem > 0)
        {
            // Line has at least one opaque pixel
            if (lineY < SHP_minY) SHP_minY = lineY;
            if (lineY > SHP_maxY) SHP_maxY = lineY;

            // Left edge: CP_R - remaining_count  (matches asm "mov eax,CP_R / sub eax,ecx")
            int left_x = CP_R - rem;
            // Note: assembly uses CP_R - ecx after repe scasb, which leaves ecx
            // = pixels remaining *after* the match. After the match, ecx has been
            // decremented once more, so left_x = CP_R - (rem-1) - 1 = CP_R - rem.
            // More intuitively: left_x = CP_L + (paneWidth - rem) - 1...
            // Keeping asm-faithful formula:
            left_x = CP_R - rem;
            if (left_x < SHP_minX) SHP_minX = left_x;

            // Scan right-to-left for last non-transparent pixel
            scan = SHP_LinePtr + paneWidth - 1;
            rem  = paneWidth;
            while (rem > 0 && *scan == (uint8_t)transparentColor) { scan--; rem--; }

            if (rem > 0)
            {
                // Right edge: CP_L + remaining_count  (matches asm "mov eax,CP_L / add eax,ecx")
                int right_x = CP_L + rem;
                if (right_x > SHP_maxX) SHP_maxX = right_x;
            }
        }

        SHP_LinePtr += CP_W; // advance by window pixel width (matches asm "mov eax,CP_W")
    }

    //
    // Pass 2: encode the shape
    //

    // Point edi at shape data area (after 16-byte table header)
    uint8_t *shape_base = (uint8_t *)buffer + 16;

    if (SHP_building)
    {
        // Write final shape header now that we know the bounding box
        SHAPEHEADER *shdr = (SHAPEHEADER *)shape_base;
        shdr->xmin = SHP_minX - hotX;
        shdr->ymin = SHP_minY - hotY;
        shdr->xmax = SHP_maxX - hotX;
        shdr->ymax = SHP_maxY - hotY;
    }

    // SHP_ShapePtr starts after the shape header
    SHP_ShapePtr = shape_base + sizeof(SHAPEHEADER);

    int shapeWidth = SHP_maxX + 1 - SHP_minX;

    // SHP_LinePtr = address of top-left pixel of the image bounding box
    SHP_LinePtr = CP_A + SHP_minY * CP_BW + SHP_minX * pixel_pitch;

    for (int lineY = SHP_minY; lineY <= SHP_maxY; lineY++)
    {
        ScanLine(shapeWidth, transparentColor, CP_L);
        SHP_LinePtr += CP_W;
    }

    // Return total bytes written (or required), including the 16-byte table header
    return (int)(SHP_ShapePtr - (uint8_t *)buffer);
}


// Font table layout:
//   [0..15]  : header (VFX_FONT, 16 bytes)
//   [16..]   : array of uint32_t offsets, one per character
//   Each offset points (from font base) to a character block whose
//   first dword is the character's pixel width.

int WINAPI VFX_character_width(VFX_FONT *font_ptr, S32 character)
{
    // Point to this character's offset entry in the table
    // (skip the 16-byte header, then index by character)
    uint32_t *offsets = (uint32_t *)((uint8_t *)font_ptr + 16);
    uint32_t  char_offset = offsets[character];

    // The first dword at the character's data block is its width
    uint8_t  *char_data = (uint8_t *)font_ptr + char_offset;
    int32_t   width     = *(int32_t *)char_data;

    return width;
}

#define RGB_TRANSPARENT 0xFFFE

int VFX_character_draw (PANE *target, S32 X, S32 Y,
                       VFX_FONT *font_ptr, S32 character, void *translate)
{
    //
    // CLIP_PANE_TO_WINDOW target
    //

    VFX_WINDOW *window = target->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return -1;

    int CP_CX = target->x0;
    int CP_CY = target->y0;

    int CP_L = (target->x0 > 0)        ? target->x0 : 0;
    int CP_T = (target->y0 > 0)        ? target->y0 : 0;
    int CP_R = (target->x1 < CP_W - 1) ? target->x1 : CP_W - 1;
    int CP_B = (target->y1 < CP_H - 1) ? target->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A        = (uint8_t *)window->buffer;
    int      pixel_pitch = window->pixel_pitch;
    int      bytes_per_pixel = window->bytes_per_pixel;

    // Convert X,Y from pane to window coordinates
    X += CP_CX;
    Y += CP_CY;

    // Locate character data in font
    // Font layout: [16-byte VFX_FONT header][uint32 offsets per char][char bitmaps]
    int      char_height   = font_ptr->char_height;
    uint32_t *offsets      = (uint32_t *)((uint8_t *)font_ptr + 16);
    uint8_t  *char_data    = (uint8_t *)font_ptr + offsets[character];
    int       src_char_width = *(int32_t *)char_data;
    int       dst_char_width = 0;

    if (src_char_width == 0)
        return 0;

    uint8_t *src = char_data + 4; // skip width dword to reach bitmap
    int ecx = src_char_width;

    // Clip right: reduce ecx if character extends past CP_R
    {
        int overflow = (CP_R + 1) - ecx - X; // eax = CP_R+1 - char_width - X
        if (overflow < 0)
        {
            ecx += overflow;  // reduce draw width
            if (ecx <= 0)
                return src_char_width; // completely off right edge
        }
    }

    // Clip left: reduce ecx and advance src if character starts before CP_L
    {
        int underflow = X - CP_L; // negative if X is left of CP_L
        if (underflow < 0)
        {
            ecx += underflow;     // reduce draw width
            if (ecx <= 0)
                return src_char_width;
            src -= underflow;     // advance src by the clipped columns
            X   -= underflow;     // clamp X to left edge
        }
    }

    // Clip bottom: reduce char_height if character extends past CP_B
    {
        int overflow = (CP_B + 1) - char_height - Y;
        if (overflow < 0)
        {
            char_height += overflow;
            if (char_height <= 0)
                return src_char_width;
        }
    }

    // Clip top: reduce char_height and advance src if character starts above CP_T
    {
        int underflow = Y - CP_T; // negative if Y is above CP_T
        if (underflow < 0)
        {
            char_height += underflow;
            if (char_height <= 0)
                return src_char_width;
            Y   -= underflow;              // clamp Y to top edge
            src -= underflow * src_char_width; // skip clipped rows in bitmap
        }
    }

    // Set up copy parameters
    dst_char_width = ecx;
    int src_skip   = src_char_width - dst_char_width; // bytes to skip in src at end of row

    // edi = window address of (X, Y)
    uint8_t *dst = CP_A + Y * CP_BW + X * pixel_pitch;

    int dst_row_advance = CP_BW - dst_char_width * pixel_pitch;

    if (translate == NULL)
    {
        // Direct copy — no translation, no transparency
        for (int row = 0; row < char_height; row++)
        {
            int pixels = dst_char_width;
            // Assembly does: shr ecx,1 / rep movsw / adc ecx,0 / rep movsb
            // i.e. copy in words then handle odd byte — equivalent to memcpy
            memcpy(dst, src, pixels);
            src += pixels + src_skip;
            dst += dst_char_width + dst_row_advance;
        }
    }
    else
    {
        // Translated copy — each source byte indexes into translate table;
        // certain sentinel values mean "transparent, skip this pixel"
        for (int row = 0; row < char_height; row++)
        {
            for (int col = 0; col < dst_char_width; col++)
            {
                uint8_t src_index = *src++;

                if (bytes_per_pixel == 4)
                {
                    // 32bpp: translate table holds DWORDs
                    // transparent if alpha byte (bits 24-31) is zero
                    uint32_t pixel = ((uint32_t *)translate)[src_index];
                    if (pixel & 0xFF000000u)
                        *(uint32_t *)dst = pixel;
                    dst += pixel_pitch;
                }
                else if (bytes_per_pixel == 2)
                {
                    // 16bpp: translate table holds WORDs
                    uint16_t pixel = ((uint16_t *)translate)[src_index];
                    if (pixel != RGB_TRANSPARENT)
                        *(uint16_t *)dst = pixel;
                    dst += pixel_pitch;
                }
                else
                {
                    // 8bpp: translate table holds BYTEs
                    // 0xFF means transparent
                    uint8_t pixel = ((uint8_t*)translate)[src_index];
                    if (pixel != 0xFF)
                        *dst = pixel;
                    dst++;
                }
            }

            src += src_skip;
            dst += dst_row_advance;
        }
    }

    // Return total character width (src_char_width = dst_char_width + src_skip)
    return src_char_width;
}

void VFX_string_draw(PANE *target, S32 x, S32 y,
                     VFX_FONT *font_ptr, const char *string, void *translate)
{
    if (!string || *string == '\0')
        return;

    const char *s = string;
    while (*s != '\0')
    {
        int char_width = VFX_character_draw(target, x, y, font_ptr,
                                            (uint8_t)*s, translate);
        x += char_width;  // advance X by the character's pixel width
        s++;
    }
}

S32  WINAPI VFX_rectangle_hash (const PANE *pane, S32 x0, S32 y0, S32 x1, S32 y1, U32 color) {
    //
    // CLIP_PANE_TO_WINDOW pane
    //

    VFX_WINDOW *window = pane->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return -1;

    int CP_CX = pane->x0;
    int CP_CY = pane->y0;

    int CP_L = (pane->x0 > 0)        ? pane->x0 : 0;
    int CP_T = (pane->y0 > 0)        ? pane->y0 : 0;
    int CP_R = (pane->x1 < CP_W - 1) ? pane->x1 : CP_W - 1;
    int CP_B = (pane->y1 < CP_H - 1) ? pane->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A = (uint8_t *)window->buffer;

    uint32_t RGB_color = color;

    // Convert rect from pane to window coordinates
    x0 += CP_CX;  y0 += CP_CY;
    x1 += CP_CX;  y1 += CP_CY;

    // Clip rect to pane bounds
    if (x0 < CP_L) x0 = CP_L;
    if (y0 < CP_T) y0 = CP_T;
    if (x1 > CP_R) x1 = CP_R;
    if (y1 > CP_B) y1 = CP_B;

    int width  = x1 - x0 + 1;
    int height = y1 - y0;      // assembly uses dec edx before the jns check,
                                // so height counts down from (y1-y0) inclusive of 0

    if (width  < 1) return -4;
    if (height < 0) return -4;

    // Base address of top-left pixel of clipped rect
    uint8_t *line_start = CP_A + y0 * CP_BW + x0 * window->pixel_pitch;

    uint16_t pixel = (uint16_t)(RGB_color & 0xFFFF);

    // Hash pattern: alternating pixels per line, phase flipped on odd lines.
    // Assembly: odd lines skip the first pixel (add edi,2 / dec ecx),
    // then write every other pixel (add edi,4 between writes).
    // Even lines start writing at the first pixel.

    for (int row = 0; row <= height; row++)
    {
        uint16_t *dst = (uint16_t *)line_start;
        int        col = width;
        int        odd = row & 1;

        if (odd)
        {
            // Odd line: skip first pixel, start at second
            dst++;
            col--;
            if (col <= 0)
            {
                line_start += CP_BW;
                continue;
            }
        }

        // Write every other pixel
        while (col > 0)
        {
            *dst = pixel;
            dst += 2;   // skip one pixel between writes (4 bytes = 2 uint16)
            col -= 2;
        }

        line_start += CP_BW;
    }

    return 0;
}

S32  WINAPI VFX_pane_wipe (const PANE *pane, U32 color) {
    //
    // CLIP_PANE_TO_WINDOW pane
    //

    VFX_WINDOW *window = pane->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return -1;

    int CP_CX = pane->x0;
    int CP_CY = pane->y0;

    int CP_L = (pane->x0 > 0)        ? pane->x0 : 0;
    int CP_T = (pane->y0 > 0)        ? pane->y0 : 0;
    int CP_R = (pane->x1 < CP_W - 1) ? pane->x1 : CP_W - 1;
    int CP_B = (pane->y1 < CP_H - 1) ? pane->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A        = (uint8_t *)window->buffer;
    int      pixel_pitch = window->pixel_pitch;

    // Build fill value: replicate 16-bit color into both halves of a dword
    // (matches "shl eax,16 / mov ax, WORD PTR RGB_color")
    uint16_t color16 = (uint16_t)(color & 0xFFFF);
    uint32_t color32 = ((uint32_t)color16 << 16) | color16;

    // Start address: top-left of clipped pane
    // GET_WINDOW_ADDRESS CP_L, CP_T
    uint8_t *dst = CP_A + CP_T * CP_BW + CP_L * pixel_pitch;

    // pane width in bytes
    int pane_bytes = (CP_R + 1 - CP_L) * pixel_pitch;

    // gap from end of pane row to start of next pane row
    int line_gap = CP_BW - pane_bytes;

    // Fill each row: words first for the remainder, then dwords for the bulk
    // (matches "and ecx,11B / shr ecx,1 / rep stosw" then "shr ecx,2 / rep stosd")
    for (int y = CP_T; y <= CP_B; y++)
    {
        uint8_t *p   = dst;
        int      rem = pane_bytes;

        // Handle up to 2 leftover bytes as a word
        int words = (rem & 3) >> 1;
        while (words--)
        {
            *(uint16_t *)p = color16;
            p   += 2;
            rem -= 2;
        }

        // Fill remaining dwords
        int dwords = rem >> 2;
        while (dwords--)
        {
            *(uint32_t *)p = color32;
            p += 4;
        }

        dst += CP_BW;
    }

    return 0;
}


U32  WINAPI VFX_pixel_write (const PANE *pane, S32 x, S32 y, U32 color) {
    //
    // CLIP_PANE_TO_WINDOW pane
    //

    VFX_WINDOW *window = pane->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return (uint32_t)-1;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return (uint32_t)-1;

    int CP_CX = pane->x0;
    int CP_CY = pane->y0;

    int CP_L = (pane->x0 > 0)        ? pane->x0 : 0;
    int CP_T = (pane->y0 > 0)        ? pane->y0 : 0;
    int CP_R = (pane->x1 < CP_W - 1) ? pane->x1 : CP_W - 1;
    int CP_B = (pane->y1 < CP_H - 1) ? pane->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return (uint32_t)-2;

    uint8_t *CP_A        = (uint8_t *)window->buffer;
    int      pixel_pitch = window->pixel_pitch;

    // Convert x,y from pane to window coordinates
    int wx = x + CP_CX;
    int wy = y + CP_CY;

    // Clip pixel to pane
    if (wx < CP_L || wx > CP_R || wy < CP_T || wy > CP_B)
        return (uint32_t)-3;

    // GET_WINDOW_ADDRESS: buffer + wy * CP_BW + wx * pixel_pitch
    uint8_t *addr = CP_A + wy * CP_BW + wx * pixel_pitch;

    // Read prior value (zero-extended to 32 bits)
    uint32_t prior = *(uint16_t *)addr;

    // Write new pixel
    *(uint16_t *)addr = (uint16_t)(color & 0xFFFF);

    return prior;
}

// Line draw mode constants
#define LD_DRAW      0
#define LD_TRANSLATE 1
#define LD_EXECUTE   2  // "jg" path in asm = any value > LD_TRANSLATE

// Fixed-point 0.5 in 32-bit unsigned (used for rounding in slope calcs)
#define ONE_HALF 0x80000000u

// Callback type for LD_EXECUTE mode
typedef void (*LineDrawCallback)(int x, int y);

// Helper: sign of a 32-bit integer, returned as 0 or -1 (all-bits-set)
// Matches the assembly "cdq" pattern which fills edx with the sign of eax
static inline int32_t sign_mask(int32_t v)
{
    return v >> 31; // arithmetic shift: 0 if positive, -1 if negative
}

// Helper: absolute value using sign mask (matches "xor eax,edx / sub eax,edx")
static inline int32_t abs_via_mask(int32_t v, int32_t mask)
{
    return (v ^ mask) - mask;
}

// Helper: apply signed direction to a value.
// Matches "xor eax,sgndxdy / sub eax,sgndxdy" — negates if sgndxdy==-1
static inline int32_t apply_sign(int32_t v, int32_t sgn)
{
    return (v ^ sgn) - sgn;
}

// Helper: clip_flags bit layout (matching asm shl/adc pattern)
// Each endpoint gets 4 bits packed via repeated "shl eax,1 / adc dl,dl":
//   bit3 = x < CP_L
//   bit2 = x > CP_R
//   bit1 = y < CP_T
//   bit0 = y > CP_B
static inline uint8_t make_clip_bits(int x, int y, int CP_L, int CP_R, int CP_T, int CP_B)
{
    uint8_t bits = 0;
    bits = (bits << 1) | (uint8_t)((uint32_t)(x - CP_L) >> 31);
    bits = (bits << 1) | (uint8_t)((uint32_t)(CP_R - x) >> 31);
    bits = (bits << 1) | (uint8_t)((uint32_t)(y - CP_T) >> 31);
    bits = (bits << 1) | (uint8_t)((uint32_t)(CP_B - y) >> 31);
    return bits;
}

// Helper: Xmajor floor calculation
// result = floor((delta * slope + ONE_HALF) / 2^32)
// Matches "mul slope / add eax,ONE_HALF / adc edx,0 / mov eax,edx"
static inline uint32_t xmaj_floor(uint32_t delta, uint32_t slope)
{
    uint64_t product = (uint64_t)delta * slope;
    product += ONE_HALF;
    return (uint32_t)(product >> 32);
}

// Helper: Ymajor ceil calculation
// result = ceil((delta - 0.5) / slope)
// Matches "mov edx,eax / dec edx / mov eax,ONE_HALF / div slope / cmp edx,1 / sbb eax,-1"
static inline uint32_t ymaj_ceil(uint32_t delta, uint32_t slope)
{
    // div slope: eax = ONE_HALF / slope, edx = ONE_HALF % slope
    // but we want (delta*2^32 - ONE_HALF) / slope ceiling
    // The asm computes: quotient = ONE_HALF / slope,
    //   remainder = delta - 1 (decremented before div)
    // then: if (remainder >= 1) quotient++ (sbb eax,-1 when CF set = remainder<1)
    uint32_t quotient  = ONE_HALF / slope;
    uint32_t remainder = delta - 1;
    if (remainder >= 1) quotient++;
    return quotient;
}

// Helper: Ymajor ceil for endpoint (slightly different: sbb eax,0 instead of -1)
static inline uint32_t ymaj_ceil_end(uint32_t delta, uint32_t slope)
{
    uint32_t quotient  = ONE_HALF / slope;
    uint32_t remainder = delta;
    if (remainder >= 1) quotient++;  // sbb eax,0 means subtract borrow (CF)
    return quotient;
}

S32  WINAPI VFX_line_draw (const PANE *pane, S32 x0, S32 y0, S32 x1, S32 y1, S32 mode, U32 parm) {
    //
    // CLIP_PANE_TO_WINDOW pane
    //

    VFX_WINDOW *window = pane->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return -1;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return -1;

    int CP_CX = pane->x0;
    int CP_CY = pane->y0;

    int CP_L = (pane->x0 > 0)        ? pane->x0 : 0;
    int CP_T = (pane->y0 > 0)        ? pane->y0 : 0;
    int CP_R = (pane->x1 < CP_W - 1) ? pane->x1 : CP_W - 1;
    int CP_B = (pane->y1 < CP_H - 1) ? pane->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return -2;

    uint8_t *CP_A        = (uint8_t *)window->buffer;
    int      pixel_pitch = window->pixel_pitch;

    // Convert endpoints from pane to window coordinates
    x0 += CP_CX;  y0 += CP_CY;
    x1 += CP_CX;  y1 += CP_CY;

    // Get RGB color if drawing
    uint32_t RGB_color = 0;
    if (mode == LD_DRAW)
        RGB_color = parm;

    // Calculate dx, dy and their signs/absolutes
    int32_t _dx   = x1 - x0;
    int32_t sgndx = sign_mask(_dx);
    int32_t absdx = abs_via_mask(_dx, sgndx);

    int32_t _dy   = y1 - y0;
    int32_t sgndy = sign_mask(_dy);
    int32_t absdy = abs_via_mask(_dy, sgndy);

    // Working copies of endpoints (modified by clipping)
    int32_t x0_ = x0, y0_ = y0;
    int32_t x1_ = x1, y1_ = y1;

    // Handle degenerate cases first
    if (_dx == 0)
    {
        // Vertical line
        if (x0 < CP_L || x0 > CP_R) return 2;

        int ymax = (y0 > y1) ? y0 : y1;
        int ymin = (y0 < y1) ? y0 : y1;
        if (ymax < CP_T || ymin > CP_B) return 2;

        // Clip y range
        y0_ = (y0 > CP_T) ? y0 : CP_T;  if (y0_ > CP_B) y0_ = CP_B;
        y1_ = (y1 > CP_T) ? y1 : CP_T;  if (y1_ > CP_B) y1_ = CP_B;
        x0_ = x0;

        int ystep = (sgndy == 0) ? CP_BW : (CP_BW ^ sgndy) - sgndy;
        int count = abs_via_mask(y1_ - y0_, sign_mask(y1_ - y0_)) + 1;

        uint8_t *dst = CP_A + y0_ * CP_BW + x0_ * pixel_pitch;

        // Fall through to Straight drawing below
        uint16_t pixel = (uint16_t)(RGB_color & 0xFFFF);
        if (mode == LD_DRAW)
        {
            while (count--) { *(uint16_t *)dst = pixel; dst += ystep; }
        }
        else if (mode == LD_TRANSLATE)
        {
            uint16_t *xlat = (uint16_t *)parm;
            while (count--)
            {
                uint16_t p = *(uint16_t *)dst;
                *(uint16_t *)dst = xlat[p];
                dst += ystep;
            }
        }
        else
        {
            LineDrawCallback cb = (LineDrawCallback)parm;
            int px = x0_ - CP_CX, py = y0_ - CP_CY;
            int ybump = (_dy == 0) ? 0 : ((sgndy == 0) ? 1 : -1);
            while (count--) { cb(px, py); py += ybump; }
        }
        return (y0_ != y0 || y1_ != y1) ? 1 : 0;
    }

    if (_dy == 0)
    {
        // Horizontal line
        if (y0 < CP_T || y0 > CP_B) return 2;

        int xmax = (x0 > x1) ? x0 : x1;
        int xmin = (x0 < x1) ? x0 : x1;
        if (xmax < CP_L || xmin > CP_R) return 2;

        x0_ = (x0 > CP_L) ? x0 : CP_L;  if (x0_ > CP_R) x0_ = CP_R;
        x1_ = (x1 > CP_L) ? x1 : CP_L;  if (x1_ > CP_R) x1_ = CP_R;
        y0_ = y0;

        // xstep: +2 if sgndx>=0, -2 if sgndx<0 (16bpp pixel = 2 bytes)
        int xstep = ((sgndx + 1) | sgndx) * 2;
        int count = abs_via_mask(x1_ - x0_, sign_mask(x1_ - x0_)) + 1;

        uint8_t *dst = CP_A + y0_ * CP_BW + x0_ * pixel_pitch;
        uint16_t pixel = (uint16_t)(RGB_color & 0xFFFF);

        if (mode == LD_DRAW)
        {
            while (count--) { *(uint16_t *)dst = pixel; dst += xstep; }
        }
        else if (mode == LD_TRANSLATE)
        {
            uint16_t *xlat = (uint16_t *)parm;
            while (count--)
            {
                uint16_t p = *(uint16_t *)dst;
                *(uint16_t *)dst = xlat[p];
                dst += xstep;
            }
        }
        else
        {
            LineDrawCallback cb = (LineDrawCallback)parm;
            int px = x0_ - CP_CX, py = y0_ - CP_CY;
            int xbump = (sgndx >= 0) ? 1 : -1;
            while (count--) { cb(px, py); px += xbump; }
        }
        return (x0_ != x0 || x1_ != x1) ? 1 : 0;
    }

    // General case: compute slope
    // slope = (max(absdx,absdy)-1) / min(absdx,absdy) as 32-bit fixed point
    // When absdx == absdy: slope = 0xFFFFFFFF (diagonal)
    uint32_t slope;
    if (absdx == absdy)
    {
        slope = 0xFFFFFFFF;
    }
    else
    {
        uint32_t major = (absdx > absdy) ? absdx : absdy;
        uint32_t minor = (absdx > absdy) ? absdy : absdx;
        slope = (uint32_t)(((uint64_t)0x100000000ULL) / minor);
        // matches "xor eax,eax / div ebx" which gives 2^32 / minor
        // but asm first swaps so major is in edx... let's match exactly:
        // asm: edx=absdx, ebx=absdy; if edx>ebx swap; xor eax,eax; div ebx
        // so it always divides 2^32 by the smaller of the two
        slope = (uint32_t)(0x100000000ULL / (uint64_t)minor);
    }

    // sgndxdy = sgndx XOR sgndy (sign of dx*dy)
    int32_t sgndxdy = sgndx ^ sgndy;

    // Cohen-Sutherland style clip loop
    uint32_t clip_flags = 0;

    for (;;)
    {
        uint8_t dl = make_clip_bits(x0_, y0_, CP_L, CP_R, CP_T, CP_B);
        uint8_t dh = make_clip_bits(x1_, y1_, CP_L, CP_R, CP_T, CP_B);

        clip_flags |= ((uint32_t)dh << 8) | dl;

        // Completely inside?
        if ((dl | dh) == 0) break;

        // Completely outside one edge?
        if (dl & dh) return 2;

        int x_major = (absdx >= absdy);

        // Clip p0
        if (dl & 0x08) // x0_ < CP_L
        {
            if (x_major)
            {
                x0_ = CP_L;
                uint32_t delta = (uint32_t)(CP_L - x0);
                y0_ = (int32_t)(y0 + apply_sign((int32_t)xmaj_floor(delta, slope), sgndxdy));
            }
            else
            {
                x0_ = CP_L;
                uint32_t delta = (uint32_t)(CP_L - x0);
                y0_ = (int32_t)(y0 + apply_sign((int32_t)ymaj_ceil(delta, slope), sgndxdy));
            }
            continue;
        }
        if (dl & 0x04) // x0_ > CP_R
        {
            if (x_major)
            {
                x0_ = CP_R;
                uint32_t delta = (uint32_t)(x0 - CP_R);
                y0_ = (int32_t)(y0 + apply_sign((int32_t)xmaj_floor(delta, slope), ~sgndxdy));
            }
            else
            {
                x0_ = CP_R;
                uint32_t delta = (uint32_t)(x0 - CP_R);
                y0_ = (int32_t)(y0 + apply_sign((int32_t)ymaj_ceil(delta, slope), ~sgndxdy));
            }
            continue;
        }
        if (dl & 0x02) // y0_ < CP_T
        {
            if (x_major)
            {
                y0_ = CP_T;
                uint32_t delta = (uint32_t)(CP_T - y0);
                x0_ = (int32_t)(x0 + apply_sign((int32_t)ymaj_ceil(delta, slope), sgndxdy));
            }
            else
            {
                y0_ = CP_T;
                uint32_t delta = (uint32_t)(CP_T - y0);
                x0_ = (int32_t)(x0 + apply_sign((int32_t)xmaj_floor(delta, slope), sgndxdy));
            }
            continue;
        }
        if (dl & 0x01) // y0_ > CP_B
        {
            if (x_major)
            {
                y0_ = CP_B;
                uint32_t delta = (uint32_t)(y0 - CP_B);
                x0_ = (int32_t)(x0 + apply_sign((int32_t)ymaj_ceil(delta, slope), ~sgndxdy));
            }
            else
            {
                y0_ = CP_B;
                uint32_t delta = (uint32_t)(y0 - CP_B);
                x0_ = (int32_t)(x0 + apply_sign((int32_t)xmaj_floor(delta, slope), ~sgndxdy));
            }
            continue;
        }

        // Clip p1
        if (dh & 0x08) // x1_ < CP_L
        {
            if (x_major)
            {
                x1_ = CP_L;
                uint32_t delta = (uint32_t)(x0 - CP_L);
                y1_ = (int32_t)(y0 + apply_sign((int32_t)xmaj_floor(delta, slope), ~sgndxdy));
            }
            else
            {
                x1_ = CP_L;
                uint32_t delta = (uint32_t)(x0 - CP_L);
                y1_ = (int32_t)(y0 + apply_sign((int32_t)ymaj_ceil_end(delta, slope), ~sgndxdy));
            }
            continue;
        }
        if (dh & 0x04) // x1_ > CP_R
        {
            if (x_major)
            {
                x1_ = CP_R;
                uint32_t delta = (uint32_t)(CP_R - x0);
                y1_ = (int32_t)(y0 + apply_sign((int32_t)xmaj_floor(delta, slope), sgndxdy));
            }
            else
            {
                x1_ = CP_R;
                uint32_t delta = (uint32_t)(CP_R - x0);
                y1_ = (int32_t)(y0 + apply_sign((int32_t)ymaj_ceil_end(delta, slope), sgndxdy));
            }
            continue;
        }
        if (dh & 0x02) // y1_ < CP_T
        {
            if (x_major)
            {
                y1_ = CP_T;
                uint32_t delta = (uint32_t)(y0 - CP_T);
                x1_ = (int32_t)(x0 + apply_sign((int32_t)ymaj_ceil_end(delta, slope), ~sgndxdy));
            }
            else
            {
                y1_ = CP_T;
                uint32_t delta = (uint32_t)(y0 - CP_T);
                x1_ = (int32_t)(x0 + apply_sign((int32_t)xmaj_floor(delta, slope), ~sgndxdy));
            }
            continue;
        }
        if (dh & 0x01) // y1_ > CP_B
        {
            if (x_major)
            {
                y1_ = CP_B;
                uint32_t delta = (uint32_t)(CP_B - y0);
                x1_ = (int32_t)(x0 + apply_sign((int32_t)ymaj_ceil_end(delta, slope), sgndxdy));
            }
            else
            {
                y1_ = CP_B;
                uint32_t delta = (uint32_t)(CP_B - y0);
                x1_ = (int32_t)(x0 + apply_sign((int32_t)xmaj_floor(delta, slope), sgndxdy));
            }
            continue;
        }
    }

    // Line accepted — set up drawing

    // Base address of first pixel
    uint8_t *dst = CP_A + y0_ * CP_BW + x0_ * pixel_pitch;

    // ystep = CP_BW * sgn(dy) — signed row stride
    int32_t ystep = (CP_BW ^ sgndy) - sgndy;

    // xstep = 2 * sgn(dx) (16bpp)
    int32_t xstep = ((sgndx + 1) | sgndx) * 2;

    uint16_t pixel   = (uint16_t)(RGB_color & 0xFFFF);
    uint16_t *xltbl  = (uint16_t *)parm;
    LineDrawCallback cb = (LineDrawCallback)parm;

    if (absdx == absdy)
    {
        // Diagonal: xystep = ystep + xstep
        int32_t xystep = ystep + xstep;
        int32_t count  = absdx + 1;

        if (mode == LD_DRAW)
        {
            while (count--) { *(uint16_t *)dst = pixel; dst += xystep; }
        }
        else if (mode == LD_TRANSLATE)
        {
            while (count--)
            {
                uint16_t p = *(uint16_t *)dst;
                *(uint16_t *)dst = xltbl[p];
                dst += xystep;
            }
        }
        else
        {
            int px = x0_ - CP_CX, py = y0_ - CP_CY;
            int xbump = (sgndx >= 0) ? 1 : -1;
            int ybump = (sgndy >= 0) ? 1 : -1;
            while (count--) { cb(px, py); px += xbump; py += ybump; }
        }
    }
    else if (absdx > absdy)
    {
        // X-major
        int32_t count = abs_via_mask(x1_ - x0_, sign_mask(x1_ - x0_)) + 1;

        // Initial accumulator: abs(x0_ - x0) * slope + ONE_HALF
        uint32_t init_delta = (uint32_t)abs_via_mask(x0_ - x0, sign_mask(x0_ - x0));
        uint64_t accum64    = (uint64_t)init_delta * slope + ONE_HALF;
        uint32_t accum      = (uint32_t)accum64; // low 32 bits

        if (sgndx >= 0) // positive x direction
        {
            if (mode == LD_DRAW)
            {
                while (count--)
                {
                    *(uint16_t *)dst = pixel;
                    dst += xstep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += ystep; // overflow = step in y
                }
            }
            else if (mode == LD_TRANSLATE)
            {
                while (count--)
                {
                    uint16_t p = *(uint16_t *)dst;
                    *(uint16_t *)dst = xltbl[p];
                    dst += xstep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += ystep;
                }
            }
            else
            {
                int px = x0_ - CP_CX, py = y0_ - CP_CY;
                int ybump = (sgndy >= 0) ? 1 : -1;
                while (count--)
                {
                    cb(px, py);
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) py += ybump;
                    px++;
                }
            }
        }
        else // negative x direction
        {
            if (mode == LD_DRAW)
            {
                while (count--)
                {
                    *(uint16_t *)dst = pixel;
                    dst += xstep; // xstep is negative
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += ystep;
                }
            }
            else if (mode == LD_TRANSLATE)
            {
                while (count--)
                {
                    uint16_t p = *(uint16_t *)dst;
                    *(uint16_t *)dst = xltbl[p];
                    dst += xstep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += ystep;
                }
            }
            else
            {
                int px = x0_ - CP_CX, py = y0_ - CP_CY;
                int ybump = (sgndy >= 0) ? 1 : -1;
                while (count--)
                {
                    cb(px, py);
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) py += ybump;
                    px--;
                }
            }
        }
    }
    else
    {
        // Y-major
        int32_t count = abs_via_mask(y1_ - y0_, sign_mask(y1_ - y0_)) + 1;

        uint32_t init_delta = (uint32_t)abs_via_mask(y0_ - y0, sign_mask(y0_ - y0));
        uint64_t accum64    = (uint64_t)init_delta * slope + ONE_HALF;
        uint32_t accum      = (uint32_t)accum64;

        if (sgndx >= 0) // positive x direction
        {
            if (mode == LD_DRAW)
            {
                while (count--)
                {
                    *(uint16_t *)dst = pixel;
                    dst += ystep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += xstep;
                }
            }
            else if (mode == LD_TRANSLATE)
            {
                while (count--)
                {
                    uint16_t p = *(uint16_t *)dst;
                    *(uint16_t *)dst = xltbl[p];
                    dst += ystep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += xstep;
                }
            }
            else
            {
                int px = x0_ - CP_CX, py = y0_ - CP_CY;
                int ybump = (sgndy >= 0) ? 1 : -1;
                while (count--)
                {
                    cb(px, py);
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) px++;
                    py += ybump;
                }
            }
        }
        else // negative x direction
        {
            if (mode == LD_DRAW)
            {
                while (count--)
                {
                    *(uint16_t *)dst = pixel;
                    dst += ystep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += xstep;
                }
            }
            else if (mode == LD_TRANSLATE)
            {
                while (count--)
                {
                    uint16_t p = *(uint16_t *)dst;
                    *(uint16_t *)dst = xltbl[p];
                    dst += ystep;
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) dst += xstep;
                }
            }
            else
            {
                int px = x0_ - CP_CX, py = y0_ - CP_CY;
                int ybump = (sgndy >= 0) ? 1 : -1;
                while (count--)
                {
                    cb(px, py);
                    uint32_t prev = accum;
                    accum += (uint32_t)slope;
                    if (accum < prev) px--;
                    py += ybump;
                }
            }
        }
    }

    // Return 0 if no clipping occurred, 1 if clipped
    return (clip_flags != 0) ? 1 : 0;
}

S32  WINAPI VFX_font_height (VFX_FONT *font) {
    return font->char_height;
}

void WINAPI VFX_ellipse_fill (PANE *pane, S32 xc, S32 yc, S32 width, S32 height, U32 color) {
    // Degenerate case: either axis is zero — draw a line instead
    if (width == 0 || height == 0)
    {
        VFX_line_draw(pane,
                      xc - width,  yc - height,
                      xc + width,  yc + height,
                      LD_DRAW, color);
        return;
    }

    //
    // CLIP_PANE_TO_WINDOW pane
    //

    VFX_WINDOW *window = pane->window;

    int CP_W = window->x_max + 1;
    if (CP_W <= 0) return;

    int CP_BW = CP_W * window->pixel_pitch;
    int CP_H  = window->y_max + 1;
    if (CP_H <= 0) return;

    int CP_CX = pane->x0;
    int CP_CY = pane->y0;

    int CP_L = (pane->x0 > 0)        ? pane->x0 : 0;
    int CP_T = (pane->y0 > 0)        ? pane->y0 : 0;
    int CP_R = (pane->x1 < CP_W - 1) ? pane->x1 : CP_W - 1;
    int CP_B = (pane->y1 < CP_H - 1) ? pane->y1 : CP_H - 1;

    if (CP_R < CP_L || CP_B < CP_T) return;

    uint8_t *CP_A        = (uint8_t *)window->buffer;
    int      pixel_pitch = window->pixel_pitch;

    // Replicate color into all bytes for fast dword fill
    // Assembly: "mov ah,al / mov [Color],eax / mov WORD PTR [Color+2],ax"
    uint16_t color16 = (uint16_t)(color & 0xFFFF);
    uint32_t color32 = ((uint32_t)color16 << 16) | color16;

    // Convert centre from pane to window coordinates
    xc += CP_CX;
    yc += CP_CY;

    // Midpoint ellipse algorithm state
    // x_bottom/y_bottom = centre; x_top/y_top = current offset from centre
    int32_t x_bottom = xc;
    int32_t y_bottom = yc;
    int32_t x_top    = 0;
    int32_t y_top    = height;

    int32_t Bsquared     = height * height;
    int32_t TwoBsquared  = 2 * Bsquared;
    int32_t Asquared     = width * width;
    int32_t TwoAsquared  = 2 * Asquared;

    int32_t var_dx   = 0;
    int32_t var_dy   = TwoAsquared * height;

    // Initial decision variable: x_vector = a^2/4 + b^2 - a^2*b
    int32_t x_vector = (Asquared >> 2) + Bsquared - Asquared * height;

    // Helper lambda: draw one clipped horizontal span at window-y=wy,
    // from window-x=left to window-x=right (inclusive), clipped to pane.
    auto hline = [&](int32_t left, int32_t right, int32_t wy)
    {
        // Clip x
        int32_t xl = (left  < CP_L) ? CP_L : left;
        int32_t xr = (right > CP_R) ? CP_R : right;
        if (xl > xr) return;

        // Clip y
        if (wy < CP_T || wy > CP_B) return;

        // GET_WINDOW_ADDRESS / BASCALC
        uint8_t *dst = CP_A + wy * CP_BW + xl * pixel_pitch;

        // Fill: dwords first, then leftover word (matches rep stosd / rep stosw)
        int32_t bytes = (xr - xl + 1) * 2; // 16bpp = 2 bytes per pixel
        int32_t dwords = bytes >> 2;
        int32_t words  = (bytes & 3) >> 1;

        uint32_t *d = (uint32_t *)dst;
        while (dwords--) *d++ = color32;
        if (words) *(uint16_t *)d = color16;
    };

    // Helper: draw both symmetric horizontal spans for current x_top/y_top
    auto ellipse_lines = [&]()
    {
        // Right end = x_bottom + x_top, left end = x_bottom - x_top
        int32_t right = x_bottom + x_top;
        int32_t left  = x_bottom - x_top;

        // Clip right end to window
        if (right < CP_L) return;
        right = (right > CP_R) ? CP_R : right;

        // Clip left end to window
        if (left > CP_R) return;
        left = (left < CP_L) ? CP_L : left;

        // Bottom span: y = y_bottom + y_top
        int32_t yb = y_bottom + y_top;
        if (yb >= CP_T && yb <= CP_B)
            hline(left, right, yb);

        // Top span: y = y_bottom - y_top
        int32_t yt = y_bottom - y_top;
        if (yt >= CP_T && yt <= CP_B)
            hline(left, right, yt);
    };

    int32_t b = height;

    // Region 1: while dx < dy (x_top is the major axis)
    while (var_dx < var_dy)
    {
        ellipse_lines();

        if (x_vector >= 0)
        {
            y_top--;
            b--;
            var_dy   -= TwoAsquared;
            x_vector -= var_dy;
        }

        x_top++;
        var_dx   += TwoBsquared;
        x_vector += var_dx + Bsquared;
    }

    // Transition: adjust x_vector for region 2
    {
        int32_t diff  = Asquared - Bsquared;
        int32_t adj   = diff + (diff >> 1); // 3*(a^2-b^2)/2
        adj -= (var_dx + var_dy);
        adj >>= 1;
        x_vector += adj;
    }

    // Region 2: while b >= 0 (y_top is the major axis)
    while (b >= 0)
    {
        ellipse_lines();

        if (x_vector < 0)
        {
            x_top++;
            var_dx   += TwoBsquared;
            x_vector += var_dx;
        }

        y_top--;
        var_dy   -= TwoAsquared;
        x_vector -= var_dy - Asquared;

        b--;
    }
}
