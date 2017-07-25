/* $XTermId: wcwidth.c,v 1.39 2017/06/20 20:35:34 tom Exp $ */

/* $XFree86: xc/programs/xterm/wcwidth.c,v 1.9 2006/06/19 00:36:52 dickey Exp $ */

/*
 * Copyright 2002-2016,2017 by Thomas E. Dickey
 *
 *                         All Rights Reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization.
 *-----------------------------------------------------------------------------
 * This is an updated version of Markus Kuhn's implementation of wcwidth.
 *
 * Originally added to xterm in 2000 (patch #141), there were a couple of
 * updates from Kuhn until 2005 (patch #202), renaming entrypoints and applying
 * data from Unicode.org (e.g., 3.2, 4.0, 4.1.0).  The Unicode data is
 * transformed into tables in this file by a script "uniset" written by Kuhn.
 *
 * While Kuhn implemented the original CJK variant, it was unused by xterm
 * until Jungshik Shin used it in 2002 to implement the -cjk_width command-line
 * option.
 *
 * Kuhn added a check for the vertical forms block (double-width) in 2007;
 * other updates were derived from the Unicode.org data (release 5.0).
 *
 * Since then, additional updates have been made:
 * + data-type fixes
 * + new Unicode releases (6.2.0, 9.0.0),
 * + additional special symbol blocks have been added to the special cases.
 * + soft-hyphen behavior has been made configurable.
 * + added table shows when a character is not part of Unicode.
 *
 * Kuhn's original header follows giving the design information:
 *-----------------------------------------------------------------------------
 * This is an implementation of wcwidth() and wcswidth() (defined in
 * IEEE Std 1002.1-2001) for Unicode.
 *
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcwidth.html
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcswidth.html
 *
 * In fixed-width output devices, Latin characters all occupy a single
 * "cell" position of equal width, whereas ideographic CJK characters
 * occupy two such cells. Interoperability between terminal-line
 * applications and (teletype-style) character terminals using the
 * UTF-8 encoding requires agreement on which character should advance
 * the cursor by how many cell positions. No established formal
 * standards exist at present on which Unicode character shall occupy
 * how many cell positions on character terminals. These routines are
 * a first attempt of defining such behavior based on simple rules
 * applied to data provided by the Unicode Consortium.
 *
 * For some graphical characters, the Unicode standard explicitly
 * defines a character-cell width via the definition of the East Asian
 * FullWidth (F), Wide (W), Half-width (H), and Narrow (Na) classes.
 * In all these cases, there is no ambiguity about which width a
 * terminal shall use. For characters in the East Asian Ambiguous (A)
 * class, the width choice depends purely on a preference of backward
 * compatibility with either historic CJK or Western practice.
 * Choosing single-width for these characters is easy to justify as
 * the appropriate long-term solution, as the CJK practice of
 * displaying these characters as double-width comes from historic
 * implementation simplicity (8-bit encoded characters were displayed
 * single-width and 16-bit ones double-width, even for Greek,
 * Cyrillic, etc.) and not any typographic considerations.
 *
 * Much less clear is the choice of width for the Not East Asian
 * (Neutral) class. Existing practice does not dictate a width for any
 * of these characters. It would nevertheless make sense
 * typographically to allocate two character cells to characters such
 * as for instance EM SPACE or VOLUME INTEGRAL, which cannot be
 * represented adequately with a single-width glyph. The following
 * routines at present merely assign a single-cell width to all
 * neutral characters, in the interest of simplicity. This is not
 * entirely satisfactory and should be reconsidered before
 * establishing a formal standard in this area. At the moment, the
 * decision which Not East Asian (Neutral) characters should be
 * represented by double-width glyphs cannot yet be answered by
 * applying a simple rule from the Unicode database content. Setting
 * up a proper standard for the behavior of UTF-8 character terminals
 * will require a careful analysis not only of each Unicode character,
 * but also of each presentation form, something the author of these
 * routines has avoided to do so far.
 *
 * http://www.unicode.org/unicode/reports/tr11/
 *
 * Markus Kuhn -- 2007-05-25 (Unicode 5.0)
 *
 * Permission to use, copy, modify, and distribute this software
 * for any purpose and without fee is hereby granted. The author
 * disclaims all warranties with regard to this software.
 *
 * Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

#include "wcwidth.h"

struct interval {
  unsigned long first;
  unsigned long last;
};

static int use_latin1 = 1;

/* auxiliary function for binary search in interval table */
static int bisearch(unsigned long ucs, const struct interval *table, int max) {

  if (ucs >= table[0].first && ucs <= table[max].last) {
    int min = 0;

    while (max >= min) {
      int mid;

      mid = (min + max) / 2;
      if (ucs > table[mid].last)
        min = mid + 1;
      else if (ucs < table[mid].first)
        max = mid - 1;
      else
        return 1;
    }
  }

  return 0;
}

/*
 * Provide a way to change the behavior of soft-hyphen.
 */
void
mk_wcwidth_init(int mode)
{
  use_latin1 = (mode == 0);
}

/* The following two functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1 in Latin-1, 0 in Unicode.
 *      An initialization function is used to switch between the two.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      Full-width (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.  In that report, some codes
 *      were unassigned.  Characters in these blocks use a column width of 1:
 *          4DC0..4DFF; Yijing Hexagram Symbols
 *          A960..A97F; Hangul Jamo Extended-A
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 *    - Codes which do not correspond to a Unicode character have a column
 *      width of -1.
 *
 * This implementation assumes that wchar_t characters are encoded
 * in ISO 10646.
 */

