#include "editor.h"
#include "io.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>   /* mlock(), munlock() */

/*
 * main.c — Menú principal del editor de texto
 *
 * Flujo:
 *   1. Crear/cargar texto en un GapBuffer (en RAM)
 *   2. Editar (insertar, borrar, mover cursor)
 *   3. Guardar → compresión en RAM → write() al disco
 *
 * SEGURIDAD DE MEMORIA:
 *   El CryptoContext (que contiene la llave AES-128) se declara en el
 *   stack y se protege con mlock() para prohibir que el kernel envíe
 *   esa página al área de Swap en el disco. Sin mlock(), si el SO se
 *   queda sin RAM, podría serializar la llave al disco duro, donde un
 *   atacante con acceso físico podría recuperarla.
 *
 *   Al terminar, crypto_clear() borra la llave con escritura volatile
 *   (resistente a optimizaciones del compilador) y munlock() devuelve
 *   la página al pool normal del kernel.
 */

#define MAX_PATH    256
#define MAX_STYLES   64

static void print_menu(void) {
    printf("\n=== EDITOR DE ARCHIVOS (OS Project) ===\n");
    printf("  [i] Insertar texto\n");
    printf("  [d] Borrar carácter (backspace)\n");
    printf("  [l] Leer/ver texto actual\n");
    printf("  [c] Mover cursor (±N posiciones)\n");
    printf("  [b] Marcar rango como Bold\n");
    printf("  [s] Guardar archivo (write + compresión)\n");
    printf("  [m] Guardar con mmap\n");
    printf("  [r] Guardar con texto enriquecido (.rich)\n");
    printf("  [e] Guardar cifrado (RLE → AES-128-CBC) [NUEVO]\n");
    printf("  [k] Configurar llave criptográfica       [NUEVO]\n");
    printf("  [o] Abrir archivo\n");
    printf("  [q] Salir\n");
    printf("Opción: ");
}

static void print_text(const GapBuffer *gb) {
    size_t len;
    char *text = gap_get_text(gb, &len);
    if (!text) return;
    printf("\n--- Texto actual (%zu chars) ---\n", len);
    printf("%s\n", text);
    printf("--- Cursor en posición %zu ---\n", gb->gap_start);
    free(text);
}

/*
 * secure_zero_stack — limpia un buffer del stack con volatile.
 * Evita que el compilador elimine la escritura como "código muerto"
 * cuando el buffer no se lee después.
 */
static void secure_zero_stack(void *ptr, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < n; i++)
        p[i] = 0;
}

