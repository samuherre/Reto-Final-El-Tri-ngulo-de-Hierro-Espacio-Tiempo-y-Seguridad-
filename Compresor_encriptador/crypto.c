#include "crypto.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>    /* open() */
#include <unistd.h>   /* read(), close() */
#include <stdlib.h>

/*
 * crypto.c — Implementación AES-128-CBC en User Space puro
 *
 * Todo el cifrado/descifrado ocurre en RAM. El kernel solo recibe
 * los bytes ya cifrados cuando hacemos write(). La llave nunca sale
 * del proceso.
 *
 * Basado en el estándar FIPS 197 (Advanced Encryption Standard).
 * AES opera en una "state" de 4×4 bytes (16 bytes = 128 bits).
 */

/* ------------------------------------------------------------------ */
/* Tablas AES estándar (FIPS 197)                                      */
/* ------------------------------------------------------------------ */

/* S-Box: sustitución no lineal de bytes (confusión) */
static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,
    0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,
    0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,
    0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,
    0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,
    0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,
    0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,
    0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,
    0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,
    0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,
    0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,
    0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,
    0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,
    0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,
    0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,
    0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,
    0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* Inversa del S-Box (para descifrado) */
static const uint8_t INV_SBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,
    0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,
    0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,
    0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,
    0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,
    0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,
    0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,
    0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,
    0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,
    0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,
    0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,
    0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,
    0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,
    0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,
    0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,
    0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,
    0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

/* Constantes de ronda Rcon (derivadas de x^i en GF(2^8)) */
static const uint8_t RCON[11] = {
    0x00, /* índice 0 no se usa */
    0x01, 0x02, 0x04, 0x08, 0x10,
    0x20, 0x40, 0x80, 0x1b, 0x36
};

/* ------------------------------------------------------------------ */
/* Aritmética en GF(2^8) — necesaria para MixColumns                  */
/* ------------------------------------------------------------------ */

/* Multiplicación por 2 en GF(2^8) con el polinomio irreducible 0x1b */
static inline uint8_t xtime(uint8_t b) {
    return (uint8_t)((b << 1) ^ ((b >> 7) & 1 ? 0x1b : 0x00));
}

/* Multiplicación general en GF(2^8) */
static inline uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a >> 7;
        a = (uint8_t)(a << 1);
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Key Expansion (Key Schedule) AES-128                               */
/* ------------------------------------------------------------------ */

/*
 * Genera las 11 round keys (44 palabras de 4 bytes = 176 bytes total)
 * a partir de la llave de 16 bytes.
 */
static void key_expansion(const uint8_t *key, uint8_t *round_keys) {
    /* Las primeras 16 bytes son la llave original */
    memcpy(round_keys, key, 16);

    for (int i = 4; i < 44; i++) {
        uint8_t temp[4];
        memcpy(temp, round_keys + (i - 1) * 4, 4);

        if (i % 4 == 0) {
            /* RotWord: rotar a la izquierda un byte */
            uint8_t t = temp[0];
            temp[0] = temp[1]; temp[1] = temp[2];
            temp[2] = temp[3]; temp[3] = t;
            /* SubWord: aplicar S-Box a cada byte */
            temp[0] = SBOX[temp[0]]; temp[1] = SBOX[temp[1]];
            temp[2] = SBOX[temp[2]]; temp[3] = SBOX[temp[3]];
            /* XOR con Rcon */
            temp[0] ^= RCON[i / 4];
        }

        /* w[i] = w[i-4] XOR temp */
        for (int j = 0; j < 4; j++) {
            round_keys[i * 4 + j] =
                round_keys[(i - 4) * 4 + j] ^ temp[j];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Las 4 transformaciones de AES (cifrado)                            */
/* ------------------------------------------------------------------ */

/* SubBytes: sustituir cada byte del state con su valor en el S-Box */
static void sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++)
        state[i] = SBOX[state[i]];
}

/* InvSubBytes: inversa de SubBytes (para descifrado) */
static void inv_sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++)
        state[i] = INV_SBOX[state[i]];
}

/*
 * ShiftRows: desplaza las filas del state hacia la izquierda.
 * El state se interpreta como una matriz 4×4 en orden columna-mayor:
 *   state[0..3]  = columna 0
 *   state[4..7]  = columna 1
 *   state[8..11] = columna 2
 *   state[12..15]= columna 3
 *
 * En representación por filas:
 *   fila 0: no se desplaza
 *   fila 1: desplazamiento 1 a la izquierda
 *   fila 2: desplazamiento 2
 *   fila 3: desplazamiento 3
 */
