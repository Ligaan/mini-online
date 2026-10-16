#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

HOST=${1:-pi}
DIR=mini-online
ADDR=$(ssh -G "$HOST" | awk '/^hostname /{print $2}')

echo "deploying to $HOST ($ADDR)"
rsync -az --delete --exclude build --exclude .git ./ "${HOST}:${DIR}/"
ssh "$HOST" "cd $DIR && PI_ADDR=$ADDR docker compose up --build -d"
echo "Deployed"