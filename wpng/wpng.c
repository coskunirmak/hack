/* Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:
     clang -o wpng wpng.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

typedef struct { FILE* f; uint32_t crc; uint32_t crc_table[256]; } pngblock;

void pngblock_write(const void* d, int len, pngblock* b) {
  fwrite(d, 1, len, b->f);
  for (int n = 0; n < len; n++)
    b->crc = b->crc_table[(b->crc ^ ((uint8_t*)d)[n]) & 0xff] ^ (b->crc >> 8);
}

void wpng(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  // http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
  pngblock b = { f, 0x575e51f5 };  // == update_crc(0xffffffff, "IHDR")
  for (int n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xedb88320L ^ (c >> 1) : c >> 1;
    b.crc_table[n] = c;
  }
  uint8_t BE[4];
#define PUTU32_BE(u) BE[0] = (u) >> 24; BE[1] = (u) >> 16; BE[2] = (u) >> 8; \
                     BE[3] = (u); pngblock_write(BE, 4, &b)
  fwrite("\x89PNG\r\n\x1a\n\0\0\0\xdIHDR", 1, 16, f);
  PUTU32_BE(w); PUTU32_BE(h);
  pngblock_write("\x8\6\0\0\0", 5, &b);  // 8bpp rgba, default flags
  PUTU32_BE(b.crc ^ 0xffffffff);  // IHDR crc32

  uint16_t scanline_size = w*4 + 1;  // one filter byte per scanline
  PUTU32_BE(6 + (5 + scanline_size)*h);
  b.crc = 0xffffffff;
  pngblock_write("IDAT\x8\x1d", 6, &b); // deflate data, 255 byte sliding window
  uint32_t a1 = 1, a2 = 0;
  for (int y = 0; y < h; ++y) {
    uint32_t s = scanline_size | (~scanline_size << 16);
    uint8_t le[] = { y == h - 1 ? 1 : 0, s, s >> 8, s >> 16, s >> 24, 0 };
    pngblock_write(le, 6, &b);
    pngblock_write(pix + y*4*w, 4*w, &b);
    const int BASE = 65521;  // largest prime smaller than 65536
    a2 = (a1 + a2) % BASE;
    for (int n = 0; n < 4*w; n++) {
      a1 = (a1 + pix[y*4*w + n]) % BASE;
      a2 = (a1 + a2) % BASE;
    }
  }
  PUTU32_BE((a2 << 16) + a1);  // adler32 of uncompressed data
  PUTU32_BE(b.crc ^ 0xffffffff);  // IDAT crc32
#undef PUTU32_BE
  fwrite("\0\0\0\0IEND\xae\x42\x60\x82", 1, 12, f);  // IEND + crc32
}

int main() {
  uint8_t pix[256*125*4];
  for (size_t i = 0; i < sizeof(pix); ++i) pix[i] = i*i;
  wpng(125, 256, pix, stdout);
}