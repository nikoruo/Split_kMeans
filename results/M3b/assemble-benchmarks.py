# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2025 Niko Ruohonen and contributors

import csv
import math
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "results" / "M3b"
SOURCES = [
    ("before-phase5", "300b06a-dirty", "300b06a-dirty", ROOT / "results" / "bench-before" / "summary.csv"),
    ("after-phase5", "300b06a-dirty", "300b06a-dirty", ROOT / "results" / "bench-after" / "summary.csv"),
    ("after-phase6b", "7836c4a-dirty", "7836c4a", ROOT / "results" / "bench-phase6" / "summary.csv"),
    ("after-phase7", "06f6228", "06f6228", OUT / "benchmark-summary.csv"),
]


def sum_times(values):
    total = 0.0
    for value in values:
        total += value
    return total


def read_benchmark(path, count):
    with path.open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file, delimiter=";")
        expected = ["dataset", "algorithm", "run", "seed", "sse", "ci",
                    "primary_best_ms", "reference_best_ms"]
        if reader.fieldnames != expected:
            raise ValueError(f"{path}: unexpected benchmark columns")
        rows = list(reader)
    if len(rows) != count:
        raise ValueError(f"{path}: expected {count} benchmark rows")
    keyed = {(row["dataset"], row["algorithm"], row["run"]): row for row in rows}
    if len(keyed) != count:
        raise ValueError(f"{path}: duplicate benchmark identity")
    return keyed


identities = {}
common_keys = None
table = []
algorithm_table = []
for phase, primary_revision, reference_revision, path in SOURCES:
    count = 64 if phase in ("after-phase6b", "after-phase7") else 61
    keyed = read_benchmark(path, count)
    original = {key: value for key, value in keyed.items() if key[0] != "a3"}
    if len(original) != 61:
        raise ValueError(f"{path}: original four-job set is incomplete")
    if common_keys is None:
        common_keys = set(original)
    if set(original) != common_keys:
        raise ValueError(f"{path}: original job identities differ")
    if count == 64 and {key for key in keyed if key[0] == "a3"} != {
        ("a3", "SKM_Global", str(run)) for run in range(1, 4)
    }:
        raise ValueError(f"{path}: added a3 job is incomplete")
    identities[phase] = keyed
    source = "\\".join(path.relative_to(ROOT).parts)
    for compiler, time_field, revision in (("MinGW", "primary_best_ms", primary_revision),
                                           ("MSVC", "reference_best_ms", reference_revision)):
        groups = defaultdict(list)
        algorithms = defaultdict(list)
        for key, row in original.items():
            value = float(row[time_field])
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"{path}: invalid timing")
            groups[key[0]].append(value)
            groups["ALL"].append(value)
            algorithms[key[1]].append(value)
        for dataset, values in sorted(groups.items()):
            table.append(["original", phase, revision, compiler, dataset, len(values),
                          f"{sum_times(values):.3f}", source])
        for algorithm, values in sorted(algorithms.items()):
            algorithm_table.append([phase, revision, compiler, algorithm, len(values),
                                    f"{sum_times(values):.3f}", source])
        if count == 64:
            values = [float(row[time_field]) for key, row in keyed.items() if key[0] == "a3"]
            if any(not math.isfinite(value) or value < 0 for value in values):
                raise ValueError(f"{path}: invalid a3 timing")
            table.append(["additional", phase, revision, compiler, "a3", len(values),
                          f"{sum_times(values):.3f}", source])

for key in common_keys:
    if len({identities[phase][key]["seed"] for phase, _, _, _ in SOURCES}) != 1:
        raise ValueError(f"Benchmark seed changed for {key}")
for first, second, keys in (
    ("before-phase5", "after-phase5", common_keys),
    ("after-phase6b", "after-phase7", set(identities["after-phase6b"])),
):
    if len(keys) == 64 and keys != set(identities[second]):
        raise ValueError(f"{first} and {second}: job sets differ")
    for key in keys:
        if any(identities[first][key][field] != identities[second][key][field] for field in ("seed", "sse", "ci")):
            raise ValueError(f"{first} and {second}: identity differs for {key}")

with (OUT / "benchmark-table.csv").open("w", newline="", encoding="utf-8") as file:
    writer = csv.writer(file, delimiter=";", lineterminator="\n")
    writer.writerow(["job_set", "phase", "source_commit", "compiler", "dataset", "runs", "sum_min_ms", "source"])
    writer.writerows(table)
with (OUT / "benchmark-algorithms.csv").open("w", newline="", encoding="utf-8") as file:
    writer = csv.writer(file, delimiter=";", lineterminator="\n")
    writer.writerow(["phase", "source_commit", "compiler", "algorithm", "runs", "sum_min_ms", "source"])
    writer.writerows(algorithm_table)
print("Validated 61 Phase 5 and 64 Phase 6b/7 benchmark identities.")
