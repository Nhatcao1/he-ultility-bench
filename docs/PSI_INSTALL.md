# PSI Install Notes

This project keeps PSI tooling separate from the normal OpenFHE benchmark
environment. OpenFHE remains responsible for CKKS numeric computation; PSI is
for private key matching.

## Recommended First Tool: OpenMined PSI

OpenMined PSI is a Private Set Intersection protocol based on ECDH with Golomb
Compressed Sets or Bloom Filters. Its README says the protocol lets the client
determine the intersection without revealing non-matching data, and that the
Python package can be installed with:

```bash
pip install openmined-psi
```

Use this first for:

```text
psi_key_match_customer_id
```

This is an exact-key matching sanity check before we combine PSI with OpenFHE.

## Optional Later Tool: Microsoft APSI

Microsoft APSI is better suited for labeled PSI / key-value lookup. APSI's
README describes labeled mode as a privacy-preserving key-value query where the
item is the key and the label is the value. APSI uses the BFV scheme implemented
in Microsoft SEAL.

Use APSI later for:

```text
customer_id -> risk_weight payload lookup
```

## Server Preparation

On the server, install base build tools:

```bash
sudo apt update
sudo apt install -y \
  git \
  build-essential \
  clang \
  cmake \
  ninja-build \
  pkg-config \
  curl \
  zip \
  unzip \
  tar \
  python3 \
  python3-venv \
  python3-pip \
  openjdk-17-jdk
```

Pull the repo and install the lightweight Python PSI package:

```bash
cd ~/he-ultility-bench
git pull

bash scripts/setup_psi_python_env.sh
source .venv-psi/bin/activate
```

If we need to build OpenMined PSI C++ targets or run its upstream benchmarks,
install Bazelisk as `bazel`:

```bash
sudo curl -L \
  -o /usr/local/bin/bazel \
  https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64
sudo chmod +x /usr/local/bin/bazel
bazel version
```

Then clone/build OpenMined PSI outside this repo:

```bash
cd ~
git clone https://github.com/OpenMined/PSI.git openmined-psi
cd openmined-psi

bazel build -c opt //private_set_intersection/cpp/...
bazel run -c opt //private_set_intersection/cpp:psi_benchmark
```

## APSI Preparation Later

For Microsoft APSI, use vcpkg:

```bash
cd ~
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install apsi
```

If we need APSI CLI tools:

```bash
cd ~
git clone https://github.com/microsoft/APSI.git
cmake -S APSI -B apsi-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DAPSI_BUILD_CLI=ON \
  -DAPSI_BUILD_TESTS=OFF

cmake --build apsi-build --parallel "$(nproc)"
```

## Local Machine

Do not build PSI libraries locally unless explicitly needed. Keep heavy PSI,
Bazel, and vcpkg work on the server.

Local repo work should stay limited to:

```text
docs
fixture generators
small Python scripts
benchmark integration code
```

## First Benchmark Path

```text
1. Generate transaction/customer key files from existing CSVs.
2. Run OpenMined PSI to match customer_id.
3. Produce aligned amount/risk_weight vectors.
4. Run OpenFHE CKKS weighted sum:
     Enc(amount) * Enc(risk_weight)
     EvalSum
5. Compare against plain C++ baseline.
```
