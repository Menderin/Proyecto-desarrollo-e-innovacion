#!/usr/bin/env python3
"""Train the ESP32 p90 threshold from labeled crossing JSON files."""

from __future__ import annotations

import argparse
import glob
import json
from pathlib import Path


def load_rows(paths: list[Path]) -> list[dict]:
    rows: list[dict] = []
    for path in paths:
        payload = json.loads(path.read_text(encoding="utf-8"))
        for crossing in payload.get("crossings", []):
            if crossing.get("class") not in {"dangerous", "allowed"}:
                continue
            if crossing.get("p90_raw") is None:
                continue
            rows.append(
                {
                    "source": str(path),
                    "label": crossing.get("label", "unknown"),
                    "class": crossing["class"],
                    "p90_raw": int(crossing["p90_raw"]),
                }
            )
    return rows


def score(rows: list[dict], threshold: float) -> dict:
    tp = fp = tn = fn = 0
    for row in rows:
        predicted_dangerous = row["p90_raw"] >= threshold
        actual_dangerous = row["class"] == "dangerous"
        if predicted_dangerous and actual_dangerous:
            tp += 1
        elif predicted_dangerous:
            fp += 1
        elif actual_dangerous:
            fn += 1
        else:
            tn += 1

    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    specificity = tn / (tn + fp) if tn + fp else 0.0
    accuracy = (tp + tn) / len(rows)
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {
        "threshold": threshold,
        "tp": tp,
        "fp": fp,
        "tn": tn,
        "fn": fn,
        "precision": precision,
        "recall": recall,
        "specificity": specificity,
        "accuracy": accuracy,
        "f1": f1,
    }


def choose_threshold(rows: list[dict], min_recall: float) -> dict:
    values = sorted({row["p90_raw"] for row in rows})
    candidates = [0.0]
    candidates.extend((a + b) / 2 for a, b in zip(values, values[1:]))
    candidates.append(values[-1] + 1.0)
    results = [score(rows, candidate) for candidate in candidates]

    eligible = [result for result in results if result["recall"] >= min_recall]
    if eligible:
        return max(
            eligible,
            key=lambda result: (
                result["specificity"],
                result["precision"],
                result["f1"],
            ),
        )
    return max(results, key=lambda result: (result["f1"], result["recall"]))


def write_header(path: Path, threshold: int) -> None:
    content = f"""#ifndef DETECTOR_THRESHOLDS_H
#define DETECTOR_THRESHOLDS_H

// Generado por tools/train_crossings.py con cruces reales.
// Peligroso si p90_raw >= DETECTOR_MAG_THRESHOLD_RAW.

#define DETECTOR_FEATURE_P90_RAW 1
#define DETECTOR_MAG_THRESHOLD_RAW {threshold}

#endif
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_files", nargs="+")
    parser.add_argument("--min-recall", type=float, default=0.90)
    parser.add_argument("--header-out", default="include/detector_thresholds.h")
    parser.add_argument("--report-out", default="data/crossing_training_report.json")
    args = parser.parse_args()

    input_paths: list[Path] = []
    for pattern in args.json_files:
        matches = [Path(name) for name in glob.glob(pattern)]
        if matches:
            input_paths.extend(matches)
        else:
            input_paths.append(Path(pattern))

    missing = [str(path) for path in input_paths if not path.is_file()]
    if missing:
        raise SystemExit(f"Archivos no encontrados: {', '.join(missing)}")

    rows = load_rows(input_paths)
    dangerous = [row for row in rows if row["class"] == "dangerous"]
    allowed = [row for row in rows if row["class"] == "allowed"]
    if not dangerous or not allowed:
        raise SystemExit("Se necesitan cruces dangerous y allowed para entrenar")

    result = choose_threshold(rows, args.min_recall)
    threshold = int(round(result["threshold"]))
    report = {
        "feature": "p90_raw",
        "min_recall": args.min_recall,
        "result": result,
        "counts": {
            "total": len(rows),
            "dangerous": len(dangerous),
            "allowed": len(allowed),
        },
        "rows": rows,
    }

    report_path = Path(args.report_out)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    write_header(Path(args.header_out), threshold)

    print(f"Threshold p90: {threshold}")
    print(
        f"precision={result['precision']:.2f} recall={result['recall']:.2f} "
        f"specificity={result['specificity']:.2f} accuracy={result['accuracy']:.2f}"
    )
    print(
        f"TP={result['tp']} FP={result['fp']} "
        f"TN={result['tn']} FN={result['fn']}"
    )
    print(f"Header: {args.header_out}")
    print(f"Report: {args.report_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
