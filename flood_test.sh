#!/usr/bin/env bash
cd "$(dirname "$0")"
PI=${1:-pi}; export PI
HOST=$(ssh -G "$PI" | awk '/^hostname /{print $2}')
echo "target: $PI ($HOST)"

echo "--- log size before";  ./pitool.sh logsize
echo "--- udp counters before"; ./pitool.sh udperr

timeout 45 stdbuf -oL ./play.sh "$HOST" alice > /tmp/alice.log 2>&1 &
timeout 45 stdbuf -oL ./play.sh "$HOST" bob   > /tmp/bob.log   2>&1 &
sleep 12                                       # both players join and settle

echo "--- flooding for 15 s"
timeout 15 ./build/fuzz/fuzz "$HOST" 9000 1 100000000 0 > /dev/null &
sleep 8
echo "--- the Pi during the flood"; ./pitool.sh stats
wait

echo "--- log size after";   ./pitool.sh logsize
echo "--- udp counters after";  ./pitool.sh udperr
echo "--- snaps/s per second for alice (normal is 30):"
grep -o 'snaps/s=[0-9]*' /tmp/alice.log | cut -d= -f2 | tr '\n' ' '; echo