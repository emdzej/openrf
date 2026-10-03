/* plat_storage_* on the host filesystem (the tests): one file per key in
   ~/Library/Application Support/Return Fire/ (the high-score file RFire_HS keeps the location and
   format the port has always used). OPENRF_HS overrides the RFire_HS file path (tests). */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *key_path(const char *key, bool make_dir)
{
    static char path[1024];
    const char *e = getenv("OPENRF_HS");
    if (!strcmp(key, "RFire_HS") && e && *e) return e;
    const char *home = getenv("HOME");
    snprintf(path, sizeof path, "%s/Library/Application Support/Return Fire", home ? home : ".");
    if (make_dir) mkdir(path, 0755);
    size_t n = strlen(path);
    snprintf(path + n, sizeof path - n, "/%s", key);
    return path;
}

const char *plat_storage_location(const char *key) { return key_path(key, true); }

int plat_storage_get(const char *key, void *dst, int cap)
{
    FILE *f = fopen(key_path(key, false), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    if (dst && n <= cap && fread(dst, 1, (size_t)n, f) != (size_t)n) n = -1;
    fclose(f);
    return (int)n;
}

bool plat_storage_set(const char *key, const void *data, int len)
{
    FILE *f = fopen(key_path(key, true), "wb");
    bool ok = f && fwrite(data, 1, (size_t)len, f) == (size_t)len;
    if (f && fclose(f) != 0) ok = false;
    return ok;
}
