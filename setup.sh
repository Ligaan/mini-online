#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config git curl rsync jq \
    libssl-dev libprotobuf-dev protobuf-compiler \
    libgrpc++-dev protobuf-compiler-grpc
echo "dependencies installed"