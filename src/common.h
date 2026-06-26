/*
 * common.h — shared helpers for both barbershop variants.
 *
 * Holds the ANSI colours and the one thing the barber actually "does":
 * service a job by streaming a text file line by line. In the analogy this
 * is cutting hair; in OS terms it is a worker thread consuming records from
 * a resource/buffer.
 */
#ifndef SB_COMMON_H
#define SB_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* ---- colours (one per role so the three terminals are easy to tell apart) ---- */
#define C_RESET  "\033[0m"
#define C_BARBER "\033[1;36m"   /* cyan   */
#define C_CUST   "\033[1;33m"   /* yellow */
#define C_WARN   "\033[1;31m"   /* red    */
#define C_OK     "\033[1;32m"   /* green  */
#define C_DIM    "\033[2m"

/* The file the barber reads while "working". Override with $SB_RESOURCE. */
static inline const char *sb_resource_path(void)
{
    const char *p = getenv("SB_RESOURCE");
    return p ? p : "resource.txt";
}

/* Pace of a single haircut step, in microseconds. Override with $SB_SPEED. */
static inline unsigned sb_step_us(void)
{
    const char *s = getenv("SB_SPEED");
    return s ? (unsigned)atoi(s) : 450000u;
}

/*
 * Service one customer: stream the resource file line by line so the operator
 * can watch the barber "work". This is deliberately the only real workload in
 * the program — everything else is synchronisation.
 */
static inline void serve_haircut(const char *who)
{
    FILE *f = fopen(sb_resource_path(), "r");
    if (!f) {
        printf(C_BARBER "  [barber] (no resource file — pretending to snip)" C_RESET "\n");
        usleep(sb_step_us());
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        printf(C_BARBER "  [barber] %s  " C_DIM "(serving %s)" C_RESET "\n", line, who);
        fflush(stdout);
        usleep(sb_step_us());
    }
    fclose(f);
}

/* ------------------------------------------------------------------------
 * Customer-side queue tracking. All customers live as threads in the single
 * `customer` process, so the process can keep a local registry of everyone it
 * has sent in and print the current waiting room to the customer console.
 *   state: 0 = waiting, 1 = served, 2 = left (no haircut)
 * ---------------------------------------------------------------------- */
#define SB_MAXC 256
static struct { char name[32]; int state; } sb_reg[SB_MAXC];
static int sb_reg_n;
static pthread_mutex_t sb_reg_lock = PTHREAD_MUTEX_INITIALIZER;

/* Register a newly-arrived customer; returns its index. */
static inline int sb_queue_add(const char *name)
{
    pthread_mutex_lock(&sb_reg_lock);
    int i = sb_reg_n < SB_MAXC ? sb_reg_n++ : SB_MAXC - 1;
    snprintf(sb_reg[i].name, sizeof sb_reg[i].name, "%s", name);
    sb_reg[i].state = 0;
    pthread_mutex_unlock(&sb_reg_lock);
    return i;
}

/* Update a customer's state (1 = served, 2 = left). */
static inline void sb_queue_set(int i, int state)
{
    pthread_mutex_lock(&sb_reg_lock);
    sb_reg[i].state = state;
    pthread_mutex_unlock(&sb_reg_lock);
}

/* Print the current waiting room (everyone still in state "waiting"). */
static inline void sb_queue_print(void)
{
    pthread_mutex_lock(&sb_reg_lock);
    int n = 0;
    for (int i = 0; i < sb_reg_n; i++) if (sb_reg[i].state == 0) n++;
    printf(C_DIM "[queue] waiting room (%d): ", n);
    if (n == 0) {
        printf("(empty)");
    } else {
        for (int i = 0; i < sb_reg_n; i++)
            if (sb_reg[i].state == 0) printf("%s ", sb_reg[i].name);
    }
    printf(C_RESET "\n");
    fflush(stdout);
    pthread_mutex_unlock(&sb_reg_lock);
}

#endif /* SB_COMMON_H */
