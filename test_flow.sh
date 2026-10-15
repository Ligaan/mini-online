#!/usr/bin/env bash
cd "$(dirname "$0")"

MM=http://localhost:8080
pass=0
fail=0

check() {
    if [ "$2" = "$3" ]; then
        echo "PASS $1"; pass=$((pass + 1))
    else
        echo "FAIL $1 (expected '$2', got '$3')"; fail=$((fail + 1))
    fi
}

code() {
    curl -s -o /dev/null -w "%{http_code}" "$@"
}

login() {
    curl -s -X POST $MM/login -d "{\"name\":\"$1\"}" | jq -r .token
}

A=$(login alice)
B=$(login bob)
C=$(login carol)

check "queue without token" 401 "$(code -X POST $MM/queue)"
check "alice queue"         202 "$(code -X POST $MM/queue -H "Authorization: Bearer $A")"

BOBQ=$(curl -s -X POST $MM/queue -H "Authorization: Bearer $B")
check "bob is matched"       matched "$(echo "$BOBQ" | jq -r .status)"
TB=$(echo "$BOBQ" | jq -r .ticket)
SID=$(echo "$BOBQ" | jq -r .session)
TA=$(curl -s $MM/match -H "Authorization: Bearer $A" | jq -r .ticket)

check "alice sees her match"        matched "$(curl -s $MM/match -H "Authorization: Bearer $A" | jq -r .status)"
check "alice queues again"          409 "$(code -X POST $MM/queue -H "Authorization: Bearer $A")"
check "carol blocked from session"  403 "$(code $MM/session/$SID -H "Authorization: Bearer $C")"
check "alice reads her session"     200 "$(code $MM/session/$SID -H "Authorization: Bearer $A")"
check "ticket rejected as bearer"   401 "$(code -X POST $MM/queue -H "Authorization: Bearer $TB")"


timeout 6 stdbuf -oL ./build/client/client 127.0.0.1 "$TA" > /tmp/alice.log 2>&1 &
timeout 6 stdbuf -oL ./build/client/client 127.0.0.1 "$TB" > /tmp/bob.log 2>&1 &
timeout 6 stdbuf -oL ./build/client/client 127.0.0.1 "${TA}x" > /tmp/forged.log 2>&1 &
wait

check "alice receives snapshots" yes "$(grep  -q 'snaps/s=[1-9]' /tmp/alice.log && echo yes || echo no)"
check "bob receives snapshots"      yes "$(grep -q 'snaps/s=[1-9]' /tmp/bob.log && echo yes || echo no)"
check "forged ticket gets nothing"  no  "$(grep -q 'snaps/s=[1-9]' /tmp/forged.log && echo yes || echo no)"

echo
echo "passed: $pass     failed: $fail"
exit $((fail > 0)) 