#ifndef BITSET_H
#define BITSET_H

//  bitset.h
//
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include <stdint.h>

uint64_t setbits64(uint64_t bitset, unsigned int width, unsigned int n1,
                   unsigned int n2, unsigned int step);

#endif