static void shift_rows(uint8_t s[16]) {
    uint8_t t;

    /* Fila 1: shift izquierda 1 */
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;

    /* Fila 2: shift izquierda 2 */
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;

    /* Fila 3: shift izquierda 3 (= shift derecha 1) */
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

/* InvShiftRows: inversa (shift a la derecha) */
static void inv_shift_rows(uint8_t s[16]) {
    uint8_t t;

    /* Fila 1: shift derecha 1 */
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;

    /* Fila 2: shift derecha 2 */
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;

    /* Fila 3: shift derecha 3 */
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

/*
 * MixColumns: mezcla cada columna del state en GF(2^8).
 * Es la operación de "difusión": un byte de entrada afecta a todos
 * los bytes de la columna de salida.
 */
static void mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *col = s + c * 4;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2        ^ a3;
        col[1] = a0          ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
        col[2] = a0          ^ a1          ^ gmul(a2, 2) ^ gmul(a3, 3);
        col[3] = gmul(a0, 3) ^ a1          ^ a2          ^ gmul(a3, 2);
    }
}

/* InvMixColumns: inversa con los coeficientes {14,11,13,9} */
static void inv_mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *col = s + c * 4;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3, 9);
        col[1] = gmul(a0, 9) ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13);
        col[2] = gmul(a0,13) ^ gmul(a1, 9) ^ gmul(a2,14) ^ gmul(a3,11);
        col[3] = gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2, 9) ^ gmul(a3,14);
    }
}

/* AddRoundKey: XOR del state con la round key actual */
static void add_round_key(uint8_t state[16], const uint8_t *round_key) {
    for (int i = 0; i < 16; i++)
        state[i] ^= round_key[i];
}

/* ------------------------------------------------------------------ */
/* AES-128: cifrar/descifrar un bloque de 16 bytes                    */
/* ------------------------------------------------------------------ */

/*
 * Cifra un bloque de exactamente 16 bytes.
 * round_keys: 176 bytes generados por key_expansion().
 */
static void aes128_encrypt_block(const uint8_t *in,
                                  uint8_t       *out,
                                  const uint8_t *round_keys) {
    uint8_t state[16];
    memcpy(state, in, 16);

    /* Ronda inicial: solo AddRoundKey */
    add_round_key(state, round_keys);

    /* 9 rondas completas */
    for (int round = 1; round <= 9; round++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_keys + round * 16);
    }

    /* Ronda final: sin MixColumns */
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_keys + 10 * 16);

    memcpy(out, state, 16);
}

/* Descifra un bloque de 16 bytes */
static void aes128_decrypt_block(const uint8_t *in,
                                  uint8_t       *out,
                                  const uint8_t *round_keys) {
    uint8_t state[16];
    memcpy(state, in, 16);

    /* Ronda inicial (con la última round key) */
    add_round_key(state, round_keys + 10 * 16);

    /* 9 rondas inversas */
    for (int round = 9; round >= 1; round--) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, round_keys + round * 16);
        inv_mix_columns(state);
    }

    /* Ronda final inversa */
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, round_keys);

    memcpy(out, state, 16);
}

/* ------------------------------------------------------------------ */
/* API pública: init, passphrase, encrypt, decrypt, clear             */
/* ------------------------------------------------------------------ */

int crypto_init(CryptoContext *ctx,
                const uint8_t *key, const uint8_t *iv) {
    if (!ctx || !key || !iv) return -1;
    memcpy(ctx->key, key, AES_KEY_SIZE);
    memcpy(ctx->iv,  iv,  AES_IV_SIZE);
    ctx->initialized = 1;
    return 0;
}

/*
 * Deriva una llave de 16 bytes de una passphrase usando un hash
 * iterativo simple. No es PBKDF2, pero demuestra el concepto.
 */
int crypto_from_passphrase(CryptoContext *ctx, const char *passphrase) {
    if (!ctx || !passphrase || passphrase[0] == '\0') return -1;

    size_t plen = strlen(passphrase);

    /* Inicializar llave con patrón determinista */
    for (int i = 0; i < AES_KEY_SIZE; i++)
        ctx->key[i] = (uint8_t)(0x36 ^ i);

    /* Mezclar cada byte de la passphrase en la llave */
    for (size_t i = 0; i < plen * 1024; i++) {
        int ki = (int)(i % AES_KEY_SIZE);
        ctx->key[ki] ^= (uint8_t)(passphrase[i % plen]);
        /* Rotación para propagar bits */
        ctx->key[ki] = (uint8_t)((ctx->key[ki] << 3) |
                                  (ctx->key[ki] >> 5));
        /* Combinar con el byte anterior */
        ctx->key[(ki + 1) % AES_KEY_SIZE] ^= ctx->key[ki];
    }

    /* IV derivado de la llave con permutación */
    for (int i = 0; i < AES_IV_SIZE; i++)
        ctx->iv[i] = (uint8_t)(ctx->key[(i * 7 + 3) % AES_KEY_SIZE]
                               ^ (uint8_t)(i * 0x5A));

    ctx->initialized = 1;
    return 0;
}

int crypto_gen_iv(CryptoContext *ctx) {
    if (!ctx) return -1;

    /* Leer bytes aleatorios del CSPRNG del kernel */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/urandom");
        return -1;
    }
    ssize_t n = read(fd, ctx->iv, AES_IV_SIZE);
    close(fd);

    if (n != AES_IV_SIZE) {
        fprintf(stderr, "crypto_gen_iv: no se pudieron leer %d bytes\n",
                AES_IV_SIZE);
        return -1;
    }
    return 0;
}

