/*
 * correct.c — the CORRECT barbershop, using three semaphores.
 *
 * Same two-terminal format as the naive version:
 *     ./correct barber [N]   terminal 1: the barber (N waiting seats, default 3)
 *     ./correct customer     terminal 2: press ENTER to add a customer
 *     ./correct prove [N]    race barber vs. customer N times: NONE are lost
 *     ./correct reset        remove the shared shop
 *
 * Faithful implementation of the classic solution:
 *
 *     barberReady       = 0   (signals the customer the barber is ready)
 *     accessWRSeats     = 1   (mutex protecting numberOfFreeWRSeats)
 *     custReady         = 0   (counts customers waiting to be served)
 *     numberOfFreeWRSeats = N
 *
 * The naive lost-wakeup cannot happen here: a customer always
 * signal(custReady) when it sits down, and the semaphore REMEMBERS that
 * signal even if the barber has not reached wait(custReady) yet. No matter
 * WHEN the customer arrives, it is served (or cleanly turned away if the
 * waiting room is full).
 *
 * There is no artificial timing window. `./correct prove` runs the SAME real
 * race that `./naive deadlock` loses — two threads, a sched_yield() at the
 * exact race point, no manufactured gap — and shows every customer is served.
 * Identical interleaving, opposite outcome.
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

#define SHM_NAME   "/sb_correct_shm"
#define SEM_BARBER "/sb_correct_barberReady"
#define SEM_ACCESS "/sb_correct_accessWRSeats"
#define SEM_CUST   "/sb_correct_custReady"

typedef struct {
    int numberOfFreeWRSeats;   /* protected by accessWRSeats */
} shop_t;

static shop_t *shop;
static sem_t  *barberReady, *accessWRSeats, *custReady;

static void die(const char *msg) { perror(msg); exit(1); }

static void cleanup(void)
{
    sem_unlink(SEM_BARBER);
    sem_unlink(SEM_ACCESS);
    sem_unlink(SEM_CUST);
    shm_unlink(SHM_NAME);
}

static void on_sigint(int sig) { (void)sig; printf("\n" C_DIM "(barber closing shop)" C_RESET "\n"); cleanup(); _exit(0); }

static void attach_shop(void)
{
    int fd = -1;
    for (int i = 0; i < 40 && fd < 0; i++) {     /* wait up to ~2s for the barber */
        fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd < 0) usleep(50000);
    }
    if (fd < 0) { fprintf(stderr, C_WARN "No shop open. Start the barber first: ./correct barber" C_RESET "\n"); exit(1); }
    shop = mmap(NULL, sizeof *shop, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shop == MAP_FAILED) die("mmap");
    barberReady   = sem_open(SEM_BARBER, 0);
    accessWRSeats = sem_open(SEM_ACCESS, 0);
    custReady     = sem_open(SEM_CUST, 0);
    if (barberReady == SEM_FAILED || accessWRSeats == SEM_FAILED || custReady == SEM_FAILED) die("sem_open");
}

/* ------------------------------------------------------------------ barber */
static void run_barber(int seats)
{
    cleanup();
    signal(SIGINT, on_sigint);

    /* Semaphores BEFORE the shm: once a customer sees the shm, the sems exist. */
    barberReady   = sem_open(SEM_BARBER, O_CREAT, 0666, 0);
    accessWRSeats = sem_open(SEM_ACCESS, O_CREAT, 0666, 1);
    custReady     = sem_open(SEM_CUST,   O_CREAT, 0666, 0);
    if (barberReady == SEM_FAILED || accessWRSeats == SEM_FAILED || custReady == SEM_FAILED) die("sem_open");

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) die("shm_open");
    if (ftruncate(fd, sizeof(shop_t)) < 0) die("ftruncate");
    shop = mmap(NULL, sizeof *shop, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shop == MAP_FAILED) die("mmap");
    shop->numberOfFreeWRSeats = seats;

    printf(C_BARBER "[barber] Shop is open with %d waiting-room seats." C_RESET "\n", seats);

    for (;;) {                              /* def Barber(): while true */
        /* Mirror the naive barber: if nobody is waiting right now, announce a
         * nap and commit to sleeping on custReady. The decisive difference is
         * that custReady is a COUNTING semaphore — a customer arriving in the
         * race window below already did signal(custReady), and the semaphore
         * REMEMBERS it, so the wait returns instead of blocking forever. */
        if (sem_trywait(custReady) != 0) {  /* no customer is ready this instant */
            printf(C_BARBER "[barber] Waiting room looks empty. I'll take a nap." C_RESET "\n");
            fflush(stdout);
            sem_wait(custReady);            /* wait(custReady) — REMEMBERS a post made in the race window */
        }                                   /* (else: sem_trywait already took a waiting customer) */
        sem_wait(accessWRSeats);            /* wait(accessWRSeats) */
        shop->numberOfFreeWRSeats += 1;     /* a chair frees up    */
        sem_post(barberReady);              /* signal(barberReady) — I'm ready to cut */
        sem_post(accessWRSeats);            /* signal(accessWRSeats) */

        printf(C_BARBER "[barber] Inviting a customer to the chair (free seats: %d)." C_RESET "\n",
               shop->numberOfFreeWRSeats);
        serve_haircut("the customer");      /* (cut hair here) */
        printf(C_OK "[barber] Haircut finished." C_RESET "\n");
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
    sem_wait(accessWRSeats);                /* wait(accessWRSeats) */
    if (shop->numberOfFreeWRSeats > 0) {    /* if free seats: */
        shop->numberOfFreeWRSeats -= 1;     /*   sit down */
        printf(C_CUST "[%s] Took a seat (free seats now: %d)." C_RESET "\n",
               name, shop->numberOfFreeWRSeats);
        sem_post(custReady);                /*   signal(custReady) — notify barber */
        sem_post(accessWRSeats);            /*   signal(accessWRSeats) */
        printf(C_CUST "[%s] Waiting for the barber..." C_RESET "\n", name);
        sb_queue_print();                   /*   show the queue */
        sem_wait(barberReady);              /*   wait(barberReady) */
        sb_queue_set(slot, 1);              /*   served */
        printf(C_OK "[%s] In the chair — getting my haircut. Leaving happy." C_RESET "\n", name);
        sb_queue_print();                   /*   show the queue */
    } else {                                /* else: no free seats */
        sem_post(accessWRSeats);            /*   signal(accessWRSeats) */
        sb_queue_set(slot, 2);              /*   left without a haircut */
        printf(C_WARN "[%s] Waiting room is full — leaving without a haircut." C_RESET "\n", name);
        sb_queue_print();                   /*   show the queue */
    }
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
    for (long i = 0; i < n; i++) pthread_join(th[i], NULL);
}

