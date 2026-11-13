# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2025 Niko Ruohonen and contributors
#
# Split_kMeans build (GNU Make 4.3+, gcc or clang; on Windows mingw32-make from MinGW-w64).
# Run `make help` for targets and variables.

.DEFAULT_GOAL := all

# --- Configuration --------------------------------------------------------
# `make asan check` must build and test the same configuration, so the goal picks it.
ifneq ($(filter asan,$(MAKECMDGOALS)),)
  CONFIG := asan
else ifneq ($(filter ubsan,$(MAKECMDGOALS)),)
  CONFIG := ubsan
else ifneq ($(filter debug,$(MAKECMDGOALS)),)
  CONFIG := debug
else
  CONFIG := release
endif

ifeq ($(origin CC),default)
  CC := gcc
endif

ifeq ($(OS),Windows_NT)
  EXE   := .exe
  NULLDEV := NUL
  fixpath = $(subst /,\,$1)
  MKDIR = if not exist $(call fixpath,$1) mkdir $(call fixpath,$1)
  RMDIR = if exist $(call fixpath,$1) rmdir /s /q $(call fixpath,$1)
else
  EXE   :=
  NULLDEV := /dev/null
  fixpath = $1
  MKDIR = mkdir -p $1
  RMDIR = rm -rf $1
endif

