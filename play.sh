#!/usr/bin/env bash
HOST=$1; NAME=$2$RANDOM; MODE=${3:-}
MM=http://$HOST:8080
T=$(curl -s -X POST $MM/login -d "{\"name\":\"$NAME\"}" | jq -r .token)
curl -s -X POST $MM/queue -H "Authorization: Bearer $T" > /dev/null
echo "$NAME waiting for an opponent..."
until TK=$(curl -s $MM/match -H "Authorization: Bearer $T" | jq -er .ticket); do sleep 1; done
exec  "$(dirname "$0")/build/client/client" "$HOST" "$TK" $MODE