int main(void) {
    GapBuffer  gb;
    StyleEntry styles[MAX_STYLES];
    uint16_t   style_count = 0;
    char       path[MAX_PATH] = {0};

    /*
     * CryptoContext en el stack — NUNCA sale del proceso.
     *
     * mlock() le dice al kernel: "esta página de RAM no puede ir a Swap".
     * Sin esto, si el SO necesita memoria, podría escribir la página que
     * contiene la llave al disco de intercambio, comprometiendo la seguridad.
     *
     * Referencia: man 2 mlock — requiere privilegios o RLIMIT_MEMLOCK.
     * Si falla (sistema sin privilegios suficientes), el programa advierte
     * pero continúa: la funcionalidad no depende de mlock.
     */
    CryptoContext crypto_ctx;
    memset(&crypto_ctx, 0, sizeof(crypto_ctx));

    if (mlock(&crypto_ctx, sizeof(crypto_ctx)) != 0) {
        fprintf(stderr,
            "[AVISO] mlock() falló: la llave podría ir a Swap si el SO "
            "necesita memoria. Ejecuta como root o ajusta RLIMIT_MEMLOCK "
            "para mayor seguridad.\n");
        /* No es fatal — el programa sigue funcionando */
    } else {
        printf("[SEGURIDAD] Página de llave bloqueada en RAM (no va a Swap).\n");
    }

    if (gap_init(&gb) != 0) {
        fprintf(stderr, "Error inicializando editor\n");
        munlock(&crypto_ctx, sizeof(crypto_ctx));
        return 1;
    }

    printf("Ruta del archivo (Enter para 'archivo.ed'): ");
    if (fgets(path, MAX_PATH, stdin) == NULL) {
        fprintf(stderr, "Error leyendo ruta\n");
        gap_free(&gb);
        munlock(&crypto_ctx, sizeof(crypto_ctx));
        return 1;
    }
    path[strcspn(path, "\n")] = '\0';
    if (strlen(path) == 0) {
        strncpy(path, "archivo.ed", MAX_PATH - 1);
        path[MAX_PATH - 1] = '\0';
    }

    int  running = 1;
    char opcion;

    while (running) {
        print_menu();

        if (scanf(" %c", &opcion) != 1) {
            fprintf(stderr, "Error leyendo opción\n");
            break;
        }
        getchar();

        switch (opcion) {

        case 'i': {
            printf("Texto a insertar (Enter para terminar):\n");
            char linea[1024];
            if (fgets(linea, sizeof(linea), stdin) == NULL) {
                fprintf(stderr, "Error leyendo texto\n");
                break;
            }
            for (size_t i = 0; i < strlen(linea); i++) {
                if (gap_insert(&gb, linea[i]) != 0) {
                    fprintf(stderr, "Error insertando carácter\n");
                    break;
                }
            }
            printf("[OK] Texto insertado.\n");
            break;
        }

        case 'd':
            gap_delete_left(&gb);
            printf("[OK] Carácter borrado.\n");
            break;

        case 'l':
            print_text(&gb);
            break;

        case 'c': {
            int delta;
            printf("Delta (negativo=izquierda, positivo=derecha): ");
            if (scanf("%d", &delta) != 1) {
                fprintf(stderr, "Error leyendo delta\n");
                getchar();
                break;
            }
            getchar();
            gap_move_cursor(&gb, delta);
            printf("[OK] Cursor movido %d posiciones. Posición actual: %zu\n",
                   delta, gb.gap_start);
            break;
        }

        case 'b': {
            if (style_count >= MAX_STYLES) {
                printf("[AVISO] Límite de %d estilos alcanzado.\n", MAX_STYLES);
                break;
            }
            size_t start_pos, length;
            printf("Posición inicio del estilo: ");
            if (scanf("%zu", &start_pos) != 1) {
                fprintf(stderr, "Error leyendo posición\n");
                getchar();
                break;
            }
            printf("Longitud: ");
            if (scanf("%zu", &length) != 1) {
                fprintf(stderr, "Error leyendo longitud\n");
                getchar();
                break;
            }
            getchar();
            styles[style_count].offset  = (uint32_t)start_pos;
            styles[style_count].length  = (uint32_t)length;
            styles[style_count].flags   = STYLE_BOLD;
            styles[style_count].color_r = 0;
            styles[style_count].color_g = 0;
            styles[style_count].color_b = 0;
            style_count++;
            printf("[OK] Estilo Bold aplicado: posición %zu, largo %zu. "
                   "Total estilos: %u\n", start_pos, length, style_count);
            break;
        }

        case 's': {
            size_t len;
            char *text = gap_get_text(&gb, &len);
            if (!text) { fprintf(stderr, "Error obteniendo texto\n"); break; }
            printf("[Guardando con write() en '%s'...]\n", path);
            if (save_file(path, text, len) == 0)
                printf("[OK] Archivo guardado.\n");
            else
                fprintf(stderr, "[ERROR] Fallo al guardar.\n");
            free(text);
            break;
        }

        case 'm': {
            size_t len;
            char *text = gap_get_text(&gb, &len);
            if (!text) { fprintf(stderr, "Error obteniendo texto\n"); break; }
            char mmap_path[MAX_PATH];
            snprintf(mmap_path, MAX_PATH, "%s.mmap", path);
            printf("[Guardando con mmap() en '%s'...]\n", mmap_path);
            if (save_file_mmap(mmap_path, text, len) == 0)
                printf("[OK] Archivo guardado con mmap.\n");
            else
                fprintf(stderr, "[ERROR] Fallo al guardar con mmap.\n");
            free(text);
            break;
        }

        case 'r': {
            size_t len;
            char *text = gap_get_text(&gb, &len);
            if (!text) { fprintf(stderr, "Error obteniendo texto\n"); break; }
            char rich_path[MAX_PATH];
            snprintf(rich_path, MAX_PATH, "%s.rich", path);
            printf("[Guardando con texto enriquecido en '%s'...]\n", rich_path);
            if (save_rich_file(rich_path, text, len, styles, style_count) == 0)
                printf("[OK] Archivo enriquecido guardado. "
                       "(%u estilo(s) almacenado(s))\n", style_count);
            else
                fprintf(stderr, "[ERROR] Fallo al guardar archivo enriquecido.\n");
            free(text);
            break;
        }

        case 'o': {
            char *text = NULL;
            printf("[Abriendo '%s'...]\n", path);
            ssize_t len = load_file(path, &text);
            if (len < 0) {
                fprintf(stderr, "[ERROR] No se pudo abrir el archivo.\n");
                break;
            }
            gap_free(&gb);
            if (gap_init(&gb) != 0) {
                fprintf(stderr, "Error reinicializando editor\n");
                free(text);
                break;
            }
            for (ssize_t i = 0; i < len; i++) {
                if (gap_insert(&gb, text[i]) != 0) {
                    fprintf(stderr, "Error cargando carácter %zd\n", i);
                    break;
                }
            }
            free(text);
            style_count = 0;
            printf("[OK] Archivo cargado. %zd caracteres.\n", len);
            break;
        }

        case 'e': {
            if (!crypto_ctx.initialized) {
                printf("[AVISO] No hay llave configurada. Use [k] primero.\n");
                break;
            }
            size_t len;
            char *text = gap_get_text(&gb, &len);
            if (!text) { fprintf(stderr, "Error obteniendo texto\n"); break; }

            char sec_path[MAX_PATH];
            snprintf(sec_path, MAX_PATH, "%s.sec", path);
            printf("[Guardando cifrado en '%s'...]\n", sec_path);
            printf("[INFO] Pipeline: texto → RLE (comprime) → AES-128-CBC (cifra) → disco\n");

            if (save_file_secure(sec_path, text, len, &crypto_ctx) == 0)
                printf("[OK] Archivo cifrado guardado.\n");
            else
                fprintf(stderr, "[ERROR] Fallo al guardar archivo cifrado.\n");
            free(text);
            break;
        }

        case 'k': {
            /*
             * La passphrase se lee del stack, se procesa en crypto_ctx,
             * y se borra con secure_zero_stack() inmediatamente después.
             *
             * No se pasa como argumento argv (evita que aparezca en
             * ps aux o /proc/<pid>/cmdline, visible a otros procesos).
             */
            char passphrase[256];
            printf("Ingrese la contraseña (passphrase): ");
            if (fgets(passphrase, sizeof(passphrase), stdin) == NULL) {
                fprintf(stderr, "Error leyendo passphrase\n");
                break;
            }
            passphrase[strcspn(passphrase, "\n")] = '\0';

            if (crypto_from_passphrase(&crypto_ctx, passphrase) == 0) {
                printf("[OK] Llave criptográfica configurada (AES-128).\n");
                printf("[INFO] Pipeline activo: texto → RLE → AES-128-CBC → disco\n");
            } else {
                fprintf(stderr, "[ERROR] Passphrase inválida.\n");
            }

            /*
             * Borrar la passphrase del stack con volatile.
             * Un memset() sin volatile puede ser eliminado por -O2
             * porque el compilador detecta que 'passphrase' no se lee
             * después. Con volatile la escritura es obligatoria.
             */
            secure_zero_stack(passphrase, sizeof(passphrase));
            break;
        }

        default:
            printf("Opción no reconocida: '%c'\n", opcion);
        }
    }

    /* Limpieza final */
    gap_free(&gb);

    /*
     * 1. Borrar llave de RAM con volatile (resistente a -O2)
     * 2. Devolver la página al pool normal del kernel
     */
    crypto_clear(&crypto_ctx);
    munlock(&crypto_ctx, sizeof(crypto_ctx));

    return 0;
}
