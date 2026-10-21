CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=c99
TARGET = test_native

all: $(TARGET) libbinpack.a

$(TARGET): binpack.c test_native.c binpack.h
	$(CC) $(CFLAGS) binpack.c test_native.c -o $(TARGET)

libbinpack.a: binpack.o
	ar rcs libbinpack.a binpack.o

binpack.o: binpack.c binpack.h
	$(CC) $(CFLAGS) -c binpack.c -o binpack.o

clean:
	rm -f $(TARGET) *.o *.a *.so *.dll *.exe

.PHONY: all clean
