#!/usr/bin/env bash
#
# test_correct.sh — proves the semaphore shop is CORRECT, using the SAME
# two-terminal format: a barber process, and customers added by feeding ENTER
# (newlines) into `./correct customer`.
#
#   1. no lost wakeup : a customer added at the "bad" moment is still served
#   2. full room      : with N seats, extra simultaneous customers leave cleanly
#
# Deterministic — no terminals required. Run from the repo root.
set -u
cd "$(dirname "$0")/.."

C_OK=$'\033[1;32m'; C_BAD=$'\033[1;31m'; C_H=$'\033[1;36m'; C_0=$'\033[0m'
pass=0; fail=0
ok()  { echo "${C_OK}PASS${C_0} $1"; pass=$((pass+1)); }
bad() { echo "${C_BAD}FAIL${C_0} $1"; fail=$((fail+1)); }
export SB_SPEED=15000
pkill -x correct 2>/dev/null; ./correct reset >/dev/null 2>&1; sleep 0.3   # hermetic start

echo "${C_H}== correct test 1: NO LOST WAKEUP (same real race naive deadlocks on) ==${C_0}"
# `prove` runs the SAME thread race that ./naive deadlock loses — a sched_yield
# at the exact race point, no artificial gap — many times over. Where naive
# deadlocks, signal(custReady) is remembered here, so every customer is served.
out=$(timeout 30 ./correct prove 2>&1)
echo "$out" | grep -E "served, 0 lost|NOT served" | sed 's/^/    /'
if echo "$out" | grep -q "0 lost"; then
    ok "every customer served across the race — the lost-wakeup bug is gone"
else
    bad "expected 0 lost customers; the fix appears broken"
fi

echo "${C_H}== correct test 2: FULL WAITING ROOM (N=1, three customers at once) ==${C_0}"
SB_SPEED=300000 ./correct barber 1 </dev/null >/tmp/sb_c2_barber.log 2>&1 &
bpid=$!
sleep 0.5
# Three customers arrive together but there is only one seat -> at least one
# must leave without a haircut (cleanly — no hang, no corruption).
printf '\n\n\n' | timeout 8 ./correct customer >/tmp/sb_c2_cust.log 2>&1
kill $bpid 2>/dev/null; wait $bpid 2>/dev/null
pkill -x correct 2>/dev/null
./correct reset >/dev/null 2>&1
if grep -q "without a haircut" /tmp/sb_c2_cust.log; then
    n=$(grep -c "without a haircut" /tmp/sb_c2_cust.log)
    ok "$n customer(s) found the room full and left cleanly"
else
    bad "expected at least one customer to leave without a haircut"
fi

echo
echo "${C_H}correct: ${pass} passed, ${fail} failed${C_0}"
[ $fail -eq 0 ]
