#!/usr/bin/env python3
"""Generate small FedAvg merge fixtures for plaintext and OpenFHE tests."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np


@dataclass(frozen=True)
class LayerSpec:
    name: str
    shape: tuple[int, ...]


@dataclass(frozen=True)
class FixtureSpec:
    name: str
    client_count: int
    example_counts: tuple[int, ...]
    layers: tuple[LayerSpec, ...]


FIXTURES = [
    FixtureSpec(
        name="tiny_mlp_11_c2",
        client_count=2,
        example_counts=(1000, 3000),
        layers=(
            LayerSpec("layer1.weight", (2, 3)),
            LayerSpec("layer1.bias", (2,)),
            LayerSpec("layer2.weight", (1, 2)),
            LayerSpec("layer2.bias", (1,)),
        ),
    ),
    FixtureSpec(
        name="mini_mlp_75_c4",
        client_count=4,
        example_counts=(800, 1200, 1600, 2400),
        layers=(
            LayerSpec("dense1.weight", (6, 8)),
            LayerSpec("dense1.bias", (6,)),
            LayerSpec("dense2.weight", (3, 6)),
            LayerSpec("dense2.bias", (3,)),
        ),
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate JSON fixtures for FedAvg merge benchmarks."
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/generated_fedavg"),
        help="Output root. Default: data/generated_fedavg.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Base RNG seed. Default: 42.",
    )
    return parser.parse_args()


def layer_size(shape: tuple[int, ...]) -> int:
    size = 1
    for dim in shape:
        size *= dim
    return size


def build_layout(layers: tuple[LayerSpec, ...]) -> list[dict[str, Any]]:
    layout = []
    cursor = 0
    for layer in layers:
        size = layer_size(layer.shape)
        layout.append(
            {
                "name": layer.name,
                "shape": list(layer.shape),
                "start": cursor,
                "end": cursor + size,
            }
        )
        cursor += size
    return layout


def reshape_for_json(values: np.ndarray, shape: tuple[int, ...]) -> Any:
    return values.reshape(shape).tolist()


def unflatten(flat: np.ndarray, layout: list[dict[str, Any]]) -> dict[str, Any]:
    parameters = {}
    for entry in layout:
        start = int(entry["start"])
        end = int(entry["end"])
        shape = tuple(int(dim) for dim in entry["shape"])
        parameters[str(entry["name"])] = reshape_for_json(flat[start:end], shape)
    return parameters


def make_client_flat(
    rng: np.random.Generator,
    param_count: int,
    client_index: int,
) -> np.ndarray:
    base = np.linspace(0.25, 2.50, param_count, dtype=np.float64)
    trend = (client_index + 1) * 0.125
    jitter = rng.normal(loc=0.0, scale=0.015, size=param_count)
    return base + trend + jitter


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n")


def generate_fixture(spec: FixtureSpec, out_root: Path, seed: int) -> None:
    fixture_dir = out_root / spec.name
    fixture_dir.mkdir(parents=True, exist_ok=True)

    layout = build_layout(spec.layers)
    param_count = int(layout[-1]["end"])
    rng = np.random.default_rng(seed + param_count + spec.client_count)

    total_examples = sum(spec.example_counts)
    clients = []
    global_flat = np.zeros(param_count, dtype=np.float64)

    for client_index in range(spec.client_count):
        client_id = f"client_{client_index + 1:03d}"
        num_examples = spec.example_counts[client_index]
        alpha = num_examples / total_examples
        flat = make_client_flat(rng, param_count, client_index)
        global_flat += alpha * flat

        clients.append(
            {
                "client_id": client_id,
                "num_examples": num_examples,
                "alpha": alpha,
                "parameters": unflatten(flat, layout),
                "parameters_flat": flat.tolist(),
            }
        )

    write_json(
        fixture_dir / "layout.json",
        {
            "fixture_name": spec.name,
            "param_count": param_count,
            "layout": layout,
        },
    )
    write_json(
        fixture_dir / "clients.json",
        {
            "fixture_name": spec.name,
            "client_count": spec.client_count,
            "param_count": param_count,
            "total_examples": total_examples,
            "clients": clients,
        },
    )
    write_json(
        fixture_dir / "expected_global.json",
        {
            "fixture_name": spec.name,
            "param_count": param_count,
            "expected_global_parameters": unflatten(global_flat, layout),
            "expected_global_flat": global_flat.tolist(),
        },
    )
    write_json(
        fixture_dir / "README.json",
        {
            "purpose": "FedAvg merge fixture for plaintext and OpenFHE CKKS benchmarks.",
            "notes": [
                "clients.json is the generated client result package.",
                "expected_global.json is the correctness oracle.",
                "OpenFHE benchmark encrypts flattened chunks, serializes/deserializes ciphertexts, merges, decrypts, and compares.",
            ],
        },
    )

    print(
        f"Generated {fixture_dir} "
        f"clients={spec.client_count} params={param_count}"
    )


def main() -> None:
    args = parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for spec in FIXTURES:
        generate_fixture(spec, args.out, args.seed)
    print(f"Saved FedAvg fixtures under: {args.out}")


if __name__ == "__main__":
    main()
