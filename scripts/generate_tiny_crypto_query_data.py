#!/usr/bin/env python3
"""Generate tiny CSV data for encrypted lookup, join, and comparison tests.

This generator is intentionally separate from generate_benchmark_data.py.
The normal generator is for throughput-style aggregation benchmarks. This one
is for tiny correctness/feasibility tests where fully encrypted keys may be
involved and naive encrypted joins can become rows x rows expensive.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


TRANSACTION_HEADER = [
    "tx_id",
    "customer_id",
    "amount",
    "channel_id",
    "x1",
    "x2",
]

CUSTOMER_HEADER = [
    "customer_id",
    "risk_weight",
    "segment_id",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate tiny query-primitive CSV data for OpenFHE tests."
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/generated_tiny_crypto_query"),
        help="Output root. Default: data/generated_tiny_crypto_query.",
    )
    parser.add_argument(
        "--rows",
        type=int,
        default=16,
        help="Transaction rows to generate. Default: 16.",
    )
    parser.add_argument(
        "--key-domain",
        type=int,
        default=16,
        help="Customer key domain size. Default: 16.",
    )
    parser.add_argument(
        "--name",
        default="join_lookup_16",
        help="Dataset directory name under --out. Default: join_lookup_16.",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if args.rows <= 0:
        raise ValueError("--rows must be positive")
    if args.key_domain <= 0:
        raise ValueError("--key-domain must be positive")


def onehot_columns(prefix: str, size: int) -> list[str]:
    return [f"{prefix}_{idx}" for idx in range(size)]


def onehot(value: int, size: int) -> list[int]:
    return [1 if idx == value else 0 for idx in range(size)]


def build_customers(key_domain: int) -> list[dict[str, float | int]]:
    customers: list[dict[str, float | int]] = []
    for customer_id in range(key_domain):
        # Deterministic small values make decrypted correctness easy to inspect.
        risk_weight = 0.50 + customer_id * 0.125
        segment_id = customer_id % 4
        customers.append(
            {
                "customer_id": customer_id,
                "risk_weight": risk_weight,
                "segment_id": segment_id,
            }
        )
    return customers


def build_transactions(rows: int, key_domain: int) -> list[dict[str, float | int]]:
    transactions: list[dict[str, float | int]] = []
    for tx_id in range(rows):
        # This pattern repeats keys but avoids purely sorted input. It is useful
        # for testing equality masks and lookup without needing a random seed.
        customer_id = (tx_id * 5 + 3) % key_domain
        amount = 100.0 + tx_id * 125.0
        channel_id = (tx_id * 3 + 1) % 4
        x1 = 0.25 + tx_id * 0.50
        x2 = 1.00 + tx_id * 0.25
        transactions.append(
            {
                "tx_id": tx_id,
                "customer_id": customer_id,
                "amount": amount,
                "channel_id": channel_id,
                "x1": x1,
                "x2": x2,
            }
        )
    return transactions


def write_rows(path: Path, header: list[str], rows: list[list[object]]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(rows)


def write_transactions(path: Path, transactions: list[dict[str, float | int]]) -> None:
    rows = [
        [
            tx["tx_id"],
            tx["customer_id"],
            f"{float(tx['amount']):.6f}",
            tx["channel_id"],
            f"{float(tx['x1']):.6f}",
            f"{float(tx['x2']):.6f}",
        ]
        for tx in transactions
    ]
    write_rows(path, TRANSACTION_HEADER, rows)


def write_transactions_onehot(
    path: Path,
    transactions: list[dict[str, float | int]],
    key_domain: int,
) -> None:
    header = TRANSACTION_HEADER + onehot_columns("customer_id_is", key_domain)
    rows = []
    for tx in transactions:
        rows.append(
            [
                tx["tx_id"],
                tx["customer_id"],
                f"{float(tx['amount']):.6f}",
                tx["channel_id"],
                f"{float(tx['x1']):.6f}",
                f"{float(tx['x2']):.6f}",
                *onehot(int(tx["customer_id"]), key_domain),
            ]
        )
    write_rows(path, header, rows)


def write_customers(path: Path, customers: list[dict[str, float | int]]) -> None:
    rows = [
        [
            customer["customer_id"],
            f"{float(customer['risk_weight']):.6f}",
            customer["segment_id"],
        ]
        for customer in customers
    ]
    write_rows(path, CUSTOMER_HEADER, rows)


def write_customers_onehot(
    path: Path,
    customers: list[dict[str, float | int]],
    key_domain: int,
) -> None:
    header = CUSTOMER_HEADER + onehot_columns("customer_id_is", key_domain)
    rows = []
    for customer in customers:
        rows.append(
            [
                customer["customer_id"],
                f"{float(customer['risk_weight']):.6f}",
                customer["segment_id"],
                *onehot(int(customer["customer_id"]), key_domain),
            ]
        )
    write_rows(path, header, rows)


def write_expected_lookup(
    path: Path,
    transactions: list[dict[str, float | int]],
    customers_by_id: dict[int, dict[str, float | int]],
) -> None:
    rows = []
    for tx in transactions:
        customer = customers_by_id[int(tx["customer_id"])]
        rows.append(
            [
                tx["tx_id"],
                tx["customer_id"],
                f"{float(customer['risk_weight']):.6f}",
            ]
        )
    write_rows(path, ["tx_id", "customer_id", "expected_risk_weight"], rows)


def write_expected_join(
    path: Path,
    transactions: list[dict[str, float | int]],
    customers_by_id: dict[int, dict[str, float | int]],
) -> None:
    rows = []
    for tx in transactions:
        customer = customers_by_id[int(tx["customer_id"])]
        amount = float(tx["amount"])
        risk_weight = float(customer["risk_weight"])
        rows.append(
            [
                tx["tx_id"],
                tx["customer_id"],
                f"{amount:.6f}",
                f"{risk_weight:.6f}",
                f"{amount * risk_weight:.6f}",
            ]
        )
    write_rows(
        path,
        ["tx_id", "customer_id", "amount", "risk_weight", "amount_times_risk"],
        rows,
    )


def write_expected_compare(
    path: Path,
    transactions: list[dict[str, float | int]],
    key_domain: int,
) -> None:
    header = [
        "tx_id",
        "amount_gt_500",
        "amount_gt_1000",
        "channel_eq_2",
        *onehot_columns("customer_eq", key_domain),
    ]
    rows = []
    for tx in transactions:
        amount = float(tx["amount"])
        channel_id = int(tx["channel_id"])
        customer_id = int(tx["customer_id"])
        rows.append(
            [
                tx["tx_id"],
                1 if amount > 500.0 else 0,
                1 if amount > 1000.0 else 0,
                1 if channel_id == 2 else 0,
                *onehot(customer_id, key_domain),
            ]
        )
    write_rows(path, header, rows)


def write_readme(path: Path, rows: int, key_domain: int) -> None:
    text = f"""# Tiny Crypto Query Dataset

