#ifndef _CRT_ARM64_ASMDEFS_H
#define _CRT_ARM64_ASMDEFS_H

#define ENTRY_ALIGN(name, alignment) \
    .text; \
    .balign (1 << (alignment)); \
    .global name; \
    name:

#define ENTRY(name) ENTRY_ALIGN(name, 6)
#define END(name)
#define L(label) .L##label

#endif
