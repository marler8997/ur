#include "unicode.h"

u32 wtf16_decode_next(const u16* s, size_t len, size_t* i)
{
    u16 hi = s[*i];
    *i += 1;
    if (hi >= 0xD800 && hi <= 0xDBFF && *i < len) {     /* lead surrogate -> maybe a pair */
        u16 lo = s[*i];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {             /* trail surrogate -> combine */
            *i += 1;
            return 0x10000 + (((u32)(hi - 0xD800)) << 10) + (u32)(lo - 0xDC00);
        }
    }
    return hi;   /* BMP scalar, or a lone surrogate kept as-is */
}

int utf8_encode(u32 cp, u8* out)
{
    if (cp < 0x80) {
        out[0] = (u8)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (u8)(0xC0 | (cp >> 6));
        out[1] = (u8)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (u8)(0xE0 | (cp >> 12));
        out[1] = (u8)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (u8)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (u8)(0xF0 | (cp >> 18));
    out[1] = (u8)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (u8)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (u8)(0x80 | (cp & 0x3F));
    return 4;
}

u32 utf8_decode_next(const u8* s, size_t len, size_t* i)
{
    u8 c = s[*i];
    *i += 1;
    if (c < 0x80) return c;
    u32 cp; int extra;
    if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else return 0xFFFD;                              /* invalid lead byte */
    for (int k = 0; k < extra; k++) {
        if (*i >= len || (s[*i] & 0xC0) != 0x80) return 0xFFFD;   /* truncated / bad continuation */
        cp = (cp << 6) | (u32)(s[*i] & 0x3F);
        *i += 1;
    }
    return cp;
}

int wtf16_encode(u32 cp, u16* out)
{
    if (cp < 0x10000) { out[0] = (u16)cp; return 1; }   /* BMP, incl. a lone surrogate kept as-is */
    cp -= 0x10000;
    out[0] = (u16)(0xD800 + (cp >> 10));
    out[1] = (u16)(0xDC00 + (cp & 0x3FF));
    return 2;
}

size_t wtf16_from_utf8(const u8* s, size_t len, u16* out)
{
    size_t i = 0, n = 0;
    while (i < len) {
        u32 cp = utf8_decode_next(s, len, &i);
        u16 buf[2];
        int k = wtf16_encode(cp, buf);
        if (out) { out[n] = buf[0]; if (k == 2) out[n + 1] = buf[1]; }
        n += (size_t)k;
    }
    return n;
}

size_t utf8_from_wtf16(const u16* s, size_t len, u8* out)
{
    size_t i = 0, n = 0;
    while (i < len) {
        u32 cp = wtf16_decode_next(s, len, &i);
        u8 buf[4];
        int k = utf8_encode(cp, buf);
        if (out) for (int j = 0; j < k; j++) out[n + j] = buf[j];
        n += (size_t)k;
    }
    return n;
}