/*
 * AES-128-CBC Encrypt con PKCS#7 padding.
 *
 * Modo CBC: cada bloque de plaintext se XOR con el ciphertext anterior
 * antes de cifrarse. El primer bloque se XOR con el IV.
 *
 *   C[0] = AES_Enc(P[0] XOR IV)
 *   C[i] = AES_Enc(P[i] XOR C[i-1])
 */
ssize_t aes128_cbc_encrypt(const CryptoContext *ctx,
                            const uint8_t *in,  size_t in_len,
                            uint8_t       *out, size_t out_cap) {
    if (!ctx || !ctx->initialized || !in || !out) return -1;

    /* Tamaño con padding PKCS#7: siempre hay padding (mín 1 byte) */
    size_t pad_len  = AES_BLOCK_SIZE - (in_len % AES_BLOCK_SIZE);
    size_t total    = in_len + pad_len;

    if (out_cap < total) {
        fprintf(stderr, "aes128_cbc_encrypt: buffer insuficiente\n");
        return -1;
    }

    /* Construir buffer con padding en memoria temporal */
    uint8_t *padded = (uint8_t *)malloc(total);
    if (!padded) { perror("malloc padded"); return -1; }

    memcpy(padded, in, in_len);
    /* PKCS#7: rellenar con el valor del padding */
    for (size_t i = in_len; i < total; i++)
        padded[i] = (uint8_t)pad_len;

    /* Generar round keys */
    uint8_t round_keys[176];
    key_expansion(ctx->key, round_keys);

    /* CBC: cifrar bloque a bloque */
    const uint8_t *prev = ctx->iv;  /* primer XOR con IV */
    uint8_t block_in[AES_BLOCK_SIZE];

    for (size_t b = 0; b < total; b += AES_BLOCK_SIZE) {
        /* XOR plaintext con bloque anterior (o IV) */
        for (int j = 0; j < AES_BLOCK_SIZE; j++)
            block_in[j] = padded[b + j] ^ prev[j];

        /* Cifrar el bloque */
        aes128_encrypt_block(block_in, out + b, round_keys);

        /* El próximo bloque usará este ciphertext como "IV" */
        prev = out + b;
    }

    /* Limpiar material sensible del stack */
    memset(padded, 0, total);
    free(padded);
    memset(round_keys, 0, sizeof(round_keys));

    return (ssize_t)total;
}

/*
 * AES-128-CBC Decrypt con remoción de PKCS#7 padding.
 *
 * Inverso de CBC:
 *   P[0] = AES_Dec(C[0]) XOR IV
 *   P[i] = AES_Dec(C[i]) XOR C[i-1]
 */
ssize_t aes128_cbc_decrypt(const CryptoContext *ctx,
                            const uint8_t *in,  size_t in_len,
                            uint8_t       *out, size_t out_cap) {
    if (!ctx || !ctx->initialized || !in || !out) return -1;
    if (in_len == 0 || in_len % AES_BLOCK_SIZE != 0) {
        fprintf(stderr, "aes128_cbc_decrypt: in_len debe ser múltiplo de 16\n");
        return -1;
    }
    if (out_cap < in_len) {
        fprintf(stderr, "aes128_cbc_decrypt: buffer de salida insuficiente\n");
        return -1;
    }

    /* Generar round keys */
    uint8_t round_keys[176];
    key_expansion(ctx->key, round_keys);

    /* CBC inverso: descifrar bloque a bloque */
    const uint8_t *prev = ctx->iv;
    uint8_t block_out[AES_BLOCK_SIZE];

    for (size_t b = 0; b < in_len; b += AES_BLOCK_SIZE) {
        aes128_decrypt_block(in + b, block_out, round_keys);
        for (int j = 0; j < AES_BLOCK_SIZE; j++)
            out[b + j] = block_out[j] ^ prev[j];
        prev = in + b;
    }

    /* Remover padding PKCS#7 */
    uint8_t pad_val = out[in_len - 1];
    if (pad_val == 0 || pad_val > AES_BLOCK_SIZE) {
        fprintf(stderr, "aes128_cbc_decrypt: padding PKCS#7 inválido (val=%u)\n",
                pad_val);
        memset(round_keys, 0, sizeof(round_keys));
        return -1;
    }
    /* Verificar que todos los bytes de padding son iguales */
    for (size_t i = in_len - pad_val; i < in_len; i++) {
        if (out[i] != pad_val) {
            fprintf(stderr, "aes128_cbc_decrypt: padding PKCS#7 corrupto\n");
            memset(round_keys, 0, sizeof(round_keys));
            return -1;
        }
    }

    memset(round_keys, 0, sizeof(round_keys));
    return (ssize_t)(in_len - pad_val);
}

/*
 * Borrado seguro de la llave en RAM.
 * volatile evita que el compilador elimine el memset como "código muerto".
 */
void crypto_clear(CryptoContext *ctx) {
    if (!ctx) return;
    volatile uint8_t *p = (volatile uint8_t *)ctx;
    for (size_t i = 0; i < sizeof(CryptoContext); i++)
        p[i] = 0;
}
