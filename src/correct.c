/*
 * correct.c — the CORRECT barbershop, using three semaphores.
 *
 * Same two-terminal format as the naive version:
 *     ./correct barber [N]   terminal 1: the barber (N waiting seats, default 3)
 *     ./correct customer     terminal 2: press ENTER to add a customer
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
 * WHEN you press ENTER, the customer is served (or cleanly turned away if the
 * waiting room is full).
 */
#define _GNU_SOURCE
#include "common.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <pthread.h>

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

/* The SAME dozing-off window the naive barber has, inserted right before the
 * barber commits to sleeping on custReady. In the naive shop a customer who
 * arrives during this gap is LOST. Here it is the proof of the fix: a customer
 * who arrives in the gap has already done signal(custReady), the semaphore
 * REMEMBERS that signal, and so the sem_wait(custReady) just below returns
 * immediately instead of blocking forever.
 * Interactive: a visible countdown. Tests: a fixed $BARBER_WINDOW seconds. */
static void scheduler_gap(int secs)
{
    if (secs <= 0) {
        printf(C_DIM "[barber] (the scheduler pauses me here for a moment)" C_RESET "\n");
        fflush(stdout);
        return;
    }
    for (int s = secs; s > 0; s--) {
        printf(C_BARBER "[barber] ...about to nod off in %d  " C_DIM "(add a customer NOW — they'll STILL be served)" C_RESET "\n", s);
        fflush(stdout);
        sleep(1);
    }
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

    const char *win = getenv("BARBER_WINDOW");
    int gap = win ? atoi(win) : 8;          /* interactive default: 8s window */

    printf(C_BARBER "[barber] Shop is open with %d waiting-room seats." C_RESET "\n", seats);

    for (;;) {                              /* def Barber(): while true */
        /* Mirror the naive barber: if nobody is waiting right now, announce a
         * nap and run the SAME bug window before committing to sleep. The only
         * difference is the line that follows — and it is the whole point. */
        if (sem_trywait(custReady) != 0) {  /* no customer is ready this instant */
            printf(C_BARBER "[barber] Waiting room looks empty. I'll take a nap." C_RESET "\n");
            fflush(stdout);
            scheduler_gap(gap);             /* the deadlock-ignition window      */
            sem_wait(custReady);            /* wait(custReady) — REMEMBERS a post made during the gap */
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s barber [N] | customer | reset\n", argv[0]);
        return 2;
    }
    if      (!strcmp(argv[1], "barber"))   run_barber(argc > 2 ? atoi(argv[2]) : 3);
    else if (!strcmp(argv[1], "customer")) run_customer();
    else if (!strcmp(argv[1], "reset"))    { cleanup(); printf("shop reset\n"); }
    else { fprintf(stderr, "unknown role: %s\n", argv[1]); return 2; }
    return 0;
}
