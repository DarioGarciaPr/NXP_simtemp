#!/bin/bash
set -e

# Paths
MODULE_DIR="$(dirname "$0")/../kernel"
CLI_DIR="$(dirname "$0")/../user/cli"
MODULE_NAME="nxp_simtemp.ko"
DEVICE_NAME="nxp_simtemp"

echo "=== Building nxp_simtemp module and CLI ==="

# --- Clean and build kernel module ---
echo "[MODULE] Cleaning..."
make -C "$MODULE_DIR" clean

echo "[MODULE] Building..."
make -C "$MODULE_DIR"

echo "[MODULE PASS] Module built: $MODULE_NAME"

# --- Build CLI ---
echo "[CLI] Cleaning..."
rm -f "$CLI_DIR/main"

echo "[CLI] Building..."
g++ -O2 -Wall -std=c++17 -o "$CLI_DIR/main" "$CLI_DIR/main.cpp"

echo "[CLI PASS] CLI built"

