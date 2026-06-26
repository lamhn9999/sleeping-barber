#!/usr/bin/env bash
#
# served_correct.sh — the SAME two-terminal scenario on the CORRECT shop.
#
#     1. BARBER      — ./correct barber 3
#     2. CUSTOMER    — ./correct customer  (press ENTER to add a customer)
#
# The barber runs the SAME 8-second dozing-off countdown as the naive shop
# ("...about to nod off"). In ./demo/lost_customer.sh, pressing ENTER DURING
# that countdown loses the customer forever. Here, do the exact same thing —
# press ENTER mid-countdown — and the customer is STILL served, because
# signal(custReady) is remembered. That is the side-by-side payoff: identical
# timing, opposite outcome. Add more customers than the 3 seats and the extras
# leave cleanly ("waiting room is full").
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
No graphical terminal detected. Open TWO terminals in $(pwd) and run:

  Terminal 1 (barber):    ./correct barber 3
  Terminal 2 (customer):  ./correct customer    # press ENTER to add a customer
EOF
    exit 0
fi

launch "1-BARBER (correct)"   "SB_SPEED=$SB_SPEED ./correct barber 3"
launch "2-CUSTOMER (correct)" "SB_SPEED=$SB_SPEED ./correct customer"

cat <<EOF
Two windows opened:
  1-BARBER     runs the SAME 8s countdown as the naive shop whenever it naps
  2-CUSTOMER   press ENTER to add a customer to the queue

Do the EXACT move that loses a customer in ./demo/lost_customer.sh: press ENTER
in the CUSTOMER window WHILE the barber is counting down. Here the customer is
STILL served — same timing, opposite outcome. Press it 4+ times fast to fill the
3 seats and watch the extra customer leave cleanly.

Close the demo with Ctrl-C in the BARBER window (it resets the shared shop).
EOF
