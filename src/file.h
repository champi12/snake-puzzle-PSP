/* file.h - lectura de los archivos de data/ (comprimidos o no) */
#ifndef FILE_H
#define FILE_H
#include <stdint.h>

/* Lee el archivo entero en un bloque alineado a 64 bytes (lo puede leer el GE) que se libera con free().
 * Si empieza por "SNZ1" + tamano (u32) esta comprimido con zlib (tools/extract.py) y se descomprime.
 * Devuelve NULL si no existe o no se puede leer. */
void *file_load(const char *path, uint32_t *size);

#endif
