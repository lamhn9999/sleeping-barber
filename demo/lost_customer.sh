#!/usr/bin/env bash
#
# lost_customer.sh — VISUAL "lost customer" demo for the NAIVE shop.
#
# Two terminals only:
#     1. BARBER      — ./naive barber
#     2. CUSTOMER    — ./naive customer  (press ENTER to add a customer)
#
# --------------------------------------------------------------------------
# HOW TO RUN (narrate as you click):
#
#   The barber starts, sees an empty room, and begins an 8-second countdown
#   before napping: "...about to nod off (add a customer NOW to lose them)".
#
#   * To LOSE a customer (the bug):
#       press ENTER in the CUSTOMER window DURING the countdown.
#       The customer sees the barber still on his feet and sits to wait.
#       When the countdown ends the barber sleeps — and never wakes:
#         BARBER  : "Zzz... still asleep — DEADLOCK..."   (forever)
#         CUSTOMER: "still waiting — am I LOST?"           (forever)
#
#   * To SERVE a customer (for contrast, on a fresh run):
#       wait for the countdown to finish (barber says "Zzz..."), THEN press
#       ENTER. The customer wakes him and gets a haircut.
#
#   Same function both times — only the moment you press ENTER differs.
#   Compare with: ./demo/served_correct.sh
# --------------------------------------------------------------------------
set -u
cd "$(dirname "$0")/.."
make -s naive
export SB_SPEED=350000          # readable pace for the "snip" lines

term=""
for t in konsole gnome-terminal xfce4-terminal xterm; do
    command -v "$t" >/dev/null 2>&1 && { term=$t; break; }
done

launch() {  # launch "Title" "command..."
    local title=$1 cmd=$2
    case "$term" in
        konsole)        konsole --hold -p tabtitle="$title" -e bash -c "$cmd" & ;;
        gnome-terminal) gnome-terminal --title="$title" -- bash -c "$cmd; exec bash" & ;;
        xfce4-terminal) xfce4-terminal --title="$title" --hold -e "bash -c '$cmd'" & ;;
        xterm)          xterm -T "$title" -e bash -c "$cmd; exec bash" & ;;
    esac
}

pkill -x naive 2>/dev/null
./naive reset >/dev/null 2>&1

if [ -z "$term" ] || [ -z "${DISPLAY:-}" ]; then
    cat <<EOF
No graphical terminal detected. Open TWO terminals in $(pwd) and run:

  Terminal 1 (barber):    ./naive barber
  Terminal 2 (customer):  ./naive customer    # press ENTER to add a customer
                          (ENTER during the countdown -> LOST; after "Zzz..." -> served)
EOF
    exit 0
fi

launch "1-BARBER (naive)"   "SB_SPEED=$SB_SPEED ./naive barber"
launch "2-CUSTOMER (naive)" "SB_SPEED=$SB_SPEED ./naive customer"

cat <<EOF
Two windows opened:
  1-BARBER     starts an 8s countdown whenever the room is empty
  2-CUSTOMER   press ENTER to add a customer to the queue

To see the LOST customer: press ENTER in the CUSTOMER window WHILE the barber
is counting down. Then watch both windows heartbeat forever (deadlock).

Close the demo with Ctrl-C in the BARBER window (it resets the shared shop).
EOF
