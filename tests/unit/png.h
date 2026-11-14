/* SPDX-License-Identifier: GPL-3.0-or-later
 * A tiny PNG writer for test snapshots: RGB, uncompressed deflate ("stored" blocks). No zlib needed. */
#ifndef STK_TEST_PNG_H
#define STK_TEST_PNG_H
#include <stdint.h>
#include <stdio.h>

static uint32_t png_crc_tab[256];
static void png_crc_init(void)
{
    uint32_t n, c, k;
    for (n = 0; n < 256; n++) {
        for (c = n, k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        png_crc_tab[n] = c;
    }
}
static uint32_t png_crc(uint32_t c, const uint8_t *p, size_t n)
{
    while (n--)
        c = png_crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c;
}
static void png_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t b[4];
    uint32_t c;
    png_be32(b, n);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (n)
        fwrite(d, 1, n, f);
    c = png_crc(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = png_crc(c, d, n) ^ 0xFFFFFFFFu;
    png_be32(b, c);
    fwrite(b, 1, 4, f);
}

/* rgb: w*h*3 bytes */
static int png_write_rgb(const char *path, const uint8_t *rgb, uint32_t w, uint32_t h)
{
    static uint8_t raw[1 << 20], z[(1 << 20) + 4096];
    uint32_t row = w * 3 + 1, n = row * h, y, zi = 0, off = 0, a = 1, b = 0, i;
    uint8_t ihdr[13];
    FILE *f;
    if (n > sizeof raw)
        return -1;
    png_crc_init();
    for (y = 0; y < h; y++) {
        raw[y * row] = 0;    /* filter: none */
        for (i = 0; i < w * 3; i++)
            raw[y * row + 1 + i] = rgb[y * w * 3 + i];
    }
    z[zi++] = 0x78;
    z[zi++] = 0x01;
    while (off < n) {        /* stored blocks of at most 65535 bytes */
        uint32_t len = n - off > 65535u ? 65535u : n - off;
        z[zi++] = off + len == n;
        z[zi++] = len & 0xFF;
        z[zi++] = len >> 8;
        z[zi++] = ~len & 0xFF;
        z[zi++] = (~len >> 8) & 0xFF;
        for (i = 0; i < len; i++)
            z[zi++] = raw[off + i];
        off += len;
    }
    for (i = 0; i < n; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    png_be32(&z[zi], b << 16 | a);
    zi += 4;
    if (!(f = fopen(path, "wb")))
        return -1;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    png_be32(ihdr, w);
    png_be32(ihdr + 4, h);
    ihdr[8] = 8;             /* bit depth */
    ihdr[9] = 2;             /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, zi);
    png_chunk(f, "IEND", 0, 0);
    fclose(f);
    return 0;
}
#endif