int mk_wcwidth(wchar_t ucs)
{
  unsigned long cmp = (unsigned long) ucs;

  /* sorted list of non-overlapping intervals of non-spacing characters */
  /* generated by
   *    uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c
   */
  static const struct interval combining[] = {
    { 0x0300, 0x036F }, { 0x0483, 0x0489 }, { 0x0591, 0x05BD },
    { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 }, { 0x05C4, 0x05C5 },
    { 0x05C7, 0x05C7 }, { 0x0600, 0x0605 }, { 0x0610, 0x061A },
    { 0x061C, 0x061C }, { 0x064B, 0x065F }, { 0x0670, 0x0670 },
    { 0x06D6, 0x06DD }, { 0x06DF, 0x06E4 }, { 0x06E7, 0x06E8 },
    { 0x06EA, 0x06ED }, { 0x070F, 0x070F }, { 0x0711, 0x0711 },
    { 0x0730, 0x074A }, { 0x07A6, 0x07B0 }, { 0x07EB, 0x07F3 },
    { 0x0816, 0x0819 }, { 0x081B, 0x0823 }, { 0x0825, 0x0827 },
    { 0x0829, 0x082D }, { 0x0859, 0x085B }, { 0x08D4, 0x0902 },
    { 0x093A, 0x093A }, { 0x093C, 0x093C }, { 0x0941, 0x0948 },
    { 0x094D, 0x094D }, { 0x0951, 0x0957 }, { 0x0962, 0x0963 },
    { 0x0981, 0x0981 }, { 0x09BC, 0x09BC }, { 0x09C1, 0x09C4 },
    { 0x09CD, 0x09CD }, { 0x09E2, 0x09E3 }, { 0x0A01, 0x0A02 },
    { 0x0A3C, 0x0A3C }, { 0x0A41, 0x0A42 }, { 0x0A47, 0x0A48 },
    { 0x0A4B, 0x0A4D }, { 0x0A51, 0x0A51 }, { 0x0A70, 0x0A71 },
    { 0x0A75, 0x0A75 }, { 0x0A81, 0x0A82 }, { 0x0ABC, 0x0ABC },
    { 0x0AC1, 0x0AC5 }, { 0x0AC7, 0x0AC8 }, { 0x0ACD, 0x0ACD },
    { 0x0AE2, 0x0AE3 }, { 0x0AFA, 0x0AFF }, { 0x0B01, 0x0B01 },
    { 0x0B3C, 0x0B3C }, { 0x0B3F, 0x0B3F }, { 0x0B41, 0x0B44 },
    { 0x0B4D, 0x0B4D }, { 0x0B56, 0x0B56 }, { 0x0B62, 0x0B63 },
    { 0x0B82, 0x0B82 }, { 0x0BC0, 0x0BC0 }, { 0x0BCD, 0x0BCD },
    { 0x0C00, 0x0C00 }, { 0x0C3E, 0x0C40 }, { 0x0C46, 0x0C48 },
    { 0x0C4A, 0x0C4D }, { 0x0C55, 0x0C56 }, { 0x0C62, 0x0C63 },
    { 0x0C81, 0x0C81 }, { 0x0CBC, 0x0CBC }, { 0x0CBF, 0x0CBF },
    { 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD }, { 0x0CE2, 0x0CE3 },
    { 0x0D00, 0x0D01 }, { 0x0D3B, 0x0D3C }, { 0x0D41, 0x0D44 },
    { 0x0D4D, 0x0D4D }, { 0x0D62, 0x0D63 }, { 0x0DCA, 0x0DCA },
    { 0x0DD2, 0x0DD4 }, { 0x0DD6, 0x0DD6 }, { 0x0E31, 0x0E31 },
    { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E }, { 0x0EB1, 0x0EB1 },
    { 0x0EB4, 0x0EB9 }, { 0x0EBB, 0x0EBC }, { 0x0EC8, 0x0ECD },
    { 0x0F18, 0x0F19 }, { 0x0F35, 0x0F35 }, { 0x0F37, 0x0F37 },
    { 0x0F39, 0x0F39 }, { 0x0F71, 0x0F7E }, { 0x0F80, 0x0F84 },
    { 0x0F86, 0x0F87 }, { 0x0F8D, 0x0F97 }, { 0x0F99, 0x0FBC },
    { 0x0FC6, 0x0FC6 }, { 0x102D, 0x1030 }, { 0x1032, 0x1037 },
    { 0x1039, 0x103A }, { 0x103D, 0x103E }, { 0x1058, 0x1059 },
    { 0x105E, 0x1060 }, { 0x1071, 0x1074 }, { 0x1082, 0x1082 },
    { 0x1085, 0x1086 }, { 0x108D, 0x108D }, { 0x109D, 0x109D },
    { 0x1160, 0x11FF }, { 0x135D, 0x135F }, { 0x1712, 0x1714 },
    { 0x1732, 0x1734 }, { 0x1752, 0x1753 }, { 0x1772, 0x1773 },
    { 0x17B4, 0x17B5 }, { 0x17B7, 0x17BD }, { 0x17C6, 0x17C6 },
    { 0x17C9, 0x17D3 }, { 0x17DD, 0x17DD }, { 0x180B, 0x180E },
    { 0x1885, 0x1886 }, { 0x18A9, 0x18A9 }, { 0x1920, 0x1922 },
    { 0x1927, 0x1928 }, { 0x1932, 0x1932 }, { 0x1939, 0x193B },
    { 0x1A17, 0x1A18 }, { 0x1A1B, 0x1A1B }, { 0x1A56, 0x1A56 },
    { 0x1A58, 0x1A5E }, { 0x1A60, 0x1A60 }, { 0x1A62, 0x1A62 },
    { 0x1A65, 0x1A6C }, { 0x1A73, 0x1A7C }, { 0x1A7F, 0x1A7F },
    { 0x1AB0, 0x1ABE }, { 0x1B00, 0x1B03 }, { 0x1B34, 0x1B34 },
    { 0x1B36, 0x1B3A }, { 0x1B3C, 0x1B3C }, { 0x1B42, 0x1B42 },
    { 0x1B6B, 0x1B73 }, { 0x1B80, 0x1B81 }, { 0x1BA2, 0x1BA5 },
    { 0x1BA8, 0x1BA9 }, { 0x1BAB, 0x1BAD }, { 0x1BE6, 0x1BE6 },
    { 0x1BE8, 0x1BE9 }, { 0x1BED, 0x1BED }, { 0x1BEF, 0x1BF1 },
    { 0x1C2C, 0x1C33 }, { 0x1C36, 0x1C37 }, { 0x1CD0, 0x1CD2 },
    { 0x1CD4, 0x1CE0 }, { 0x1CE2, 0x1CE8 }, { 0x1CED, 0x1CED },
    { 0x1CF4, 0x1CF4 }, { 0x1CF8, 0x1CF9 }, { 0x1DC0, 0x1DF9 },
    { 0x1DFB, 0x1DFF }, { 0x200B, 0x200F }, { 0x202A, 0x202E },
    { 0x2060, 0x2064 }, { 0x2066, 0x206F }, { 0x20D0, 0x20F0 },
    { 0x2CEF, 0x2CF1 }, { 0x2D7F, 0x2D7F }, { 0x2DE0, 0x2DFF },
    { 0x302A, 0x302D }, { 0x3099, 0x309A }, { 0xA66F, 0xA672 },
    { 0xA674, 0xA67D }, { 0xA69E, 0xA69F }, { 0xA6F0, 0xA6F1 },
    { 0xA802, 0xA802 }, { 0xA806, 0xA806 }, { 0xA80B, 0xA80B },
    { 0xA825, 0xA826 }, { 0xA8C4, 0xA8C5 }, { 0xA8E0, 0xA8F1 },
    { 0xA926, 0xA92D }, { 0xA947, 0xA951 }, { 0xA980, 0xA982 },
    { 0xA9B3, 0xA9B3 }, { 0xA9B6, 0xA9B9 }, { 0xA9BC, 0xA9BC },
    { 0xA9E5, 0xA9E5 }, { 0xAA29, 0xAA2E }, { 0xAA31, 0xAA32 },
    { 0xAA35, 0xAA36 }, { 0xAA43, 0xAA43 }, { 0xAA4C, 0xAA4C },
    { 0xAA7C, 0xAA7C }, { 0xAAB0, 0xAAB0 }, { 0xAAB2, 0xAAB4 },
    { 0xAAB7, 0xAAB8 }, { 0xAABE, 0xAABF }, { 0xAAC1, 0xAAC1 },
    { 0xAAEC, 0xAAED }, { 0xAAF6, 0xAAF6 }, { 0xABE5, 0xABE5 },
    { 0xABE8, 0xABE8 }, { 0xABED, 0xABED }, { 0xFB1E, 0xFB1E },
    { 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F }, { 0xFEFF, 0xFEFF },
    { 0xFFF9, 0xFFFB }, { 0x101FD, 0x101FD }, { 0x102E0, 0x102E0 },
    { 0x10376, 0x1037A }, { 0x10A01, 0x10A03 }, { 0x10A05, 0x10A06 },
    { 0x10A0C, 0x10A0F }, { 0x10A38, 0x10A3A }, { 0x10A3F, 0x10A3F },
    { 0x10AE5, 0x10AE6 }, { 0x11001, 0x11001 }, { 0x11038, 0x11046 },
    { 0x1107F, 0x11081 }, { 0x110B3, 0x110B6 }, { 0x110B9, 0x110BA },
    { 0x110BD, 0x110BD }, { 0x11100, 0x11102 }, { 0x11127, 0x1112B },
    { 0x1112D, 0x11134 }, { 0x11173, 0x11173 }, { 0x11180, 0x11181 },
    { 0x111B6, 0x111BE }, { 0x111CA, 0x111CC }, { 0x1122F, 0x11231 },
    { 0x11234, 0x11234 }, { 0x11236, 0x11237 }, { 0x1123E, 0x1123E },
    { 0x112DF, 0x112DF }, { 0x112E3, 0x112EA }, { 0x11300, 0x11301 },
    { 0x1133C, 0x1133C }, { 0x11340, 0x11340 }, { 0x11366, 0x1136C },
    { 0x11370, 0x11374 }, { 0x11438, 0x1143F }, { 0x11442, 0x11444 },
    { 0x11446, 0x11446 }, { 0x114B3, 0x114B8 }, { 0x114BA, 0x114BA },
    { 0x114BF, 0x114C0 }, { 0x114C2, 0x114C3 }, { 0x115B2, 0x115B5 },
    { 0x115BC, 0x115BD }, { 0x115BF, 0x115C0 }, { 0x115DC, 0x115DD },
    { 0x11633, 0x1163A }, { 0x1163D, 0x1163D }, { 0x1163F, 0x11640 },
    { 0x116AB, 0x116AB }, { 0x116AD, 0x116AD }, { 0x116B0, 0x116B5 },
    { 0x116B7, 0x116B7 }, { 0x1171D, 0x1171F }, { 0x11722, 0x11725 },
    { 0x11727, 0x1172B }, { 0x11A01, 0x11A06 }, { 0x11A09, 0x11A0A },
    { 0x11A33, 0x11A38 }, { 0x11A3B, 0x11A3E }, { 0x11A47, 0x11A47 },
    { 0x11A51, 0x11A56 }, { 0x11A59, 0x11A5B }, { 0x11A8A, 0x11A96 },
    { 0x11A98, 0x11A99 }, { 0x11C30, 0x11C36 }, { 0x11C38, 0x11C3D },
    { 0x11C3F, 0x11C3F }, { 0x11C92, 0x11CA7 }, { 0x11CAA, 0x11CB0 },
    { 0x11CB2, 0x11CB3 }, { 0x11CB5, 0x11CB6 }, { 0x11D31, 0x11D36 },
    { 0x11D3A, 0x11D3A }, { 0x11D3C, 0x11D3D }, { 0x11D3F, 0x11D45 },
    { 0x11D47, 0x11D47 }, { 0x16AF0, 0x16AF4 }, { 0x16B30, 0x16B36 },
    { 0x16F8F, 0x16F92 }, { 0x1BC9D, 0x1BC9E }, { 0x1BCA0, 0x1BCA3 },
    { 0x1D167, 0x1D169 }, { 0x1D173, 0x1D182 }, { 0x1D185, 0x1D18B },
    { 0x1D1AA, 0x1D1AD }, { 0x1D242, 0x1D244 }, { 0x1DA00, 0x1DA36 },
    { 0x1DA3B, 0x1DA6C }, { 0x1DA75, 0x1DA75 }, { 0x1DA84, 0x1DA84 },
    { 0x1DA9B, 0x1DA9F }, { 0x1DAA1, 0x1DAAF }, { 0x1E000, 0x1E006 },
    { 0x1E008, 0x1E018 }, { 0x1E01B, 0x1E021 }, { 0x1E023, 0x1E024 },
    { 0x1E026, 0x1E02A }, { 0x1E8D0, 0x1E8D6 }, { 0x1E944, 0x1E94A },
    { 0xE0001, 0xE0001 }, { 0xE0020, 0xE007F }, { 0xE0100, 0xE01EF }
  };

  /* sorted list of non-overlapping intervals of non-characters */
  /* generated by
   *    uniset +0000..DFFF -4e00..9fd5 +F900..10FFFD unknown +2028..2029 c
   */
  static const struct interval unknowns[] = {
    { 0x0378, 0x0379 }, { 0x0380, 0x0383 }, { 0x038B, 0x038B },
    { 0x038D, 0x038D }, { 0x03A2, 0x03A2 }, { 0x0530, 0x0530 },
    { 0x0557, 0x0558 }, { 0x0560, 0x0560 }, { 0x0588, 0x0588 },
    { 0x058B, 0x058C }, { 0x0590, 0x0590 }, { 0x05C8, 0x05CF },
    { 0x05EB, 0x05EF }, { 0x05F5, 0x05FF }, { 0x061D, 0x061D },
    { 0x070E, 0x070E }, { 0x074B, 0x074C }, { 0x07B2, 0x07BF },
    { 0x07FB, 0x07FF }, { 0x082E, 0x082F }, { 0x083F, 0x083F },
    { 0x085C, 0x085D }, { 0x085F, 0x085F }, { 0x086B, 0x089F },
    { 0x08B5, 0x08B5 }, { 0x08BE, 0x08D3 }, { 0x0984, 0x0984 },
    { 0x098D, 0x098E }, { 0x0991, 0x0992 }, { 0x09A9, 0x09A9 },
    { 0x09B1, 0x09B1 }, { 0x09B3, 0x09B5 }, { 0x09BA, 0x09BB },
    { 0x09C5, 0x09C6 }, { 0x09C9, 0x09CA }, { 0x09CF, 0x09D6 },
    { 0x09D8, 0x09DB }, { 0x09DE, 0x09DE }, { 0x09E4, 0x09E5 },
    { 0x09FE, 0x0A00 }, { 0x0A04, 0x0A04 }, { 0x0A0B, 0x0A0E },
    { 0x0A11, 0x0A12 }, { 0x0A29, 0x0A29 }, { 0x0A31, 0x0A31 },
    { 0x0A34, 0x0A34 }, { 0x0A37, 0x0A37 }, { 0x0A3A, 0x0A3B },
    { 0x0A3D, 0x0A3D }, { 0x0A43, 0x0A46 }, { 0x0A49, 0x0A4A },
    { 0x0A4E, 0x0A50 }, { 0x0A52, 0x0A58 }, { 0x0A5D, 0x0A5D },
    { 0x0A5F, 0x0A65 }, { 0x0A76, 0x0A80 }, { 0x0A84, 0x0A84 },
    { 0x0A8E, 0x0A8E }, { 0x0A92, 0x0A92 }, { 0x0AA9, 0x0AA9 },
    { 0x0AB1, 0x0AB1 }, { 0x0AB4, 0x0AB4 }, { 0x0ABA, 0x0ABB },
    { 0x0AC6, 0x0AC6 }, { 0x0ACA, 0x0ACA }, { 0x0ACE, 0x0ACF },
    { 0x0AD1, 0x0ADF }, { 0x0AE4, 0x0AE5 }, { 0x0AF2, 0x0AF8 },
    { 0x0B00, 0x0B00 }, { 0x0B04, 0x0B04 }, { 0x0B0D, 0x0B0E },
    { 0x0B11, 0x0B12 }, { 0x0B29, 0x0B29 }, { 0x0B31, 0x0B31 },
    { 0x0B34, 0x0B34 }, { 0x0B3A, 0x0B3B }, { 0x0B45, 0x0B46 },
    { 0x0B49, 0x0B4A }, { 0x0B4E, 0x0B55 }, { 0x0B58, 0x0B5B },
    { 0x0B5E, 0x0B5E }, { 0x0B64, 0x0B65 }, { 0x0B78, 0x0B81 },
    { 0x0B84, 0x0B84 }, { 0x0B8B, 0x0B8D }, { 0x0B91, 0x0B91 },
    { 0x0B96, 0x0B98 }, { 0x0B9B, 0x0B9B }, { 0x0B9D, 0x0B9D },
    { 0x0BA0, 0x0BA2 }, { 0x0BA5, 0x0BA7 }, { 0x0BAB, 0x0BAD },
    { 0x0BBA, 0x0BBD }, { 0x0BC3, 0x0BC5 }, { 0x0BC9, 0x0BC9 },
    { 0x0BCE, 0x0BCF }, { 0x0BD1, 0x0BD6 }, { 0x0BD8, 0x0BE5 },
    { 0x0BFB, 0x0BFF }, { 0x0C04, 0x0C04 }, { 0x0C0D, 0x0C0D },
    { 0x0C11, 0x0C11 }, { 0x0C29, 0x0C29 }, { 0x0C3A, 0x0C3C },
    { 0x0C45, 0x0C45 }, { 0x0C49, 0x0C49 }, { 0x0C4E, 0x0C54 },
    { 0x0C57, 0x0C57 }, { 0x0C5B, 0x0C5F }, { 0x0C64, 0x0C65 },
    { 0x0C70, 0x0C77 }, { 0x0C84, 0x0C84 }, { 0x0C8D, 0x0C8D },
    { 0x0C91, 0x0C91 }, { 0x0CA9, 0x0CA9 }, { 0x0CB4, 0x0CB4 },
    { 0x0CBA, 0x0CBB }, { 0x0CC5, 0x0CC5 }, { 0x0CC9, 0x0CC9 },
    { 0x0CCE, 0x0CD4 }, { 0x0CD7, 0x0CDD }, { 0x0CDF, 0x0CDF },
    { 0x0CE4, 0x0CE5 }, { 0x0CF0, 0x0CF0 }, { 0x0CF3, 0x0CFF },
    { 0x0D04, 0x0D04 }, { 0x0D0D, 0x0D0D }, { 0x0D11, 0x0D11 },
    { 0x0D45, 0x0D45 }, { 0x0D49, 0x0D49 }, { 0x0D50, 0x0D53 },
    { 0x0D64, 0x0D65 }, { 0x0D80, 0x0D81 }, { 0x0D84, 0x0D84 },
    { 0x0D97, 0x0D99 }, { 0x0DB2, 0x0DB2 }, { 0x0DBC, 0x0DBC },
    { 0x0DBE, 0x0DBF }, { 0x0DC7, 0x0DC9 }, { 0x0DCB, 0x0DCE },
    { 0x0DD5, 0x0DD5 }, { 0x0DD7, 0x0DD7 }, { 0x0DE0, 0x0DE5 },
    { 0x0DF0, 0x0DF1 }, { 0x0DF5, 0x0E00 }, { 0x0E3B, 0x0E3E },
    { 0x0E5C, 0x0E80 }, { 0x0E83, 0x0E83 }, { 0x0E85, 0x0E86 },
    { 0x0E89, 0x0E89 }, { 0x0E8B, 0x0E8C }, { 0x0E8E, 0x0E93 },
    { 0x0E98, 0x0E98 }, { 0x0EA0, 0x0EA0 }, { 0x0EA4, 0x0EA4 },
    { 0x0EA6, 0x0EA6 }, { 0x0EA8, 0x0EA9 }, { 0x0EAC, 0x0EAC },
    { 0x0EBA, 0x0EBA }, { 0x0EBE, 0x0EBF }, { 0x0EC5, 0x0EC5 },
    { 0x0EC7, 0x0EC7 }, { 0x0ECE, 0x0ECF }, { 0x0EDA, 0x0EDB },
    { 0x0EE0, 0x0EFF }, { 0x0F48, 0x0F48 }, { 0x0F6D, 0x0F70 },
    { 0x0F98, 0x0F98 }, { 0x0FBD, 0x0FBD }, { 0x0FCD, 0x0FCD },
    { 0x0FDB, 0x0FFF }, { 0x10C6, 0x10C6 }, { 0x10C8, 0x10CC },
    { 0x10CE, 0x10CF }, { 0x1249, 0x1249 }, { 0x124E, 0x124F },
    { 0x1257, 0x1257 }, { 0x1259, 0x1259 }, { 0x125E, 0x125F },
    { 0x1289, 0x1289 }, { 0x128E, 0x128F }, { 0x12B1, 0x12B1 },
    { 0x12B6, 0x12B7 }, { 0x12BF, 0x12BF }, { 0x12C1, 0x12C1 },
    { 0x12C6, 0x12C7 }, { 0x12D7, 0x12D7 }, { 0x1311, 0x1311 },
    { 0x1316, 0x1317 }, { 0x135B, 0x135C }, { 0x137D, 0x137F },
    { 0x139A, 0x139F }, { 0x13F6, 0x13F7 }, { 0x13FE, 0x13FF },
    { 0x169D, 0x169F }, { 0x16F9, 0x16FF }, { 0x170D, 0x170D },
    { 0x1715, 0x171F }, { 0x1737, 0x173F }, { 0x1754, 0x175F },
    { 0x176D, 0x176D }, { 0x1771, 0x1771 }, { 0x1774, 0x177F },
    { 0x17DE, 0x17DF }, { 0x17EA, 0x17EF }, { 0x17FA, 0x17FF },
    { 0x180F, 0x180F }, { 0x181A, 0x181F }, { 0x1878, 0x187F },
    { 0x18AB, 0x18AF }, { 0x18F6, 0x18FF }, { 0x191F, 0x191F },
    { 0x192C, 0x192F }, { 0x193C, 0x193F }, { 0x1941, 0x1943 },
    { 0x196E, 0x196F }, { 0x1975, 0x197F }, { 0x19AC, 0x19AF },
    { 0x19CA, 0x19CF }, { 0x19DB, 0x19DD }, { 0x1A1C, 0x1A1D },
    { 0x1A5F, 0x1A5F }, { 0x1A7D, 0x1A7E }, { 0x1A8A, 0x1A8F },
    { 0x1A9A, 0x1A9F }, { 0x1AAE, 0x1AAF }, { 0x1ABF, 0x1AFF },
    { 0x1B4C, 0x1B4F }, { 0x1B7D, 0x1B7F }, { 0x1BF4, 0x1BFB },
    { 0x1C38, 0x1C3A }, { 0x1C4A, 0x1C4C }, { 0x1C89, 0x1CBF },
    { 0x1CC8, 0x1CCF }, { 0x1CFA, 0x1CFF }, { 0x1DFA, 0x1DFA },
    { 0x1F16, 0x1F17 }, { 0x1F1E, 0x1F1F }, { 0x1F46, 0x1F47 },
    { 0x1F4E, 0x1F4F }, { 0x1F58, 0x1F58 }, { 0x1F5A, 0x1F5A },
    { 0x1F5C, 0x1F5C }, { 0x1F5E, 0x1F5E }, { 0x1F7E, 0x1F7F },
    { 0x1FB5, 0x1FB5 }, { 0x1FC5, 0x1FC5 }, { 0x1FD4, 0x1FD5 },
    { 0x1FDC, 0x1FDC }, { 0x1FF0, 0x1FF1 }, { 0x1FF5, 0x1FF5 },
    { 0x1FFF, 0x1FFF }, { 0x2028, 0x2029 }, { 0x2065, 0x2065 },
    { 0x2072, 0x2073 }, { 0x208F, 0x208F }, { 0x209D, 0x209F },
    { 0x20C0, 0x20CF }, { 0x20F1, 0x20FF }, { 0x218C, 0x218F },
    { 0x2427, 0x243F }, { 0x244B, 0x245F }, { 0x2B74, 0x2B75 },
    { 0x2B96, 0x2B97 }, { 0x2BBA, 0x2BBC }, { 0x2BC9, 0x2BC9 },
    { 0x2BD3, 0x2BEB }, { 0x2BF0, 0x2BFF }, { 0x2C2F, 0x2C2F },
    { 0x2C5F, 0x2C5F }, { 0x2CF4, 0x2CF8 }, { 0x2D26, 0x2D26 },
    { 0x2D28, 0x2D2C }, { 0x2D2E, 0x2D2F }, { 0x2D68, 0x2D6E },
    { 0x2D71, 0x2D7E }, { 0x2D97, 0x2D9F }, { 0x2DA7, 0x2DA7 },
    { 0x2DAF, 0x2DAF }, { 0x2DB7, 0x2DB7 }, { 0x2DBF, 0x2DBF },
    { 0x2DC7, 0x2DC7 }, { 0x2DCF, 0x2DCF }, { 0x2DD7, 0x2DD7 },
    { 0x2DDF, 0x2DDF }, { 0x2E4A, 0x2E7F }, { 0x2E9A, 0x2E9A },
    { 0x2EF4, 0x2EFF }, { 0x2FD6, 0x2FEF }, { 0x2FFC, 0x2FFF },
    { 0x3040, 0x3040 }, { 0x3097, 0x3098 }, { 0x3100, 0x3104 },
    { 0x312F, 0x3130 }, { 0x318F, 0x318F }, { 0x31BB, 0x31BF },
    { 0x31E4, 0x31EF }, { 0x321F, 0x321F }, { 0x32FF, 0x32FF },
    { 0x4DB6, 0x4DBF }, { 0x9FD6, 0x9FFF }, { 0xA48D, 0xA48F },
    { 0xA4C7, 0xA4CF }, { 0xA62C, 0xA63F }, { 0xA6F8, 0xA6FF },
    { 0xA7AF, 0xA7AF }, { 0xA7B8, 0xA7F6 }, { 0xA82C, 0xA82F },
    { 0xA83A, 0xA83F }, { 0xA878, 0xA87F }, { 0xA8C6, 0xA8CD },
    { 0xA8DA, 0xA8DF }, { 0xA8FE, 0xA8FF }, { 0xA954, 0xA95E },
    { 0xA97D, 0xA97F }, { 0xA9CE, 0xA9CE }, { 0xA9DA, 0xA9DD },
    { 0xA9FF, 0xA9FF }, { 0xAA37, 0xAA3F }, { 0xAA4E, 0xAA4F },
    { 0xAA5A, 0xAA5B }, { 0xAAC3, 0xAADA }, { 0xAAF7, 0xAB00 },
    { 0xAB07, 0xAB08 }, { 0xAB0F, 0xAB10 }, { 0xAB17, 0xAB1F },
    { 0xAB27, 0xAB27 }, { 0xAB2F, 0xAB2F }, { 0xAB66, 0xAB6F },
    { 0xABEE, 0xABEF }, { 0xABFA, 0xABFF }, { 0xD7A4, 0xD7AF },
    { 0xD7C7, 0xD7CA }, { 0xD7FC, 0xDFFF }, { 0xFA6E, 0xFA6F },
    { 0xFADA, 0xFAFF }, { 0xFB07, 0xFB12 }, { 0xFB18, 0xFB1C },
    { 0xFB37, 0xFB37 }, { 0xFB3D, 0xFB3D }, { 0xFB3F, 0xFB3F },
    { 0xFB42, 0xFB42 }, { 0xFB45, 0xFB45 }, { 0xFBC2, 0xFBD2 },
    { 0xFD40, 0xFD4F }, { 0xFD90, 0xFD91 }, { 0xFDC8, 0xFDEF },
    { 0xFDFE, 0xFDFF }, { 0xFE1A, 0xFE1F }, { 0xFE53, 0xFE53 },
    { 0xFE67, 0xFE67 }, { 0xFE6C, 0xFE6F }, { 0xFE75, 0xFE75 },
    { 0xFEFD, 0xFEFE }, { 0xFF00, 0xFF00 }, { 0xFFBF, 0xFFC1 },
    { 0xFFC8, 0xFFC9 }, { 0xFFD0, 0xFFD1 }, { 0xFFD8, 0xFFD9 },
    { 0xFFDD, 0xFFDF }, { 0xFFE7, 0xFFE7 }, { 0xFFEF, 0xFFF8 },
    { 0xFFFE, 0xFFFF }, { 0x1000C, 0x1000C }, { 0x10027, 0x10027 },
    { 0x1003B, 0x1003B }, { 0x1003E, 0x1003E }, { 0x1004E, 0x1004F },
    { 0x1005E, 0x1007F }, { 0x100FB, 0x100FF }, { 0x10103, 0x10106 },
    { 0x10134, 0x10136 }, { 0x1018F, 0x1018F }, { 0x1019C, 0x1019F },
    { 0x101A1, 0x101CF }, { 0x101FE, 0x1027F }, { 0x1029D, 0x1029F },
    { 0x102D1, 0x102DF }, { 0x102FC, 0x102FF }, { 0x10324, 0x1032C },
    { 0x1034B, 0x1034F }, { 0x1037B, 0x1037F }, { 0x1039E, 0x1039E },
    { 0x103C4, 0x103C7 }, { 0x103D6, 0x103FF }, { 0x1049E, 0x1049F },
    { 0x104AA, 0x104AF }, { 0x104D4, 0x104D7 }, { 0x104FC, 0x104FF },
    { 0x10528, 0x1052F }, { 0x10564, 0x1056E }, { 0x10570, 0x105FF },
    { 0x10737, 0x1073F }, { 0x10756, 0x1075F }, { 0x10768, 0x107FF },
    { 0x10806, 0x10807 }, { 0x10809, 0x10809 }, { 0x10836, 0x10836 },
    { 0x10839, 0x1083B }, { 0x1083D, 0x1083E }, { 0x10856, 0x10856 },
    { 0x1089F, 0x108A6 }, { 0x108B0, 0x108DF }, { 0x108F3, 0x108F3 },
    { 0x108F6, 0x108FA }, { 0x1091C, 0x1091E }, { 0x1093A, 0x1093E },
    { 0x10940, 0x1097F }, { 0x109B8, 0x109BB }, { 0x109D0, 0x109D1 },
    { 0x10A04, 0x10A04 }, { 0x10A07, 0x10A0B }, { 0x10A14, 0x10A14 },
    { 0x10A18, 0x10A18 }, { 0x10A34, 0x10A37 }, { 0x10A3B, 0x10A3E },
    { 0x10A48, 0x10A4F }, { 0x10A59, 0x10A5F }, { 0x10AA0, 0x10ABF },
    { 0x10AE7, 0x10AEA }, { 0x10AF7, 0x10AFF }, { 0x10B36, 0x10B38 },
    { 0x10B56, 0x10B57 }, { 0x10B73, 0x10B77 }, { 0x10B92, 0x10B98 },
    { 0x10B9D, 0x10BA8 }, { 0x10BB0, 0x10BFF }, { 0x10C49, 0x10C7F },
    { 0x10CB3, 0x10CBF }, { 0x10CF3, 0x10CF9 }, { 0x10D00, 0x10E5F },
    { 0x10E7F, 0x10FFF }, { 0x1104E, 0x11051 }, { 0x11070, 0x1107E },
    { 0x110C2, 0x110CF }, { 0x110E9, 0x110EF }, { 0x110FA, 0x110FF },
    { 0x11135, 0x11135 }, { 0x11144, 0x1114F }, { 0x11177, 0x1117F },
    { 0x111CE, 0x111CF }, { 0x111E0, 0x111E0 }, { 0x111F5, 0x111FF },
    { 0x11212, 0x11212 }, { 0x1123F, 0x1127F }, { 0x11287, 0x11287 },
    { 0x11289, 0x11289 }, { 0x1128E, 0x1128E }, { 0x1129E, 0x1129E },
    { 0x112AA, 0x112AF }, { 0x112EB, 0x112EF }, { 0x112FA, 0x112FF },
    { 0x11304, 0x11304 }, { 0x1130D, 0x1130E }, { 0x11311, 0x11312 },
    { 0x11329, 0x11329 }, { 0x11331, 0x11331 }, { 0x11334, 0x11334 },
    { 0x1133A, 0x1133B }, { 0x11345, 0x11346 }, { 0x11349, 0x1134A },
    { 0x1134E, 0x1134F }, { 0x11351, 0x11356 }, { 0x11358, 0x1135C },
    { 0x11364, 0x11365 }, { 0x1136D, 0x1136F }, { 0x11375, 0x113FF },
    { 0x1145A, 0x1145A }, { 0x1145C, 0x1145C }, { 0x1145E, 0x1147F },
    { 0x114C8, 0x114CF }, { 0x114DA, 0x1157F }, { 0x115B6, 0x115B7 },
    { 0x115DE, 0x115FF }, { 0x11645, 0x1164F }, { 0x1165A, 0x1165F },
    { 0x1166D, 0x1167F }, { 0x116B8, 0x116BF }, { 0x116CA, 0x116FF },
    { 0x1171A, 0x1171C }, { 0x1172C, 0x1172F }, { 0x11740, 0x1189F },
    { 0x118F3, 0x118FE }, { 0x11900, 0x119FF }, { 0x11A48, 0x11A4F },
    { 0x11A84, 0x11A85 }, { 0x11A9D, 0x11A9D }, { 0x11AA3, 0x11ABF },
    { 0x11AF9, 0x11BFF }, { 0x11C09, 0x11C09 }, { 0x11C37, 0x11C37 },
    { 0x11C46, 0x11C4F }, { 0x11C6D, 0x11C6F }, { 0x11C90, 0x11C91 },
    { 0x11CA8, 0x11CA8 }, { 0x11CB7, 0x11CFF }, { 0x11D07, 0x11D07 },
    { 0x11D0A, 0x11D0A }, { 0x11D37, 0x11D39 }, { 0x11D3B, 0x11D3B },
    { 0x11D3E, 0x11D3E }, { 0x11D48, 0x11D4F }, { 0x11D5A, 0x11FFF },
    { 0x1239A, 0x123FF }, { 0x1246F, 0x1246F }, { 0x12475, 0x1247F },
    { 0x12544, 0x12FFF }, { 0x1342F, 0x143FF }, { 0x14647, 0x167FF },
    { 0x16A39, 0x16A3F }, { 0x16A5F, 0x16A5F }, { 0x16A6A, 0x16A6D },
    { 0x16A70, 0x16ACF }, { 0x16AEE, 0x16AEF }, { 0x16AF6, 0x16AFF },
    { 0x16B46, 0x16B4F }, { 0x16B5A, 0x16B5A }, { 0x16B62, 0x16B62 },
    { 0x16B78, 0x16B7C }, { 0x16B90, 0x16EFF }, { 0x16F45, 0x16F4F },
    { 0x16F7F, 0x16F8E }, { 0x16FA0, 0x16FDF }, { 0x16FE2, 0x187FF },
    { 0x18AF3, 0x1AFFF }, { 0x1B11F, 0x1B16F }, { 0x1B2FC, 0x1BBFF },
    { 0x1BC6B, 0x1BC6F }, { 0x1BC7D, 0x1BC7F }, { 0x1BC89, 0x1BC8F },
    { 0x1BC9A, 0x1BC9B }, { 0x1BCA4, 0x1CFFF }, { 0x1D0F6, 0x1D0FF },
    { 0x1D127, 0x1D128 }, { 0x1D1E9, 0x1D1FF }, { 0x1D246, 0x1D2FF },
    { 0x1D357, 0x1D35F }, { 0x1D372, 0x1D3FF }, { 0x1D455, 0x1D455 },
    { 0x1D49D, 0x1D49D }, { 0x1D4A0, 0x1D4A1 }, { 0x1D4A3, 0x1D4A4 },
    { 0x1D4A7, 0x1D4A8 }, { 0x1D4AD, 0x1D4AD }, { 0x1D4BA, 0x1D4BA },
    { 0x1D4BC, 0x1D4BC }, { 0x1D4C4, 0x1D4C4 }, { 0x1D506, 0x1D506 },
    { 0x1D50B, 0x1D50C }, { 0x1D515, 0x1D515 }, { 0x1D51D, 0x1D51D },
    { 0x1D53A, 0x1D53A }, { 0x1D53F, 0x1D53F }, { 0x1D545, 0x1D545 },
    { 0x1D547, 0x1D549 }, { 0x1D551, 0x1D551 }, { 0x1D6A6, 0x1D6A7 },
    { 0x1D7CC, 0x1D7CD }, { 0x1DA8C, 0x1DA9A }, { 0x1DAA0, 0x1DAA0 },
    { 0x1DAB0, 0x1DFFF }, { 0x1E007, 0x1E007 }, { 0x1E019, 0x1E01A },
    { 0x1E022, 0x1E022 }, { 0x1E025, 0x1E025 }, { 0x1E02B, 0x1E7FF },
    { 0x1E8C5, 0x1E8C6 }, { 0x1E8D7, 0x1E8FF }, { 0x1E94B, 0x1E94F },
    { 0x1E95A, 0x1E95D }, { 0x1E960, 0x1EDFF }, { 0x1EE04, 0x1EE04 },
    { 0x1EE20, 0x1EE20 }, { 0x1EE23, 0x1EE23 }, { 0x1EE25, 0x1EE26 },
    { 0x1EE28, 0x1EE28 }, { 0x1EE33, 0x1EE33 }, { 0x1EE38, 0x1EE38 },
    { 0x1EE3A, 0x1EE3A }, { 0x1EE3C, 0x1EE41 }, { 0x1EE43, 0x1EE46 },
    { 0x1EE48, 0x1EE48 }, { 0x1EE4A, 0x1EE4A }, { 0x1EE4C, 0x1EE4C },
    { 0x1EE50, 0x1EE50 }, { 0x1EE53, 0x1EE53 }, { 0x1EE55, 0x1EE56 },
    { 0x1EE58, 0x1EE58 }, { 0x1EE5A, 0x1EE5A }, { 0x1EE5C, 0x1EE5C },
    { 0x1EE5E, 0x1EE5E }, { 0x1EE60, 0x1EE60 }, { 0x1EE63, 0x1EE63 },
    { 0x1EE65, 0x1EE66 }, { 0x1EE6B, 0x1EE6B }, { 0x1EE73, 0x1EE73 },
    { 0x1EE78, 0x1EE78 }, { 0x1EE7D, 0x1EE7D }, { 0x1EE7F, 0x1EE7F },
    { 0x1EE8A, 0x1EE8A }, { 0x1EE9C, 0x1EEA0 }, { 0x1EEA4, 0x1EEA4 },
    { 0x1EEAA, 0x1EEAA }, { 0x1EEBC, 0x1EEEF }, { 0x1EEF2, 0x1EFFF },
    { 0x1F02C, 0x1F02F }, { 0x1F094, 0x1F09F }, { 0x1F0AF, 0x1F0B0 },
    { 0x1F0C0, 0x1F0C0 }, { 0x1F0D0, 0x1F0D0 }, { 0x1F0F6, 0x1F0FF },
    { 0x1F10D, 0x1F10F }, { 0x1F12F, 0x1F12F }, { 0x1F16C, 0x1F16F },
    { 0x1F1AD, 0x1F1E5 }, { 0x1F203, 0x1F20F }, { 0x1F23C, 0x1F23F },
    { 0x1F249, 0x1F24F }, { 0x1F252, 0x1F25F }, { 0x1F266, 0x1F2FF },
    { 0x1F6D5, 0x1F6DF }, { 0x1F6ED, 0x1F6EF }, { 0x1F6F9, 0x1F6FF },
    { 0x1F774, 0x1F77F }, { 0x1F7D5, 0x1F7FF }, { 0x1F80C, 0x1F80F },
    { 0x1F848, 0x1F84F }, { 0x1F85A, 0x1F85F }, { 0x1F888, 0x1F88F },
    { 0x1F8AE, 0x1F8FF }, { 0x1F90C, 0x1F90F }, { 0x1F93F, 0x1F93F },
    { 0x1F94D, 0x1F94F }, { 0x1F96C, 0x1F97F }, { 0x1F998, 0x1F9BF },
    { 0x1F9C1, 0x1F9CF }, { 0x1F9E7, 0x1FFFF }, { 0x2A6D7, 0x2F7FF },
    { 0x2FA1E, 0xE0000 }, { 0xE0002, 0xE001F }, { 0xE0080, 0xE00FF },
    { 0xE01F0, 0x10FFFD }
  };

  int result;

#define Lookup(cmp, table) \
      bisearch(cmp, table, \
               (int) (sizeof(table) / sizeof(struct interval) - 1))

  /* test for 8-bit control characters */
  if (cmp == 0) {
    result = 0;
  } else if (cmp < 32 || (cmp >= 0x7f && cmp < 0xa0)) {
    result = -1;
  } else if (cmp == 0xad) {
    result = use_latin1;
  } else if (Lookup(cmp, combining)) {
    /* binary search in table of non-spacing characters */
    result = 0;
  } else {
    /* if we arrive here, cmp is not a combining or C0/C1 control character */
    result = 1;

    if (cmp >= 0x1100 &&
        (cmp <= 0x115f ||                    /* Hangul Jamo init. consonants */
         cmp == 0x2329 ||
         cmp == 0x232a ||
         (cmp >= 0x2e80 && cmp <= 0x4dbf &&
          cmp != 0x303f) ||                  /* CJK ... Yi */
         (cmp >= 0x4e00 && cmp <= 0xa4cf) || /* CJK Unified Ideographs, Yi */
         (cmp >= 0xa960 && cmp <= 0xa97f) || /* Hangul Jamo Extended-A */
         (cmp >= 0xac00 && cmp <= 0xd7a3) || /* Hangul Syllables */
         (cmp >= 0xf900 && cmp <= 0xfaff) || /* CJK Compatibility Ideographs */
         (cmp >= 0xfe10 && cmp <= 0xfe19) || /* Vertical forms */
         (cmp >= 0xfe30 && cmp <= 0xfe6f) || /* CJK Compatibility Forms */
         (cmp >= 0xff00 && cmp <= 0xff60) || /* Fullwidth Forms */
         (cmp >= 0xffe0 && cmp <= 0xffe6) ||
         (cmp >= 0x20000 && cmp <= 0x2fffd) ||
         (cmp >= 0x30000 && cmp <= 0x3fffd))) {
      result = 2;
    }
    if (cmp >= unknowns[0].first && Lookup(cmp, unknowns)) {
      result = -1;
    }
  }
  return result;
}


