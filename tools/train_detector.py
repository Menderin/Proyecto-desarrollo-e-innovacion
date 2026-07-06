#!/usr/bin/env python3
"""Train a small ESP32-friendly magnetic object detector from raw JSON captures."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


AXES = ("x", "y", "z")
DANGEROUS_PREFIXES = ("cuchillo_", "cortacarton_")
ALLOWED_PREFIXES = ("base_", "baseline", "llaves_", "celular_", "mochila_")


def median(values: list[float]) -> float:
    return float(statistics.median(values)) if values else 0.0


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return float(ordered[round((len(ordered) - 1) * q)])


def median_int(values: list[int]) -> int:
    return int(statistics.median(values)) if values else 0


def magnitude(delta: dict[str, int]) -> float:
    return math.sqrt(sum(value * value for value in delta.values()))


def phase_label(phase: str) -> str | None:
    if phase.startswith(DANGEROUS_PREFIXES):
        return "dangerous"
    if phase.startswith(ALLOWED_PREFIXES):
        return "allowed"
    return None


def derive_baseline(data: dict[str, Any], records: list[dict[str, Any]]) -> dict[str, int]:
    baseline_rounds = [
        round_info for round_info in data.get("rounds", [])
        if round_info.get("kind") == "baseline" and round_info.get("baseline_raw")
    ]
    if baseline_rounds:
        return {
            axis: int(baseline_rounds[-1]["baseline_raw"][axis])
            for axis in AXES
        }

    base_rows = [
        record["med_raw"] for record in records
        if record.get("phase") == "base_habitacion" and record.get("med_raw")
    ]
    if not base_rows:
        raise ValueError("No hay baseline ni fase base_habitacion utilizable")

    return {
        axis: median_int([int(row[axis]) for row in base_rows])
        for axis in AXES
    }


def extract_round_features(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    records = [record for record in data.get("records", []) if record.get("med_raw")]
    baseline = derive_baseline(data, records)

    by_round: dict[tuple[int, str], list[dict[str, Any]]] = {}
    for record in records:
        phase = record.get("phase", "")
        label = phase_label(phase)
        if label is None:
            continue

        round_id = int(record.get("round_id") or 0)
        key = (round_id, phase)
        by_round.setdefault(key, []).append(record)

    rows: list[dict[str, Any]] = []
    for (round_id, phase), round_records in sorted(by_round.items()):
        magnitudes: list[float] = []
        deltas = {axis: [] for axis in AXES}

        for record in round_records:
            raw = {axis: int(record["med_raw"][axis]) for axis in AXES}
            delta = {axis: raw[axis] - baseline[axis] for axis in AXES}
            for axis in AXES:
                deltas[axis].append(delta[axis])
            magnitudes.append(magnitude(delta))

        rows.append({
            "source": str(path),
            "round_id": round_id,
            "phase": phase,
            "label": phase_label(phase),
            "n": len(round_records),
            "median_mag": median(magnitudes),
            "p90_mag": percentile(magnitudes, 0.90),
            "p95_mag": percentile(magnitudes, 0.95),
            "max_mag": max(magnitudes) if magnitudes else 0.0,
            "median_dx": median(deltas["x"]),
            "median_dy": median(deltas["y"]),
            "median_dz": median(deltas["z"]),
        })

    return rows


def score_threshold(rows: list[dict[str, Any]], feature: str, threshold: float) -> dict[str, float]:
    tp = fp = tn = fn = 0
    for row in rows:
        predicted = row[feature] >= threshold
        actual = row["label"] == "dangerous"
        if predicted and actual:
            tp += 1
        elif predicted and not actual:
            fp += 1
        elif not predicted and actual:
            fn += 1
        else:
            tn += 1

    precision = tp / (tp + fp) if (tp + fp) else 0.0
    recall = tp / (tp + fn) if (tp + fn) else 0.0
    f1 = (2.0 * precision * recall / (precision + recall)) if (precision + recall) else 0.0
    accuracy = (tp + tn) / (tp + tn + fp + fn) if rows else 0.0

    return {
        "threshold": threshold,
        "tp": tp,
        "fp": fp,
        "tn": tn,
        "fn": fn,
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "accuracy": accuracy,
    }


def train_threshold(rows: list[dict[str, Any]], feature: str, min_precision: float) -> dict[str, float]:
    candidates = sorted({float(row[feature]) for row in rows})
    if not candidates:
        raise ValueError("No hay datos para entrenar")

    thresholds = [0.0]
    thresholds.extend((a + b) / 2.0 for a, b in zip(candidates, candidates[1:]))
    thresholds.append(candidates[-1] + 1.0)

    scored = [score_threshold(rows, feature, threshold) for threshold in thresholds]
    eligible = [row for row in scored if row["precision"] >= min_precision]
    if eligible:
        return max(eligible, key=lambda row: (row["recall"], row["f1"], row["accuracy"]))
    return max(scored, key=lambda row: (row["f1"], row["recall"], row["precision"], row["accuracy"]))


def write_header(path: Path, feature: str, result: dict[str, float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    threshold = int(round(result["threshold"]))
    content = f"""#ifndef DETECTOR_THRESHOLDS_H
#define DETECTOR_THRESHOLDS_H

// Archivo generado por tools/train_detector.py.
// Detector inicial: peligroso si {feature} >= DETECTOR_MAG_THRESHOLD_RAW.

#define DETECTOR_FEATURE_{feature.upper()} 1
#define DETECTOR_MAG_THRESHOLD_RAW {threshold}

#endif
"""
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_files", nargs="+", help="Archivos data/raw/*.json")
    parser.add_argument("--feature", default="median_mag", choices=["median_mag", "p90_mag", "p95_mag", "max_mag"])
    parser.add_argument("--min-precision", type=float, default=0.80)
    parser.add_argument("--header-out", default="include/detector_thresholds.h")
    parser.add_argument("--report-out", default="data/detector_training_report.json")
    args = parser.parse_args()

    rows: list[dict[str, Any]] = []
    for file_name in args.json_files:
        rows.extend(extract_round_features(Path(file_name)))

    if not rows:
        raise SystemExit("No se extrajeron rondas etiquetadas")

    result = train_threshold(rows, args.feature, args.min_precision)
    report = {
        "feature": args.feature,
        "min_precision": args.min_precision,
        "result": result,
        "rows": rows,
        "dangerous_prefixes": DANGEROUS_PREFIXES,
        "allowed_prefixes": ALLOWED_PREFIXES,
    }

    Path(args.report_out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.report_out).write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    write_header(Path(args.header_out), args.feature, result)

    print(f"Feature: {args.feature}")
    print(f"Threshold: {result['threshold']:.2f}")
    print(
        "Metrics: "
        f"precision={result['precision']:.2f} recall={result['recall']:.2f} "
        f"f1={result['f1']:.2f} accuracy={result['accuracy']:.2f}"
    )
    print(f"Confusion: TP={result['tp']} FP={result['fp']} TN={result['tn']} FN={result['fn']}")
    print(f"Header: {args.header_out}")
    print(f"Report: {args.report_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
