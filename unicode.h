#ifndef _UNICODE_H
#define _UNICODE_H

#include "int.h"
#include "size_t.h"

u32 wtf16_decode_next(const u16* s, size_t len, size_t* i);
int utf8_encode(u32 cp, u8* out);
u32 utf8_decode_next(const u8* s, size_t len, size_t* i);
int wtf16_encode(u32 cp, u16* out);
// whole-string conversions; pass out==0 to measure. Return the output unit count (u16s / bytes).
size_t wtf16_from_utf8(const u8* s, size_t len, u16* out);
size_t utf8_from_wtf16(const u16* s, size_t len, u8* out);

#endif // _UNICODE_H
