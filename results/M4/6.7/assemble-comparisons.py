#!/usr/bin/env python3
"""Rebuild the labeled 6.7 summaries and paired Local comparison."""

import csv
from decimal import Decimal, localcontext
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
REPORT_FIELDS = (
    "sse_mean", "sse_sd", "ci_mean", "relative_ci", "success_pct",
    "time_ms_mean", "time_ms_sd", "full_time_ms_mean",
)


def read_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as file:
        return list(csv.DictReader(file, delimiter=";"))


def write_csv(path, fields, rows):
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fields, delimiter=";", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def config(path):
    result = {}
    with path.open(encoding="utf-8-sig") as file:
        for line in file:
            line = line.rstrip("\r\n")
            if line == "calls:":
                break
            if ": " in line and not line.startswith("#"):
                key, value = line.split(": ", 1)
                if key in result:
                    raise ValueError(f"{path}: repeated configuration field {key}")
                result[key] = value
        else:
            raise ValueError(f"{path}: missing calls: line")
    for key in ("commit", "compiler", "flags", "seed", "repartition"):
        if key not in result:
            raise ValueError(f"{path}: missing configuration field {key}")
    return result


def relative(path):
    return path.relative_to(REPO).as_posix()


def mode_for(algorithm, settings):
    if algorithm == "SKM - Local":
        return "repartition=" + settings["repartition"]
    if algorithm == "Bisecting KM":
        return "bisect-select=" + settings["bisect-select"]
    return "default"


def report_rows():
    manifest = read_csv(HERE / "input-hashes.csv")
    datasets = {row["dataset"]: row for row in manifest}
    if len(datasets) != 23:
        raise ValueError("Expected 23 distinct dataset fingerprints")
    build_info = dict(line.split(": ", 1) for line in
                      (HERE / "build-info.txt").read_text(encoding="utf-8-sig").splitlines())
    report_commit = build_info["commit"]
    sources = (
        ("prior-6.6", REPO / "results" / "M4" / "6.6-after", 83),
        ("prior-bisect-sse", REPO / "results" / "M4" / "6.6b-after" / "sse", 10),
        ("prior-bisect-size", REPO / "results" / "M4" / "6.6b-after" / "size", 10),
        ("dim-default", HERE / "dim-default", 32),
        ("local-inward", HERE / "local-inward", 23),
        ("local-both", HERE / "local-both", 23),
        ("global-worms", HERE / "global-worms", 1),
    )
    combined = []
    seen = set()
    for label, source, expected_count in sources:
        settings_path = HERE / label / "run_config.txt"
        settings = config(settings_path)
        report_path = HERE / label / "report" / "summary.csv"
        report = read_csv(report_path)
        if len(report) != expected_count:
            raise ValueError(f"{report_path}: expected {expected_count} rows, got {len(report)}")
        for row in report:
            algorithm = row["algorithm"]
            if label == "prior-6.6" and algorithm in ("SKM - Local", "Bisecting KM"):
                continue
            dataset = row["dataset"]
            if dataset not in datasets:
                raise ValueError(f"{report_path}: unknown dataset {dataset}")
            mode = mode_for(algorithm, settings)
            key = dataset, algorithm, mode
            if key in seen:
                raise ValueError(f"Duplicate comparison row: {key}")
            seen.add(key)
            combined.append({
                "dataset": dataset,
                "k": datasets[dataset]["k"],
                "algorithm": algorithm,
                "mode": mode,
                "run_count": row["runs"],
                "seed": settings["seed"],
                "commit": settings["commit"],
                "compiler": settings["compiler"],
                "build_flags": settings["flags"],
                "source_folder": relative(source),
                "source_configuration": relative(settings_path),
                "report_file": relative(report_path),
                "report_commit": report_commit,
                **{field: row[field] for field in REPORT_FIELDS},
            })
    if len(combined) != 162:
        raise ValueError(f"Expected 162 labeled rows, got {len(combined)}")
    combined.sort(key=lambda row: (row["dataset"], row["algorithm"], row["mode"]))
    fields = ("dataset", "k", "algorithm", "mode", "run_count", "seed", "commit", "compiler",
              "build_flags", "source_folder", "source_configuration", "report_file", "report_commit",
              *REPORT_FIELDS)
    write_csv(HERE / "derived-comparison.csv", fields, combined)
    return datasets, combined


