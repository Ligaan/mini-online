#!/usr/bin/env bash
set -euo pipefail

HOST=pi
DIR=mini-online

rsync -az --delete --exclude build --exclude .git ./ "${HOST}:${DIR}/"
ssh "$HOST" "cd $DIR && cmake -S . -B build && cmake --build build"
ssh "$HOST" "sudo systemctl restart mini-online"
echo "Deployed"