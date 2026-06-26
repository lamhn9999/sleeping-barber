# Sleeping Barber — naive (broken) vs correct (semaphore) demonstrations.
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
LDLIBS  = -lrt -pthread

all: naive correct

naive: src/naive.c src/common.h
	$(CC) $(CFLAGS) -o $@ src/naive.c $(LDLIBS)

correct: src/correct.c src/common.h
	$(CC) $(CFLAGS) -o $@ src/correct.c $(LDLIBS)

# Automated, deterministic tests (no terminals needed).
test: all
	./tests/test_naive.sh
	./tests/test_correct.sh

clean:
	rm -f naive correct
	-./naive reset   >/dev/null 2>&1 || true
	-./correct reset >/dev/null 2>&1 || true

.PHONY: all test clean
