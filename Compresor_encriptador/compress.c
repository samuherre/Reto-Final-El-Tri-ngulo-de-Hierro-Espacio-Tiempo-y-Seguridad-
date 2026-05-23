#include "compress.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * compress.c — Implementación de RLE y CRC32
 *
 * Todo ocurre en RAM (User Space). El kernel nunca ve el texto plano:
 * solo recibe bytes comprimidos cuando hacemos write().
 */

/* ------------------------------------------------------------------ */
/* CRC32 simple (polinomio estándar 0xEDB88320)                        */
/* ------------------------------------------------------------------ */

uint32_t compute_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* RLE Compresión                                                       */
/* ------------------------------------------------------------------ */

ssize_t rle_compress(const uint8_t *input,  size_t input_len,
                           uint8_t *output, size_t output_cap) {
    if (!input || !output || input_len == 0) return -1;

    size_t out_pos = 0;
    size_t i = 0;

    while (i < input_len) {
        uint8_t current = input[i];
        uint8_t count   = 1;

        /* Cuenta cuántas veces se repite el carácter actual (máx 255) */
        while (i + count < input_len &&
               input[i + count] == current &&
               count < 255) {
            count++;
        }

        /* Necesitamos 2 bytes en output: [count][value] */
        if (out_pos + 2 > output_cap) {
            fprintf(stderr, "rle_compress: buffer de salida insuficiente\n");
            return -1;
        }

        output[out_pos++] = count;
        output[out_pos++] = current;
        i += count;
    }

    return (ssize_t)out_pos;
}

/* ------------------------------------------------------------------ */
/* RLE Descompresión                                                    */
/* ------------------------------------------------------------------ */

ssize_t rle_decompress(const uint8_t *input,  size_t input_len,
                             uint8_t *output, size_t output_cap) {
    if (!input || !output || input_len == 0) return -1;
    if (input_len % 2 != 0) {
        fprintf(stderr, "rle_decompress: input_len impar — datos corruptos\n");
        return -1;
    }

    size_t out_pos = 0;

    for (size_t i = 0; i < input_len; i += 2) {
        uint8_t count = input[i];
        uint8_t value = input[i + 1];

        if (out_pos + count > output_cap) {
            fprintf(stderr, "rle_decompress: buffer de salida insuficiente (datos corruptos o buffer muy pequeño)\n");
            return -1;
        }

        /* Escribe 'count' copias de 'value' */
        for (uint8_t j = 0; j < count; j++) {
            output[out_pos++] = value;
        }
    }

    return (ssize_t)out_pos;
}
