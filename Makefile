CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -pedantic -g
LDFLAGS :=

TARGET  := test_pool
SRCS    := pool.c test_pool.c
OBJS    := $(SRCS:.c=.o)
HDRS    := pool.h

VALGRIND_FLAGS := --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1

.PHONY: all test valgrind asan clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET)

valgrind: $(TARGET)
	valgrind $(VALGRIND_FLAGS) ./$(TARGET)

asan: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(SRCS) -o $(TARGET)_asan
	./$(TARGET)_asan
	rm -f $(TARGET)_asan

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_asan
