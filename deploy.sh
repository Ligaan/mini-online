#!/usr/bin/env bash
set -euo pipefail

HOST=pi
DIR=mini-online

rsync -az --delete --exclude build --exclude .git ./ "${HOST}:${DIR}/"
ssh "$HOST" "cd $DIR && docker compose up --build -d"
echo "Deployed"