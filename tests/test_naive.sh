#!/usr/bin/env bash
#
# test_naive.sh — proves the naive shop is BROKEN, using the SAME two-terminal
# format as the demo: a barber process, and customers added by feeding ENTER
# (newlines) into `./naive customer`.
#
#   1. lost-wakeup : the genuine barber/customer race deadlocks (no fake gap)
#   2. happy path  : a customer added after the barber is asleep IS served
#   3. counter race: unsynchronised waiting++ loses increments
#
# Deterministic — no terminals required. Run from the repo root.
set -u
cd "$(dirname "$0")/.."

C_OK=$'\033[1;32m'; C_BAD=$'\033[1;31m'; C_H=$'\033[1;36m'; C_0=$'\033[0m'
pass=0; fail=0
ok()  { echo "${C_OK}PASS${C_0} $1"; pass=$((pass+1)); }
bad() { echo "${C_BAD}FAIL${C_0} $1"; fail=$((fail+1)); }
export SB_SPEED=15000
pkill -x naive 2>/dev/null; ./naive reset >/dev/null 2>&1; sleep 0.3   # hermetic start

echo "${C_H}== naive test 1: LOST WAKEUP (genuine race, no artificial gap) ==${C_0}"
# `deadlock` races a real barber thread against a real customer thread, with no
# manufactured delay, until the unlucky interleaving loses the wake-up.
out=$(timeout 30 ./naive deadlock 2>&1)
echo "$out" | grep -E "LOST WAKEUP|Reproduced" | sed 's/^/    /'
if echo "$out" | grep -q "Reproduced from a REAL race"; then
    ok "the lost-wakeup deadlock was reproduced from a real race"
else
    bad "expected a reproduced deadlock (timing dependent; try rerunning)"
fi

echo "${C_H}== naive test 2: HAPPY PATH (customer added after barber sleeps) ==${C_0}"
./naive barber </dev/null >/tmp/sb_n2_barber.log 2>&1 &
bpid=$!
sleep 0.8                                    # room empty => barber is already asleep
printf '\n' | timeout 6 ./naive customer >/tmp/sb_n2_cust.log 2>&1
rc=$?
kill $bpid 2>/dev/null; wait $bpid 2>/dev/null
./naive reset >/dev/null 2>&1
if [ $rc -eq 0 ] && grep -q "Leaving happy" /tmp/sb_n2_cust.log; then
    ok "the customer was served — same function, different timing"
else
    bad "expected the customer to be served (rc=0); got rc=$rc"
fi

echo "${C_H}== naive test 3: COUNTER RACE (unsynchronised waiting++) ==${C_0}"
out=$(./naive race 8)
echo "$out" | sed 's/^/    /'
if echo "$out" | grep -q "increments LOST"; then
    ok "increments were lost — classic race condition"
else
    bad "race did not lose increments (timing dependent; try rerunning)"
fi

echo
echo "${C_H}naive: ${pass} passed, ${fail} failed${C_0}"
[ $fail -eq 0 ]
