# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2025 Niko Ruohonen and contributors
#
# Makefile — Split_kMeans
# --------------------------------------------------------------------
# Overview
#   Simple GNU Make build for a single-binary C project in this folder.
#
# Requirements
#   - GNU Make (tested with 3.81+)
#   - GCC/Clang toolchain
#
# Common variables (override via environment or command line):
#   OUT       - output binary name (default: split_kmeans)
#   CC        - C compiler (default: gcc)
#   CSTD      - C standard (default: -std=c11)
#   OPTFLAGS  - optimization flags (default: -O3)
#   WARNFLAGS - warning flags (default: -Wall -Wextra -Wpedantic)
#   WERROR    - empty by default; set to -Werror in CI if desired
#   THREADS   - 1 to enable pthreads (adds -pthread), 0 otherwise
#   ARGS      - arguments passed to the binary via `make run ARGS="..."`
#
# Targets
#   all (default)   - build the binary
#   run             - run ./$(OUT) with optional ARGS
#   debug           - rebuild with -O0 -g3
#   asan            - rebuild with AddressSanitizer
#   ubsan           - rebuild with UndefinedBehaviorSanitizer
#   clean           - remove outputs and deps
#
# Usage examples
#   make
#   make THREADS=1
#   make debug
#   make run ARGS="--help"
#
# Changelog
#   Version 1.0.0 - 22-10-2025 by Niko Ruohonen
#	-Initial version of this Makefile
# ----------------------------------------------------
#   Version 1.1...
#
#

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