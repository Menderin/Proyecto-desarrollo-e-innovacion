#!/usr/bin/env python3
"""Summarize magnetometer characterization JSON captures."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


AXES = ("x", "y", "z")


def median_int(values: list[int]) -> int:
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2 == 0:
        return int((ordered[mid - 1] + ordered[mid]) / 2)
    return ordered[mid]


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = round((len(ordered) - 1) * q)
    return ordered[index]


def magnitude(delta: dict[str, int]) -> float:
    return math.sqrt(sum(value * value for value in delta.values()))


def derive_baseline(records: list[dict[str, Any]], phase: str) -> dict[str, int]:
    rows = [record["med_raw"] for record in records if record.get("phase") == phase and record.get("med_raw")]
    if not rows:
        raise SystemExit(f"No hay registros med_raw para la fase baseline {phase!r}")

    return {
        axis: median_int([int(row[axis]) for row in rows])
        for axis in AXES
    }


def summarize(path: Path, baseline_phase: str) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    records = [record for record in data.get("records", []) if record.get("med_raw")]
    baseline_rounds = [
        round_info for round_info in data.get("rounds", [])
        if round_info.get("kind") == "baseline" and round_info.get("baseline_raw")
    ]
    if baseline_rounds:
        baseline = {
            axis: int(baseline_rounds[-1]["baseline_raw"][axis])
            for axis in AXES
        }
        baseline_source = f"baseline_round_{baseline_rounds[-1].get('round_id')}"
    else:
        baseline = derive_baseline(records, baseline_phase)
        baseline_source = baseline_phase

    by_phase: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        raw = {axis: int(record["med_raw"][axis]) for axis in AXES}
        delta = {axis: raw[axis] - baseline[axis] for axis in AXES}
        row = {
            "raw": raw,
            "delta": delta,
            "delta_magnitude_raw": magnitude(delta),
        }
        by_phase.setdefault(record["phase"], []).append(row)

    phase_summaries = []
    for phase, rows in by_phase.items():
        mags = [row["delta_magnitude_raw"] for row in rows]
        med_delta = {
            axis: median_int([row["delta"][axis] for row in rows])
            for axis in AXES
        }
        phase_summaries.append({
            "phase": phase,
            "n": len(rows),
            "median": round(statistics.median(mags), 2),
            "p75": round(percentile(mags, 0.75), 2),
            "p90": round(percentile(mags, 0.90), 2),
            "p95": round(percentile(mags, 0.95), 2),
            "max": round(max(mags), 2),
            "min": round(min(mags), 2),
            "median_delta_raw": med_delta,
        })

    return {
        "file": str(path),
        "baseline_phase": baseline_source,
        "baseline_raw": baseline,
        "records": len(records),
        "phases": phase_summaries,
    }


def print_table(summary: dict[str, Any]) -> None:
    print(f"Archivo: {summary['file']}")
    print(f"Baseline: {summary['baseline_phase']} {summary['baseline_raw']}")
    print("")
    print("phase,n,median,p75,p90,p95,max,min,median_delta_raw")
    for row in summary["phases"]:
        print(
            f"{row['phase']},{row['n']},{row['median']:.2f},{row['p75']:.2f},"
            f"{row['p90']:.2f},{row['p95']:.2f},{row['max']:.2f},{row['min']:.2f},"
            f"{row['median_delta_raw']}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_file", help="Archivo data/raw/*.json")
    parser.add_argument("--baseline-phase", default="base_habitacion")
    parser.add_argument("--out", help="Guardar resumen JSON opcional")
    args = parser.parse_args()

    summary = summarize(Path(args.json_file), args.baseline_phase)
    print_table(summary)

    if args.out:
        Path(args.out).write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
