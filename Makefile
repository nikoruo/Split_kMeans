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
UNIT          := $(BUILD_DIR)/test_unit$(EXE)
BENCH_RUNNER  := $(BUILD_DIR)/bench$(EXE)
UNIT_OBJECTS  := $(addprefix $(BUILD_DIR)/,algorithms.o baselines.o dataset.o experiment.o input_identity.o kmeans.o metrics.o platform.o rng.o sha256.o split.o tracking.o util.o)
TEST_DIR      := build/testdata
TESTDATA_DIRS := $(foreach g,main edge pending,$(foreach d,data gt centroids,$(TEST_DIR)/$(g)/$(d)))

# --- Regression tests: fixed seed, golden <Algo>_runs.csv per dataset --------
# The runs write to $(CHECK_ROOT)/check, which is deleted first. Inside a OneDrive folder the
# per-swap tracking files hit sharing violations, so pass for example CHECK_ROOT=C:/Temp there.
CHECK_ROOT   ?= build
CHECK_DIR    := $(CHECK_ROOT)/check
UNIT_DIR     := $(CHECK_DIR)/unit
# CHECK_BIN runs the checks with another binary, for example the Visual Studio Release build.
CHECK_BIN    ?=
TEST_BIN     := $(if $(CHECK_BIN),$(CHECK_BIN),$(BIN))
BENCH_ROOT   ?= build/bench
ifeq ($(OS),Windows_NT)
  BENCH_DEFAULT_REF := build/msvc/x64/Release/Split_kMeans.exe
  BENCH_REF  ?= $(BENCH_DEFAULT_REF)
else
  BENCH_REF  ?= -
endif
# run_in starts three folders below the repo root, so relative paths get a ../../../ prefix.
from_run_dir = $(if $(or $(filter /%,$1),$(findstring :,$1)),$1,../../../$1)
CHECK_OUT    := $(call from_run_dir,$(CHECK_DIR))
CHECK_SEED   := 20251026
CHECK_ALGOS  := km,skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting
MAIN_SETS    := s01_blobs2d s02_blobs8d s03_overlap s04_unbalanced s05_k15
EDGE_SETS    := e01_k1 e02_k_equals_n e03_duplicates e04_tiny e05_constant_dim e06_highdim e07_k2
GOLDEN_ALGOS := KM RKM RS SKM_Random SKM_Intra SKM_Global SKM_Global_Basic SKM_Local Bisecting_KM
GOLDEN_EDGE  := $(foreach s,$(EDGE_SETS),$(foreach a,$(GOLDEN_ALGOS),edge/$(s)/$(a)_runs.csv))
GOLDEN_FILES := $(foreach s,$(MAIN_SETS),$(foreach a,$(GOLDEN_ALGOS),main/$(s)/$(a)_runs.csv)) $(GOLDEN_EDGE)
REPARTITION_MODES := inward inward-nonchild both
REPARTITION_FILES := $(foreach m,$(REPARTITION_MODES),modes/$(m)/s03_overlap/SKM_Local_runs.csv)
BISECT_SIZE_FILE := modes/bisect-size/s03_overlap/Bisecting_KM_runs.csv
GOLDEN_DIRS  := $(addprefix tests/golden/main/,$(MAIN_SETS)) $(addprefix tests/golden/edge/,$(EDGE_SETS)) tests/golden/report $(foreach m,$(REPARTITION_MODES),tests/golden/modes/$(m)/s03_overlap) tests/golden/modes/bisect-size/s03_overlap
# The report of the main group without time columns (whole lines are compared)
REPORT_FILES := $(addprefix report/,summary.csv table_ci.csv table_success.csv table_sse.csv $(foreach s,$(MAIN_SETS),curves_$(s).csv))
# Tracking, snapshots and logging must not change the results: the edge group runs again with each.
VARIANT_notrack   := --no-track
VARIANT_snapshots := --snapshots all:all:split --track-inner
VARIANT_log3      := --log-level 3
VARIANTS     := notrack snapshots log3