def run_rows(mode, dataset):
    path = HERE / ("local-" + mode) / dataset / "SKM_Local_runs.csv"
    rows = read_csv(path)
    if len(rows) != 100:
        raise ValueError(f"{path}: expected 100 runs, got {len(rows)}")
    for i, row in enumerate(rows, 1):
        if int(row["run"]) != i or not Decimal(row["sse"]).is_finite():
            raise ValueError(f"{path}: invalid run {i}")
    return rows


def paired_local(datasets):
    inward_dir, both_dir = HERE / "local-inward", HERE / "local-both"
    inward_cfg = config(inward_dir / "run_config.txt")
    both_cfg = config(both_dir / "run_config.txt")
    if inward_cfg["repartition"] != "inward" or both_cfg["repartition"] != "both":
        raise ValueError("Unexpected Local modes")
    if {k: v for k, v in inward_cfg.items() if k != "repartition"} != {
            k: v for k, v in both_cfg.items() if k != "repartition"}:
        raise ValueError("Local configurations differ beyond repartition mode")
    summaries = {
        mode: {row["dataset"]: row for row in read_csv(HERE / ("local-" + mode) / "report" / "summary.csv")}
        for mode in ("inward", "both")
    }
    if any(set(summary) != set(datasets) for summary in summaries.values()):
        raise ValueError("Local report datasets differ from the input manifest")

    paired = []
    with localcontext() as context:
        context.prec = 50
        for dataset, metadata in sorted(datasets.items()):
            inward, both = run_rows("inward", dataset), run_rows("both", dataset)
            counts = dict(sse_lower=0, sse_equal=0, sse_higher=0, ci_lower=0, ci_equal=0, ci_higher=0,
                          inward_only_success=0, both_only_success=0)
            sse_delta = Decimal(0)
            ci_delta = 0
            for old, new in zip(inward, both):
                if old["run"] != new["run"] or old["seed"] != new["seed"]:
                    raise ValueError(f"{dataset}: unmatched run or derived seed")
                a, b = Decimal(old["sse"]), Decimal(new["sse"])
                ai, bi = int(old["ci"]), int(new["ci"])
                counts["sse_lower" if b < a else "sse_higher" if b > a else "sse_equal"] += 1
                counts["ci_lower" if bi < ai else "ci_higher" if bi > ai else "ci_equal"] += 1
                counts["inward_only_success"] += ai == 0 and bi != 0
                counts["both_only_success"] += bi == 0 and ai != 0
                sse_delta += b - a
                ci_delta += bi - ai
            a, b = summaries["inward"][dataset], summaries["both"][dataset]
            if a["runs"] != "100" or b["runs"] != "100":
                raise ValueError(f"{dataset}: incomplete Local summary")
            paired.append({
                "dataset": dataset, "k": metadata["k"], "runs": 100,
                "seed": inward_cfg["seed"], "commit": inward_cfg["commit"],
                "inward_sse_mean": a["sse_mean"], "both_sse_mean": b["sse_mean"],
                "mean_sse_delta_both_minus_inward": f"{sse_delta / 100:.6f}",
                "both_sse_lower": counts["sse_lower"], "sse_equal": counts["sse_equal"],
                "both_sse_higher": counts["sse_higher"],
                "inward_ci_mean": a["ci_mean"], "both_ci_mean": b["ci_mean"],
                "mean_ci_delta_both_minus_inward": f"{Decimal(ci_delta) / 100:.4f}",
                "both_ci_lower": counts["ci_lower"], "ci_equal": counts["ci_equal"],
                "both_ci_higher": counts["ci_higher"],
                "inward_success_pct": a["success_pct"], "both_success_pct": b["success_pct"],
                "inward_only_success": counts["inward_only_success"],
                "both_only_success": counts["both_only_success"],
                "inward_source": relative(inward_dir), "both_source": relative(both_dir),
                "inward_report": relative(inward_dir / "report" / "summary.csv"),
                "both_report": relative(both_dir / "report" / "summary.csv"),
            })
    write_csv(HERE / "paired-local.csv", tuple(paired[0]), paired)