This dataset is for encrypted lookup, encrypted join, and encrypted comparison
feasibility tests. It is intentionally tiny.

```text
transaction_rows = {rows}
customer_key_domain = {key_domain}
```

Files:

```text
transactions.csv          scalar customer_id representation
transactions_onehot.csv   one-hot customer_id representation
customers.csv             customer lookup table
customers_onehot.csv      one-hot customer_id representation
expected_lookup.csv       expected customer_id -> risk_weight lookup
expected_join.csv         expected joined amount/risk_weight rows
expected_compare.csv      expected comparison/equality masks
```

The scalar key form is useful for testing encrypted equality directly. The
one-hot form is useful for HE-friendly lookup designs where selecting a table
entry becomes a dot product with a mask vector.

Do not use this dataset for throughput claims. Use it to answer:

```text
Can the encrypted primitive work correctly at all?
How expensive is one tiny encrypted lookup/join/compare?
```
"""
    path.write_text(text)


def main() -> None:
    args = parse_args()
    validate_args(args)

    dataset_dir = args.out / args.name
    dataset_dir.mkdir(parents=True, exist_ok=True)

    customers = build_customers(args.key_domain)
    transactions = build_transactions(args.rows, args.key_domain)
    customers_by_id = {int(customer["customer_id"]): customer for customer in customers}

    write_transactions(dataset_dir / "transactions.csv", transactions)
    write_transactions_onehot(
        dataset_dir / "transactions_onehot.csv",
        transactions,
        args.key_domain,
    )
    write_customers(dataset_dir / "customers.csv", customers)
    write_customers_onehot(dataset_dir / "customers_onehot.csv", customers, args.key_domain)
    write_expected_lookup(dataset_dir / "expected_lookup.csv", transactions, customers_by_id)
    write_expected_join(dataset_dir / "expected_join.csv", transactions, customers_by_id)
    write_expected_compare(dataset_dir / "expected_compare.csv", transactions, args.key_domain)
    write_readme(dataset_dir / "README.md", args.rows, args.key_domain)

    print(f"Generated tiny crypto query dataset: {dataset_dir}")


if __name__ == "__main__":
    main()
