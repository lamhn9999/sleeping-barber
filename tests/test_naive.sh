#!/usr/bin/env bash
#
# test_naive.sh — proves the naive shop is BROKEN, using the SAME two-terminal
# format as the demo: a barber process, and customers added by feeding ENTER
# (newlines) into `./naive customer`.
#
#   1. lost-wakeup : a customer added during the barber's gap is never served
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

echo "${C_H}== naive test 1: LOST WAKEUP (customer added during the gap) ==${C_0}"
BARBER_WINDOW=3 ./naive barber </dev/null >/tmp/sb_n1_barber.log 2>&1 &
bpid=$!
sleep 0.8                                    # barber is mid-gap, not yet asleep
printf '\n' | timeout 6 ./naive customer >/tmp/sb_n1_cust.log 2>&1
rc=$?
kill $bpid 2>/dev/null; wait $bpid 2>/dev/null
./naive reset >/dev/null 2>&1
if [ $rc -eq 124 ] && ! grep -q "Leaving happy" /tmp/sb_n1_cust.log; then
    ok "the customer was LOST (stuck forever) — the bug is reproduced"
else
    bad "expected the customer to hang (rc=124); got rc=$rc"
fi

echo "${C_H}== naive test 2: HAPPY PATH (customer added after barber sleeps) ==${C_0}"
BARBER_WINDOW=0 ./naive barber </dev/null >/tmp/sb_n2_barber.log 2>&1 &
bpid=$!
sleep 0.8                                    # gap=0 => barber is already asleep
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