BUILD_DIR := build/$(CONFIG)
BIN       := $(BUILD_DIR)/split_kmeans$(EXE)
SRC       := $(wildcard src/*.c)
OBJ       := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEP       := $(OBJ:.o=.d)

GEN_DATA      := $(BUILD_DIR)/gen_data$(EXE)
COMPARE       := $(BUILD_DIR)/compare_runs$(EXE)
TEST_DIR      := build/testdata
TESTDATA_DIRS := $(foreach g,main edge pending,$(foreach d,data gt centroids,$(TEST_DIR)/$(g)/$(d)))

# --- Regression tests: fixed seed, golden <Algo>_runs.csv per dataset --------
# The runs write to $(CHECK_ROOT)/check, which is deleted first. Inside a OneDrive folder the
# per-swap tracking files hit sharing violations, so pass for example CHECK_ROOT=C:/Temp there.
CHECK_ROOT   ?= build
CHECK_DIR    := $(CHECK_ROOT)/check
# run_in starts three folders below the repo root, so relative roots get a ../../../ prefix.
CHECK_OUT    := $(if $(or $(filter /%,$(CHECK_ROOT)),$(findstring :,$(CHECK_ROOT))),$(CHECK_DIR),../../../$(CHECK_DIR))
CHECK_SEED   := 20251026
CHECK_ALGOS  := km,skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting
# Bisecting hangs on e02_k_equals_n (it picks a one-point cluster) until plan step 2.4.
EDGE_ALGOS   := km,skm-random,skm-intra,skm-global,skm-global-basic,skm-local
MAIN_SETS    := s01_blobs2d s02_blobs8d s03_overlap s04_unbalanced s05_k15
EDGE_SETS    := e01_k1 e02_k_equals_n e03_duplicates e04_tiny e05_constant_dim e06_highdim e07_k2
GOLDEN_ALGOS := KM RKM RS SKM_Random SKM_Intra SKM_Global SKM_Global_Basic SKM_Local Bisecting_KM
GOLDEN_EDGE  := $(filter-out Bisecting_KM,$(GOLDEN_ALGOS))
GOLDEN_FILES := $(foreach s,$(MAIN_SETS),$(foreach a,$(GOLDEN_ALGOS),main/$(s)/$(a)_runs.csv)) \
                $(foreach s,$(EDGE_SETS),$(foreach a,$(GOLDEN_EDGE),edge/$(s)/$(a)_runs.csv))
GOLDEN_DIRS  := $(addprefix tests/golden/main/,$(MAIN_SETS)) $(addprefix tests/golden/edge/,$(EDGE_SETS))

# Runs the program inside a test-data group so batch mode finds data/, gt/ and centroids/.
run_in = cd $(call fixpath,$(TEST_DIR)/$1) && $(call fixpath,../../$(CONFIG)/split_kmeans$(EXE))

# --- Flags (user CFLAGS, CPPFLAGS, LDFLAGS, LDLIBS are appended) -----------
BASE_CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic
BASE_CPPFLAGS :=
BASE_LDFLAGS  :=
BASE_LDLIBS   := -lm

ifeq ($(CONFIG),release)
  BASE_CFLAGS   += -O3
  BASE_CPPFLAGS += -DNDEBUG
else ifeq ($(CONFIG),debug)
  BASE_CFLAGS   += -O0 -g3
else ifeq ($(CONFIG),asan)
  BASE_CFLAGS   += -O1 -g -fno-omit-frame-pointer -fsanitize=address
  BASE_LDFLAGS  += -fsanitize=address
else ifeq ($(CONFIG),ubsan)
  BASE_CFLAGS   += -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined
  BASE_LDFLAGS  += -fsanitize=undefined
endif

ifeq ($(WERROR),1)
  BASE_CFLAGS += -Werror
endif
ifeq ($(NATIVE),1)
  BASE_CFLAGS += -march=native
endif
ifeq ($(THREADS),1)
  BASE_CFLAGS += -pthread
  BASE_LDLIBS += -pthread
endif

ALL_CFLAGS   := $(BASE_CFLAGS) $(CFLAGS)
ALL_CPPFLAGS := $(BASE_CPPFLAGS) $(CPPFLAGS)
ALL_LDFLAGS  := $(BASE_LDFLAGS) $(LDFLAGS)
ALL_LDLIBS   := $(BASE_LDLIBS) $(LDLIBS)

# --- Targets ----------------------------------------------------------------
.PHONY: all run check check-outputs debug asan ubsan bench clean help golden-update testdata

all: $(BIN)

debug asan ubsan: all

$(BIN): $(OBJ)
	$(CC) $(ALL_LDFLAGS) $^ -o $@ $(ALL_LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	$(call MKDIR,$@)

$(GEN_DATA): tests/gen_data.c src/rng.c src/rng.h | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -Isrc tests/gen_data.c src/rng.c -o $@ $(ALL_LDFLAGS) $(ALL_LDLIBS)

$(COMPARE): tests/compare_runs.c | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) tests/compare_runs.c -o $@ $(ALL_LDFLAGS) $(ALL_LDLIBS)

$(TESTDATA_DIRS):
	$(call MKDIR,$@)

# Integer-only synthetic datasets for the regression tests
testdata: $(GEN_DATA) | $(TESTDATA_DIRS)
	$(call fixpath,$(GEN_DATA)) $(TEST_DIR)

# Runs from the repo root so batch mode finds data/, gt/ and centroids/.
run: $(BIN)
	$(call fixpath,$(BIN)) $(ARGS)

# RKM and RS run with their default single run; the other algorithms run 5 times.
check-outputs: $(BIN) testdata
	$(call RMDIR,$(CHECK_DIR))
	$(call MKDIR,$(CHECK_DIR))
	$(call run_in,main) --seed $(CHECK_SEED) -r 5 --algo $(CHECK_ALGOS) -o $(CHECK_OUT)/main > $(NULLDEV)
	$(call run_in,main) --seed $(CHECK_SEED) --algo rkm,rs -o $(CHECK_OUT)/main > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) -r 5 --algo $(EDGE_ALGOS) -o $(CHECK_OUT)/edge > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) --algo rkm,rs -o $(CHECK_OUT)/edge > $(NULLDEV)

# Golden comparison, then smoke tests of the two default invocations (CLI with and without ground truth).
check: check-outputs $(COMPARE)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR) $(GOLDEN_FILES)
	$(call run_in,main) > $(NULLDEV)
	$(call run_in,main) -k 4 data/s01_blobs2d.txt gt/s01_blobs2d.txt > $(NULLDEV)
	$(call run_in,main) -k 4 data/s01_blobs2d.txt > $(NULLDEV)

$(GOLDEN_DIRS):
	$(call MKDIR,$@)

golden-update: check-outputs $(COMPARE) | $(GOLDEN_DIRS)
	$(call fixpath,$(COMPARE)) --update tests/golden $(CHECK_DIR) $(GOLDEN_FILES)

bench:
	$(error make bench arrives in plan step 5.7)

clean:
	$(call RMDIR,build)

help:
	@echo Targets:
	@echo   all            release build: build/release/split_kmeans, -O3, NDEBUG
	@echo   debug          -O0 -g3 build in build/debug, asserts on
	@echo   asan, ubsan    sanitizer builds in build/asan and build/ubsan, asserts on
	@echo   run            build and run from the repo root, arguments via ARGS="..."
	@echo   check          tests, combine with a configuration: make asan check
	@echo   golden-update  regenerate golden files, algorithm-change commits only
	@echo   bench          benchmark run
	@echo   testdata       generate the synthetic test datasets in build/testdata
	@echo   clean          remove build/
	@echo Variables:
	@echo   CC=gcc or clang, WERROR=1, NATIVE=1, THREADS=1
	@echo   CHECK_ROOT=dir  check output root, default build; dir/check is deleted first, no spaces
	@echo   CFLAGS, CPPFLAGS, LDFLAGS, LDLIBS are appended to the defaults

-include $(DEP)
