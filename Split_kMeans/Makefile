# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2025 Niko Ruohonen and contributors
#
# Makefile — split_kmeans
# --------------------------------------------------------------------
# Overview
#   GNU Make build system for the Split K-Means clustering algorithm.
#   Produces a single binary from C sources in the current directory.
#
# Requirements
#   - GNU Make 3.81+ (tested with 4.3+)
#   - GCC or Clang toolchain
#   - Supported platforms: Linux, macOS, Windows (MinGW/MSYS/Cygwin)
#
# Variables (override via environment or command line)
#   OUT       - output binary name (default: split_kmeans)
#   CC        - C compiler (default: gcc)
#   CSTD      - C standard (default: -std=c11)
#   OPTFLAGS  - optimization flags (default: -O3)
#   WARNFLAGS - warning flags (default: -Wall -Wextra -Wpedantic)
#   WERROR    - empty by default; set to -Werror in CI
#   THREADS   - set to 1 to enable pthreads (adds -pthread)
#   ARGS      - runtime arguments for `make run` target
#
# Targets
#   all (default) - build the binary
#   run           - execute ./$(OUT) with optional ARGS
#   debug         - rebuild with -O0 -g3 (no optimizations)
#   asan          - rebuild with AddressSanitizer
#   ubsan         - rebuild with UndefinedBehaviorSanitizer
#   clean         - remove binary, objects, and dependency files
#
# Usage Examples
#   make                              # build release binary
#   make THREADS=1                    # build with pthread support
#   make debug                        # build for debugging
#   make run ARGS="-k 5 data.txt"     # run with arguments
#   make CC=clang OPTFLAGS=-O2        # use Clang with -O2
#
# Notes
#   - Windows builds automatically append .exe extension
#   - Dependencies are auto-generated via -MMD -MP
#   - Math library (-lm) is linked by default
#   - Use CPPFLAGS for preprocessor defines (e.g., -DDEBUG)
#
# Changelog
#   Version 1.0.0 - 26-10-2025 by Niko Ruohonen
#     - Initial version of this Makefile
# ----------------------------------------------------
#   Version 1.1...
#
#

# Output
OUT       ?= split_kmeans

# Detect Windows (MinGW/MSYS/Cygwin)
ifdef WINDIR
  EXEEXT := .exe
else
  EXEEXT :=
endif

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

all: $(OUT)$(EXEEXT)

# Link
$(OUT)$(EXEEXT): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Compile with automatic dependency generation
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Run with optional args: make run ARGS="..."
run: $(OUT)$(EXEEXT)
	./$(OUT)$(EXEEXT) $(ARGS)

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
	$(RM) -f $(OUT)$(EXEEXT) $(OUT) $(OBJ) $(DEPS)

# Include generated dependency files if present
-include $(DEPS)