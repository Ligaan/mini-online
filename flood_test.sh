#!/usr/bin/env bash
cd "$(dirname "$0")"
PI=${1:-pi}; export PI
HOST=$(ssh -G "$PI" | awk '/^hostname /{print $2}')
echo "target: $PI ($HOST)"

echo "--- clean start (restart matchmaker, wait for the game server to re-register)"
./pitool.sh restart > /dev/null
sleep 6

echo "--- log size before";     ./pitool.sh logsize
echo "--- udp counters before"; ./pitool.sh udperr

timeout 50 stdbuf -oL ./play.sh "$HOST" alice > /tmp/alice.log 2>&1 &
timeout 50 stdbuf -oL ./play.sh "$HOST" bob   > /tmp/bob.log   2>&1 &

joined() {
    grep -q 'snaps/s=[1-9]' /tmp/alice.log 2>/dev/null && grep -q 'snaps/s=[1-9]' /tmp/bob.log 2>/dev/null
}
for i in $(seq 1 25); do joined && break; sleep 1; done
if ! joined; then
    echo "players did not join, aborting. Start of the logs:"
    head -n 5 /tmp/alice.log /tmp/bob.log
    kill $(jobs -p) 2>/dev/null
    exit 1
fi
sleep 3

echo "--- flooding for 15 s"
timeout 15 stdbuf -oL ./build/fuzz/fuzz "$HOST" 9000 1 100000000 0 > /tmp/fuzz.log &
sleep 8
echo "--- CPU on the Pi during the flood"; ./pitool.sh cpu
wait
echo "--- fuzz progress: $(grep -o 'sent [0-9]*' /tmp/fuzz.log | tail -1)"

echo "--- log size after";        ./pitool.sh logsize
echo "--- udp counters after";    ./pitool.sh udperr
echo "--- alice snaps/s per second (normal 30):"
grep -o 'snaps/s=[0-9]*' /tmp/alice.log | cut -d= -f2 | tr '\n' ' '; echo
echo "--- alice pending per second (normal 1):"
grep -o 'pending=[0-9]*' /tmp/alice.log | cut -d= -f2 | tr '\n' ' '; echo
echo "--- alice corrections per second (normal 0):"
grep -o 'corrections=[0-9]*' /tmp/alice.log | cut -d= -f2 | tr '\n' ' '; echo
echo "--- the server's own view:"
./pitool.sh logs 150 | grep -E "stats:|inputs/s"