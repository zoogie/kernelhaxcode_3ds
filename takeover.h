#pragma once

// For usage by the calling code

#ifndef KERNVA2PA
#define KERNVA2PA(a)    ((a) + (*(vu32 *)0x1FF80060 < SYSTEM_VERSION(2, 44, 6) ? 0xD0000000 : 0xC0000000))
#endif

#ifndef IS_N3DS
#define IS_N3DS         (*(vu32 *)0x1FF80030 >= 6) // APPMEMTYPE. Hacky but doesn't use APT
#endif

#ifndef SYSTEM_VERSION
/// Packs a system version from its components.
#define SYSTEM_VERSION(major, minor, revision) \
    (((major)<<24)|((minor)<<16)|((revision)<<8))
#endif

#define MAP_ADDR        0x40000000

typedef struct BlobLayout {
    u8 padding0[0x1000]; // to account for firmlaunch params in case we're placed at FCRAM+0
    u8 code[0x20000];
    u32 l2table[0x100];
    u32 padding[0x400 - 0x100];
} BlobLayout;

static inline void lcdDebug(bool topScreen, u32 r, u32 g, u32 b)
{
    u32 base = topScreen ? MAP_ADDR + 0xA0200 : MAP_ADDR + 0xA0A00;
    *(vu32 *)(base + 4) = BIT(24) | b << 16 | g << 8 | r;
}

static inline void khc3dsPrepareL2Table(BlobLayout *layout)
{
    u32 *l2table = layout->l2table;

    u32 vaddr = (u32)layout->code;
    u32 paddr;
    switch (vaddr) {
        case 0x14000000 ... 0x1C000000 - 1: paddr = vaddr + 0x0C000000; break; // LINEAR heap
        case 0x30000000 ... 0x40000000 - 1: paddr = vaddr - 0x10000000; break; // v8.x+ LINEAR heap
        default: paddr = 0; break; // should never be reached
    }

    // Map AXIWRAM RWX RWX Shared, Outer Noncacheable, Inner Cached Write-Back Write-Allocate
    // DCache is PIPT
    for(u32 offset = 0; offset < 0x80000; offset += 0x1000) {
        l2table[offset >> 12] = (0x1FF80000 + offset) | 0x5B6;
    }

    // Map the code buffer cacheable
    // ICache is VIPT, but we should be fine as the only other mapping is XN and
    // shouldn't have filled any instruction cache line...
    for(u32 offset = 0; offset < sizeof(layout->code); offset += 0x1000) {
        l2table[(0x80000 + offset) >> 12] = (paddr + offset) | 0x5B6;
    }

    // LCD registers (for debug), RW, Shared, Device
    l2table[0xA0000 >> 12] = 0x10202000 | 0x437;
}

static inline Result khc3dsTakeover(const char *payloadFileName, size_t payloadFileOffset)
{
    u64 firmTidMask = IS_N3DS ? 0x0004013820000000ULL : 0x0004013800000000ULL;
    u32 kernelVersion = *(vu32 *)0x1FF80060;
    u32 firmTidLow;

    switch (kernelVersion & ~0xFF) {
        // Up to 11.2: use safehax
        case 0 ... SYSTEM_VERSION(2, 53, 0) - 1:
            firmTidLow = 3;
            break;
        // 11.3: use safehax v1.1
        case SYSTEM_VERSION(2, 53, 0):
            firmTidLow = 2;
            break;
        default:
            // No exploit
            return 0xDEADFFFF;
    }

    // DSB, Flush Prefetch Buffer (more or less "isb")
    __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" :: "r" (0) : "memory");
    __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" :: "r" (0) : "memory");

    return ((Result (*)(u64, const char *, size_t))(MAP_ADDR + 0x80000))(firmTidMask | firmTidLow, payloadFileName, payloadFileOffset);
}