def paired_global_basic(rows):
    candidates = {}
    for row in rows:
        if row["algorithm"] in ("SKM - Global", "SKM - Global basic"):
            candidates.setdefault(row["dataset"], {})[row["algorithm"]] = row
    paired = []
    with localcontext() as context:
        context.prec = 50
        for dataset, algorithms in sorted(candidates.items()):
            if len(algorithms) != 2:
                continue
            global_row = algorithms["SKM - Global"]
            basic_row = algorithms["SKM - Global basic"]
            if any(global_row[key] != basic_row[key] for key in
                   ("source_folder", "source_configuration", "commit", "seed", "run_count")):
                continue
            source = REPO / global_row["source_folder"] / dataset
            global_runs = read_csv(source / "SKM_Global_runs.csv")
            basic_runs = read_csv(source / "SKM_Global_Basic_runs.csv")
            count = int(global_row["run_count"])
            if len(global_runs) != count or len(basic_runs) != count:
                raise ValueError(f"{dataset}: incomplete Global/Basic runs")
            outcomes = dict(sse_lower=0, sse_equal=0, sse_higher=0, ci_lower=0, ci_equal=0, ci_higher=0,
                            global_only_success=0, basic_only_success=0)
            sse_delta, ci_delta = Decimal(0), 0
            for i, (old, new) in enumerate(zip(global_runs, basic_runs), 1):
                if int(old["run"]) != i or old["run"] != new["run"] or old["seed"] != new["seed"]:
                    raise ValueError(f"{dataset}: unmatched Global/Basic run or seed")
                a, b = Decimal(old["sse"]), Decimal(new["sse"])
                if not a.is_finite() or not b.is_finite():
                    raise ValueError(f"{dataset}: non-finite Global/Basic SSE")
                ai, bi = int(old["ci"]), int(new["ci"])
                outcomes["sse_lower" if b < a else "sse_higher" if b > a else "sse_equal"] += 1
                outcomes["ci_lower" if bi < ai else "ci_higher" if bi > ai else "ci_equal"] += 1
                outcomes["global_only_success"] += ai == 0 and bi != 0
                outcomes["basic_only_success"] += bi == 0 and ai != 0
                sse_delta += b - a
                ci_delta += bi - ai
            paired.append({
                "dataset": dataset, "k": global_row["k"], "runs": count,
                "seed": global_row["seed"], "commit": global_row["commit"],
                "global_sse_mean": global_row["sse_mean"], "basic_sse_mean": basic_row["sse_mean"],
                "mean_sse_delta_basic_minus_global": f"{sse_delta / count:.6f}",
                "basic_sse_lower": outcomes["sse_lower"], "sse_equal": outcomes["sse_equal"],
                "basic_sse_higher": outcomes["sse_higher"],
                "global_ci_mean": global_row["ci_mean"], "basic_ci_mean": basic_row["ci_mean"],
                "mean_ci_delta_basic_minus_global": f"{Decimal(ci_delta) / count:.4f}",
                "basic_ci_lower": outcomes["ci_lower"], "ci_equal": outcomes["ci_equal"],
                "basic_ci_higher": outcomes["ci_higher"],
                "global_success_pct": global_row["success_pct"], "basic_success_pct": basic_row["success_pct"],
                "global_only_success": outcomes["global_only_success"],
                "basic_only_success": outcomes["basic_only_success"],
                "source_folder": global_row["source_folder"], "report_file": global_row["report_file"],
            })
    if len(paired) != 13:
        raise ValueError(f"Expected 13 comparable Global/Basic datasets, got {len(paired)}")
    write_csv(HERE / "paired-global-basic.csv", tuple(paired[0]), paired)
    return len(paired)


