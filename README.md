# The Sleeping Barber Problem — naive vs. correct

A small, demo-focused OS project showing why concurrent programs need
synchronisation. Two C programs implement the same barbershop:

| Program   | Synchronisation        | Result                                   |
| --------- | ---------------------- | ---------------------------------------- |
| `naive`   | none (plain variables) | **lost wakeups** and **race conditions** |
| `correct` | three semaphores       | every customer is served, no corruption  |

Everything runs in **two terminals**:

```
Terminal 1:  ./naive barber        the barber
Terminal 2:  ./naive customer      press ENTER to add a customer to the queue
```

The headline demo is the **lost customer**: add a customer at exactly the wrong
moment and the barber sleeps through them forever.

The customer terminal logs the **waiting queue** after every change, e.g.
`[queue] waiting room (2): Customer-1 Customer-2` — so you can watch it fill up
(and, in the correct shop, drain as each customer is served).

## What maps to what

| Barbershop          | Real OS / system              |
| ------------------- | ----------------------------- |
| Barber              | worker process / thread       |
| Customer            | request / task / job          |
| Barber chair        | resource being serviced       |
| Waiting-room chairs | queue / buffer capacity       |
| Sleeping barber     | worker idle, waiting for work |

In the code the barber's "work" is literally **streaming `resource.txt` line by
line** — a stand-in for a worker consuming records from a resource.

## Build

```sh
make            # builds ./naive and ./correct
```

Requires `gcc` and POSIX shared memory + semaphores (Linux). One binary plays
every role, chosen by its first argument:

```sh
./naive   barber          # start the (broken) shop
./naive   customer        # press ENTER to add a customer (every customer = same function)
./naive   race 8          # bonus: the unsynchronised counter race
./naive   reset           # tear down the shared shop

./correct barber 3        # correct shop with 3 waiting seats
./correct customer        # press ENTER to add a customer
./correct reset
```

## The visual demo (two terminals, colour)

```sh
./demo/lost_customer.sh     # NAIVE: add a customer mid-countdown -> deadlock
./demo/served_correct.sh    # CORRECT: add customers any time -> all served
```

Each opens a **BARBER** window and a **CUSTOMER** window (auto-detects
`konsole`, `gnome-terminal`, `xfce4-terminal`, or `xterm`; falls back to
printing the two commands if there is no graphical session).

### Narrating the lost-customer demo

1. The **barber** opens, sees an empty room, and starts an 8-second countdown
   before napping: *"...about to nod off (add a customer NOW to lose them)"*.
2. **To lose a customer:** press **ENTER in the CUSTOMER window during the
   countdown**. The customer sees the barber still on his feet, assumes he was
   noticed, and sits to wait.
3. The countdown ends and the barber falls asleep. Now watch both windows
   **heartbeat forever**:
   - BARBER: `Zzz... still asleep — no wake-up arrived (DEADLOCK ...)`
   - CUSTOMER: `still waiting — the barber never called me (am I LOST?)`

   The customer's wake-up was never sent. **Deadlock.**
4. **For contrast** (fresh run): wait for the countdown to finish — the barber
   says `Zzz...` — *then* press ENTER. The customer wakes him and is served.

Same function both times; only the **moment you press ENTER** differs. Then run
`./demo/served_correct.sh` to show the semaphore version never loses anyone.

## The correct algorithm (`src/correct.c`)

A faithful implementation of the classic three-semaphore solution:

```text
barberReady   = 0     # barber signals he is ready to cut
accessWRSeats = 1     # mutex over numberOfFreeWRSeats
custReady     = 0     # counts customers waiting to be served
numberOfFreeWRSeats = N

Barber:                          Customer:
  wait(custReady)                  wait(accessWRSeats)
  wait(accessWRSeats)              if numberOfFreeWRSeats > 0:
  numberOfFreeWRSeats += 1            numberOfFreeWRSeats -= 1
  signal(barberReady)                signal(custReady)
  signal(accessWRSeats)              signal(accessWRSeats)
  # cut hair                         wait(barberReady)
                                     # get hair cut
                                   else:
                                     signal(accessWRSeats)   # full -> leave
```

`custReady` is a **counting** semaphore: a customer's `signal(custReady)` is
remembered even if the barber has not yet reached `wait(custReady)`. That is
exactly what the naive version lacks, so no wake-up can be lost.

## The bugs the naive shop has

1. **Lost wakeup** — the barber checks the room (empty) and, before he actually
   sleeps, a customer arrives, sees him standing, and waits to be noticed. The
   barber then sleeps; nobody wakes him. **Deadlock.**
2. **Race condition** — `waiting++` is `read; +1; write` with no lock, so
   concurrent customers overwrite each other's updates (`./naive race`).

## Automated tests (no terminals needed)

Same two-program format as the demo — the tests just feed ENTER (newlines) into
`./naive customer` / `./correct customer` with controlled timing.

```sh
make test
# or individually:
./tests/test_naive.sh     # lost wakeup, happy path, counter race
./tests/test_correct.sh   # no lost wakeup, full-room handled cleanly
```

## Layout

```
src/common.h     colours + serve_haircut() (streams resource.txt)
src/naive.c      broken shop    (barber | customer | race | reset)
src/correct.c    semaphore shop (barber | customer | reset)
resource.txt     the "job" the barber services
tests/           deterministic automated tests
demo/            two-terminal visual demos
Makefile         build + test
```

## Cleanup

```sh
make clean       # remove binaries and any leftover shared shop
```