# Runs the program inside a test-data group so batch mode finds data/, gt/ and centroids/.
run_in = cd $(call fixpath,$(TEST_DIR)/$1) && $(call fixpath,$(call from_run_dir,$(TEST_BIN)))

# --- Flags (user CFLAGS, CPPFLAGS, LDFLAGS, LDLIBS are appended) -----------
# -ffp-contract=off: no fused multiply-add, so every compiler and target rounds the same way.
BASE_CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -ffp-contract=off
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

ALL_CFLAGS   := $(BASE_CFLAGS) $(CFLAGS)
ALL_CPPFLAGS := $(BASE_CPPFLAGS) $(CPPFLAGS)
ALL_LDFLAGS  := $(BASE_LDFLAGS) $(LDFLAGS)
ALL_LDLIBS   := $(BASE_LDLIBS) $(LDLIBS)

# --- Build information for run_config.txt: git commit and compile command ----
# build_info.h is rewritten only when its text changes, so its users recompile after a commit or a flag change.
BUILD_INFO := $(BUILD_DIR)/build_info.h
hash := \#
define newline


endef
ifneq ($(filter-out clean help,$(or $(MAKECMDGOALS),all)),)
  GIT_COMMIT := $(shell git rev-parse --short HEAD 2>$(NULLDEV))
  # -dirty: changed tracked files, or files in src/ that git does not track yet (the build compiles every src/*.c)
  GIT_DIRTY  := $(if $(strip $(shell git status --porcelain --untracked-files=no 2>$(NULLDEV))$(shell git ls-files --others --exclude-standard -- src 2>$(NULLDEV))),-dirty)
  BUILD_INFO_TEXT := $(hash)define SKM_GIT_COMMIT "$(or $(GIT_COMMIT),unknown)$(GIT_DIRTY)"$(newline)$(hash)define SKM_BUILD_FLAGS "$(subst ",\",$(strip $(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS)))"
  ifneq ($(file <$(BUILD_INFO)),$(BUILD_INFO_TEXT))
    $(shell $(call MKDIR,$(BUILD_DIR)))
    $(file >$(BUILD_INFO),$(BUILD_INFO_TEXT))
  endif
endif

# --- Targets ----------------------------------------------------------------
.PHONY: all run check check-outputs check-variants check-modes debug asan ubsan bench clean help golden-update testdata

all: $(BIN)

debug asan ubsan: all

$(BIN): $(OBJ)
	$(CC) $(ALL_LDFLAGS) $^ -o $@ $(ALL_LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -DSKM_BUILD_INFO -I$(BUILD_DIR) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	$(call MKDIR,$@)

$(GEN_DATA): tests/gen_data.c src/rng.c src/rng.h | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -Isrc tests/gen_data.c src/rng.c -o $@ $(ALL_LDFLAGS) $(ALL_LDLIBS)

$(COMPARE): tests/compare_runs.c | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) tests/compare_runs.c -o $@ $(ALL_LDFLAGS) $(ALL_LDLIBS)

$(UNIT): tests/test_unit.c $(UNIT_OBJECTS) | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -Isrc $^ -o $@ $(ALL_LDFLAGS) $(ALL_LDLIBS)

$(BENCH_RUNNER): tests/bench.c src/platform.c src/platform.h | $(BUILD_DIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -Isrc tests/bench.c src/platform.c -o $@ $(ALL_LDFLAGS) $(ALL_LDLIBS)

$(TESTDATA_DIRS):
	$(call MKDIR,$@)

# Integer-only synthetic datasets for the regression tests
testdata: $(GEN_DATA) | $(TESTDATA_DIRS)
	$(call fixpath,$(GEN_DATA)) $(TEST_DIR)

# Runs from the repo root so batch mode finds data/, gt/ and centroids/.
run: $(BIN)
	$(call fixpath,$(BIN)) $(ARGS)

# RKM and RS run with their default single run; the other algorithms run 5 times.
check-outputs: $(if $(CHECK_BIN),,$(BIN)) testdata
	$(call RMDIR,$(CHECK_DIR))
	$(call MKDIR,$(CHECK_DIR))
	$(call run_in,main) --seed $(CHECK_SEED) -r 5 --algo $(CHECK_ALGOS) -o $(CHECK_OUT)/main > $(NULLDEV)
	$(call run_in,main) --seed $(CHECK_SEED) --algo rkm,rs -o $(CHECK_OUT)/main > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) -r 5 --algo $(CHECK_ALGOS) -o $(CHECK_OUT)/edge > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) --algo rkm,rs -o $(CHECK_OUT)/edge > $(NULLDEV)

