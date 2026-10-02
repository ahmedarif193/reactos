/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Big-endian Open Firmware stage that starts the little-endian loader
 *
 * Built for powerpc-unknown-elf (big-endian) and linked as a flat binary at
 * STAGE0_BASE, where QEMU and Open Firmware place a raw -kernel image on
 * PReP. It embeds the little-endian loader PE image, copies it to its
 * preferred base and enters its entry descriptor with MSR[LE] set.
 */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define PPC_STAGE0_MAGIC   0x30475453
#define PPC_STAGE0_VERSION 1
#define IMAGE_FILE_MACHINE_POWERPC 0x1F0

/* Must match PPC_STAGE0_INFO in include/arch/powerpc/ofw.h (little-endian). */
struct stage0_info
{
    u32 Magic;
    u32 Version;
    u32 OfEntry;
    u32 BeGate;
    u32 Stage0Base;
    u32 Stage0Size;
    u32 ImageBase;
    u32 ImageSize;
    u32 InitrdBase;
    u32 InitrdSize;
    u32 MachineId;
    u32 Reserved[5];
};

extern const u8 LoaderImage[], LoaderImageEnd[];
extern u8 __stage0_start[], __stage0_end[];
extern u8 LeStackTop[];
extern void BeGate(void);
extern void EnterLe(u32 Code, u32 Toc, u32 Arg, u32 Stack);

static int (*ofw)(void *);
static u32 stdout_ih;
static struct stage0_info Info;

static int of_call(const char *svc, int nargs, int nret, u32 *a)
{
    u32 args[16];
    int i;

    args[0] = (u32)svc;
    args[1] = nargs;
    args[2] = nret;
    for (i = 0; i < nargs; i++)
        args[3 + i] = a[i];
    ofw(args);
    for (i = 0; i < nret; i++)
        a[nargs + i] = args[3 + nargs + i];
    return 0;
}

static void puts_of(const char *s)
{
    u32 n = 0, a[4];

    while (s[n])
        n++;
    a[0] = stdout_ih;
    a[1] = (u32)s;
    a[2] = n;
    of_call("write", 3, 1, a);
}

static void puthex(u32 v)
{
    char b[11];
    int i;

    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; i++)
        b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[10] = 0;
    puts_of(b);
}

static void halt(const char *why)
{
    puts_of("stage0: ");
    puts_of(why);
    puts_of("\r\n");
    for (;;)
        ;
}

static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 le16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 bswap(u32 v) { return __builtin_bswap32(v); }

static void flush(u8 *p, u32 n)
{
    u32 i;

    for (i = 0; i < n; i += 32)
        __asm__ volatile("dcbst 0,%0" :: "r"(p + i) : "memory");
    __asm__ volatile("sync" ::: "memory");
    for (i = 0; i < n; i += 32)
        __asm__ volatile("icbi 0,%0" :: "r"(p + i) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
}

/* Claim a physical range 1:1 so the firmware never hands it out; a range
 * the firmware already reserved for us is fine. */
static void claim(u32 base, u32 size)
{
    u32 a[4];

    base &= ~0xFFFu;
    size = (size + 0xFFFu) & ~0xFFFu;
    a[0] = base;
    a[1] = size;
    a[2] = 0;
    a[3] = 0;
    of_call("claim", 3, 1, a);
    if (a[3] != base)
    {
        puts_of("stage0: note: claim of ");
        puthex(base);
        puts_of(" returned ");
        puthex(a[3]);
        puts_of("\r\n");
    }
}

void Stage0Main(u32 r3, u32 r4, void *r5)
{
    const u8 *pe = LoaderImage;
    const u8 *opt, *sh;
    u32 a[5], chosen, nt, base, entry, imgsize, hdrsize, code, toc, stack, i, s;
    u16 nsec, optsz;
    u8 *img;

    ofw = r5;
    a[0] = (u32)"/chosen";
    of_call("finddevice", 1, 1, a);
    chosen = a[1];
    a[0] = chosen;
    a[1] = (u32)"stdout";
    a[2] = (u32)&stdout_ih;
    a[3] = 4;
    of_call("getprop", 4, 1, a);

    puts_of("\r\nReactOS PowerPC stage0\r\n");

    if ((u32)(LoaderImageEnd - LoaderImage) < 0x200)
        halt("no loader image");
    nt = le32(pe + 0x3c);
    if (le32(pe + nt) != 0x4550 || le16(pe + nt + 4) != IMAGE_FILE_MACHINE_POWERPC)
        halt("loader image is not a PowerPC PE");

    nsec = le16(pe + nt + 6);
    optsz = le16(pe + nt + 20);
    opt = pe + nt + 24;
    base = le32(opt + 28);
    entry = le32(opt + 16);
    imgsize = le32(opt + 56);
    hdrsize = le32(opt + 60);

    /* Keep the firmware off stage0 (the BE gate lives here until the
     * kernel handoff) and off the loader image. */
    claim((u32)__stage0_start, (u32)(__stage0_end - __stage0_start));
    if (base + imgsize > (u32)__stage0_start && base < (u32)__stage0_end)
        halt("loader image overlaps stage0");
    claim(base, imgsize);

    img = (u8 *)base;
    for (i = 0; i < imgsize; i++)
        img[i] = 0;
    for (i = 0; i < hdrsize; i++)
        img[i] = pe[i];
    sh = opt + optsz;
    for (s = 0; s < nsec; s++, sh += 40)
    {
        u32 va = le32(sh + 12), rawsz = le32(sh + 16), rawp = le32(sh + 20), vsz = le32(sh + 8);

        if (rawsz > vsz && vsz != 0)
            rawsz = vsz;
        for (i = 0; i < rawsz; i++)
            img[va + i] = pe[rawp + i];
    }
    flush(img, imgsize);

    code = le32(img + entry);
    toc = le32(img + entry + 4);

    Info.Magic = bswap(PPC_STAGE0_MAGIC);
    Info.Version = bswap(PPC_STAGE0_VERSION);
    Info.OfEntry = bswap((u32)ofw);
    Info.BeGate = bswap((u32)BeGate);
    Info.Stage0Base = bswap((u32)__stage0_start);
    Info.Stage0Size = bswap((u32)(__stage0_end - __stage0_start));
    Info.ImageBase = bswap(base);
    Info.ImageSize = bswap(imgsize);
    /* Open Firmware passes a -initrd image like a Linux kernel expects. */
    if (r3 != 0 && r4 != 0 && r3 != 0xFFFFFFFFu)
    {
        Info.InitrdBase = bswap(r3);
        Info.InitrdSize = bswap(r4);
        claim(r3, r4);
    }

    puts_of("stage0: loader base ");
    puthex(base);
    puts_of(" size ");
    puthex(imgsize);
    puts_of(" entry ");
    puthex(code);
    puts_of("\r\n");

    stack = ((u32)LeStackTop - 64) & ~15u;
    *(u32 *)stack = 0;
    EnterLe(code, toc, (u32)&Info, stack);
}
