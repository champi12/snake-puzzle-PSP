/* file.c - lectura de los archivos de data/
 *
 * tools/extract.py comprime con zlib las texturas, los fondos, los niveles, la tienda y los efectos (de 16 MB a
 * menos de 6 en la Memory Stick, sin perder nada): aqui se descomprimen al cargarlos. Los MP3 van tal cual. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <zlib.h>
#include "file.h"
#include "devlog.h"

void *file_load(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* alineado a 64: la paleta y las texturas los lee el GE directamente */
    uint8_t *m = memalign(64, n + 64);
    if (m && fread(m, 1, n, f) != (size_t)n) {
        free(m);
        m = NULL;
    }
    fclose(f);
    if (!m) return NULL;
    if (n >= 8 && !memcmp(m, "SNZ1", 4)) {
        uLongf raw = m[4] | m[5] << 8 | m[6] << 16 | (uint32_t)m[7] << 24;
        uint8_t *d = memalign(64, raw + 64);
        if (!d || uncompress(d, &raw, m + 8, n - 8) != Z_OK) {
            dlog_error("no se pudo descomprimir %s", path);
            free(d);
            free(m);
            return NULL;
        }
        free(m);
        m = d;
        n = raw;
    }
    if (size) *size = n;
    return m;
}
