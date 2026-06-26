/*
 * naive.c — the BROKEN barbershop (no proper synchronisation).
 *
 * Roles:
 *     ./naive barber        terminal 1: the barber
 *     ./naive customer      terminal 2: press ENTER to add a customer
 *     ./naive deadlock [N]  reliably reproduce the lost-wakeup DEADLOCK
 *     ./naive race [N]      (bonus) the lost-increment race on `waiting`
 *     ./naive reset         remove the shared shop
 *
 * The bug: the barber checks the waiting room, finds it empty, and decides to
 * sleep. Between that check and actually falling asleep there is an UNLOCKED
 * window. A customer arriving in that window sees the barber still on his feet
 * (barber_sleeping == 0), assumes it was noticed, and sits to wait WITHOUT
 * sending a wake-up. The barber then sleeps on a waiting customer. Both block
 * forever. DEADLOCK.
 *
 * That window is REAL but only a few instructions wide — nothing artificial
 * holds it open. By hand (the two-terminal shop) you will almost always be
 * served; the race is simply too narrow to hit on purpose. To see the bug
 * reliably, `./naive deadlock` drives the SAME unsynchronised logic with two
 * real threads and repeats it until the unlucky interleaving actually occurs.
 *
 * To make a stuck shop visible, the barber and any stuck customer print a
 * "heartbeat" while blocked, so you watch them spin forever.
 */
#define _GNU_SOURCE
#include "common.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#define SHM_NAME "/sb_naive_shm"
#define SEM_WAKE "/sb_naive_wake"   /* barber sleeps here; customer "wakes" him */
#define SEM_CUT  "/sb_naive_cut"    /* customer waits here to be served         */

typedef struct {
    int waiting;          /* customers in the waiting room (NO mutex — races!) */
    int barber_sleeping;  /* 1 while the barber is asleep                       */
} shop_t;

static shop_t *shop;
static sem_t  *wake, *cut;

static void die(const char *msg) { perror(msg); exit(1); }

static void cleanup(void)
{
    sem_unlink(SEM_WAKE);
    sem_unlink(SEM_CUT);
    shm_unlink(SHM_NAME);
}

static void on_sigint(int sig) { (void)sig; printf("\n" C_DIM "(barber closing shop)" C_RESET "\n"); cleanup(); _exit(0); }

/* Block on a semaphore, but print a heartbeat every 2s so a deadlock is visible. */
static void wait_with_heartbeat(sem_t *s, const char *colour, const char *msg)
{
    for (;;) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        if (sem_timedwait(s, &ts) == 0) return;     /* woken / served */
        printf("%s%s%s\n", colour, msg, C_RESET);   /* still stuck */
        fflush(stdout);
    }
}

/* Map an already-created shop (used by the customer terminal). */
static void attach_shop(void)
{
    int fd = -1;
    for (int i = 0; i < 40 && fd < 0; i++) {     /* wait up to ~2s for the barber */
        fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd < 0) usleep(50000);
    }
    if (fd < 0) { fprintf(stderr, C_WARN "No shop open. Start the barber first: ./naive barber" C_RESET "\n"); exit(1); }
    shop = mmap(NULL, sizeof *shop, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shop == MAP_FAILED) die("mmap");
    wake = sem_open(SEM_WAKE, 0);
    cut  = sem_open(SEM_CUT, 0);
    if (wake == SEM_FAILED || cut == SEM_FAILED) die("sem_open");
}

/* ------------------------------------------------------------------ barber */
static void run_barber(void)
{
    cleanup();
    signal(SIGINT, on_sigint);

    /* Semaphores BEFORE the shm: once a customer sees the shm, the sems exist. */
    wake = sem_open(SEM_WAKE, O_CREAT, 0666, 0);
    cut  = sem_open(SEM_CUT,  O_CREAT, 0666, 0);
    if (wake == SEM_FAILED || cut == SEM_FAILED) die("sem_open");

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) die("shm_open");
    if (ftruncate(fd, sizeof(shop_t)) < 0) die("ftruncate");
    shop = mmap(NULL, sizeof *shop, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shop == MAP_FAILED) die("mmap");
    shop->waiting = 0;
    shop->barber_sleeping = 0;

    printf(C_BARBER "[barber] Shop is open." C_RESET "\n");

    for (;;) {
        if (shop->waiting == 0) {                       /* T1: room looks empty */
            printf(C_BARBER "[barber] Waiting room looks empty. I'll take a nap." C_RESET "\n");
            fflush(stdout);

            /* THE BUG: nothing locks the gap between the check above and the
             * sleep below. A customer arriving here reads barber_sleeping==0,
             * assumes it was noticed, and never sends a wake-up. No artificial
             * delay props this window open — it is just a few instructions, so
             * by hand you will nearly always be served. `./naive deadlock`
             * reproduces the unlucky interleaving reliably. */
            shop->barber_sleeping = 1;                  /* T2: go to sleep */

            printf(C_BARBER "[barber] Zzz... (asleep, waiting for a wake-up)" C_RESET "\n");
            fflush(stdout);
            wait_with_heartbeat(wake, C_BARBER,
                "[barber] Zzz... still asleep — no wake-up arrived (DEADLOCK if a customer waits)");
            shop->barber_sleeping = 0;
            printf(C_OK "[barber] *yawn* Someone woke me!" C_RESET "\n");
        }

        shop->waiting--;                                /* take a customer (race) */
        printf(C_BARBER "[barber] Next please — sit down, starting your haircut." C_RESET "\n");
        serve_haircut("the customer");
        sem_post(cut);                                  /* tell them we're done   */
        printf(C_OK "[barber] All done! Who's next?" C_RESET "\n");
    }
}

