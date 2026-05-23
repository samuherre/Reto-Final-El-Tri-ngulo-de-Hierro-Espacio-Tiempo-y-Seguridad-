#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>

/*
 * editor.h — Estructura de datos Gap Buffer
 *
 * Un Gap Buffer es un array de caracteres con un "hueco" (gap)
 * posicionado en el cursor. Insertar texto es O(1) mientras el
 * cursor no se mueva; mover el cursor es O(distancia).
 *
 * Representación visual:
 *   Texto lógico: "Hola mundo"
 *   Buffer físico: [H][o][l][a][ ][_][_][_][m][u][n][d][o]
 *                               ^gap_start  ^gap_end
 *
 * gap_start = índice donde empieza el gap (donde está el cursor)
 * gap_end   = índice donde termina el gap (exclusive)
 * El texto después del cursor está en buf[gap_end..capacity]
 */

#define GAP_INITIAL_SIZE 64   /* Tamaño inicial del gap en bytes */
#define GAP_GROW_SIZE    128  /* Cuánto crece el gap cuando se agota */

typedef struct {
    char   *buf;        /* Array dinámico de caracteres (malloc) */
    size_t  gap_start;  /* Inicio del gap = posición del cursor */
    size_t  gap_end;    /* Fin del gap (exclusive) */
    size_t  capacity;   /* Tamaño total del buffer (buf) */
} GapBuffer;

/* Crea un GapBuffer vacío. Retorna 0 si OK, -1 si falla malloc. */
int  gap_init(GapBuffer *gb);

/* Libera toda la memoria del GapBuffer. */
void gap_free(GapBuffer *gb);

/* Inserta un carácter en la posición del cursor. */
int  gap_insert(GapBuffer *gb, char c);

/* Elimina el carácter a la izquierda del cursor (Backspace). */
void gap_delete_left(GapBuffer *gb);

/* Elimina el carácter a la derecha del cursor (Delete). */
void gap_delete_right(GapBuffer *gb);

/* Mueve el cursor N posiciones a la izquierda (N > 0) o derecha (N < 0). */
void gap_move_cursor(GapBuffer *gb, int delta);

/* Retorna el texto lógico completo copiado a un buffer nuevo (caller hace free). */
char *gap_get_text(const GapBuffer *gb, size_t *out_len);

/* Retorna la longitud lógica del texto (sin contar el gap). */
size_t gap_text_length(const GapBuffer *gb);

#endif /* EDITOR_H */