# The edge runs again with each tracking variant, into $(CHECK_DIR)/<variant>/edge.
check-variants: check-outputs
	$(call MKDIR,$(CHECK_DIR)/notrack)
	$(call MKDIR,$(CHECK_DIR)/snapshots)
	$(call MKDIR,$(CHECK_DIR)/log3)
	$(call run_in,edge) --seed $(CHECK_SEED) -r 5 --algo $(CHECK_ALGOS) $(VARIANT_notrack) -o $(CHECK_OUT)/notrack/edge > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) --algo rkm,rs $(VARIANT_notrack) -o $(CHECK_OUT)/notrack/edge > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) -r 5 --algo $(CHECK_ALGOS) $(VARIANT_snapshots) -o $(CHECK_OUT)/snapshots/edge > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) --algo rkm,rs $(VARIANT_snapshots) -o $(CHECK_OUT)/snapshots/edge > $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) -r 5 --algo $(CHECK_ALGOS) $(VARIANT_log3) -o $(CHECK_OUT)/log3/edge > $(NULLDEV) 2> $(NULLDEV)
	$(call run_in,edge) --seed $(CHECK_SEED) --algo rkm,rs $(VARIANT_log3) -o $(CHECK_OUT)/log3/edge > $(NULLDEV) 2> $(NULLDEV)

check-modes: check-outputs
	$(call run_in,main) --seed $(CHECK_SEED) --datasets s03_overlap --algo skm-local -r 5 --max-iterations 1 --repartition inward --snapshots none -o $(CHECK_OUT)/modes/inward > $(NULLDEV)
	$(call run_in,main) --seed $(CHECK_SEED) --datasets s03_overlap --algo skm-local -r 5 --max-iterations 1 --repartition inward-nonchild --snapshots none -o $(CHECK_OUT)/modes/inward-nonchild > $(NULLDEV)
	$(call run_in,main) --seed $(CHECK_SEED) --datasets s03_overlap --algo skm-local -r 5 --max-iterations 1 --repartition both --snapshots none -o $(CHECK_OUT)/modes/both > $(NULLDEV)
	$(call run_in,main) --seed $(CHECK_SEED) --datasets s03_overlap --algo bisecting -r 5 --max-iterations 1 --bisect-select size --snapshots none -o $(CHECK_OUT)/modes/bisect-size > $(NULLDEV)

# Unit tests (fixtures in $(UNIT_DIR)), golden comparison (also of the tracking variants and of the
# report), then smoke tests of the two default invocations (CLI with and without ground truth) and of
# the subcommands.
check: check-variants check-modes $(COMPARE) $(UNIT)
	$(call MKDIR,$(UNIT_DIR))
	$(call run_in,main) --seed $(CHECK_SEED) --datasets s01_blobs2d --algo km -r 1 --max-iterations 1 --snapshots none -o $(CHECK_OUT)/cap-batch > $(NULLDEV)
	$(call fixpath,$(UNIT)) $(call fixpath,$(UNIT_DIR)) $(call fixpath,$(TEST_BIN)) $(call fixpath,$(TEST_DIR)/main/data/s01_blobs2d.txt) $(call fixpath,$(TEST_DIR)/main/gt/s01_blobs2d.txt) $(call fixpath,$(CHECK_DIR)/cap-batch)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR) $(GOLDEN_FILES)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR)/notrack $(GOLDEN_EDGE)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR)/snapshots $(GOLDEN_EDGE)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR)/log3 $(GOLDEN_EDGE)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR) $(REPARTITION_FILES)
	$(call fixpath,$(COMPARE)) tests/golden $(CHECK_DIR) $(BISECT_SIZE_FILE)
	$(call run_in,main) report --no-time -o $(CHECK_OUT)/report $(CHECK_OUT)/main > $(NULLDEV)
	$(call fixpath,$(COMPARE)) --whole tests/golden $(CHECK_DIR) $(REPORT_FILES)
	$(call run_in,main) > $(NULLDEV)
	$(call run_in,main) -k 4 data/s01_blobs2d.txt gt/s01_blobs2d.txt > $(NULLDEV)
	$(call run_in,main) -k 4 data/s01_blobs2d.txt > $(NULLDEV)
	$(call run_in,main) ci gt/s01_blobs2d.txt gt/s01_blobs2d.txt > $(NULLDEV)
	$(call run_in,main) sse data/s01_blobs2d.txt gt/s01_blobs2d.txt > $(NULLDEV)

