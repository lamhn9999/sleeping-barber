/*
 * naive.c — the BROKEN barbershop (no proper synchronisation).
 *
 * Two terminals:
 *     ./naive barber      terminal 1: the barber
 *     ./naive customer    terminal 2: press ENTER to add a customer to the queue
 *     ./naive race [N]     (bonus) the lost-increment race on `waiting`
 *     ./naive reset        remove the shared shop
 *
 * Every customer you add runs the SAME function (customer_body). Whether a
 * customer is served or lost depends only on TIMING:
 *
 *   The barber looks at the waiting room, sees it empty, and starts to nod
 *   off. If you add a customer DURING that gap, the customer sees the barber
 *   still on his feet, assumes he was noticed, and sits to wait. The barber
 *   then falls asleep — and nobody ever wakes him. Both wait forever. DEADLOCK.
 *
 * If instead you add a customer once the barber is already asleep, the
 * customer wakes him and gets served. Same function, different moment.
 *
 * To make the deadlock visible, the barber and any stuck customer print a
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

/* The gap between "the room looks empty" and "I'm now asleep" — the bug window.
 * Interactive: a visible countdown (add a customer during it).
 * Tests: a fixed $BARBER_WINDOW seconds (0 = sleep immediately). */
static void scheduler_gap(int secs)
{
    if (secs <= 0) {
        printf(C_DIM "[barber] (the scheduler pauses me here for a moment)" C_RESET "\n");
        fflush(stdout);
        return;
    }
    for (int s = secs; s > 0; s--) {
        printf(C_BARBER "[barber] ...about to nod off in %d  " C_DIM "(add a customer NOW to lose them)" C_RESET "\n", s);
        fflush(stdout);
        sleep(1);
    }
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

    const char *win = getenv("BARBER_WINDOW");
    int gap = win ? atoi(win) : 8;          /* interactive default: 8s window */

    printf(C_BARBER "[barber] Shop is open." C_RESET "\n");

    for (;;) {
        if (shop->waiting == 0) {                       /* T1: room looks empty */
            printf(C_BARBER "[barber] Waiting room looks empty. I'll take a nap." C_RESET "\n");
            fflush(stdout);

            scheduler_gap(gap);                          /* T2: the bug window  */

            shop->barber_sleeping = 1;                  /* T5: go to sleep anyway */

            if (shop->waiting > 0) {                      /* the bug, made visible */
                printf(C_WARN "[barber] !! Someone slipped in during my pause (waiting=%d)," C_RESET "\n", shop->waiting);
                printf(C_WARN "         but I already decided the room was empty — I never rechecked." C_RESET "\n");
                printf(C_WARN "         Their wake-up was LOST. I'm asleep on a waiting customer." C_RESET "\n");
            }
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
        fprintf(stderr, "usage: %s barber | customer | race [N] | reset\n", argv[0]);
        return 2;
    }
    if      (!strcmp(argv[1], "barber"))   run_barber();
    else if (!strcmp(argv[1], "customer")) run_customer();
    else if (!strcmp(argv[1], "race"))     run_race(argc > 2 ? atoi(argv[2]) : 8);
    else if (!strcmp(argv[1], "reset"))    { cleanup(); printf("shop reset\n"); }
    else { fprintf(stderr, "unknown role: %s\n", argv[1]); return 2; }
    return 0;
}
