#!/usr/bin/env bash
#
# served_correct.sh — the SAME two-terminal scenario on the CORRECT shop.
#
#     1. BARBER      — ./correct barber 3
#     2. CUSTOMER    — ./correct customer  (press ENTER to add a customer)
#     3. PROVE       — ./correct prove      (the fix, under the real race)
#
# There is no artificial countdown. By hand the barber/customer windows just
# show the shop working: every ENTER is served, and more customers than the 3
# seats leave cleanly ("waiting room is full").
#
# The payoff is the PROVE window: `./correct prove` runs the EXACT same thread
# race that `./naive deadlock` loses (a sched_yield at the race point, no
# manufactured gap), many times over, and every customer is served — because
# signal(custReady) is remembered. Identical interleaving, opposite outcome.
#
set -u
cd "$(dirname "$0")/.."
make -s correct
export SB_SPEED=350000

term=""
for t in konsole gnome-terminal xfce4-terminal xterm; do
    command -v "$t" >/dev/null 2>&1 && { term=$t; break; }
done

launch() {
    local title=$1 cmd=$2
    case "$term" in
        konsole)        konsole --hold -p tabtitle="$title" -e bash -c "$cmd" & ;;
        gnome-terminal) gnome-terminal --title="$title" -- bash -c "$cmd; exec bash" & ;;
        xfce4-terminal) xfce4-terminal --title="$title" --hold -e "bash -c '$cmd'" & ;;
        xterm)          xterm -T "$title" -e bash -c "$cmd; exec bash" & ;;
    esac
}

pkill -x correct 2>/dev/null
./correct reset >/dev/null 2>&1

if [ -z "$term" ] || [ -z "${DISPLAY:-}" ]; then
    cat <<EOF
No graphical terminal detected. Open terminals in $(pwd) and run:

  Terminal 1 (barber):    ./correct barber 3
  Terminal 2 (customer):  ./correct customer    # press ENTER to add a customer
  Terminal 3 (prove):     ./correct prove        # same real race naive loses

The prove window serves every customer; the naive shop deadlocks on the same
race (see ./naive deadlock).
EOF
    exit 0
fi

launch "1-BARBER (correct)"   "SB_SPEED=$SB_SPEED ./correct barber 3"
launch "2-CUSTOMER (correct)" "SB_SPEED=$SB_SPEED ./correct customer"
launch "3-PROVE (correct)"    "./correct prove; echo; echo '(press ENTER to close)'; read"

cat <<EOF
Three windows opened:
  1-BARBER     naps whenever the room is empty (no countdown — the real shop)
  2-CUSTOMER   press ENTER to add a customer; they get served
  3-PROVE      races a real barber vs. customer; every customer is served

Window 3 is the payoff: the SAME race that deadlocks ./naive deadlock serves
everyone here, because signal(custReady) is remembered. Press ENTER 4+ times in
the CUSTOMER window to fill the 3 seats and watch the extra customer leave
cleanly.

Close the demo with Ctrl-C in the BARBER window (it resets the shared shop).
EOF
