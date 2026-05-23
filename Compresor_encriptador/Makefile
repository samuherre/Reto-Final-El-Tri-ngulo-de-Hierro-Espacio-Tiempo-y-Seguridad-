CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
SOURCES = main.c editor.c compress.c io.c crypto.c
OBJECTS = $(SOURCES:.c=.o)
TARGET  = editor

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Ejecutar con valgrind para detectar memory leaks
valgrind:
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# Benchmark: comparar write() vs mmap con strace
bench:
	@echo "=== Benchmark write() ===" 
	strace -c ./$(TARGET) < benchmark_input.txt 2>&1 | grep -E "write|read|mmap|total"
	@echo ""
	@echo "=== Medir tiempo ==="
	time ./$(TARGET) < benchmark_input.txt

clean:
	rm -f $(OBJECTS) $(TARGET) *.ed *.ed.mmap

.PHONY: all clean valgrind bench
