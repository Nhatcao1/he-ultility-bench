#!/usr/bin/env python3
"""Validate generated FedAvg JSON fixtures without OpenFHE."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check FedAvg JSON fixtures against expected_global.json."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("data/generated_fedavg"),
        help="Fixture root. Default: data/generated_fedavg.",
    )
    parser.add_argument(
        "--tolerance",
        type=float,
        default=1e-12,
        help="Max absolute error tolerance. Default: 1e-12.",
    )
    parser.add_argument(
        "--fixtures",
        nargs="+",
        default=None,
        help="Optional fixture names to check. Default: every generated fixture.",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def fedavg(clients: list[dict[str, Any]], param_count: int) -> list[float]:
    total_examples = sum(int(client["num_examples"]) for client in clients)
    if total_examples <= 0:
        raise ValueError("total_examples must be positive")

    global_flat = [0.0 for _ in range(param_count)]
    for client in clients:
        alpha = int(client["num_examples"]) / total_examples
        flat = client["parameters_flat"]
        if len(flat) != param_count:
            raise ValueError(f"{client['client_id']} has wrong parameter count")
        for idx, value in enumerate(flat):
            global_flat[idx] += alpha * float(value)
    return global_flat


def check_fixture(path: Path, tolerance: float) -> None:
    layout = load_json(path / "layout.json")
    clients_doc = load_json(path / "clients.json")
    expected_doc = load_json(path / "expected_global.json")

    param_count = int(layout["param_count"])
    actual = fedavg(clients_doc["clients"], param_count)
    expected = [float(value) for value in expected_doc["expected_global_flat"]]

    if len(actual) != len(expected):
        raise ValueError(f"{path}: actual/expected lengths differ")

    max_abs = max(abs(a - e) for a, e in zip(actual, expected))
    mae = sum(abs(a - e) for a, e in zip(actual, expected)) / len(actual)
    if max_abs > tolerance:
        raise ValueError(
            f"{path}: max_abs={max_abs:.6g} exceeds tolerance={tolerance:.6g}"
        )

    print(f"OK {path.name}: params={param_count} mae={mae:.3g} max_abs={max_abs:.3g}")


def main() -> None:
    args = parse_args()
    fixture_dirs = sorted(
        path for path in args.root.iterdir()
        if path.is_dir() and (path / "clients.json").exists()
    )
    if args.fixtures:
        wanted = set(args.fixtures)
        fixture_dirs = [path for path in fixture_dirs if path.name in wanted]
        missing = wanted - {path.name for path in fixture_dirs}
        if missing:
            raise ValueError(f"missing requested fixtures under {args.root}: {sorted(missing)}")
    if not fixture_dirs:
        raise ValueError(f"no FedAvg fixtures found under {args.root}")

    for fixture_dir in fixture_dirs:
        check_fixture(fixture_dir, args.tolerance)


if __name__ == "__main__":
    main()
