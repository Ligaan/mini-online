#!/usr/bin/env bash
HOST=$1; MM=http://$HOST:8080; S=$RANDOM
A=$(curl -s -X POST $MM/login -d "{\"name\":\"ta$S\"}" | jq -r .token)
B=$(curl -s -X POST $MM/login -d "{\"name\":\"tb$S\"}" | jq -r .token)
curl -s -X POST $MM/queue -H "Authorization: Bearer $A" > /dev/null
curl -s -X POST $MM/queue -H "Authorization: Bearer $B" | jq -r .ticket