def reused_bisect_pairs(datasets):
    source = REPO / "results" / "M4" / "6.6b-after"
    sse_cfg = config(HERE / "prior-bisect-sse" / "run_config.txt")
    size_cfg = config(HERE / "prior-bisect-size" / "run_config.txt")
    if sse_cfg.get("bisect-select") != "sse" or size_cfg.get("bisect-select") != "size":
        raise ValueError("Unexpected bisecting modes")
    if {k: v for k, v in sse_cfg.items() if k != "bisect-select"} != {
            k: v for k, v in size_cfg.items() if k != "bisect-select"}:
        raise ValueError("Bisecting configurations differ beyond selection mode")
    original = read_csv(source / "paired-comparison.csv")
    if len(original) != 10:
        raise ValueError("Expected 10 reused bisecting pairs")
    paired = []
    for row in original:
        dataset = row["dataset"]
        if dataset not in datasets or row["runs"] != "100":
            raise ValueError(f"{dataset}: invalid reused bisecting pair")
        sse = read_csv(source / "sse" / dataset / "Bisecting_KM_runs.csv")
        size = read_csv(source / "size" / dataset / "Bisecting_KM_runs.csv")
        if len(sse) != 100 or len(size) != 100:
            raise ValueError(f"{dataset}: incomplete reused bisecting runs")
        counts = dict(sse_lower=0, sse_equal=0, sse_higher=0, ci_lower=0, ci_equal=0, ci_higher=0)
        for i, (old, new) in enumerate(zip(sse, size), 1):
            if int(old["run"]) != i or old["run"] != new["run"] or old["seed"] != new["seed"]:
                raise ValueError(f"{dataset}: unmatched bisecting run or seed")
            a, b = Decimal(old["sse"]), Decimal(new["sse"])
            if not a.is_finite() or not b.is_finite():
                raise ValueError(f"{dataset}: non-finite bisecting SSE")
            ai, bi = int(old["ci"]), int(new["ci"])
            counts["sse_lower" if b < a else "sse_higher" if b > a else "sse_equal"] += 1
            counts["ci_lower" if bi < ai else "ci_higher" if bi > ai else "ci_equal"] += 1
        if any(int(row["size_" + key]) != value for key, value in counts.items()):
            raise ValueError(f"{dataset}: stored bisecting comparison differs from its runs")
        paired.append({
            "dataset": dataset, "k": datasets[dataset]["k"], "runs": row["runs"],
            "seed": sse_cfg["seed"], "commit": sse_cfg["commit"],
            "compiler": sse_cfg["compiler"], "build_flags": sse_cfg["flags"],
            **{key: row[key] for key in ("size_sse_lower", "size_sse_equal", "size_sse_higher",
                                         "size_ci_lower", "size_ci_equal", "size_ci_higher")},
            "sse_source": relative(source / "sse"), "size_source": relative(source / "size"),
            "sse_report": relative(HERE / "prior-bisect-sse" / "report" / "summary.csv"),
            "size_report": relative(HERE / "prior-bisect-size" / "report" / "summary.csv"),
        })
    paired.sort(key=lambda row: row["dataset"])
    write_csv(HERE / "bisect-paired-6.6b.csv", tuple(paired[0]), paired)


if __name__ == "__main__":
    dataset_manifest, comparison_rows = report_rows()
    paired_local(dataset_manifest)
    global_count = paired_global_basic(comparison_rows)
    reused_bisect_pairs(dataset_manifest)
    print(f"Wrote {len(comparison_rows)} labeled rows, {len(dataset_manifest)} Local pairs, "
          f"{global_count} Global pairs and 10 reused bisecting pairs.")
