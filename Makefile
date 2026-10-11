CC = gcc
CFLAGS = -Wall -Wextra -g

SRCS = main.c bitmap.c process.c filters.c
OBJS = $(SRCS:.c=.o)

DEPS = filters.h bitmap.h

TARGET = main

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean