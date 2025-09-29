# Makefile for compiling and running Split_kMeans when all sources are in this folder

CC      ?= gcc
CFLAGS  ?= -O3 -std=c11 -Wall -Wextra -Wpedantic
SRC     := $(wildcard *.c)
OUT     ?= split_kmeans
LDLIBS  := -lm
# Uncomment if threads are used
# LDLIBS += -pthread

.PHONY: all clean run

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDLIBS)

run: $(OUT)
	./$(OUT)

clean:
	$(RM) $(OUT)