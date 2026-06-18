#!/usr/bin/env python3
"""Generate simple dense-layer fixtures for OpenFHE matrix benchmarks."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


INPUT_DIM = 4
OUTPUT_DIM = 8

WEIGHTS = np.array(
    [
        [0.35, -0.20, 0.10, 0.45, -0.30, 0.25, 0.15, -0.40],
        [0.12, 0.30, -0.25, 0.18, 0.42, -0.16, 0.28, 0.08],
        [-0.22, 0.14, 0.36, -0.10, 0.20, 0.31, -0.18, 0.27],
        [0.05, -0.33, 0.24, 0.29, -0.12, 0.19, 0.40, -0.21],
    ],
    dtype=np.float64,
)

BIAS = np.array([-0.10, 0.05, 0.12, -0.07, 0.03, 0.09, -0.04, 0.15], dtype=np.float64)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate dense4x8 fixtures for plain C++ vs OpenFHE CKKS benchmarks."
    )
    parser.add_argument(
        "--sizes",
        nargs="+",
        type=int,
        default=[1000, 100000],
        help="Row counts to generate. Default: 1000 100000.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/generated_dense"),
        help="Output root. Default: data/generated_dense.",
    )
    parser.add_argument("--seed", type=int, default=42, help="RNG seed. Default: 42.")
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=100000,
        help="Rows written per chunk. Default: 100000.",
    )
    return parser.parse_args()


def size_label(size: int) -> str:
    if size % 1_000_000 == 0:
        return f"{size // 1_000_000}m"
    if size % 1000 == 0:
        return f"{size // 1000}k"
    return str(size)


def fixture_name(size: int) -> str:
    return f"dense4x8_{size_label(size)}"


def write_weights(path: Path) -> None:
    with path.open("w", encoding="utf-8") as output:
        output.write("input_index," + ",".join(f"w{j}" for j in range(OUTPUT_DIM)) + "\n")
        for input_index in range(INPUT_DIM):
            values = ",".join(f"{value:.17g}" for value in WEIGHTS[input_index])
            output.write(f"{input_index},{values}\n")


def write_bias(path: Path) -> None:
    with path.open("w", encoding="utf-8") as output:
        output.write("output_index,bias\n")
        for output_index, value in enumerate(BIAS):
            output.write(f"{output_index},{value:.17g}\n")


def write_features(path: Path, rows: int, seed: int, chunk_size: int) -> None:
    rng = np.random.default_rng(seed)
    with path.open("w", encoding="utf-8") as output:
        output.write("row_id,x0,x1,x2,x3\n")
        cursor = 0
        while cursor < rows:
            current = min(chunk_size, rows - cursor)

            # Keep the data normalized and CKKS-friendly. This fixture is for
            # dense-layer math, not data-loading stress.
            features = rng.normal(loc=0.0, scale=1.0, size=(current, INPUT_DIM))
            features[:, 1] += 0.15 * features[:, 0]
            features[:, 2] -= 0.10 * features[:, 0]
            features[:, 3] += 0.05 * features[:, 1]
            features = np.clip(features, -3.0, 3.0)

            row_ids = np.arange(cursor, cursor + current, dtype=np.int64)
            for row_id, row in zip(row_ids, features):
                output.write(
                    f"{int(row_id)},"
                    + ",".join(f"{float(value):.17g}" for value in row)
                    + "\n"
                )
            cursor += current


def write_metadata(path: Path, rows: int, seed: int) -> None:
    metadata = {
        "name": fixture_name(rows),
        "rows": rows,
        "input_dim": INPUT_DIM,
        "output_dim": OUTPUT_DIM,
        "seed": seed,
        "operation": "Y = X @ W + b",
        "features": "features.csv",
        "weights": "weights.csv",
        "bias": "bias.csv",
    }
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    for size in args.sizes:
        if size <= 0:
            raise ValueError("--sizes values must be positive")
        fixture_dir = args.out / fixture_name(size)
        fixture_dir.mkdir(parents=True, exist_ok=True)

        write_features(fixture_dir / "features.csv", size, args.seed + size, args.chunk_size)
        write_weights(fixture_dir / "weights.csv")
        write_bias(fixture_dir / "bias.csv")
        write_metadata(fixture_dir / "metadata.json", size, args.seed + size)
        print(f"wrote {fixture_dir}")


if __name__ == "__main__":
    main()
