/*
 * classic_io.c — Enfoque "clásico" ineficiente para benchmark.
 * Escribe texto plano byte a byte → muchos context switches.
 * Compilar: gcc -O2 -o classic classic_io.c
 * Usar con strace: strace -c ./classic
 */
#include <stdio.h>   /* fopen, fputc — nivel alto, intencionalmente malo */
#include <string.h>

int main(void) {
    const char *texto = "AAAAAABBBBBBCCCCCCDDDDDDEEEEEE";
    /* Repetir 10000 veces para tener volumen de datos */
    FILE *f = fopen("salida_clasica.txt", "w");
    if (!f) { perror("fopen"); return 1; }
    for (int rep = 0; rep < 10000; rep++) {
        for (size_t i = 0; i < strlen(texto); i++) {
            fputc(texto[i], f);   /* 1 carácter a la vez = overhead máximo */
        }
    }
    fclose(f);
    printf("Escrito: salida_clasica.txt (%zu bytes)\n",
           strlen(texto) * 10000);
    return 0;
}