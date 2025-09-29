# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2025 Niko Ruohonen and contributors

# Simple Makefile for Split_kMeans (all sources in this folder)

# Output
OUT       ?= split_kmeans

# Toolchain / flags
CC        ?= gcc
CSTD      ?= -std=c11
CPPFLAGS  ?=
OPTFLAGS  ?= -O3
WARNFLAGS ?= -Wall -Wextra -Wpedantic
WERROR    ?=                         # leave empty; CI can set -Werror
CFLAGS    ?= $(OPTFLAGS) $(CSTD) $(WARNFLAGS) $(WERROR)
LDFLAGS   ?=
LDLIBS    ?= -lm

# Toggle threads: make THREADS=1 to enable
THREADS   ?= 0
ifeq ($(THREADS),1)
  CFLAGS  += -pthread
  LDLIBS  += -pthread
endif

# Sources / objects / deps
SRC  := $(wildcard *.c)
OBJ  := $(SRC:.c=.o)
DEPS := $(OBJ:.o=.d)

.PHONY: all clean run debug asan ubsan
.DEFAULT_GOAL := all

all: $(OUT)

# Link
$(OUT): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Compile with automatic dependency generation
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Run with optional args: make run ARGS="..."
run: $(OUT)
	./$(OUT) $(ARGS)

# Convenience builds
debug: CFLAGS += -O0 -g3
debug: clean all

asan: CFLAGS += -O1 -g -fsanitize=address
asan: LDFLAGS += -fsanitize=address
asan: clean all

ubsan: CFLAGS += -O1 -g -fsanitize=undefined
ubsan: LDFLAGS += -fsanitize=undefined
ubsan: clean all

clean:
	$(RM) -f $(OUT) $(OBJ) $(DEPS)

# Include generated dependency files if present
-include $(DEPS)
