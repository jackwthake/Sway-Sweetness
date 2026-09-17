#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple BDF -> C array converter.
// Usage: bdf2c input.bdf > glyphs.h
// Produces: static const unsigned char glyph_bitmaps[256][ROWS];
// Assumes a fixed-height font (fine for Tamzen NxM variants).

#define MAX_ROWS 32

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s font.bdf\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 1; }

    unsigned char glyphs[256][MAX_ROWS];
    int glyph_rows[256];
    memset(glyphs, 0, sizeof(glyphs));
    memset(glyph_rows, 0, sizeof(glyph_rows));

    char line[256];
    int encoding = -1;
    int in_bitmap = 0;
    int row = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ENCODING", 8) == 0) {
            encoding = atoi(line + 9);
        } else if (strncmp(line, "BITMAP", 6) == 0 && strncmp(line, "BITMAP", 6) == 0 && line[6] != 'D') {
            // "BITMAP" line (not "BBX"/"DWIDTH" etc.) marks start of pixel rows
            in_bitmap = 1;
            row = 0;
        } else if (strncmp(line, "ENDCHAR", 7) == 0) {
            in_bitmap = 0;
            if (encoding >= 0 && encoding < 256) {
                glyph_rows[encoding] = row;
            }
            encoding = -1;
        } else if (in_bitmap) {
            unsigned int byte;
            if (sscanf(line, "%x", &byte) == 1 && encoding >= 0 && encoding < 256 && row < MAX_ROWS) {
                glyphs[encoding][row++] = (unsigned char)byte;
            }
        }
    }
    fclose(f);

    printf("// Auto-generated from %s\n", argv[1]);
    printf("static const unsigned char glyph_bitmaps[256][%d] = {\n", MAX_ROWS);
    for (int c = 0; c < 256; c++) {
        printf("    { ");
        for (int r = 0; r < MAX_ROWS; r++) {
            printf("0x%02X, ", glyphs[c][r]);
        }
        printf("}, // 0x%02X '%c'\n", c, (c >= 32 && c < 127) ? c : '?');
    }
    printf("};\n");

    return 0;
}
