#!/usr/bin/env python3
"""Capture labeled CROSS_JSON records emitted by the ESP32."""

from __future__ import annotations

import argparse
import json
import re
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
    from serial import SerialException
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit(
        "pyserial no esta instalado. Usa el Python de ESP-IDF o ejecuta: "
        "python -m pip install pyserial"
    ) from exc


ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
CROSS_MARKER = "CROSS_JSON:"
BASELINE_MARKER = "BASELINE_JSON:"


def parse_marker(line: str, marker: str) -> dict | None:
    clean = ANSI_RE.sub("", line)
    marker_pos = clean.find(marker)
    if marker_pos < 0:
        return None
    try:
        return json.loads(clean[marker_pos + len(marker):].strip())
    except json.JSONDecodeError:
        return None


def save_payload(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )


def available_ports() -> list:
    return list(list_ports.comports())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Puerto serial, por ejemplo COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--label", required=False, help="Objeto: cuchillo_grande, llaves, etc.")
    parser.add_argument(
        "--class",
        dest="object_class",
        choices=("dangerous", "allowed"),
        help="Clase real del objeto capturado",
    )
    parser.add_argument("--count", type=int, default=10, help="Cruces que se capturaran")
    parser.add_argument("--out-dir", default="data/crossings")
    parser.add_argument("--note", default="")
    parser.add_argument("--list-ports", action="store_true")
    args = parser.parse_args()

    ports = available_ports()
    if args.list_ports:
        for port in ports:
            print(f"{port.device}: {port.description}")
        return 0

    if not args.port or not args.label or not args.object_class:
        parser.error("--port, --label y --class son obligatorios")
    if args.count <= 0:
        parser.error("--count debe ser mayor que cero")

    safe_label = re.sub(r"[^a-zA-Z0-9_-]+", "_", args.label.strip()).strip("_")
    if not safe_label:
        parser.error("--label no contiene caracteres utilizables")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = Path(args.out_dir) / f"crossings_{stamp}_{safe_label}.json"
    payload = {
        "session": {
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "port": args.port,
            "baud": args.baud,
            "label": safe_label,
            "class": args.object_class,
            "target_crossings": args.count,
            "note": args.note,
        },
        "baseline": None,
        "crossings": [],
    }
    save_payload(output_path, payload)

    print(f"Guardando en: {output_path}")
    print("Reinicia el ESP32 sin objetos cerca y espera 'Calibracion lista'.")
    print(f"Luego realiza {args.count} cruces con: {safe_label}")

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            while len(payload["crossings"]) < args.count:
                raw_line = ser.readline()
                if not raw_line:
                    continue

                line = raw_line.decode("utf-8", errors="replace").rstrip()
                baseline = parse_marker(line, BASELINE_MARKER)
                if baseline is not None:
                    payload["baseline"] = baseline
                    save_payload(output_path, payload)
                    print(
                        "Baseline recibida: "
                        f"noise={baseline.get('noise_p90_raw')} "
                        f"threshold={baseline.get('threshold_raw')}"
                    )
                    continue

                crossing = parse_marker(line, CROSS_MARKER)
                if crossing is None:
                    continue

                crossing["sequence"] = len(payload["crossings"]) + 1
                crossing["label"] = safe_label
                crossing["class"] = args.object_class
                payload["crossings"].append(crossing)
                save_payload(output_path, payload)
                print(
                    f"[{crossing['sequence']}/{args.count}] "
                    f"p90={crossing.get('p90_raw')} "
                    f"prediction={crossing.get('prediction')}"
                )
    except KeyboardInterrupt:
        print("\nCaptura interrumpida; los registros obtenidos quedaron guardados.")
    except SerialException as exc:
        print(f"No se pudo usar {args.port}: {exc}")
        if ports:
            print("Puertos disponibles:")
            for port in ports:
                print(f"  {port.device}: {port.description}")
        return 2

    print(f"Captura finalizada: {len(payload['crossings'])} cruces")
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
