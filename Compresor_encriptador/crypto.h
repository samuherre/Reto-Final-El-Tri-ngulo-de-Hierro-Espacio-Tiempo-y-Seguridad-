#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>   /* ssize_t */

/*
 * crypto.h — Capa criptográfica simétrica AES-128-CBC
 *
 * Algoritmo: AES-128 en modo CBC (Cipher Block Chaining)
 * Padding:   PKCS#7 (rellena el último bloque con N bytes de valor N)
 *
 * ¿Por qué CBC y no ECB?
 *   ECB cifra cada bloque de forma independiente: si dos bloques de
 *   16 bytes son iguales en el plaintext, producen el mismo ciphertext.
 *   Esto filtra patrones. CBC encadena los bloques: el ciphertext del
 *   bloque i-1 se XOR con el plaintext del bloque i antes de cifrar,
 *   rompiendo esa correlación.
 *
 * ¿Por qué comprimir ANTES de encriptar?
 *   La encriptación convierte los datos en ruido estadístico uniforme
 *   (entropía máxima ~8 bits por byte). RLE busca secuencias repetidas
 *   (entropía baja). Si encriptas primero, no hay nada que comprimir:
 *   RLE expande los datos. El orden correcto es:
 *
 *       GUARDAR: texto → [RLE compress] → [AES-128-CBC encrypt] → disco
 *       CARGAR:  disco → [AES-128-CBC decrypt] → [RLE decompress] → texto
 *
 * Gestión de llaves:
 *   La llave vive en un CryptoContext en el stack/heap del proceso.
 *   Nunca se serializa a disco. Al terminar, crypto_clear() sobreescribe
 *   los bytes de la llave con ceros usando volatile para prevenir que el
 *   compilador optimice el memset (técnica estándar de limpieza segura).
 *
 * IV (Vector de Inicialización):
 *   Se genera una vez por sesión con /dev/urandom y se guarda en el
 *   SecureHeader del archivo. Sin el IV correcto, el descifrado del
 *   primer bloque produce basura (los demás bloques se recuperan
 *   igualmente porque CBC propaga el IV solo al bloque 0).
 */

#define AES_BLOCK_SIZE   16   /* AES opera en bloques de 128 bits = 16 bytes */
#define AES_KEY_SIZE     16   /* AES-128: llave de 128 bits = 16 bytes       */
#define AES_IV_SIZE      16   /* IV para modo CBC: mismo tamaño que el bloque */

/*
 * CryptoContext — gestión de la llave en RAM
 *
 * La llave JAMÁS se escribe al disco. El IV sí (va en el header del
 * archivo), porque sin él no se puede descifrar; pero el IV por sí
 * solo no compromete la confidencialidad: para eso hace falta la llave.
 */
typedef struct {
    uint8_t key[AES_KEY_SIZE];   /* Llave AES-128 (16 bytes) en RAM */
    uint8_t iv[AES_IV_SIZE];     /* IV para el próximo cifrado CBC   */
    int     initialized;         /* Flag: 1 si el contexto es válido */
} CryptoContext;

/*
 * crypto_init — inicializa con llave e IV explícitos.
 * Útil para pruebas o cuando el caller ya tiene bytes de llave.
 * Retorna 0 si OK.
 */
int crypto_init(CryptoContext *ctx,
                const uint8_t *key, const uint8_t *iv);

/*
 * crypto_from_passphrase — deriva llave e IV de una contraseña.
 *
 * Implementación: hash iterativo simple (XOR + rotación).
 * NOTA DE PRODUCCIÓN: En un sistema real se usaría PBKDF2 o Argon2
 * con una salt aleatoria para resistir ataques de diccionario.
 * Para este proyecto académico, la derivación simple es suficiente.
 *
 * Retorna 0 si OK, -1 si passphrase es NULL o vacía.
 */
int crypto_from_passphrase(CryptoContext *ctx, const char *passphrase);

/*
 * crypto_gen_iv — genera un IV aleatorio desde /dev/urandom.
 * Actualiza ctx->iv. Retorna 0 si OK, -1 si error.
 */
int crypto_gen_iv(CryptoContext *ctx);

/*
 * aes128_cbc_encrypt — encripta en modo AES-128-CBC con PKCS#7 padding.
 *
 *   in      : plaintext a cifrar
 *   in_len  : tamaño del plaintext
 *   out     : buffer de salida (ciphertext)
 *   out_cap : capacidad de out — debe ser >= (in_len / 16 + 1) * 16
 *             (siempre hay al menos un bloque de padding PKCS#7)
 *
 * El padding PKCS#7 funciona así:
 *   Si in_len = 14, faltan 2 bytes → se añaden [0x02, 0x02]
 *   Si in_len = 16 (múltiplo), se añade un bloque entero de [0x10 × 16]
 *   Esto permite al receptor siempre saber cuánto padding remover.
 *
 * Retorna bytes escritos en out (siempre múltiplo de 16), o -1 si error.
 */
ssize_t aes128_cbc_encrypt(const CryptoContext *ctx,
                            const uint8_t *in,  size_t in_len,
                            uint8_t       *out, size_t out_cap);

/*
 * aes128_cbc_decrypt — descifra en modo AES-128-CBC y remueve padding.
 *
 *   in_len debe ser múltiplo de 16 (el output de encrypt siempre lo es).
 *   Retorna bytes de plaintext (sin padding), o -1 si error.
 */
ssize_t aes128_cbc_decrypt(const CryptoContext *ctx,
                            const uint8_t *in,  size_t in_len,
                            uint8_t       *out, size_t out_cap);

/*
 * crypto_clear — borra la llave de RAM de forma segura.
 *
 * Usamos volatile para forzar que el compilador NO optimice el memset.
 * Sin volatile, -O2 puede detectar que el buffer no se lee después
 * y eliminar la escritura como "código muerto".
 */
void crypto_clear(CryptoContext *ctx);

#endif /* CRYPTO_H */
