#ifndef FORMAT_H
#define FORMAT_H

#include <stdint.h>

/*
 * format.h — Formato de archivo binario del editor
 *
 * El archivo guardado en disco tiene esta estructura:
 *
 *  [FileHeader (20 bytes)] [payload comprimido...]
 *
 * __attribute__((packed)) le dice al compilador que NO agregue
 * bytes de relleno (padding) entre los campos del struct.
 * Sin esto, el compilador podría alinear campos a 4 u 8 bytes
 * y el struct ocuparía más espacio del necesario.
 */

#define MAGIC_NUMBER    0x45443031u   /* "ED01" en hex */
#define MAGIC_SECURE    0x45443032u   /* "ED02" — archivo encriptado */
#define ALGO_RLE        0x00u
#define ALGO_HUFFMAN    0x01u
#define ALGO_RLE_AES    0x02u         /* Pipeline: RLE → AES-128-CBC */
#define STYLE_BOLD    0x01u
#define STYLE_ITALIC  0x02u
#define STYLE_COLOR   0x04u

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* Bytes 0-3:  Magic number identificador */
    uint32_t original_size;   /* Bytes 4-7:  Tamaño del texto sin comprimir */
    uint32_t compressed_size; /* Bytes 8-11: Tamaño del payload comprimido */
    uint8_t  algorithm;       /* Byte  12:   Algoritmo (RLE=0, Huffman=1) */
    uint8_t  reserved[3];     /* Bytes 13-15: Reservado para alineación futura */
    uint32_t checksum;        /* Bytes 16-19: CRC32 simple del payload */
} FileHeader;

/*
 * Verifica en tiempo de compilación que el struct mide exactamente 20 bytes.
 * Si no es así, el compilador lanza un error — útil para detectar padding.
 */
_Static_assert(sizeof(FileHeader) == 20, "FileHeader debe ser exactamente 20 bytes (sin padding)");

/* -------- TEXTO ENRIQUECIDO (adición nueva) -------- */

/*
 * Una entrada en la tabla de estilos.
 * Cada entry dice: "desde el byte 'offset', los siguientes 'length'
 * bytes tienen los flags de estilo 'flags' y el color 'color_rgb'."
 */
typedef struct __attribute__((packed)) {
    uint32_t offset;     /* Posición en el texto original */
    uint32_t length;     /* Cuántos bytes abarca este estilo */
    uint8_t  flags;      /* STYLE_BOLD | STYLE_ITALIC | STYLE_COLOR */
    uint8_t  color_r;
    uint8_t  color_g;
    uint8_t  color_b;
} StyleEntry;

_Static_assert(sizeof(StyleEntry) == 12, "StyleEntry debe ser 12 bytes sin padding");

/*
 * Header extendido para archivos con texto enriquecido.
 * Estructura en disco:
 *   [RichHeader (28 bytes)]
 *   [StyleEntry * style_count]
 *   [payload comprimido]
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* MAGIC_NUMBER */
    uint32_t original_size;   /* Bytes de texto plano */
    uint32_t compressed_size; /* Bytes del payload comprimido */
    uint8_t  algorithm;       /* ALGO_RLE */
    uint8_t  reserved[3];
    uint32_t checksum;        /* CRC32 del payload */
    uint16_t style_count;     /* Cuántos StyleEntry siguen al header */
    uint16_t rich_reserved;   /* Padding para alineación futura */
} RichHeader;

_Static_assert(sizeof(RichHeader) == 24, "RichHeader debe ser 24 bytes");

/* -------- ARCHIVO SEGURO (RLE + AES-128-CBC) -------- */

/*
 * SecureHeader — encabezado para archivos comprimidos y cifrados.
 *
 * Estructura en disco:
 *   [SecureHeader (52 bytes)]
 *   [payload: RLE-comprimido → AES-CBC-cifrado]
 *
 * El pipeline de guardado es:
 *   texto plano
 *     → rle_compress()        → compressed_size bytes
 *     → aes128_cbc_encrypt()  → encrypted_size bytes (múltiplo de 16)
 *     → write() al disco
 *
 * Campos de tamaño:
 *   original_size   : bytes del texto plano (para descomprimir)
 *   compressed_size : bytes post-RLE (para desencriptar → pasar a RLE)
 *   encrypted_size  : bytes en disco del payload cifrado
 *
 * El IV (16 bytes) se guarda en el header porque sin él no es posible
 * descifrar el primer bloque. El IV no es secreto; la llave sí.
 *
 * El checksum cubre el payload ya CIFRADO (bytes en disco).
 * Esto permite detectar corrupción antes de intentar descifrar.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* MAGIC_SECURE = 0x45443032 ("ED02")   */
    uint32_t original_size;   /* Bytes del texto plano original        */
    uint32_t compressed_size; /* Bytes después de RLE (antes de AES)  */
    uint32_t encrypted_size;  /* Bytes del payload cifrado en disco    */
    uint8_t  algorithm;       /* ALGO_RLE_AES = 0x02                  */
    uint8_t  reserved[3];     /* Reservado para extensiones futuras    */
    uint32_t checksum;        /* CRC32 del payload cifrado             */
    uint8_t  iv[16];          /* IV de AES-128-CBC (público, no secreto) */
} SecureHeader;

_Static_assert(sizeof(SecureHeader) == 40, "SecureHeader debe ser 40 bytes");

/* --------------------------------------------------- */

#endif /* FORMAT_H */