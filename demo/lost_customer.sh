#!/usr/bin/env bash
#
# lost_customer.sh — VISUAL "lost customer" demo for the NAIVE shop.
#
# Three terminals:
#     1. BARBER      — ./naive barber
#     2. CUSTOMER    — ./naive customer    (press ENTER to add a customer)
#     3. DEADLOCK    — ./naive deadlock     (reliably reproduces the bug)
#
# --------------------------------------------------------------------------
# HOW TO RUN (narrate as you click):
#
#   There is NO artificial countdown — the lost-wakeup window is the real one
#   (a few instructions between the barber's "room is empty" check and his
#   sleep). So:
#
#   * BARBER + CUSTOMER  — the everyday shop. Press ENTER to add customers;
#       they get served. By hand the race is far too narrow to hit on purpose,
#       so this window mostly just shows the shop working.
#
#   * DEADLOCK  — the reliable reproduction. It races a real barber thread
#       against a real customer thread, with no manufactured gap, repeating
#       until the unlucky interleaving loses the wake-up:
#         "LOST WAKEUP ... barber blocked on wake, customer blocked on cut"
#         "Reproduced from a REAL race after N trial(s) — no manufactured gap"
#
#   Same logic in every window — only the timing differs, and only repetition
#   makes the genuine race show up.  Compare with: ./demo/served_correct.sh
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
No graphical terminal detected. Open terminals in $(pwd) and run:

  Terminal 1 (barber):    ./naive barber
  Terminal 2 (customer):  ./naive customer    # press ENTER to add a customer
  Terminal 3 (deadlock):  ./naive deadlock     # reliably reproduces the bug

By hand the lost-wakeup race is too narrow to hit; `./naive deadlock` repeats
the real race until it fires.
EOF
    exit 0
fi

launch "1-BARBER (naive)"   "SB_SPEED=$SB_SPEED ./naive barber"
launch "2-CUSTOMER (naive)" "SB_SPEED=$SB_SPEED ./naive customer"
launch "3-DEADLOCK (naive)" "./naive deadlock; echo; echo '(press ENTER to close)'; read"

cat <<EOF
Three windows opened:
  1-BARBER     naps whenever the room is empty (no countdown — the real shop)
  2-CUSTOMER   press ENTER to add a customer; by hand they get served
  3-DEADLOCK   races a real barber vs. customer until the wake-up is LOST

The headline bug is window 3: it reproduces the genuine deadlock from a real
race, with no manufactured gap. Windows 1+2 show the everyday shop.

Close the demo with Ctrl-C in the BARBER window (it resets the shared shop).
EOF
