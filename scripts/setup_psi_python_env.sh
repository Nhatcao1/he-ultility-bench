#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

python3 -m venv .venv-psi
source .venv-psi/bin/activate

python -m pip install --upgrade pip setuptools wheel
python -m pip install -r requirements-psi.txt
python -m pip show openmined-psi

cat <<'MSG'

PSI Python environment is ready.

Activate it with:
  source .venv-psi/bin/activate

MSG
