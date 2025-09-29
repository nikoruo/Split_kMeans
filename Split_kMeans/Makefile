# Makefile for compiling and running Split_kMeans when all sources are in this folder

CC       ?= gcc
CSTD     ?= -std=c11
OPTFLAGS ?= -O3
WARNFLAGS?= -Wall -Wextra -Wpedantic
WERROR   ?=                  # set to -Werror in CI if desired

CFLAGS   ?= $(OPTFLAGS) $(CSTD) $(WARNFLAGS) $(WERROR)
SRC      := $(wildcard *.c)
OUT      ?= split_kmeans
LDLIBS   := -lm
# LDLIBS += -pthread  # uncomment if threads are used

.PHONY: all clean run

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDLIBS)

run: $(OUT)
	./$(OUT)

clean:
	$(RM) $(OUT)