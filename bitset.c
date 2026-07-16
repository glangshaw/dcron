//  bitset.c
//
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "bitset.h"

uint64_t setbits64(uint64_t bitset, unsigned int width, unsigned int n1,
                   unsigned int n2, unsigned int step)
{
  // Fill bitset, from bit 'n1' to 'n2', stepping by 'step',
  // wrapping to keep within the bounds of the bitset width.

  if (step > 0 && width > 0 && width < 65)
  {
    unsigned int c, n;

    n1 %= width;
    n2 %= width;

    n = (n1 > n2) ? width - n1 + n2 + 1 : n2 - n1 + 1;
    c = (n + step - 1) / step;

    while (c-- > 0)
    {
      bitset |= UINT64_C(1) << n1;
      n1 = (n1 + step) % width;
    }
  }
  return bitset;
}