/* -------------------------------------------- proof: the race never loses --
 * The exact counterpart to `./naive deadlock`: the SAME unsynchronised-looking
 * race (a customer arriving in the window between the barber finding the room
 * empty and committing to sleep), driven by two real threads with a sched_yield
 * at the race point and NO artificial gap. Where the naive shop deadlocks, the
 * three semaphores serve every customer — we repeat it many times to show the
 * fix holds under the genuine interleaving, not a staged one. */
typedef struct {
    int   numberOfFreeWRSeats;   /* protected by accessWRSeats              */
    sem_t barberReady, accessWRSeats, custReady;
    volatile int served;         /* set once the customer gets its haircut  */
} ptrial_t;

static void *cp_barber(void *arg)
{
    ptrial_t *t = arg;
    if (sem_trywait(&t->custReady) != 0) {  /* room looks empty this instant */
        sched_yield();                       /* real reschedule — same race point */
        sem_wait(&t->custReady);             /* REMEMBERS a post made in the window */
    }
    sem_wait(&t->accessWRSeats);
    t->numberOfFreeWRSeats += 1;
    sem_post(&t->barberReady);               /* call the customer to the chair */
    sem_post(&t->accessWRSeats);
    return NULL;
}

static void *cp_customer(void *arg)
{
    ptrial_t *t = arg;
    sem_wait(&t->accessWRSeats);
    if (t->numberOfFreeWRSeats > 0) {
        t->numberOfFreeWRSeats -= 1;
        sem_post(&t->custReady);             /* ALWAYS announce — cannot be lost */
        sem_post(&t->accessWRSeats);
        sem_wait(&t->barberReady);           /* served (never blocks forever)    */
        t->served = 1;
    } else {
        sem_post(&t->accessWRSeats);
    }
    return NULL;
}

static void run_prove(int trials)
{
    if (trials <= 0) trials = 20000;
    printf(C_BARBER "[prove] Racing barber vs. customer %d times — the SAME race", trials);
    printf(" that\n        ./naive deadlock loses, with NO artificial gap." C_RESET "\n");
    fflush(stdout);

    int served = 0;
    for (int n = 1; n <= trials; n++) {
        ptrial_t t = { .numberOfFreeWRSeats = 1, .served = 0 };
        sem_init(&t.barberReady,   0, 0);
        sem_init(&t.accessWRSeats, 0, 1);
        sem_init(&t.custReady,     0, 0);

        pthread_t bt, ct;
        pthread_create(&bt, NULL, cp_barber, &t);
        pthread_create(&ct, NULL, cp_customer, &t);

        /* The fix guarantees both threads finish; a lost wake-up would hang
         * here, so a timeout would be a real FAILURE. */
        int ok = 0;
        for (int i = 0; i < 100; i++) {        /* up to ~20ms */
            if (t.served) { ok = 1; break; }
            usleep(200);
        }
        if (!ok) {
            printf(C_WARN "[prove] Trial %d: customer NOT served — the fix is broken!" C_RESET "\n", n);
            return;                            /* leave the hung threads; bail */
        }
        pthread_join(bt, NULL);
        pthread_join(ct, NULL);
        sem_destroy(&t.barberReady);
        sem_destroy(&t.accessWRSeats);
        sem_destroy(&t.custReady);
        served++;
    }
    printf(C_OK "[prove] %d/%d customers served, 0 lost — no wake-up can be dropped." C_RESET "\n",
           served, trials);
    printf(C_DIM "[prove] Same interleaving as ./naive deadlock; opposite outcome." C_RESET "\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s barber [N] | customer | prove [N] | reset\n", argv[0]);
        return 2;
    }
    if      (!strcmp(argv[1], "barber"))   run_barber(argc > 2 ? atoi(argv[2]) : 3);
    else if (!strcmp(argv[1], "customer")) run_customer();
    else if (!strcmp(argv[1], "prove"))    run_prove(argc > 2 ? atoi(argv[2]) : 0);
    else if (!strcmp(argv[1], "reset"))    { cleanup(); printf("shop reset\n"); }
    else { fprintf(stderr, "unknown role: %s\n", argv[1]); return 2; }
    return 0;
}
