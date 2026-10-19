// unicode.h -- what strings.c asks of the Unicode Character Database, from
// the tables runtime/gen-unicode.py writes into unicode.c (D219).

#pragma once

#include <stddef.h>
#include <stdint.h>

extern const char rt_unicode_version[];

// The full uppercase or lowercase mapping of c into out, as Unicode gives
// it without a language (SpecialCasing's unconditional mappings over
// UnicodeData's): how many code points, 1 to 3; c itself if it has none.
// Lowercase's one conditional mapping, Final_Sigma, is the caller's.
size_t rt_unicode_upper(uint32_t c, uint32_t out[3]);
size_t rt_unicode_lower(uint32_t c, uint32_t out[3]);

// Whether c has the derived property Cased, or Case_Ignorable.
int rt_unicode_cased(uint32_t c);
int rt_unicode_case_ignorable(uint32_t c);