/* ---------------------------------------------------------------- customer */
/* The body every customer runs (one thread per ENTER). */
static void *customer_body(void *arg)
{
    char name[32];
    snprintf(name, sizeof name, "Customer-%ld", (long)arg);
    int slot = sb_queue_add(name);

    printf(C_CUST "[%s] Walks into the shop." C_RESET "\n", name);
    shop->waiting++;                                    /* arrive (race on waiting) */

    if (shop->barber_sleeping) {
        printf(C_CUST "[%s] Barber is asleep — I'll wake him." C_RESET "\n", name);
        sem_post(wake);
    } else {
        /* THE BUG: barber is mid-gap (not yet asleep). We assume he saw us. */
        printf(C_WARN "[%s] Barber's on his feet — he'll surely notice me. I'll sit and wait." C_RESET "\n", name);
    }

    printf(C_CUST "[%s] Sitting in the waiting room..." C_RESET "\n", name);
    sb_queue_print();                                   /* show the queue */
    char msg[96];
    snprintf(msg, sizeof msg, "[%s] still waiting — the barber never called me (am I LOST?)", name);
    wait_with_heartbeat(cut, C_WARN, msg);              /* may spin FOREVER */
    sb_queue_set(slot, 1);                              /* served */
    printf(C_OK "[%s] Got my haircut! Leaving happy." C_RESET "\n", name);
    sb_queue_print();                                   /* show the queue */
    return NULL;
}

/* Terminal 2: one ENTER (one line on stdin) adds one customer to the queue. */
static void run_customer(void)
{
    attach_shop();
    printf(C_CUST "[customer] Connected. Press ENTER to add a customer (Ctrl-D to stop)." C_RESET "\n");
    fflush(stdout);

    pthread_t th[256];
    long n = 0;
    char line[16];
    while (n < 256 && fgets(line, sizeof line, stdin)) {
        n++;
        pthread_create(&th[n - 1], NULL, customer_body, (void *)n);
    }
    for (long i = 0; i < n; i++) pthread_join(th[i], NULL);  /* lost ones block here */
}

/* --------------------------------------------- deadlock reproducer (real) --
 * The same unsynchronised logic as the two-terminal shop, but driven by two
 * real threads with NO artificial gap, repeated until the genuine lost-wakeup
 * interleaving fires. We yield the CPU once at the race point — that is a real
 * reschedule the kernel can do at any time, not a scripted countdown — so the
 * narrow window the two-terminal demo rarely hits is exposed within a handful
 * of trials. */
typedef struct {
    int   waiting;          /* unlocked, exactly like shop_t.waiting          */
    int   barber_sleeping;  /* unlocked, exactly like shop_t.barber_sleeping  */
    sem_t wake;             /* barber sleeps here; customer "wakes" him       */
    sem_t cut;              /* customer waits here to be served               */
    volatile int served;    /* set by the customer once it gets its haircut   */
} trial_t;

static void *dl_barber(void *arg)
{
    trial_t *t = arg;
    if (t->waiting == 0) {              /* room looks empty                    */
        sched_yield();                  /* real reschedule — exposes the window */
        t->barber_sleeping = 1;         /* sleep, having never re-checked      */
        sem_wait(&t->wake);             /* lost wake-up => blocks forever      */
        t->barber_sleeping = 0;
    }
    t->waiting--;
    sem_post(&t->cut);                  /* serve the customer                  */
    return NULL;
}