int mk_wcswidth(const wchar_t *pwcs, size_t n)
{
  int width = 0;

  for (;*pwcs && n-- > 0; pwcs++) {
    int w;

    if ((w = mk_wcwidth(*pwcs)) < 0)
      return -1;
    else
      width += w;
  }

  return width;
}


/*
 * The following functions are the same as mk_wcwidth() and
 * mk_wcwidth_cjk(), except that spacing characters in the East Asian
 * Ambiguous (A) category as defined in Unicode Technical Report #11
 * have a column width of 2. This variant might be useful for users of
 * CJK legacy encodings who want to migrate to UCS without changing
 * the traditional terminal character-width behaviour. It is not
 * otherwise recommended for general use.
 */
int mk_wcwidth_cjk(wchar_t ucs)
{
  /* sorted list of non-overlapping intervals of East Asian Ambiguous
   * characters, generated by
   *
   * uniset +WIDTH-A -cat=Me -cat=Mn -cat=Cf \
   *    +E000..F8FF \
   *    +F0000..FFFFD \
   *    +100000..10FFFD  c
   *
   * "WIDTH-A" is a file extracted from EastAsianWidth.txt by selecting
   * only those with width "A", and omitting:
   *
   *    0xAD
   *    all lines with "COMBINING"
   *
   * (uniset does not recognize the range expressions in WIDTH-A).
   */
  static const struct interval ambiguous[] = {
    { 0x00A1, 0x00A1 }, { 0x00A4, 0x00A4 }, { 0x00A7, 0x00A8 },
    { 0x00AA, 0x00AA }, { 0x00AE, 0x00AE }, { 0x00B0, 0x00B2 },
    { 0x00B4, 0x00B4 }, { 0x00B6, 0x00B6 }, { 0x00B8, 0x00BA },
    { 0x00BC, 0x00BC }, { 0x00BF, 0x00BF }, { 0x00C6, 0x00C6 },
    { 0x00D0, 0x00D0 }, { 0x00D7, 0x00D8 }, { 0x00DE, 0x00DE },
    { 0x00E6, 0x00E6 }, { 0x00E8, 0x00E8 }, { 0x00EC, 0x00EC },
    { 0x00F0, 0x00F0 }, { 0x00F2, 0x00F2 }, { 0x00F7, 0x00F8 },
    { 0x00FC, 0x00FC }, { 0x00FE, 0x00FE }, { 0x0101, 0x0101 },
    { 0x0111, 0x0111 }, { 0x0113, 0x0113 }, { 0x011B, 0x011B },
    { 0x0126, 0x0126 }, { 0x012B, 0x012B }, { 0x0131, 0x0131 },
    { 0x0138, 0x0138 }, { 0x013F, 0x013F }, { 0x0144, 0x0144 },
    { 0x0148, 0x0148 }, { 0x014D, 0x014D }, { 0x0152, 0x0152 },
    { 0x0166, 0x0166 }, { 0x016B, 0x016B }, { 0x01CE, 0x01CE },
    { 0x01D0, 0x01D0 }, { 0x01D2, 0x01D2 }, { 0x01D4, 0x01D4 },
    { 0x01D6, 0x01D6 }, { 0x01D8, 0x01D8 }, { 0x01DA, 0x01DA },
    { 0x01DC, 0x01DC }, { 0x0251, 0x0251 }, { 0x0261, 0x0261 },
    { 0x02C4, 0x02C4 }, { 0x02C7, 0x02C7 }, { 0x02C9, 0x02C9 },
    { 0x02CD, 0x02CD }, { 0x02D0, 0x02D0 }, { 0x02D8, 0x02D8 },
    { 0x02DD, 0x02DD }, { 0x02DF, 0x02DF }, { 0x0391, 0x0391 },
    { 0x03A3, 0x03A3 }, { 0x03B1, 0x03B1 }, { 0x03C3, 0x03C3 },
    { 0x0401, 0x0401 }, { 0x0410, 0x0410 }, { 0x0451, 0x0451 },
    { 0x2010, 0x2010 }, { 0x2013, 0x2013 }, { 0x2016, 0x2016 },
    { 0x2018, 0x2019 }, { 0x201C, 0x201D }, { 0x2020, 0x2020 },
    { 0x2024, 0x2024 }, { 0x2030, 0x2030 }, { 0x2032, 0x2032 },
    { 0x2035, 0x2035 }, { 0x203B, 0x203B }, { 0x203E, 0x203E },
    { 0x2074, 0x2074 }, { 0x207F, 0x207F }, { 0x2081, 0x2081 },
    { 0x20AC, 0x20AC }, { 0x2103, 0x2103 }, { 0x2105, 0x2105 },
    { 0x2109, 0x2109 }, { 0x2113, 0x2113 }, { 0x2116, 0x2116 },
    { 0x2121, 0x2121 }, { 0x2126, 0x2126 }, { 0x212B, 0x212B },
    { 0x2153, 0x2153 }, { 0x215B, 0x215B }, { 0x2160, 0x2160 },
    { 0x2170, 0x2170 }, { 0x2189, 0x2189 }, { 0x2190, 0x2190 },
    { 0x2195, 0x2195 }, { 0x21B8, 0x21B8 }, { 0x21D2, 0x21D2 },
    { 0x21D4, 0x21D4 }, { 0x21E7, 0x21E7 }, { 0x2200, 0x2200 },
    { 0x2202, 0x2202 }, { 0x2207, 0x2207 }, { 0x220B, 0x220B },
    { 0x220F, 0x220F }, { 0x2211, 0x2211 }, { 0x2215, 0x2215 },
    { 0x221A, 0x221A }, { 0x221D, 0x221D }, { 0x2223, 0x2223 },
    { 0x2225, 0x2225 }, { 0x2227, 0x2227 }, { 0x222E, 0x222E },
    { 0x2234, 0x2234 }, { 0x223C, 0x223C }, { 0x2248, 0x2248 },
    { 0x224C, 0x224C }, { 0x2252, 0x2252 }, { 0x2260, 0x2260 },
    { 0x2264, 0x2264 }, { 0x226A, 0x226A }, { 0x226E, 0x226E },
    { 0x2282, 0x2282 }, { 0x2286, 0x2286 }, { 0x2295, 0x2295 },
    { 0x2299, 0x2299 }, { 0x22A5, 0x22A5 }, { 0x22BF, 0x22BF },
    { 0x2312, 0x2312 }, { 0x2460, 0x2460 }, { 0x249C, 0x249C },
    { 0x24EB, 0x24EB }, { 0x2500, 0x2500 }, { 0x2550, 0x2550 },
    { 0x2580, 0x2580 }, { 0x2592, 0x2592 }, { 0x25A0, 0x25A0 },
    { 0x25A3, 0x25A3 }, { 0x25B2, 0x25B2 }, { 0x25B6, 0x25B7 },
    { 0x25BC, 0x25BC }, { 0x25C0, 0x25C1 }, { 0x25C6, 0x25C6 },
    { 0x25CB, 0x25CB }, { 0x25CE, 0x25CE }, { 0x25E2, 0x25E2 },
    { 0x25EF, 0x25EF }, { 0x2605, 0x2605 }, { 0x2609, 0x2609 },
    { 0x260E, 0x260E }, { 0x261C, 0x261C }, { 0x261E, 0x261E },
    { 0x2640, 0x2640 }, { 0x2642, 0x2642 }, { 0x2660, 0x2660 },
    { 0x2663, 0x2663 }, { 0x2667, 0x2667 }, { 0x266C, 0x266C },
    { 0x266F, 0x266F }, { 0x269E, 0x269E }, { 0x26BF, 0x26BF },
    { 0x26C6, 0x26C6 }, { 0x26CF, 0x26CF }, { 0x26D5, 0x26D5 },
    { 0x26E3, 0x26E3 }, { 0x26E8, 0x26E8 }, { 0x26EB, 0x26EB },
    { 0x26F4, 0x26F4 }, { 0x26F6, 0x26F6 }, { 0x26FB, 0x26FB },
    { 0x26FE, 0x26FE }, { 0x273D, 0x273D }, { 0x2776, 0x2776 },
    { 0x2B56, 0x2B56 }, { 0x3248, 0x3248 }, { 0xE000, 0xF8FF },
    { 0xFFFD, 0xFFFD }, { 0x1F100, 0x1F100 }, { 0x1F110, 0x1F110 },
    { 0x1F130, 0x1F130 }, { 0x1F170, 0x1F170 }, { 0x1F18F, 0x1F18F },
    { 0x1F19B, 0x1F19B }, { 0xF0000, 0xFFFFD }, { 0x100000, 0x10FFFD }
  };

  /* binary search in table of non-spacing characters */
  if (Lookup((unsigned long) ucs, ambiguous))
    return 2;

  return mk_wcwidth(ucs);
}


int mk_wcswidth_cjk(const wchar_t *pwcs, size_t n)
{
  int width = 0;

  for (;*pwcs && n-- > 0; pwcs++) {
    int w;

    if ((w = mk_wcwidth_cjk(*pwcs)) < 0)
      return -1;
    else
      width += w;
  }

  return width;
}
