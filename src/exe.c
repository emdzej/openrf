/* PE loader for the original game executable (see exe.h). */
#include "exe.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *image;          /* SizeOfImage bytes, sections copied to their RVAs */
static uint32_t image_size;
static char err[1024];
static ExeLoader *loaders, **loaders_tail = &loaders;

void exe_register(ExeLoader *l)
{
    l->next = NULL;
    *loaders_tail = l;
    loaders_tail = &l->next;
}

const char *exe_error(void) { return err; }
bool exe_loaded(void) { return image != NULL; }

static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    static uint32_t tab[256];
    if (!tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    uint32_t c = 0xffffffffu;
    while (n--) c = tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool map_pe(const uint8_t *d, size_t n)
{
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    uint32_t pe = rd32(d + 0x3c);
    if ((size_t)pe + 24 > n || memcmp(d + pe, "PE\0\0", 4)) return false;
    uint32_t nsec = rd16(d + pe + 6), optsz = rd16(d + pe + 20);
    const uint8_t *opt = d + pe + 24;
    if ((size_t)pe + 24 + optsz + nsec * 40 > n || optsz < 60) return false;
    if (rd32(opt + 28) != EXE_IMAGE_BASE) return false;
    image_size = rd32(opt + 56);
    image = calloc(1, image_size);
    if (!image) return false;
    for (uint32_t i = 0; i < nsec; i++) {
        const uint8_t *s = opt + optsz + i * 40;
        uint32_t rva = rd32(s + 12), len = rd32(s + 16), rawoff = rd32(s + 20);  /* raw data; the rest reads 0 */
        if ((uint64_t)rva + len > image_size) len = rva < image_size ? image_size - rva : 0;
        if ((uint64_t)rawoff + len > n) return false;
        memcpy(image + rva, d + rawoff, len);
    }
    return true;
}

static bool finish(const uint8_t *d, size_t n, const char *where)
{
    uint32_t crc = crc32_ieee(d, n);
    if (n != EXE_SIZE || crc != EXE_CRC32) {
        snprintf(err, sizeof err,
                 "%s is not the supported version of the Return Fire game program "
                 "(found %zu bytes, CRC-32 %08x; expected %u bytes, CRC-32 %08x).\n\n"
                 "Use RFIRE.BIN from the root of the Return Fire for Windows 95 CD.",
                 where, n, crc, EXE_SIZE, EXE_CRC32);
        return false;
    }
    if (!map_pe(d, n)) {
        free(image); image = NULL;
        snprintf(err, sizeof err, "%s: not a valid Win32 executable image.", where);
        return false;
    }
    for (ExeLoader *l = loaders; l; l = l->next) l->fn();
    return true;
}

bool exe_load(void)
{
    if (image) return true;
    size_t n = 0;
    uint8_t *d = vfs_read_all(EXE_FILE, &n);
    if (!d) {
        snprintf(err, sizeof err,
                 "%s was not found in %s.\n\nOpenRF reads the original game's tables from RFIRE.BIN: it is on the "
                 "root of the Return Fire CD, next to ART, SOUND, TITLE and WORLDS.", EXE_FILE,
                 vfs_mounted() ? vfs_describe() : "the game data");
        return false;
    }
    bool ok = finish(d, n, EXE_FILE);
    free(d);
    return ok;
}

bool exe_contains(uint32_t va, size_t n)
{
    return image && va >= EXE_IMAGE_BASE && (uint64_t)(va - EXE_IMAGE_BASE) + n <= image_size;
}

const void *exe_ptr(uint32_t va) { return exe_contains(va, 1) ? image + (va - EXE_IMAGE_BASE) : NULL; }

void exe_read(void *dst, uint32_t va, size_t n)
{
    if (!exe_contains(va, n)) {
        fprintf(stderr, "exe_read: 0x%08x+%zu outside the RFIRE.BIN image%s\n", va, n,
                image ? "" : " (not loaded: call exe_load() first)");
        abort();
    }
    memcpy(dst, image + (va - EXE_IMAGE_BASE), n);
}

uint32_t exe_u32(uint32_t va) { uint8_t b[4]; exe_read(b, va, 4); return rd32(b); }
int32_t exe_s32(uint32_t va) { return (int32_t)exe_u32(va); }
uint16_t exe_u16(uint32_t va) { uint8_t b[2]; exe_read(b, va, 2); return (uint16_t)rd16(b); }
uint8_t exe_u8(uint32_t va) { uint8_t b; exe_read(&b, va, 1); return b; }
int8_t exe_s8(uint32_t va) { return (int8_t)exe_u8(va); }

const char *exe_str(uint32_t va)
{
    if (!exe_contains(va, 1)) return "";
    const char *s = (const char *)image + (va - EXE_IMAGE_BASE);
    if (!memchr(s, 0, image_size - (va - EXE_IMAGE_BASE))) return "";
    return s;
}
