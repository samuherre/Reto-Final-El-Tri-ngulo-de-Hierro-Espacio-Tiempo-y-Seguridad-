# Editor de Archivos con Optimización de Bus I/O
### Proyecto 3 — Sistemas Operativos | Ingeniería de Sistemas

> Editor de texto en C nativo para Linux que implementa un pipeline completo de compresión en User Space antes de invocar syscalls del kernel, minimizando los context switches y la carga del bus I/O.

---

## Tabla de contenidos

- [Descripción del proyecto](#descripción-del-proyecto)
- [Arquitectura general](#arquitectura-general)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Requisitos del sistema](#requisitos-del-sistema)
- [Compilación](#compilación)
- [Uso del editor](#uso-del-editor)
- [Formato de archivo binario](#formato-de-archivo-binario)
- [Algoritmo de compresión RLE](#algoritmo-de-compresión-rle)
- [Texto enriquecido](#texto-enriquecido)
- [Profiling y benchmarking](#profiling-y-benchmarking)
- [Verificación de memoria con Valgrind](#verificación-de-memoria-con-valgrind)
- [Decisiones de diseño](#decisiones-de-diseño)

---

## Descripción del proyecto

Este proyecto implementa un editor de texto de consola en C que demuestra empíricamente cómo **invertir ciclos de CPU en comprimir datos antes de escribirlos al disco** resulta en un ahorro neto de tiempo, reduciendo la carga y latencia del bus I/O físico.

El principio central es: **el kernel nunca ve texto plano**. Todo el texto pasa por un pipeline de compresión en User Space antes de que se invoque cualquier syscall de escritura.

### Características principales

- **Gap Buffer** como estructura de datos interna del editor (inserción O(1) en la posición del cursor)
- **Compresión RLE** (Run-Length Encoding) implementada en C puro, en RAM, antes de cualquier syscall
- **Dos métodos de I/O** comparables: `write()` con buffer alineado a `PAGE_SIZE` y `mmap()`
- **Formato binario propio** con magic number, checksum CRC32 y metadatos empaquetados (`__attribute__((packed))`)
- **Soporte de texto enriquecido** con tabla de estilos (bold, italic, color) en el header binario
- **Sin uso de `stdio.h`** para I/O — solo syscalls POSIX directas (`open`, `read`, `write`, `mmap`)

---

## Arquitectura general

```
┌─────────────────────────────────── USER SPACE ────────────────────────────────────┐
│                                                                                     │
│  ┌──────────────┐    ┌─────────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │   GapBuffer  │───▶│  gap_get_text() │───▶│ rle_compress │───▶│  FileHeader  │  │
│  │  (editor.c)  │    │   (en RAM)      │    │ (compress.c) │    │  (format.h)  │  │
│  └──────────────┘    └─────────────────┘    └──────────────┘    └──────┬───────┘  │
│                                                                          │           │
│                                                               posix_memalign(4KB)   │
│                                                                          │           │
└──────────────────────────────────────────────────────────────────────── │ ──────────┘
                                                                           │
                              ── syscall boundary ──────────────────────  │
                                                                           ▼
┌─────────────────────────────────── KERNEL SPACE ──────────────────────────────────┐
│                                                                                     │
│              write(fd, buf, total_size)   ──▶   Page Cache   ──▶   Disco           │
│              (1 sola llamada, bloque alineado a 4KB)                               │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────── ┘
```

### Módulos del proyecto

| Archivo | Responsabilidad |
|---|---|
| `main.c` | Menú de consola, loop principal, manejo de opciones |
| `editor.c / editor.h` | Estructura Gap Buffer, inserción, borrado, movimiento de cursor |
| `compress.c / compress.h` | Algoritmo RLE, descompresión RLE, CRC32 |
| `io.c / io.h` | Syscalls POSIX: `save_file()`, `load_file()`, `save_file_mmap()`, `save_rich_file()`, `load_rich_file()` |
| `format.h` | Definición de `FileHeader`, `RichHeader`, `StyleEntry` con `__attribute__((packed))` |
| `classic_io.c` | Programa auxiliar de benchmark: escribe texto plano byte a byte para comparación |
| `benchmark.sh` | Script de profiling automático con `strace` y `time` |
| `Makefile` | Compilación, target valgrind, target bench |

---

## Estructura del repositorio

```
Compresor_encriptador/
├── main.c              # Punto de entrada y menú interactivo
├── editor.c            # Implementación del Gap Buffer
├── editor.h            # Interfaz pública del Gap Buffer
├── compress.c          # RLE compress/decompress y CRC32
├── compress.h          # Interfaz pública de compresión
├── io.c                # I/O con syscalls POSIX directas
├── io.h                # Interfaz pública de I/O
├── format.h            # Estructuras binarias empaquetadas
├── classic_io.c        # Programa de referencia para benchmark
├── Makefile            # Reglas de compilación
├── benchmark.sh        # Script de profiling completo
└── README.md           # Este archivo
```

---

## Requisitos del sistema

- **SO:** Linux (kernel ≥ 4.0)
- **Compilador:** GCC ≥ 7.0
- **Herramientas opcionales para profiling:**
  - `strace` — conteo de syscalls: `sudo apt install strace`
  - `valgrind` — detección de memory leaks: `sudo apt install valgrind`
  - `python3` — generación de archivos de prueba en el benchmark

---

## Compilación

```bash
# Compilar el editor principal
make

# Compilar también el programa clásico para benchmark
gcc -O2 -o classic classic_io.c

# Limpiar binarios y archivos generados
make clean
```

El `Makefile` usa `-Wall -Wextra -g -O2`. El flag `-g` mantiene símbolos de debug para valgrind; `-O2` aplica optimizaciones de compilador.

---

## Uso del editor

```bash
./editor
```

Al iniciar, el editor solicita la ruta del archivo. Si se presiona Enter sin ingresar nada, usa `archivo.ed` por defecto.

### Opciones del menú

| Opción | Acción |
|---|---|
| `i` | Insertar texto en la posición actual del cursor |
| `d` | Borrar carácter a la izquierda del cursor (Backspace) |
| `l` | Ver el texto actual en pantalla |
| `c` | Mover el cursor N posiciones (negativo = izquierda) |
| `s` | **Guardar** con `write()` — compresión RLE + bloque alineado a 4KB |
| `m` | **Guardar con `mmap()`** — mapeo en memoria + `msync()` |
| `r` | **Guardar con texto enriquecido** — incluye tabla de estilos en el header |
| `b` | Marcar un rango de texto como **Bold** |
| `o` | Abrir y descomprimir un archivo `.ed` existente |
| `q` | Salir (libera toda la memoria — verificable con valgrind) |

### Ejemplo de sesión

```
Ruta del archivo (Enter para 'archivo.ed'): mi_documento

=== EDITOR DE ARCHIVOS (OS Project) ===
  [i] Insertar texto
  ...
Opción: i
Texto a insertar (Enter para terminar):
Hola mundo, esto es una prueba de compresión.
[OK] Texto insertado.

Opción: s
[Guardando con write() en 'mi_documento.ed'...]
[IO] Guardado: 46 bytes texto → 62 bytes comprimidos (−34.8%)
[OK] Archivo guardado.
```

> **Nota sobre RLE:** el algoritmo RLE comprime eficientemente texto con caracteres repetidos consecutivos (p. ej. `AAAAAABBBBB`). En texto con poca repetición puede expandir ligeramente el tamaño, lo cual es esperable y se discute en el reporte de profiling.

---

## Formato de archivo binario

### Archivo estándar (`.ed`)

```
Offset  Tamaño  Campo             Descripción
──────  ───────  ────────────────  ─────────────────────────────────────────
0x00    4 bytes  magic             Identificador: 0x45443031 ("ED01")
0x04    4 bytes  original_size     Tamaño del texto antes de comprimir
0x08    4 bytes  compressed_size   Tamaño del payload RLE
0x0C    1 byte   algorithm         0x00 = RLE
0x0D    3 bytes  reserved          Reservado (padding futuro)
0x10    4 bytes  checksum          CRC32 del payload comprimido
0x14    N bytes  payload           Datos RLE comprimidos
```

Total del header: **20 bytes exactos** (verificado con `_Static_assert`).

### Archivo con texto enriquecido (`.ed.rich`)

```
Offset      Tamaño              Campo           Descripción
──────────  ──────────────────  ──────────────  ──────────────────────────────────
0x00        24 bytes            RichHeader      Header extendido (ver abajo)
0x18        style_count × 12B   StyleEntry[]    Tabla de estilos
0x18 + M    compressed_size B   payload         Datos RLE comprimidos
```

**RichHeader (24 bytes):**

```
magic(4) | original_size(4) | compressed_size(4) | algorithm(1) | reserved(3) | checksum(4) | style_count(2) | rich_reserved(2)
```

**StyleEntry (12 bytes por entrada):**

```
offset(4) | length(4) | flags(1) | color_r(1) | color_g(1) | color_b(1)
```

Flags disponibles: `STYLE_BOLD = 0x01`, `STYLE_ITALIC = 0x02`, `STYLE_COLOR = 0x04`.

---

## Algoritmo de compresión RLE

Run-Length Encoding codifica cada secuencia de bytes iguales como un par `(conteo, valor)`:

```
Entrada:  A A A A B B C C C C C     →  11 bytes
Salida:   04 'A' 02 'B' 05 'C'     →   6 bytes  (-45%)
```

Cada "run" ocupa exactamente 2 bytes. El conteo máximo por run es 255 (un `uint8_t`). La descompresión es el proceso inverso y siempre produce exactamente `original_size` bytes, verificado por CRC32.

---

## Texto enriquecido

El editor permite marcar rangos de texto con atributos de estilo sin alterar el contenido del texto plano subyacente. Los estilos se almacenan como una tabla de `StyleEntry` entre el header y el payload comprimido.

```bash
# Flujo de uso:
# 1. Insertar texto con [i]
# 2. Marcar un rango como bold con [b] (indica posición y longitud)
# 3. Guardar con [r] → genera archivo .ed.rich
```

---

## Profiling y benchmarking

```bash
# Dar permisos de ejecución al script
chmod +x benchmark.sh

# Ejecutar el benchmark completo
./benchmark.sh
```

El script realiza:
1. Compila `classic_io.c` si no existe el binario
2. Ejecuta `strace -c` sobre ambos programas y muestra la tabla de syscalls
3. Mide tiempo con `time` (separa `real`, `user`, `sys`)
4. Compara tamaños en disco
5. Imprime una tabla resumen para el informe

Para un análisis manual más detallado:

```bash
# Contar syscalls del editor
strace -c ./editor < /tmp/mi_input.txt 2>&1

# Contar syscalls del enfoque clásico
strace -c ./classic 2>&1

# Medir tiempo separando CPU de I/O
time ./editor < /tmp/mi_input.txt
```

---


---

## Criptografía simétrica — Pipeline RLE → AES-128-CBC

### Activar el modo seguro

```bash
# En el menú del editor:
[k]  → ingresar passphrase (la llave se deriva y se guarda en RAM)
[e]  → guardar con pipeline completo: texto → RLE → AES-128-CBC → disco
```

Al iniciar, el editor bloquea la página de RAM que contiene la llave con `mlock()`, impidiendo que el kernel la envíe al área de Swap:

```
[SEGURIDAD] Página de llave bloqueada en RAM (no va a Swap).
```

### Orden de transformaciones (crítico)

```
CORRECTO:   texto plano → rle_compress() → aes128_cbc_encrypt() → write()
INCORRECTO: texto plano → aes128_cbc_encrypt() → rle_compress() → write()
```

AES produce salida de alta entropía (ruido uniforme). Comprimir después de cifrar es inútil porque no hay patrones. Comprimir primero aprovecha la baja entropía del texto natural y reduce el bus I/O en ~70%.

### Gestión segura de la llave

| Técnica | Dónde se aplica | Por qué |
|---|---|---|
| `volatile uint8_t*` en `secure_zero()` | Passphrase, round keys, CryptoContext | Previene eliminación del `memset` por el compilador `-O2` |
| `mlock(&crypto_ctx)` en `main.c` | Estructura con la llave AES | Prohíbe que el kernel envíe la página al Swap en disco |
| `munlock()` + `crypto_clear()` al salir | Final del programa | Borra la llave y libera la página |

La llave nunca se escribe al disco. Existe únicamente en DRAM volátil durante la sesión del proceso.

### Formato del archivo `.sec`

```
[ SecureHeader (bytes 0-N) ]
  magic           : MAGIC_SECURE
  original_size   : tamaño del texto original
  compressed_size : tamaño tras RLE
  encrypted_size  : tamaño tras AES (múltiplo de 16, PKCS#7)
  algorithm       : ALGO_RLE_AES
  checksum        : CRC32 del ciphertext
  iv[16]          : IV aleatorio de /dev/urandom (no es secreto)

[ Payload: ciphertext AES-128-CBC ]
```

El IV se almacena en el header porque es necesario para descifrar y no compromete la confidencialidad por sí solo. Sin la llave, el ciphertext es ruido ilegible.

## Verificación de memoria con Valgrind

```bash
make valgrind
# equivalente a:
valgrind --leak-check=full --show-leak-kinds=all ./editor
```

El editor libera toda la memoria antes de salir (`gap_free()` en `main.c`). Una ejecución limpia debe mostrar:

```
LEAK SUMMARY:
   definitely lost: 0 bytes in 0 blocks
   indirectly lost: 0 bytes in 0 blocks
```

---

## Decisiones de diseño

### ¿Por qué Gap Buffer y no lista enlazada?

El Gap Buffer garantiza inserción O(1) en la posición del cursor sin fragmentación de memoria — todo el texto vive en un único bloque contiguo de RAM, lo que es amigable con el caché del procesador. Una lista enlazada tendría mejor complejidad asintótica para inserciones en posiciones arbitrarias, pero con peor localidad de referencia.

### ¿Por qué `posix_memalign` y no `malloc`?

El kernel de Linux trabaja con páginas de 4096 bytes. Cuando `write()` recibe un buffer no alineado, el kernel puede necesitar copias adicionales entre páginas. Con `posix_memalign(PAGE_SIZE)` el buffer de escritura queda alineado a los límites de página, reduciendo el trabajo interno del kernel durante la transferencia al page cache.

### ¿Por qué un solo `write()` y no múltiples?

Cada llamada a `write()` es un context switch User→Kernel→User. Un editor que escribe carácter a carácter genera miles de context switches por operación de guardado. Nuestro diseño construye el payload completo en RAM y lo entrega al kernel en una sola llamada, lo que `strace -c` confirma con un conteo de `write()` cercano a 1.

### ¿`write()` vs `mmap()`?

`write()` con buffer alineado es predecible y explícito. `mmap()` delega la decisión de cuándo hacer flush al kernel (dirty pages), lo que puede resultar en menor latencia percibida pero menos control. El benchmark mide ambos y permite comparar empíricamente en el hardware específico.

### ¿Por qué `__attribute__((packed))` en los structs?

Sin `packed`, el compilador agrega bytes de relleno entre campos para satisfacer requisitos de alineación del procesador. Esto haría que el `FileHeader` ocupe más de 20 bytes y el layout en disco no sería determinístico entre compiladores o arquitecturas. `__attribute__((packed))` garantiza que cada campo ocupa exactamente sus bytes declarados, y `_Static_assert` verifica esto en tiempo de compilación.
### ¿Por qué `mlock()` y no solo `memset` para proteger la llave?

Un `memset` convencional puede ser eliminado por el compilador con optimización `-O2` si detecta que el buffer no se lee después (código muerto). Incluso si el `memset` se ejecuta, sin `mlock()` el sistema operativo puede haber enviado esa página al Swap antes de que la borráramos, dejando la llave en disco. `mlock()` es la única garantía de que la página nunca abandonó la DRAM.

### ¿Por qué `volatile` en `secure_zero()` y no `explicit_bzero()`?

`explicit_bzero()` es la solución estándar en sistemas modernos y hace exactamente esto. La implementación manual con `volatile uint8_t*` es equivalente y didácticamente más clara: evidencia directa de por qué el compilador no puede eliminar la escritura. Ambas soluciones son correctas; la implementación propia demuestra comprensión del mecanismo subyacente.