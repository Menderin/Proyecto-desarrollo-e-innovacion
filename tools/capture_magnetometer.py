#!/usr/bin/env python3
"""Capture ESP32 magnetometer characterization logs into data/raw JSON files."""

from __future__ import annotations

import argparse
import json
import math
import re
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit(
        "pyserial no esta instalado. Usa el Python de ESP-IDF o instala pyserial."
    ) from exc


RAW_TO_UT = 100.0 / 1090.0
CHAR_RE = re.compile(r"CHAR_JSON:(\{.*\})")
PHASES = [
    "base_habitacion",
    "cuchillo_pequeno_5cm",
    "cuchillo_pequeno_10cm",
    "cuchillo_pequeno_15cm",
    "cuchillo_pequeno_20cm",
    "cuchillo_grande_5cm",
    "cuchillo_grande_10cm",
    "cuchillo_grande_15cm",
    "cuchillo_grande_20cm",
    "cortacarton_5cm",
    "cortacarton_10cm",
    "cortacarton_15cm",
    "cortacarton_20cm",
    "llaves_5cm",
    "llaves_10cm",
    "llaves_15cm",
    "llaves_20cm",
    "celular_10cm",
    "celular_20cm",
    "mochila_sin_objeto",
]


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def raw_to_ut(raw: dict[str, int]) -> dict[str, float]:
    return {axis: round(value * RAW_TO_UT, 4) for axis, value in raw.items()}


def delta_raw(raw: dict[str, int], baseline: dict[str, int] | None) -> dict[str, int] | None:
    if baseline is None:
        return None
    return {axis: raw[axis] - baseline[axis] for axis in ("x", "y", "z")}


def magnitude(values: dict[str, int] | None) -> float | None:
    if values is None:
        return None
    return round(math.sqrt(sum(value * value for value in values.values())), 4)


def median_int(values: list[int]) -> int:
    if not values:
        return 0

    sorted_values = sorted(values)
    mid = len(sorted_values) // 2
    if len(sorted_values) % 2 == 0:
        return int((sorted_values[mid - 1] + sorted_values[mid]) / 2)
    return sorted_values[mid]


def median_raw(records: list[dict[str, Any]]) -> dict[str, int]:
    return {
        axis: median_int([int(record["med_raw"][axis]) for record in records])
        for axis in ("x", "y", "z")
    }


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp_path.replace(path)


def keyboard_loop(state: dict[str, Any], stop_event: threading.Event) -> None:
    print("")
    print("Controles:")
    print("  Enter        -> iniciar ronda de captura en la fase activa")
    print("  n+Enter      -> avanzar a la siguiente fase sin capturar")
    print("  texto+Enter  -> cambiar a una fase personalizada")
    print("  b+Enter      -> capturar baseline estable")
    print("  q+Enter      -> terminar y guardar")
    print("")

    while not stop_event.is_set():
        try:
            command = input().strip()
        except EOFError:
            stop_event.set()
            return

        with state["lock"]:
            if command.lower() == "q":
                stop_event.set()
                return

            if command.lower() == "b":
                if state["capturing"]:
                    print("Ya hay una ronda en curso.")
                    continue

                state["round_seq"] += 1
                state["capturing"] = True
                state["baseline_capture"] = True
                state["round_records"] = 0
                state["baseline_records"] = []
                state["current_round"] = {
                    "round_id": state["round_seq"],
                    "phase": "baseline",
                    "target_records": state["baseline_records_target"],
                    "started_utc": utc_now_iso(),
                    "kind": "baseline",
                }
                print(
                    f"Capturando baseline: ronda {state['round_seq']} "
                    f"objetivo={state['baseline_records_target']} registros"
                )
                continue

            if command.lower() == "n":
                state["phase_index"] = (state["phase_index"] + 1) % len(PHASES)
                state["phase"] = PHASES[state["phase_index"]]
                print(f"Fase activa: {state['phase']}")
                continue

            if command:
                state["phase"] = command
                print(f"Fase activa: {state['phase']}")
                continue

            if state["capturing"]:
                print("Ya hay una ronda en curso.")
                continue

            state["round_seq"] += 1
            state["capturing"] = True
            state["round_records"] = 0
            state["current_round"] = {
                "round_id": state["round_seq"],
                "phase": state["phase"],
                "target_records": state["records_per_round"],
                "started_utc": utc_now_iso(),
                "kind": "measurement",
            }
            print(
                f"Capturando ronda {state['round_seq']} fase={state['phase']} "
                f"objetivo={state['records_per_round']} registros"
            )