$(GOLDEN_DIRS):
	$(call MKDIR,$@)

golden-update: check-modes $(COMPARE) | $(GOLDEN_DIRS)
	$(call fixpath,$(COMPARE)) --update tests/golden $(CHECK_DIR) $(GOLDEN_FILES)
	$(call fixpath,$(COMPARE)) --update tests/golden $(CHECK_DIR) $(REPARTITION_FILES)
	$(call fixpath,$(COMPARE)) --update tests/golden $(CHECK_DIR) $(BISECT_SIZE_FILE)
	$(call run_in,main) report --no-time -o $(CHECK_OUT)/report $(CHECK_OUT)/main > $(NULLDEV)
	$(call fixpath,$(COMPARE)) --update --whole tests/golden $(CHECK_DIR) $(REPORT_FILES)

bench: $(BIN) $(BENCH_RUNNER)
	$(if $(filter release,$(CONFIG)),,$(error bench requires a release build; run make bench separately))
	$(call fixpath,$(BENCH_RUNNER)) "$(call fixpath,$(BIN))" "$(call fixpath,$(BENCH_REF))" "$(call fixpath,$(BENCH_ROOT))" $(NULLDEV)

ifeq ($(OS),Windows_NT)
ifeq ($(subst \,/,$(BENCH_REF)),$(BENCH_DEFAULT_REF))
$(BENCH_DEFAULT_REF): $(SRC) $(wildcard src/*.h) msvc/Split_kMeans.vcxproj Split_kMeans.sln
	@echo Build Visual Studio 2026 Release x64 before running make bench.
	@exit 1
bench: $(BENCH_DEFAULT_REF)
endif
endif

# Removes the Makefile configurations, test data and build/check; build/msvc stays.
clean:
	$(call RMDIR,build/release)
	$(call RMDIR,build/debug)
	$(call RMDIR,build/asan)
	$(call RMDIR,build/ubsan)
	$(call RMDIR,$(TEST_DIR))
	$(call RMDIR,build/check)

help:
	@echo Targets:
	@echo   all            release build: build/release/split_kmeans, -O3, NDEBUG
	@echo   debug          -O0 -g3 build in build/debug, asserts on
	@echo   asan, ubsan    sanitizer builds in build/asan and build/ubsan, asserts on
	@echo   run            build and run from the repo root, arguments via ARGS="..."
	@echo   check          tests, combine with a configuration: make asan check
	@echo   golden-update  regenerate golden files, algorithm-change commits only
	@echo   bench          benchmark release builds on local real datasets
	@echo   testdata       generate the synthetic test datasets in build/testdata
	@echo   clean          remove Makefile builds, build/testdata and build/check; build/msvc stays
	@echo Variables:
	@echo   CC=gcc or clang, WERROR=1
	@echo   CHECK_ROOT=dir  check output root, default build; dir/check is deleted first, no spaces
	@echo   CHECK_BIN=exe   run check with another binary, for example build/msvc/x64/Release/Split_kMeans.exe
	@echo   BENCH_ROOT=dir  benchmark output root, default build/bench; each run gets a new folder
	@echo   BENCH_REF=exe   compare another release binary; Windows defaults to Visual Studio, - disables
	@echo   CFLAGS, CPPFLAGS, LDFLAGS, LDLIBS are appended to the defaults

-include $(DEP)
