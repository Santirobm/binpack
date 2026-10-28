CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=gnu99 -Ideps/include -D_POSIX_C_SOURCE=199309L

ifeq ($(OS),Windows_NT)
    LDFLAGS ?= -Ldeps/lib -lzstd
else
    LDFLAGS ?= -lzstd -lm
endif
TARGET = binpack
TEST_TARGET = test_native

all: $(TARGET) $(TEST_TARGET) libbinpack.a

$(TARGET): binpack.c binpack_cli.c binpack.h
	$(CC) $(CFLAGS) binpack.c binpack_cli.c $(LDFLAGS) -o $(TARGET)

$(TEST_TARGET): binpack.c test_native.c binpack.h
	$(CC) $(CFLAGS) binpack.c test_native.c $(LDFLAGS) -o $(TEST_TARGET)

libbinpack.a: binpack.o
	ar rcs libbinpack.a binpack.o

binpack.o: binpack.c binpack.h
	$(CC) $(CFLAGS) -c binpack.c -o binpack.o

clean:
	rm -f $(TARGET) $(TEST_TARGET) *.o *.a *.so *.dll *.exe

.PHONY: all clean