static void *dl_customer(void *arg)
{
    trial_t *t = arg;
    t->waiting++;                       /* arrive                              */
    if (t->barber_sleeping)
        sem_post(&t->wake);             /* barber asleep => wake him (safe)    */
    /* else: barber on his feet — assume he noticed us (THE BUG: no wake sent) */
    sem_wait(&t->cut);                  /* lost wake-up => blocks forever      */
    t->served = 1;
    return NULL;
}

static void run_deadlock(int max_trials)
{
    if (max_trials <= 0) max_trials = 100000;
    printf(C_CUST "[deadlock] Racing barber vs. customer with NO artificial gap." C_RESET "\n");
    printf(C_DIM  "[deadlock] Same unsynchronised logic as the shop; repeating until the\n"
                  "           real lost-wakeup interleaving actually happens..." C_RESET "\n");
    fflush(stdout);

    for (int n = 1; n <= max_trials; n++) {
        trial_t t = { .waiting = 0, .barber_sleeping = 0, .served = 0 };
        sem_init(&t.wake, 0, 0);
        sem_init(&t.cut, 0, 0);

        pthread_t bt, ct;
        pthread_create(&bt, NULL, dl_barber, &t);
        pthread_create(&ct, NULL, dl_customer, &t);

        /* A served trial finishes in microseconds. If the customer is still
         * not served after the timeout, the wake-up was lost and both threads
         * are blocked forever. */
        int deadlocked = 1;
        for (int i = 0; i < 100; i++) {        /* up to ~20ms */
            if (t.served) { deadlocked = 0; break; }
            usleep(200);
        }

        if (!deadlocked) {
            pthread_join(bt, NULL);
            pthread_join(ct, NULL);
            sem_destroy(&t.wake);
            sem_destroy(&t.cut);
            if (n % 1000 == 0) {
                printf(C_DIM "[deadlock] %d trials served so far, still racing...\n" C_RESET, n);
                fflush(stdout);
            }
            continue;
        }

        /* Reproduced. The two threads are stuck on each other; report it and
         * return (process teardown reaps them). */
        printf(C_WARN "[deadlock] Trial %d: LOST WAKEUP." C_RESET "\n", n);
        printf(C_WARN "  barber  : saw room==empty, then slept; never noticed the customer." C_RESET "\n");
        printf(C_WARN "  customer: arrived after that check, saw barber awake, sent no wake-up." C_RESET "\n");
        printf(C_WARN "  => barber blocked on `wake`, customer blocked on `cut`. DEADLOCK." C_RESET "\n");
        printf(C_OK   "[deadlock] Reproduced from a REAL race after %d trial(s) — no manufactured gap." C_RESET "\n", n);
        fflush(stdout);
        return;
    }
    printf(C_OK "[deadlock] %d trials, no lost wake-up this run (timing-dependent — rerun)." C_RESET "\n", max_trials);
}

/* -------------------------------------------------- bonus: counter race ---- */
static volatile long race_counter;
static long race_iters;
static void *race_worker(void *arg)
{
    (void)arg;
    for (long i = 0; i < race_iters; i++) {
        long t = race_counter;   /* read  */
        t = t + 1;               /* +1    */
        race_counter = t;        /* write */  /* no lock: updates get lost */
    }
    return NULL;
}
static void run_race(int threads)
{
    if (threads < 2) threads = 8;
    if (threads > 64) threads = 64;
    race_iters = 100000;
    race_counter = 0;
    pthread_t th[64];
    for (int i = 0; i < threads; i++) pthread_create(&th[i], NULL, race_worker, NULL);
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    long expected = (long)threads * race_iters;
    printf(C_CUST "[race] %d threads each did waiting++ %ld times." C_RESET "\n", threads, race_iters);
    printf("       expected = %ld\n", expected);
    int ok = (race_counter == expected);
    printf("%s       actual   = %ld%s" C_RESET "\n",
           ok ? C_OK : C_WARN, race_counter, ok ? "" : "   <-- increments LOST");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s barber | customer | deadlock [N] | race [N] | reset\n", argv[0]);
        return 2;
    }
    if      (!strcmp(argv[1], "barber"))   run_barber();
    else if (!strcmp(argv[1], "customer")) run_customer();
    else if (!strcmp(argv[1], "deadlock")) run_deadlock(argc > 2 ? atoi(argv[2]) : 0);
    else if (!strcmp(argv[1], "race"))     run_race(argc > 2 ? atoi(argv[2]) : 8);
    else if (!strcmp(argv[1], "reset"))    { cleanup(); printf("shop reset\n"); }
    else { fprintf(stderr, "unknown role: %s\n", argv[1]); return 2; }
    return 0;
}
