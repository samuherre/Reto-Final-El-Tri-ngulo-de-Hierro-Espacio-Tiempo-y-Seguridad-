#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * editor.c — Implementación del Gap Buffer
 *
 * Toda operación de memoria usa malloc/calloc/realloc/free explícitos.
 * No hay uso de funciones de alto nivel del SO para I/O aquí.
 */

/* --- Función interna: asegura que el gap tenga al menos 1 espacio libre --- */
static int gap_ensure_space(GapBuffer *gb) {
    if (gb->gap_end > gb->gap_start) {
        return 0; /* Ya hay espacio en el gap */
    }

    /*
     * El gap se agotó. Necesitamos crecer el buffer.
     * Calculamos la nueva capacidad y hacemos realloc.
     */
    size_t new_capacity = gb->capacity + GAP_GROW_SIZE;
    char *new_buf = (char *)malloc(new_capacity);
    if (!new_buf) {
        perror("malloc en gap_ensure_space");
        return -1;
    }

    /* Copiamos la parte antes del gap */
    memcpy(new_buf, gb->buf, gb->gap_start);

    /* Copiamos la parte después del gap al FINAL del nuevo buffer,
     * dejando un hueco de GAP_GROW_SIZE en el medio */
    size_t after_len = gb->capacity - gb->gap_end;
    memcpy(new_buf + new_capacity - after_len,
           gb->buf + gb->gap_end,
           after_len);

    free(gb->buf);
    gb->buf      = new_buf;
    gb->gap_end  = new_capacity - after_len;
    gb->capacity = new_capacity;

    return 0;
}

/* ------------------------------------------------------------------ */

int gap_init(GapBuffer *gb) {
    gb->capacity  = GAP_INITIAL_SIZE;
    gb->gap_start = 0;
    gb->gap_end   = GAP_INITIAL_SIZE; /* Todo el buffer es gap inicialmente */

    /*
     * calloc: inicializa a 0 (más seguro que malloc para debugging).
     * calloc(n, size) = malloc(n * size) + memset a 0.
     */
    gb->buf = (char *)calloc(gb->capacity, sizeof(char));
    if (!gb->buf) {
        perror("calloc en gap_init");
        return -1;
    }
    return 0;
}

void gap_free(GapBuffer *gb) {
    if (gb->buf) {
        free(gb->buf);
        gb->buf = NULL; /* Evitar double-free */
    }
    gb->gap_start = 0;
    gb->gap_end   = 0;
    gb->capacity  = 0;
}

int gap_insert(GapBuffer *gb, char c) {
    if (gap_ensure_space(gb) != 0) return -1;

    /* Insertar en gap_start y avanzar el inicio del gap */
    gb->buf[gb->gap_start] = c;
    gb->gap_start++;
    return 0;
}

void gap_delete_left(GapBuffer *gb) {
    /* Backspace: retrocede gap_start (borra el carácter a la izquierda) */
    if (gb->gap_start > 0) {
        gb->gap_start--;
        gb->buf[gb->gap_start] = '\0'; /* Limpieza opcional */
    }
}

void gap_delete_right(GapBuffer *gb) {
    /* Delete: avanza gap_end (salta el carácter a la derecha) */
    if (gb->gap_end < gb->capacity) {
        gb->buf[gb->gap_end] = '\0'; /* Limpieza opcional */
        gb->gap_end++;
    }
}

void gap_move_cursor(GapBuffer *gb, int delta) {
    if (delta < 0) {
        /* Mover cursor a la izquierda: mueve un char desde antes del gap al final */
        while (delta < 0 && gb->gap_start > 0) {
            gb->gap_start--;
            gb->gap_end--;
            gb->buf[gb->gap_end] = gb->buf[gb->gap_start];
            delta++;
        }
    } else {
        /* Mover cursor a la derecha: mueve un char desde después del gap al inicio */
        while (delta > 0 && gb->gap_end < gb->capacity) {
            gb->buf[gb->gap_start] = gb->buf[gb->gap_end];
            gb->gap_start++;
            gb->gap_end++;
            delta--;
        }
    }
}

char *gap_get_text(const GapBuffer *gb, size_t *out_len) {
    /*
     * El texto lógico está en dos fragmentos:
     *   1) buf[0 .. gap_start - 1]
     *   2) buf[gap_end .. capacity - 1]
     * Los concatenamos en un buffer nuevo.
     */
    size_t before_len = gb->gap_start;
    size_t after_len  = gb->capacity - gb->gap_end;
    size_t total_len  = before_len + after_len;

    char *text = (char *)malloc(total_len + 1); /* +1 para '\0' */
    if (!text) {
        perror("malloc en gap_get_text");
        return NULL;
    }

    memcpy(text, gb->buf, before_len);
    memcpy(text + before_len, gb->buf + gb->gap_end, after_len);
    text[total_len] = '\0';

    if (out_len) *out_len = total_len;
    return text;
}

size_t gap_text_length(const GapBuffer *gb) {
    return gb->gap_start + (gb->capacity - gb->gap_end);
}
