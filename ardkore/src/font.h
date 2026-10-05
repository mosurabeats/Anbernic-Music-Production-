#ifndef ARDKORE_FONT_H
#define ARDKORE_FONT_H

#define FONT_W 5
#define FONT_H 7
#define FONT_GLYPHS 68

/* Control codes for the extra symbols. */
#define FONT_PLAY '\x01'
#define FONT_STOP '\x02'
#define FONT_DOT '\x03'

/* Seven rows, bit 4 = leftmost pixel. */
const unsigned char *font_glyph(int c);

#endif
