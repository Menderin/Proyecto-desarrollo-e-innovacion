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


def normalize_label(label: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_-]+", "_", label.strip()).strip("_")


def prompt_batch(default_count: int) -> dict | None:
    print("\nNueva serie. Escribe 'fin' para terminar y guardar.")
    while True:
        class_text = input("Clase [p=peligroso, s=seguro, fin]: ").strip().lower()
        if class_text in {"fin", "f", "done"}:
            return None
        if class_text in {"p", "peligroso", "dangerous"}:
            object_class = "dangerous"
            break
        if class_text in {"s", "seguro", "allowed"}:
            object_class = "allowed"
            break
        print("Clase invalida.")

    while True:
        safe_label = normalize_label(input("Nombre del objeto: "))
        if safe_label:
            break
        print("El nombre no puede quedar vacio.")

    count_text = input(f"Cantidad de mediciones [{default_count}]: ").strip()
    try:
        count = int(count_text) if count_text else default_count
    except ValueError:
        count = 0
    if count <= 0:
        print("Cantidad invalida; se usara el valor por defecto.")
        count = default_count

    return {"label": safe_label, "class": object_class, "count": count}


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

    if not args.port:
        parser.error("--port es obligatorio")
    if bool(args.label) != bool(args.object_class):
        parser.error("--label y --class deben usarse juntos")
    if args.count <= 0:
        parser.error("--count debe ser mayor que cero")

    interactive = not args.label
    if interactive:
        batch = prompt_batch(args.count)
        if batch is None:
            print("No se solicitaron mediciones.")
            return 0
    else:
        safe_label = normalize_label(args.label)
        if not safe_label:
            parser.error("--label no contiene caracteres utilizables")
        batch = {
            "label": safe_label,
            "class": args.object_class,
            "count": args.count,
        }

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    file_suffix = "session" if interactive else batch["label"]
    output_path = Path(args.out_dir) / f"crossings_{stamp}_{file_suffix}.json"
    payload = {
        "session": {
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "port": args.port,
            "baud": args.baud,
            "mode": "interactive" if interactive else "single",
            "note": args.note,
        },
        "baseline": None,
        "batches": [],
        "crossings": [],
    }
    save_payload(output_path, payload)

    print(f"Guardando en: {output_path}")
    print("Reinicia el ESP32 sin objetos cerca y espera 'Calibracion lista'.")
    print(
        f"Luego realiza {batch['count']} mediciones con: "
        f"{batch['label']} ({batch['class']})"
    )

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            batch_start = len(payload["crossings"])
            while batch is not None:
                raw_line = ser.readline()
                if not raw_line:
                    continue

                line = raw_line.decode("utf-8", errors="replace").rstrip()
                baseline = parse_marker(line, BASELINE_MARKER)
                if baseline is not None:
                    payload["baseline"] = baseline
                    save_payload(output_path, payload)
                    axes = baseline.get("baseline", {})
                    print(
                        "Baseline recibida: "
                        f"x={axes.get('x')} y={axes.get('y')} z={axes.get('z')} "
                        f"noise={baseline.get('noise_p90_raw')} "
                        f"threshold={baseline.get('threshold_raw')}"
                    )
                    if -4096 in (axes.get("x"), axes.get("y"), axes.get("z")):
                        print(
                            "ERROR: baseline saturada (-4096). "
                            "No realices mediciones; aleja metales y reinicia."
                        )
                    continue

                crossing = parse_marker(line, CROSS_MARKER)
                if crossing is None:
                    continue

                batch_sequence = len(payload["crossings"]) - batch_start + 1
                crossing["sequence"] = len(payload["crossings"]) + 1
                crossing["batch_sequence"] = batch_sequence
                crossing["label"] = batch["label"]
                crossing["class"] = batch["class"]
                payload["crossings"].append(crossing)
                save_payload(output_path, payload)
                print(
                    f"[{batch_sequence}/{batch['count']}] "
                    f"{batch['label']} "
                    f"p90={crossing.get('p90_raw')} "
                    f"prediction={crossing.get('prediction')}"
                )

                if batch_sequence < batch["count"]:
                    continue

                payload["batches"].append(
                    {
                        "label": batch["label"],
                        "class": batch["class"],
                        "count": batch_sequence,
                    }
                )
                save_payload(output_path, payload)
                if not interactive:
                    batch = None
                    continue

                batch = prompt_batch(args.count)
                batch_start = len(payload["crossings"])
                if batch is not None:
                    print(
                        f"Realiza {batch['count']} mediciones con: "
                        f"{batch['label']} ({batch['class']})"
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
