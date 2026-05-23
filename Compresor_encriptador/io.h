#ifndef IO_H
#define IO_H

#include "format.h"
#include <stddef.h>
#include <sys/types.h>   /* ssize_t */

/*
 * io.h — I/O directa al kernel usando syscalls POSIX
 *
 * NUNCA usamos fopen/fwrite/fprintf (stdio.h).
 * Usamos open/read/write de <fcntl.h> y <unistd.h>.
 *
 * ¿Por qué importa el tamaño del bloque?
 *   El disco físico opera en sectores de 512B o 4096B (4KB).
 *   El kernel agrupa accesos en páginas de 4096B (PAGE_SIZE).
 *   Si haces write() de 1 byte a la vez, cada llamada es un
 *   "context switch" User→Kernel→User. Con bloques de 4KB
 *   reduces esos context switches drásticamente (medible con strace).
 */

#define PAGE_SIZE_BYTES 4096   /* Tamaño de página del kernel Linux */

/*
 * Guarda el texto en un archivo con formato binario comprimido.
 *   - Comprime 'text' de 'text_len' bytes en RAM (User Space)
 *   - Construye el FileHeader
 *   - Hace write() en un solo bloque (o múltiplos de PAGE_SIZE)
 *
 * Retorna 0 si OK, -1 si error.
 */
int save_file(const char *path, const char *text, size_t text_len);

/*
 * Carga un archivo en formato binario comprimido.
 *   - Lee el FileHeader
 *   - Verifica magic number y checksum
 *   - Descomprime el payload en RAM
 *   - Retorna el texto en 'out_text' (caller debe hacer free)
 *
 * Retorna el tamaño del texto descomprimido, o -1 si error.
 */
ssize_t load_file(const char *path, char **out_text);

/*
 * Versión alternativa usando mmap en lugar de write.
 * Para el benchmark: compara tiempo y syscalls de save_file vs save_file_mmap.
 */
int save_file_mmap(const char *path, const char *text, size_t text_len);

int save_rich_file(const char *path, const char *text, size_t text_len,
                   const StyleEntry *styles, uint16_t style_count);

ssize_t load_rich_file(const char *path, char **out_text,
                        StyleEntry **out_styles, uint16_t *out_style_count);

/* ------------------------------------------------------------------ */
/* I/O Seguro: compresión RLE + cifrado AES-128-CBC                   */
/* ------------------------------------------------------------------ */

#include "crypto.h"

/*
 * save_file_secure — guarda el texto con pipeline RLE → AES-128-CBC.
 *
 * Pipeline completo en RAM:
 *   1. rle_compress(texto) → buffer comprimido
 *   2. aes128_cbc_encrypt(buffer comprimido) → buffer cifrado
 *   3. Construir SecureHeader (con IV y tamaños)
 *   4. write() al disco: [SecureHeader][payload cifrado]
 *
 * La llave NUNCA se escribe al disco.
 * El IV sí (en el SecureHeader), porque es necesario para descifrar.
 *
 * Retorna 0 si OK, -1 si error.
 */
int save_file_secure(const char *path, const char *text, size_t text_len,
                     CryptoContext *ctx);

/*
 * load_file_secure — carga y descifra un archivo guardado con save_file_secure.
 *
 * Pipeline inverso en RAM:
 *   1. read() → [SecureHeader][payload cifrado]
 *   2. Verificar magic MAGIC_SECURE y CRC32
 *   3. Cargar IV del SecureHeader al contexto
 *   4. aes128_cbc_decrypt(payload) → buffer comprimido
 *   5. rle_decompress(buffer comprimido) → texto plano
 *
 * Retorna el tamaño del texto descomprimido, o -1 si error.
 * El texto se devuelve en *out_text (caller debe hacer free).
 */
ssize_t load_file_secure(const char *path, char **out_text,
                          CryptoContext *ctx);

#endif /* IO_H */
