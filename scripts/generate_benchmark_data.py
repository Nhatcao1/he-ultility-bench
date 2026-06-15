#!/usr/bin/env python3
"""Generate numeric benchmark CSV data for HE and plaintext baselines."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import numpy as np


PRODUCTS = [
    "iPhone 16 Pro",
    "Samsung S25 Ultra",
    "MacBook Pro M4",
    "Galaxy Tab",
    "AirPods Pro",
    "Apple Watch",
    "Dell XPS",
    "ThinkPad X1",
    "Sony XM6",
    "Netflix",
    "Spotify",
    "YouTube Premium",
    "ChatGPT Plus",
    "AWS EC2",
    "Azure VM",
    "Google Workspace",
    "Microsoft 365",
    "Viettel Fiber",
    "VNPT Fiber",
    "FPT Internet",
]

CHANNELS = [
    "Facebook",
    "TikTok",
    "YouTube",
    "Instagram",
    "Telegram",
    "Zalo",
    "Website",
    "Shopee",
    "Lazada",
    "Tiki",
]

START_DATE = np.datetime64("2024-01-01")
END_DATE = np.datetime64("2025-12-31")
MAX_DAYS = int((END_DATE - START_DATE) / np.timedelta64(1, "D"))

TRANSACTION_HEADER = [
    "row_id",
    "customer_id",
    "product_id",
    "channel_id",
    "event_day",
    "x1",
    "x2",
    "x3",
    "i1",
    "i2",
    "amount",
    "mask_channel_5",
    "mask_amount_gt_5000",
    "linear_score",
    "label",
]

CUSTOMER_HEADER = ["customer_id", "risk_weight", "segment_id", "region_id"]


@dataclass(frozen=True)
class DatasetSpec:
    rows: int
    name: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate numeric CSV datasets for OpenFHE benchmark work."
    )
    parser.add_argument(
        "--sizes",
        nargs="+",
        type=int,
        default=[1_000, 10_000, 100_000],
        help="Dataset row counts to generate. Default: 1000 10000 100000.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/generated"),
        help="Output directory. Default: data/generated.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Base random seed. Default: 42.",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=100_000,
        help="Rows to write per CSV chunk. Default: 100000.",
    )
    parser.add_argument(
        "--num-customers",
        type=int,
        default=10_000,
        help="Number of customer dimension rows. Default: 10000.",
    )
    parser.add_argument(
        "--noise-std",
        type=float,
        default=300.0,
        help="Gaussian noise standard deviation for linear_score. Default: 300.",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if not args.sizes:
        raise ValueError("--sizes must include at least one row count")
    if any(size <= 0 for size in args.sizes):
        raise ValueError("all --sizes values must be positive")
    if args.chunk_size <= 0:
        raise ValueError("--chunk-size must be positive")
    if args.num_customers <= 0:
        raise ValueError("--num-customers must be positive")
    if args.noise_std < 0:
        raise ValueError("--noise-std must be non-negative")


def dataset_name(rows: int) -> str:
    if rows == 1_000:
        return "tiny_1k"
    if rows == 10_000:
        return "small_10k"
    if rows == 100_000:
        return "medium_100k"
    if rows % 1_000_000 == 0:
        return f"custom_{rows // 1_000_000}m"
    if rows % 1_000 == 0:
        return f"custom_{rows // 1_000}k"
    return f"custom_{rows}"


def dataset_specs(sizes: Iterable[int]) -> list[DatasetSpec]:
    return [DatasetSpec(rows=size, name=dataset_name(size)) for size in sizes]


def write_lookup(path: Path, id_name: str, value_name: str, values: list[str]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([id_name, value_name])
        writer.writerows((idx, value) for idx, value in enumerate(values))


def write_customers(path: Path, rng: np.random.Generator, num_customers: int) -> None:
    customer_id = np.arange(num_customers, dtype=np.int64)
    risk_weight = rng.uniform(0.1, 2.0, num_customers).astype(np.float32)
    segment_id = rng.integers(0, 8, num_customers, dtype=np.int32)
    region_id = rng.integers(0, 16, num_customers, dtype=np.int32)

    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(CUSTOMER_HEADER)
        for row in zip(customer_id, risk_weight, segment_id, region_id):
            writer.writerow(
                [
                    int(row[0]),
                    f"{float(row[1]):.6f}",
                    int(row[2]),
                    int(row[3]),
                ]
            )


def generate_transaction_arrays(
    rng: np.random.Generator,
    rows: int,
    num_customers: int,
    noise_std: float,
) -> dict[str, np.ndarray]:
    data: dict[str, np.ndarray] = {
        "row_id": np.arange(rows, dtype=np.int64),
        "customer_id": rng.integers(0, num_customers, rows, dtype=np.int64),
        "product_id": rng.integers(0, len(PRODUCTS), rows, dtype=np.int32),
        "channel_id": rng.integers(0, len(CHANNELS), rows, dtype=np.int32),
        "event_day": rng.integers(0, MAX_DAYS, rows, dtype=np.int32),
        "x1": rng.uniform(0, 10_000, rows).astype(np.float32),
        "x2": rng.uniform(0, 10_000, rows).astype(np.float32),
        "x3": rng.uniform(0, 10_000, rows).astype(np.float32),
        "i1": rng.integers(0, 1_000_000, rows, dtype=np.int32),
        "i2": rng.integers(0, 1_000_000, rows, dtype=np.int32),
        "amount": rng.uniform(0, 10_000, rows).astype(np.float32),
    }

    noise = rng.normal(0, noise_std, rows).astype(np.float32)
    linear_score = (
        data["x1"] * 0.30
        + data["x2"] * 0.20
        + data["x3"] * 0.10
        + data["i1"] / 50_000
        + data["i2"] / 50_000
        + noise
    ).astype(np.float32)
    threshold = np.percentile(linear_score, 70)

    data["mask_channel_5"] = (data["channel_id"] == 5).astype(np.int8)
    data["mask_amount_gt_5000"] = (data["amount"] > 5_000).astype(np.int8)
    data["linear_score"] = linear_score
    data["label"] = (linear_score > threshold).astype(np.int8)
    return data


def write_transactions(path: Path, data: dict[str, np.ndarray], chunk_size: int) -> None:
    rows = len(data["row_id"])

    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(TRANSACTION_HEADER)

        for start in range(0, rows, chunk_size):
            end = min(start + chunk_size, rows)
            chunk_rows = []

            for idx in range(start, end):
                chunk_rows.append(
                    [
                        int(data["row_id"][idx]),
                        int(data["customer_id"][idx]),
                        int(data["product_id"][idx]),
                        int(data["channel_id"][idx]),
                        int(data["event_day"][idx]),
                        f"{float(data['x1'][idx]):.6f}",
                        f"{float(data['x2'][idx]):.6f}",
                        f"{float(data['x3'][idx]):.6f}",
                        int(data["i1"][idx]),
                        int(data["i2"][idx]),
                        f"{float(data['amount'][idx]):.6f}",
                        int(data["mask_channel_5"][idx]),
                        int(data["mask_amount_gt_5000"][idx]),
                        f"{float(data['linear_score'][idx]):.6f}",
                        int(data["label"][idx]),
                    ]
                )

            writer.writerows(chunk_rows)


def generate_dataset(
    spec: DatasetSpec,
    out_dir: Path,
    seed: int,
    chunk_size: int,
    num_customers: int,
    noise_std: float,
) -> None:
    dataset_dir = out_dir / spec.name
    dataset_dir.mkdir(parents=True, exist_ok=True)

    # Derive per-dataset streams so changing one dataset size does not alter another.
    transaction_rng = np.random.default_rng(seed + spec.rows)
    customer_rng = np.random.default_rng(seed + spec.rows + 1_000_000_007)

    print(f"Generating {spec.name}: {spec.rows:,} transaction rows")
    write_lookup(dataset_dir / "product_lookup.csv", "product_id", "product_name", PRODUCTS)
    write_lookup(dataset_dir / "channel_lookup.csv", "channel_id", "channel_name", CHANNELS)
    write_customers(dataset_dir / "customers.csv", customer_rng, num_customers)

    transactions = generate_transaction_arrays(
        transaction_rng,
        spec.rows,
        num_customers,
        noise_std,
    )
    write_transactions(dataset_dir / "transactions.csv", transactions, chunk_size)

    label_rate = float(np.mean(transactions["label"]))
    print(
        f"  saved {dataset_dir / 'transactions.csv'} "
        f"(label_1_rate={label_rate:.4f})"
    )


def main() -> None:
    args = parse_args()
    validate_args(args)
    args.out.mkdir(parents=True, exist_ok=True)

    for spec in dataset_specs(args.sizes):
        generate_dataset(
            spec=spec,
            out_dir=args.out,
            seed=args.seed,
            chunk_size=args.chunk_size,
            num_customers=args.num_customers,
            noise_std=args.noise_std,
        )

    print("DONE")
    print(f"Saved datasets under: {args.out}")


if __name__ == "__main__":
    main()
