/* Cinepak (cvid) decoder producing XRGB8888. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t y[4]; int8_t u, v; } CvidEntry;

typedef struct {
    int w, h;
    uint32_t *frame;          /* persistent: inter frames update in place */
    CvidEntry v1[32][256];    /* per-strip codebooks */
    CvidEntry v4[32][256];
} Cinepak;

void cinepak_init(Cinepak *c, int w, int h);
void cinepak_free(Cinepak *c);
/* Returns 0 on success. */
int cinepak_decode(Cinepak *c, const uint8_t *data, size_t size);