def build_payload(args: argparse.Namespace, output_path: Path) -> dict[str, Any]:
    return {
        "session": {
            "created_utc": utc_now_iso(),
            "operator_note": args.note,
            "port": args.port,
            "baud": args.baud,
            "sensor": "HMC5883L",
            "raw_to_ut": RAW_TO_UT,
            "output_path": str(output_path),
            "phase_presets": PHASES,
            "records_per_round": args.records_per_round,
            "baseline_records": args.baseline_records,
        },
        "rounds": [],
        "records": [],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Puerto serial, por ejemplo COM3")
    parser.add_argument("--baud", type=int, default=115200, help="Baudios del monitor serial")
    parser.add_argument("--out-dir", default="data/raw", help="Directorio de salida JSON")
    parser.add_argument("--note", default="", help="Nota libre para la sesion")
    parser.add_argument("--list-ports", action="store_true", help="Listar puertos seriales y salir")
    parser.add_argument(
        "--records-per-round",
        type=int,
        default=100,
        help="Cantidad de registros CHAR_JSON a guardar por ronda",
    )
    parser.add_argument(
        "--baseline-records",
        type=int,
        default=30,
        help="Cantidad de registros para calcular baseline estable",
    )
    args = parser.parse_args()

    ports = list(list_ports.comports())
    if args.list_ports:
        if not ports:
            print("No se encontraron puertos seriales.")
        else:
            for port in ports:
                print(f"{port.device}: {port.description}")
        return 0

    if args.port is None:
        print("Falta --port. Puertos disponibles:")
        if not ports:
            print("  ninguno")
        else:
            for port in ports:
                print(f"  {port.device}: {port.description}")
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = out_dir / f"magnetometer_characterization_{stamp}.json"

    payload = build_payload(args, output_path)
    stop_event = threading.Event()
    state: dict[str, Any] = {
        "lock": threading.Lock(),
        "phase": PHASES[0],
        "phase_index": 0,
        "baseline_raw": None,
        "latest_seen_record": None,
        "capturing": False,
        "baseline_capture": False,
        "baseline_records": [],
        "current_round": None,
        "round_seq": 0,
        "round_records": 0,
        "records_per_round": args.records_per_round,
        "baseline_records_target": args.baseline_records,
    }

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"No se pudo abrir {args.port}: {exc}")
        print("Puertos disponibles:")
        if not ports:
            print("  ninguno")
        else:
            for port in ports:
                print(f"  {port.device}: {port.description}")
        print("Cierra idf.py monitor, Arduino Serial Monitor u otro programa que use el puerto.")
        return 2

    input_thread = threading.Thread(target=keyboard_loop, args=(state, stop_event), daemon=True)
    input_thread.start()

    print(f"Guardando en: {output_path}")
    print(f"Fase activa: {PHASES[0]}")
    print("Esperando Enter para iniciar la primera ronda.")

    with ser:
        while not stop_event.is_set():
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue

            match = CHAR_RE.search(line)
            if match is None:
                continue

            try:
                esp_record = json.loads(match.group(1))
            except json.JSONDecodeError:
                print(f"Linea CHAR_JSON invalida: {line}")
                continue

            with state["lock"]:
                phase = state["phase"]
                baseline = state["baseline_raw"]
                capturing = state["capturing"]
                current_round = dict(state["current_round"]) if state["current_round"] else None
                round_records = state["round_records"]

            med_raw = esp_record.get("med_raw")
            if not esp_record.get("ok") or not isinstance(med_raw, dict):
                record = {
                    "host_time_utc": utc_now_iso(),
                    "phase": phase,
                    "round_id": current_round["round_id"] if current_round else None,
                    "round_index": round_records if current_round else None,
                    "esp": esp_record,
                }
            else:
                med_raw_int = {axis: int(med_raw[axis]) for axis in ("x", "y", "z")}
                delta = delta_raw(med_raw_int, baseline)
                record = {
                    "host_time_utc": utc_now_iso(),
                    "phase": phase,
                    "round_id": current_round["round_id"] if current_round else None,
                    "round_index": round_records if current_round else None,
                    "seq": esp_record.get("seq"),
                    "window_ms": esp_record.get("window_ms"),
                    "sample_ms": esp_record.get("sample_ms"),
                    "samples": esp_record.get("samples"),
                    "med_raw": med_raw_int,
                    "med_ut": raw_to_ut(med_raw_int),
                    "min_raw": esp_record.get("min_raw"),
                    "max_raw": esp_record.get("max_raw"),
                    "baseline_raw": baseline,
                    "delta_raw": delta,
                    "delta_magnitude_raw": magnitude(delta),
                }

            with state["lock"]:
                state["latest_seen_record"] = record

                if not capturing:
                    continue

                payload["records"].append(record)
                if state["baseline_capture"] and record.get("med_raw") is not None:
                    state["baseline_records"].append(record)

                state["round_records"] += 1
                saved_in_round = state["round_records"]
                target_records = (
                    state["baseline_records_target"]
                    if state["baseline_capture"]
                    else state["records_per_round"]
                )

                if saved_in_round >= target_records:
                    finished_round = dict(state["current_round"])
                    finished_round["finished_utc"] = utc_now_iso()
                    finished_round["saved_records"] = saved_in_round

                    if state["baseline_capture"]:
                        new_baseline = median_raw(state["baseline_records"])
                        state["baseline_raw"] = new_baseline
                        finished_round["baseline_raw"] = new_baseline

                    payload["rounds"].append(finished_round)

                    state["capturing"] = False
                    state["baseline_capture"] = False
                    state["baseline_records"] = []
                    state["current_round"] = None
                    state["round_records"] = 0
                    if finished_round.get("kind") == "measurement":
                        state["phase_index"] = (state["phase_index"] + 1) % len(PHASES)
                        state["phase"] = PHASES[state["phase_index"]]
                else:
                    finished_round = None

            atomic_write_json(output_path, payload)

            summary = (
                f"{record['host_time_utc']} phase={record['phase']} "
                f"round={record.get('round_id')} {saved_in_round}/{target_records} "
                f"seq={record.get('seq')} med={record.get('med_raw')} "
                f"delta_mag={record.get('delta_magnitude_raw')}"
            )
            print(summary)

            if finished_round is not None:
                print(
                    f"Ronda {finished_round['round_id']} completada: "
                    f"{finished_round['saved_records']} registros guardados."
                )
                if finished_round.get("baseline_raw") is not None:
                    print(f"Baseline estable actualizado: {finished_round['baseline_raw']}")
                print(f"Fase activa: {state['phase']}")
                print("Esperando Enter para iniciar la siguiente ronda.")

    atomic_write_json(output_path, payload)
    print(f"Sesion guardada: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
