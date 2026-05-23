#ifndef COMPRESS_H
#define COMPRESS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */

/*
 * compress.h — Compresión/descompresión en User Space
 *
 * Algoritmo: RLE (Run-Length Encoding)
 * Idea: secuencias repetidas se codifican como (conteo, valor).
 *
 * Ejemplo:
 *   Entrada:  "AAABBBCCDDDDEE"  (14 bytes)
 *   Salida:   [3,'A'][3,'B'][2,'C'][4,'D'][2,'E']  (10 bytes)
 *
 * Formato de cada "run":
 *   [uint8_t count][uint8_t value]  = 2 bytes por run
 *
 * Nota: RLE es simple y demostrable. Para textos con pocas repeticiones
 * puede NO comprimir bien. Para el benchmark usa archivos con mucha
 * repetición, o menciona en tu informe que podrías usar zlib (deflate).
 */

/*
 * Comprime 'input_len' bytes de 'input' en el buffer 'output'.
 * 'output' debe tener al menos input_len * 2 bytes de espacio
 * (en el peor caso, RLE puede expandir los datos).
 *
 * Retorna el número de bytes escritos en 'output', o -1 si error.
 */
ssize_t rle_compress(const uint8_t *input,  size_t input_len,
                           uint8_t *output, size_t output_cap);

/*
 * Descomprime 'input_len' bytes de 'input' (formato RLE) en 'output'.
 * 'output_cap' debe ser al menos el tamaño original (guardado en el header).
 *
 * Retorna el número de bytes escritos en 'output', o -1 si error.
 */
ssize_t rle_decompress(const uint8_t *input,  size_t input_len,
                             uint8_t *output, size_t output_cap);

/* CRC32 simple para verificar integridad del payload */
uint32_t compute_crc32(const uint8_t *data, size_t len);

#endif /* COMPRESS_H */
