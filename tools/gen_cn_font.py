#!/usr/bin/env python3
"""Generate LVGL font C files with 3600+ common Chinese characters from SimHei TTF."""

import freetype
import struct
import sys
import os

FONT_PATH = r"C:\Windows\Fonts\simhei.ttf"
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "main")

# GB2312 Level 1: 3755 most common Chinese characters
# We generate this programmatically from GB2312 encoding
def gb2312_level1_chars():
    chars = []
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                encoded = bytes([high, low])
                char = encoded.decode('gb2312')
                chars.append(char)
            except (UnicodeDecodeError, ValueError):
                continue
    return chars

# ASCII printable range
def ascii_chars():
    return [chr(i) for i in range(0x20, 0x7F)]

# Common punctuation
def punctuation_chars():
    return list("，。！？、；：""''（）【】《》—…·～￥")


def generate_font(font_path, size_px, chars, output_path, font_name):
    """Generate LVGL font C file."""
    print(f"Generating {font_name} ({size_px}px) with {len(chars)} characters...")

    face = freetype.Face(font_path)
    face.set_char_size(size_px * 64)

    # Render all glyphs
    glyphs = []
    for ch in chars:
        cp = ord(ch)
        try:
            face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        except freetype.FreeTypeException:
            try:
                face.load_char('?', freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
            except:
                continue

        bitmap = face.glyph.bitmap
        width = bitmap.width
        rows = bitmap.rows
        advance = face.glyph.advance.x >> 6

        # Get pixel data (8-bit grayscale, we'll convert to 4-bit)
        pixels = []
        for r in range(rows):
            row_data = bitmap.buffer[r * bitmap.pitch : r * bitmap.pitch + bitmap.width]
            pixels.append(list(row_data))

        glyphs.append({
            'cp': cp,
            'char': ch,
            'width': width,
            'height': rows,
            'advance': advance,
            'bitmap': pixels,
            'left': face.glyph.bitmap_left,
            'top': face.glyph.bitmap_top,
        })

    print(f"  Rendered {len(glyphs)} glyphs")

    # Find max dimensions for bounding box
    max_w = max(g['width'] for g in glyphs) if glyphs else 0
    max_h = max(g['height'] for g in glyphs) if glyphs else 0
    print(f"  Max glyph: {max_w}x{max_h}, typical advance: {glyphs[0]['advance'] if glyphs else 0}")

    # Calculate total bitmap size (4-bit packed)
    total_bitmap_bytes = 0
    for g in glyphs:
        w = g['width']
        h = g['height']
        if w == 0 or h == 0:
            g['bitmap_offset'] = total_bitmap_bytes
            g['bitmap_data'] = []
            continue
        # 4-bit per pixel, packed 2 pixels per byte
        row_bytes = (w + 1) // 2
        g_bytes = row_bytes * h
        g['bitmap_offset'] = total_bitmap_bytes

        # Convert grayscale (0-255) to 4-bit (0-15)
        bitmap_data = []
        for r in range(h):
            for c in range(0, w, 2):
                v1 = (pixels[r][c] * 15 + 127) // 255 if c < len(pixels[r]) else 0
                v2 = (pixels[r][c+1] * 15 + 127) // 255 if c+1 < len(pixels[r]) else 0
                bitmap_data.append((v1 << 4) | v2)
        g['bitmap_data'] = bitmap_data
        total_bitmap_bytes += len(bitmap_data)

    print(f"  Total bitmap: {total_bitmap_bytes} bytes ({total_bitmap_bytes/1024:.1f} KB)")

    # Write C file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('#include "lvgl.h"\n\n')
        f.write(f'/* {font_name} - SimHei {size_px}px */\n')
        f.write(f'/* {len(glyphs)} characters (ASCII + GB2312 Level 1 Chinese) */\n\n')

        # Bitmaps
        f.write('/*-----------------\n')
        f.write(' *    BITMAPS\n')
        f.write(' *----------------*/\n')
        f.write('static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n')

        for g in glyphs:
            cp = g['cp']
            if cp < 0x80:
                name = chr(cp)
                f.write(f'    /* U+{cp:04X} "{name}" */\n')
            else:
                f.write(f'    /* U+{cp:04X} "{g["char"]}" */\n')

            if g['bitmap_data']:
                for i in range(0, len(g['bitmap_data']), 16):
                    chunk = g['bitmap_data'][i:i+16]
                    hex_vals = ', '.join(f'0x{b:02x}' for b in chunk)
                    f.write(f'    {hex_vals},\n')
            else:
                f.write('    /* empty */\n')

        f.write('};\n\n')

        # Glyph descriptors
        f.write('/*---------------------\n')
        f.write(' *  GLYPH DESCRIPTIONS\n')
        f.write(' *---------------------*/\n')
        f.write('static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n')

        for g in glyphs:
            w = g['width']
            h = g['height']
            adv = g['advance']
            bo = g['bitmap_offset']
            f.write(f'    {{{adv}, {bo}, {w}, {h}, {g["left"]}, {g["top"]}}}, /* U+{g["cp"]:04X} */\n')

        f.write('};\n\n')

        # Character maps - split into ranges for efficiency
        f.write('/*---------------------\n')
        f.write(' * CHARACTER MAPPING\n')
        f.write(' *---------------------*/\n')

        # Build ranges: ASCII, then BMP ranges for CJK
        ranges = []
        # ASCII range
        ascii_g = [g for g in glyphs if g['cp'] < 0x80]
        if ascii_g:
            ranges.append({
                'data': list(range(len(ascii_g))),
                'start': ascii_g[0]['cp'],
                'end': ascii_g[-1]['cp'],
            })

        # CJK ranges (group consecutive codepoints)
        cjk_g = sorted([g for g in glyphs if g['cp'] >= 0x80], key=lambda g: g['cp'])
        if cjk_g:
            # Split into chunks of ~200 chars to avoid huge cmap tables
            chunk_size = 200
            for i in range(0, len(cjk_g), chunk_size):
                chunk = cjk_g[i:i+chunk_size]
                ranges.append({
                    'data': [glyphs.index(g) for g in chunk],
                    'start': chunk[0]['cp'],
                    'end': chunk[-1]['cp'],
                })

        f.write('static const uint16_t cmap_map[] = {\n')
        for g in glyphs:
            f.write(f'    {glyphs.index(g)}, /* U+{g["cp"]:04X} */\n')
        f.write('};\n\n')

        # Build range tables
        f.write('static const lv_font_fmt_txt_cmap_t cmaps[] = {\n')
        for idx, r in enumerate(ranges):
            data_list = ', '.join(str(d) for d in r['data'])
            f.write(f'    {{{r["start"]}, {r["end"]} - {r["start"]} + 1, &cmap_map[{ranges[idx]["data"][0]}]}}},\n')
        f.write('};\n\n')

        # Font descriptor
        f.write('#if LVGL_VERSION_MAJOR == 8\n')
        f.write('static lv_font_fmt_txt_glyph_cache_t cache;\n')
        f.write('#endif\n\n')

        f.write(f'#if LVGL_VERSION_MAJOR >= 8\n')
        f.write(f'static const lv_font_fmt_txt_dsc_t font_dsc = {{\n')
        f.write(f'#else\n')
        f.write(f'static lv_font_fmt_txt_dsc_t font_dsc = {{\n')
        f.write(f'#endif\n')
        f.write(f'    .glyph_bitmap = glyph_bitmap,\n')
        f.write(f'    .glyph_dsc = glyph_dsc,\n')
        f.write(f'    .cmaps = cmaps,\n')
        f.write(f'    .kern_dsc = NULL,\n')
        f.write(f'    .kern_scale = 0,\n')
        f.write(f'    .cmap_num = {len(ranges)},\n')
        f.write(f'    .bpp = 4,\n')
        f.write(f'    .kern_classes = 0,\n')
        f.write(f'    .bitmap_format = 0,\n')
        f.write(f'#if LVGL_VERSION_MAJOR == 8\n')
        f.write(f'    .cache = &cache\n')
        f.write(f'#endif\n')
        f.write(f'}};\n\n')

        # Public font struct
        ascent = size_px - 2
        descent = 2
        line_height = ascent + descent

        f.write(f'/*-----------------\n')
        f.write(f' *  PUBLIC FONT\n')
        f.write(f' *----------------*/\n\n')
        f.write(f'#if LVGL_VERSION_MAJOR >= 8\n')
        f.write(f'const lv_font_t {font_name} = {{\n')
        f.write(f'#else\n')
        f.write(f'lv_font_t {font_name} = {{\n')
        f.write(f'#endif\n')
        f.write(f'    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n')
        f.write(f'    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n')
        f.write(f'    .line_height = {line_height},\n')
        f.write(f'    .base_line = {descent},\n')
        f.write(f'#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)\n')
        f.write(f'    .subpx = LV_FONT_SUBPX_NONE,\n')
        f.write(f'#endif\n')
        f.write(f'#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8\n')
        f.write(f'    .underline_position = -1,\n')
        f.write(f'    .underline_thickness = 1,\n')
        f.write(f'#endif\n')
        f.write(f'    .dsc = &font_dsc,\n')
        f.write(f'#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9\n')
        f.write(f'    .fallback = NULL,\n')
        f.write(f'#endif\n')
        f.write(f'#if LVGL_VERSION_MAJOR >= 8\n')
        f.write(f'    .user_data = NULL,\n')
        f.write(f'#endif\n')
        f.write(f'}};\n')

    print(f"  Written to {output_path}")
    return glyphs


def main():
    # Collect all characters
    chars = []
    chars.extend(ascii_chars())
    chars.extend(punctuation_chars())
    chars.extend(gb2312_level1_chars())

    # Remove duplicates, sort by codepoint
    seen = set()
    unique = []
    for ch in chars:
        cp = ord(ch)
        if cp not in seen:
            seen.add(cp)
            unique.append(ch)
    chars = unique
    print(f"Total unique characters: {len(chars)}")

    for size_px, font_name in [(16, "font_cn_16"), (20, "font_cn_20")]:
        output_path = os.path.join(OUTPUT_DIR, f"{font_name}.c")
        generate_font(FONT_PATH, size_px, chars, output_path, font_name)
        print()


if __name__ == '__main__':
    main()
