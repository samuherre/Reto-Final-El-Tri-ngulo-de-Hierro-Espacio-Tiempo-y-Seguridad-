#include "io.h"
#include "compress.h"
#include "crypto.h"
#include <fcntl.h>      /* open(), O_RDWR, O_CREAT... */
#include <unistd.h>     /* read(), write(), close(), lseek() */
#include <sys/mman.h>   /* mmap(), munmap() */
#include <sys/stat.h>   /* fstat() */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * io.c — Implementación de I/O con syscalls directas
 *
 * PIPELINE completo de guardado:
 *   texto en RAM → compresión RLE en RAM → FileHeader → write() al kernel
 *
 * NUNCA el texto plano toca el disco.
 */

/* ------------------------------------------------------------------ */
/* Guardar: write() en bloques alineados a PAGE_SIZE                   */
/* ------------------------------------------------------------------ */

int save_file(const char *path, const char *text, size_t text_len) {
    if (!path || !text) return -1;

    /* 1. Allocar buffer para compresión (peor caso: 2x el tamaño original) */
    size_t   max_compressed = text_len * 2 + 2;
    uint8_t *compressed_buf = (uint8_t *)malloc(max_compressed);
    if (!compressed_buf) {
        perror("malloc compressed_buf");
        return -1;
    }

    /* 2. Comprimir en User Space — el kernel NO interviene aquí */
    ssize_t compressed_size = rle_compress(
        (const uint8_t *)text, text_len,
        compressed_buf, max_compressed
    );
    if (compressed_size < 0) {
        free(compressed_buf);
        return -1;
    }

    /* 3. Construir el FileHeader en RAM */
    FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic           = MAGIC_NUMBER;
    hdr.original_size   = (uint32_t)text_len;
    hdr.compressed_size = (uint32_t)compressed_size;
    hdr.algorithm       = ALGO_RLE;
    hdr.checksum        = compute_crc32(compressed_buf, (size_t)compressed_size);

    /* 4. Calcular tamaño total del archivo = header + payload */
    size_t total_size = sizeof(FileHeader) + (size_t)compressed_size;

    /*
     * 5. Construir el "write buffer" completo en RAM.
     * Lo alineamos a PAGE_SIZE para que el kernel haga menos trabajo
     * al copiar desde User Space al buffer del kernel (page cache).
     *
     * posix_memalign garantiza alineación al tamaño de página.
     */
    size_t aligned_size = ((total_size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES)
                          * PAGE_SIZE_BYTES;
    uint8_t *write_buf = NULL;
    if (posix_memalign((void **)&write_buf, PAGE_SIZE_BYTES, aligned_size) != 0) {
        perror("posix_memalign");
        free(compressed_buf);
        return -1;
    }
    memset(write_buf, 0, aligned_size);

    /* Copiar header y payload al buffer alineado */
    memcpy(write_buf, &hdr, sizeof(hdr));
    memcpy(write_buf + sizeof(hdr), compressed_buf, (size_t)compressed_size);
    free(compressed_buf); /* ya no necesitamos el buffer intermedio */

    /* 6. Abrir el archivo con syscall directa open() — NO fopen() */
    int fd = open(path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        perror("open");
        free(write_buf);
        return -1;
    }

    /*
     * 7. Escribir en UN SOLO write() del tamaño total.
     * Esto minimiza context switches User→Kernel.
     * strace mostrará UNA llamada a write() en lugar de miles.
     */
    ssize_t written = write(fd, write_buf, total_size);
    if (written < 0 || (size_t)written != total_size) {
        perror("write");
        close(fd);
        free(write_buf);
        return -1;
    }

    close(fd);
    free(write_buf);

    printf("[IO] Guardado: %zu bytes texto → %zd bytes comprimidos (%.1f%%)\n",
           text_len, compressed_size,
           100.0 * (1.0 - (double)compressed_size / (double)text_len));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Cargar: read() + descompresión en RAM                               */
/* ------------------------------------------------------------------ */

ssize_t load_file(const char *path, char **out_text) {
    if (!path || !out_text) return -1;

    /* 1. Abrir el archivo */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open para lectura");
        return -1;
    }

    /* 2. Obtener tamaño del archivo con fstat */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    size_t file_size = (size_t)st.st_size;

    if (file_size < sizeof(FileHeader)) {
        fprintf(stderr, "Archivo demasiado pequeño para ser válido\n");
        close(fd);
        return -1;
    }

    /* 3. Leer todo el archivo en un buffer */
    uint8_t *file_buf = (uint8_t *)malloc(file_size);
    if (!file_buf) {
        perror("malloc file_buf");
        close(fd);
        return -1;
    }

    ssize_t bytes_read = read(fd, file_buf, file_size);
    close(fd);

    if (bytes_read < 0 || (size_t)bytes_read != file_size) {
        perror("read");
        free(file_buf);
        return -1;
    }

    /* 4. Parsear el FileHeader */
    FileHeader hdr;
    memcpy(&hdr, file_buf, sizeof(hdr));

    /* 5. Verificar magic number */
    if (hdr.magic != MAGIC_NUMBER) {
        fprintf(stderr, "Magic number inválido: 0x%08X (esperado 0x%08X)\n",
                hdr.magic, MAGIC_NUMBER);
        free(file_buf);
        return -1;
    }

    /* 6. Verificar checksum del payload */
    uint8_t *payload = file_buf + sizeof(FileHeader);
    uint32_t crc = compute_crc32(payload, hdr.compressed_size);
    if (crc != hdr.checksum) {
        fprintf(stderr, "Checksum inválido: archivo corrupto\n");
        free(file_buf);
        return -1;
    }

    /* 7. Descomprimir en User Space */
    uint8_t *text_buf = (uint8_t *)malloc(hdr.original_size + 1);
    if (!text_buf) {
        perror("malloc text_buf");
        free(file_buf);
        return -1;
    }

    ssize_t decompressed = rle_decompress(
        payload, hdr.compressed_size,
        text_buf, hdr.original_size
    );

    free(file_buf); /* liberar buffer del archivo */

    if (decompressed < 0) {
        free(text_buf);
        return -1;
    }

    text_buf[decompressed] = '\0'; /* Terminador nulo — CRÍTICO */
    *out_text = (char *)text_buf;

    printf("[IO] Cargado: %zd bytes en disco → %zd bytes en RAM\n",
           (ssize_t)file_size, decompressed);
    return decompressed;
}

/* ------------------------------------------------------------------ */
/* Guardar con mmap (versión alternativa para el benchmark)            */
/* ------------------------------------------------------------------ */

int save_file_mmap(const char *path, const char *text, size_t text_len) {
    if (!path || !text) return -1;

    /* Comprimir igual que en save_file */
    size_t   max_compressed = text_len * 2 + 2;
    uint8_t *compressed_buf = (uint8_t *)malloc(max_compressed);
    if (!compressed_buf) { perror("malloc"); return -1; }

    ssize_t compressed_size = rle_compress(
        (const uint8_t *)text, text_len,
        compressed_buf, max_compressed
    );
    if (compressed_size < 0) { free(compressed_buf); return -1; }

    FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic           = MAGIC_NUMBER;
    hdr.original_size   = (uint32_t)text_len;
    hdr.compressed_size = (uint32_t)compressed_size;
    hdr.algorithm       = ALGO_RLE;
    hdr.checksum        = compute_crc32(compressed_buf, (size_t)compressed_size);

    size_t total_size = sizeof(FileHeader) + (size_t)compressed_size;

    /*
     * mmap: el kernel mapea el archivo directamente en el espacio de
     * direcciones del proceso. Escribir en ese puntero == escribir al disco.
     * El kernel gestiona cuándo hacer flush (menos context switches).
     *
     * Pasos:
     *   1. Abrir y truncar el archivo al tamaño final
     *   2. mmap con PROT_WRITE | MAP_SHARED
     *   3. memcpy al puntero mapeado
     *   4. msync para forzar escritura
     *   5. munmap
     */
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open mmap"); free(compressed_buf); return -1; }

    /* Ajustar el tamaño del archivo */
    if (ftruncate(fd, (off_t)total_size) < 0) {
        perror("ftruncate");
        close(fd);
        free(compressed_buf);
        return -1;
    }

    /* Mapear en memoria */
    void *mapped = mmap(NULL, total_size,
                        PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        close(fd);
        free(compressed_buf);
        return -1;
    }

    /* Escribir al área mapeada (sin syscall write explícita) */
    memcpy(mapped, &hdr, sizeof(hdr));
    memcpy((uint8_t *)mapped + sizeof(hdr), compressed_buf, (size_t)compressed_size);

    /* Forzar escritura al disco */
    msync(mapped, total_size, MS_SYNC);
    munmap(mapped, total_size);
    close(fd);
    free(compressed_buf);

    printf("[MMAP] Guardado con mmap: %zu bytes texto → %zd bytes en disco\n",
           text_len, compressed_size);
    return 0;
}

/* io.c — implementación de save_rich_file */
int save_rich_file(const char *path, const char *text, size_t text_len,
                   const StyleEntry *styles, uint16_t style_count) {
    /* 1. Comprimir texto en RAM */
    size_t max_compressed = text_len * 2 + 2;
    uint8_t *comp_buf = (uint8_t *)malloc(max_compressed);
    if (!comp_buf) { perror("malloc comp_buf"); return -1; }

    ssize_t comp_size = rle_compress((const uint8_t *)text, text_len,
                                      comp_buf, max_compressed);
    if (comp_size < 0) { free(comp_buf); return -1; }

    /* 2. Construir RichHeader en RAM */
    RichHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic           = MAGIC_NUMBER;
    hdr.original_size   = (uint32_t)text_len;
    hdr.compressed_size = (uint32_t)comp_size;
    hdr.algorithm       = ALGO_RLE;
    hdr.checksum        = compute_crc32(comp_buf, (size_t)comp_size);
    hdr.style_count     = style_count;

    /* 3. Calcular tamaño total = RichHeader + tabla de estilos + payload */
    size_t styles_size = style_count * sizeof(StyleEntry);
    size_t total_size  = sizeof(RichHeader) + styles_size + (size_t)comp_size;

    /* 4. Buffer alineado a PAGE_SIZE para write() eficiente */
    size_t aligned = ((total_size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES)
                     * PAGE_SIZE_BYTES;
    uint8_t *write_buf = NULL;
    if (posix_memalign((void **)&write_buf, PAGE_SIZE_BYTES, aligned) != 0) {
        perror("posix_memalign"); free(comp_buf); return -1;
    }
    memset(write_buf, 0, aligned);

    /* 5. Ensamblar: [RichHeader][StyleEntry...][payload] */
    memcpy(write_buf, &hdr, sizeof(hdr));
    if (styles && style_count > 0)
        memcpy(write_buf + sizeof(hdr), styles, styles_size);
    memcpy(write_buf + sizeof(hdr) + styles_size, comp_buf, (size_t)comp_size);
    free(comp_buf);

    /* 6. Un solo write() al kernel */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) { perror("open"); free(write_buf); return -1; }

    ssize_t written = write(fd, write_buf, total_size);
    close(fd);
    free(write_buf);

    if (written < 0 || (size_t)written != total_size) {
        perror("write rich"); return -1;
    }

    printf("[RICH] Guardado: %zu bytes texto, %u estilos → %zd bytes disco\n",
           text_len, style_count, comp_size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* I/O Seguro: pipeline RLE → AES-128-CBC                             */
/* ------------------------------------------------------------------ */

/*
 * save_file_secure — Guarda comprimido y cifrado en disco.
 *
 * Orden de transformaciones (CRÍTICO):
 *   texto → comprimir primero → cifrar después
 *
 * ¿Por qué este orden?
 *   - El texto tiene baja entropía (letras, palabras repetidas) → RLE
 *     puede reducirlo significativamente.
 *   - AES produce salida con distribución uniforme (alta entropía).
 *     Si comprimiéramos DESPUÉS de cifrar, RLE no encontraría patrones
 *     y expandiría los datos. El orden correcto aprovecha la baja
 *     entropía del texto ANTES de que AES la destruya.
 */
int save_file_secure(const char *path, const char *text, size_t text_len,
                     CryptoContext *ctx) {
    if (!path || !text || !ctx || !ctx->initialized) return -1;

    /* ---- PASO 1: Comprimir en RAM --------------------------------- */
    size_t   max_comp   = text_len * 2 + 2;
    uint8_t *comp_buf   = (uint8_t *)malloc(max_comp);
    if (!comp_buf) { perror("malloc comp_buf"); return -1; }

    ssize_t comp_size = rle_compress(
        (const uint8_t *)text, text_len, comp_buf, max_comp);
    if (comp_size < 0) {
        free(comp_buf);
        return -1;
    }

    printf("[SECURE] RLE: %zu → %zd bytes (%.1f%%)\n",
           text_len, comp_size,
           100.0 * (1.0 - (double)comp_size / (double)text_len));

    /* ---- PASO 2: Generar IV aleatorio para esta sesión ------------- */
    if (crypto_gen_iv(ctx) != 0) {
        fprintf(stderr, "Error generando IV\n");
        free(comp_buf);
        return -1;
    }

    /* ---- PASO 3: Cifrar en RAM ------------------------------------ */
    /*
     * PKCS#7 padding: el ciphertext siempre es un múltiplo de 16.
     * Peor caso: comp_size + AES_BLOCK_SIZE bytes.
     */
    size_t   max_enc  = (size_t)comp_size + AES_BLOCK_SIZE;
    uint8_t *enc_buf  = (uint8_t *)malloc(max_enc);
    if (!enc_buf) { perror("malloc enc_buf"); free(comp_buf); return -1; }

    ssize_t enc_size = aes128_cbc_encrypt(ctx,
                                           comp_buf, (size_t)comp_size,
                                           enc_buf,  max_enc);
    free(comp_buf); /* el comprimido ya no se necesita */

    if (enc_size < 0) {
        free(enc_buf);
        return -1;
    }

    printf("[SECURE] AES-128-CBC: %zd → %zd bytes (+%zd padding)\n",
           comp_size, enc_size, enc_size - comp_size);

    /* ---- PASO 4: Construir SecureHeader en RAM -------------------- */
    SecureHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic           = MAGIC_SECURE;
    hdr.original_size   = (uint32_t)text_len;
    hdr.compressed_size = (uint32_t)comp_size;
    hdr.encrypted_size  = (uint32_t)enc_size;
    hdr.algorithm       = ALGO_RLE_AES;
    hdr.checksum        = compute_crc32(enc_buf, (size_t)enc_size);
    memcpy(hdr.iv, ctx->iv, AES_IV_SIZE);   /* IV público en el header */

    /* ---- PASO 5: Ensamblar y escribir al disco en un write() ------ */
    size_t total_size = sizeof(SecureHeader) + (size_t)enc_size;
    size_t aligned    = ((total_size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES)
                        * PAGE_SIZE_BYTES;

    uint8_t *write_buf = NULL;
    if (posix_memalign((void **)&write_buf, PAGE_SIZE_BYTES, aligned) != 0) {
        perror("posix_memalign"); free(enc_buf); return -1;
    }
    memset(write_buf, 0, aligned);

    memcpy(write_buf, &hdr, sizeof(hdr));
    memcpy(write_buf + sizeof(hdr), enc_buf, (size_t)enc_size);
    free(enc_buf);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) { perror("open secure"); free(write_buf); return -1; }

    ssize_t written = write(fd, write_buf, total_size);
    close(fd);
    free(write_buf);

    if (written < 0 || (size_t)written != total_size) {
        perror("write secure"); return -1;
    }

    printf("[SECURE] Guardado en '%s': %zu bytes texto → %zu bytes en disco\n",
           path, text_len, total_size);
    return 0;
}

/*
 * load_file_secure — Carga, descifra y descomprime un archivo seguro.
 *
 * Pipeline inverso:
 *   disco → [SecureHeader] → verificar CRC → AES-CBC decrypt → RLE decompress → texto
 */
ssize_t load_file_secure(const char *path, char **out_text,
                          CryptoContext *ctx) {
    if (!path || !out_text || !ctx || !ctx->initialized) return -1;

    /* ---- PASO 1: Leer el archivo completo ------------------------- */
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open secure load"); return -1; }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return -1; }
    size_t file_size = (size_t)st.st_size;

    if (file_size < sizeof(SecureHeader)) {
        fprintf(stderr, "Archivo demasiado pequeño\n");
        close(fd); return -1;
    }

    uint8_t *file_buf = (uint8_t *)malloc(file_size);
    if (!file_buf) { perror("malloc file_buf"); close(fd); return -1; }

    ssize_t bytes_read = read(fd, file_buf, file_size);
    close(fd);
    if (bytes_read < 0 || (size_t)bytes_read != file_size) {
        perror("read secure"); free(file_buf); return -1;
    }

    /* ---- PASO 2: Parsear y verificar el SecureHeader -------------- */
    SecureHeader hdr;
    memcpy(&hdr, file_buf, sizeof(hdr));

    if (hdr.magic != MAGIC_SECURE) {
        fprintf(stderr, "Magic inválido: 0x%08X (esperado 0x%08X)\n",
                hdr.magic, MAGIC_SECURE);
        free(file_buf); return -1;
    }

    uint8_t *payload = file_buf + sizeof(SecureHeader);
    uint32_t crc     = compute_crc32(payload, hdr.encrypted_size);
    if (crc != hdr.checksum) {
        fprintf(stderr, "[SECURE] CRC32 inválido — archivo corrupto o llave incorrecta\n");
        free(file_buf); return -1;
    }

    /* ---- PASO 3: Restaurar IV del header al contexto -------------- */
    memcpy(ctx->iv, hdr.iv, AES_IV_SIZE);

    /* ---- PASO 4: Descifrar el payload en RAM ---------------------- */
    uint8_t *dec_buf = (uint8_t *)malloc(hdr.encrypted_size);
    if (!dec_buf) { perror("malloc dec_buf"); free(file_buf); return -1; }

    ssize_t dec_size = aes128_cbc_decrypt(ctx,
                                           payload, hdr.encrypted_size,
                                           dec_buf, hdr.encrypted_size);
    free(file_buf);

    if (dec_size < 0) {
        fprintf(stderr, "[SECURE] Error al descifrar — ¿llave incorrecta?\n");
        free(dec_buf); return -1;
    }

    /* ---- PASO 5: Descomprimir en RAM ------------------------------ */
    uint8_t *text_buf = (uint8_t *)malloc(hdr.original_size + 1);
    if (!text_buf) { perror("malloc text_buf"); free(dec_buf); return -1; }

    ssize_t decompressed = rle_decompress(dec_buf, (size_t)dec_size,
                                           text_buf, hdr.original_size);
    free(dec_buf);

    if (decompressed < 0) {
        fprintf(stderr, "[SECURE] Error al descomprimir\n");
        free(text_buf); return -1;
    }

    text_buf[decompressed] = '\0';
    *out_text = (char *)text_buf;

    printf("[SECURE] Cargado: %zu bytes en disco → %zd bytes texto\n",
           file_size, decompressed);
    return decompressed;
}