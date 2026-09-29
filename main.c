/*
 * main.c
 * ======
 * Menu-driven console application for the ER Patient Management System.
 *
 * Input handling:
 *   - ALL input is read with fgets into a buffer, then parsed with strtol.
 *   - read_int()    : reads and validates an integer in [min, max].
 *   - read_string() : reads a non-empty, non-whitespace-only string.
 *   - EOF on stdin is detected and causes a clean exit.
 */

#include "patient.h"
#include "cqueue.h"
#include "stack.h"
#include "ermanager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

/* -----------------------------------------------------------------------
 * Input buffer size
 * ----------------------------------------------------------------------- */
#define INPUT_BUF 256

/* -----------------------------------------------------------------------
 * Simulation defaults
 * ----------------------------------------------------------------------- */
#define SIM_MAX_PATIENTS 200

/* -----------------------------------------------------------------------
 * Forward declarations of helpers
 * ----------------------------------------------------------------------- */
static int  read_int(const char *prompt, int min, int max);
static void read_string(const char *prompt, char *buf, int size);
static void flush_line(void);
static void print_status(ERStatus st, const char *context);
static void do_admit(ERManager *mgr);
static void do_treat(ERManager *mgr);
static void do_peek(ERManager *mgr);
static void do_retriage(ERManager *mgr);
static void do_discharge(ERManager *mgr);
static void do_search(ERManager *mgr);
static void do_advance_time(ERManager *mgr);
static void do_simulation(ERManager *mgr);
static void print_menu(int clock);

/* -----------------------------------------------------------------------
 * flush_line
 * Purpose   : Discard remaining characters in stdin up to and including
 *             the next newline or EOF.  Used to clear an oversized input.
 * ----------------------------------------------------------------------- */
static void flush_line(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* -----------------------------------------------------------------------
 * read_int
 * Purpose   : Display prompt, read a line from stdin, parse as integer
 *             in [min, max].  Re-prompt on invalid input or out-of-range.
 *             Exits cleanly on EOF.
 * Returns   : The validated integer.
 * ----------------------------------------------------------------------- */
static int read_int(const char *prompt, int min, int max)
{
    char buf[INPUT_BUF];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            /* EOF */
            printf("\nEOF detected. Exiting.\n");
            exit(0);
        }

        /* If input was longer than the buffer, flush the rest */
        if (strchr(buf, '\n') == NULL) {
            flush_line();
        }

        /* Parse */
        char *endptr;
        errno = 0;
        long val = strtol(buf, &endptr, 10);

        /* Validate: endptr must point past at least one digit, no trailing
           non-whitespace, no overflow */
        if (endptr == buf || errno == ERANGE) {
            printf("  [!] Invalid number. Please enter an integer.\n");
            continue;
        }
        /* Skip trailing whitespace */
        while (*endptr != '\0' && isspace((unsigned char)*endptr)) endptr++;
        if (*endptr != '\0') {
            printf("  [!] Unexpected characters after number.\n");
            continue;
        }
        if (val < (long)min || val > (long)max) {
            printf("  [!] Value must be between %d and %d.\n", min, max);
            continue;
        }
        return (int)val;
    }
}

/* -----------------------------------------------------------------------
 * read_string
 * Purpose   : Display prompt, read a non-empty string (trims whitespace).
 *             Re-prompts if blank.  Exits on EOF.
 * ----------------------------------------------------------------------- */
static void read_string(const char *prompt, char *buf, int size)
{
    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(buf, size, stdin) == NULL) {
            printf("\nEOF detected. Exiting.\n");
            exit(0);
        }

        /* Flush overflow */
        if (strchr(buf, '\n') == NULL) {
            flush_line();
        }

        if (trim_string(buf)) {
            return;
        }
        printf("  [!] Name cannot be empty or blank.\n");
    }
}

/* -----------------------------------------------------------------------
 * print_status
 * Purpose   : Print a user-friendly message for a given ERStatus code.
 * ----------------------------------------------------------------------- */
static void print_status(ERStatus st, const char *context)
{
    switch (st) {
        case ER_OK:        printf("  [OK] %s succeeded.\n", context); break;
        case ER_FULL:      printf("  [!] Cannot %s: queue or stack is full.\n", context); break;
        case ER_EMPTY:     printf("  [!] Cannot %s: no patients available.\n", context); break;
        case ER_NOT_FOUND: printf("  [!] Cannot %s: patient ID not found.\n", context); break;
        case ER_DUPLICATE: printf("  [!] Cannot %s: patient ID already exists.\n", context); break;
        case ER_INVALID:   printf("  [!] Cannot %s: invalid input.\n", context); break;
        default:           printf("  [?] Unknown status for %s.\n", context); break;
    }
}

/* -----------------------------------------------------------------------
 * print_menu
 * ----------------------------------------------------------------------- */
static void print_menu(int clock)
{
    printf("\n");
    printf("  +==================================================+\n");
    printf("  |  ER PATIENT MANAGEMENT SYSTEM  [Time: %4d min]  |\n", clock);
    printf("  +==================================================+\n");
    printf("  |  1. Admit new patient                            |\n");
    printf("  |  2. Treat next patient                           |\n");
    printf("  |  3. Peek at next patient                         |\n");
    printf("  |  4. Re-triage patient by ID                      |\n");
    printf("  |  5. Discharge patient by ID                      |\n");
    printf("  |  6. Display all waiting patients                 |\n");
    printf("  |  7. Search for patient by ID                     |\n");
    printf("  |  8. Statistics                                    |\n");
    printf("  |  9. Simulation mode                              |\n");
    printf("  | 10. Undo last treatment                          |\n");
    printf("  | 11. View treatment history                       |\n");
    printf("  | 12. Advance simulated time                       |\n");
    printf("  |  0. Exit                                         |\n");
    printf("  +==================================================+\n");
    printf("  Choice: ");
    fflush(stdout);
}

/* -----------------------------------------------------------------------
 * do_admit
 * ----------------------------------------------------------------------- */
static void do_admit(ERManager *mgr)
{
    printf("\n  -- Admit New Patient --\n");
    Patient p;
    patient_init(&p);

    p.id = read_int("  Patient ID (positive integer): ", 1, INT_MAX);
    read_string("  Patient name: ", p.name, MAX_NAME_LEN);
    p.age = read_int("  Age (0-120): ", MIN_AGE, MAX_AGE);

    printf("  Severity levels: 1=Critical  2=Emergent  3=Urgent  4=Semi-urgent  5=Non-urgent\n");
    p.severity = read_int("  Severity (1-5): ", MIN_SEVERITY, MAX_SEVERITY);

    ERStatus st = er_admit(mgr, &p);
    if (st == ER_OK) {
        printf("  [OK] Patient %d (%s) admitted at Level %d (%s). Clock is now %d.\n",
               p.id, p.name, p.severity, SEVERITY_NAME[p.severity], mgr->clock);
    } else {
        print_status(st, "admit patient");
    }
}

/* -----------------------------------------------------------------------
 * do_treat
 * ----------------------------------------------------------------------- */
static void do_treat(ERManager *mgr)
{
    printf("\n  -- Treat Next Patient --\n");
    Patient p;
    ERStatus st = er_treat_next(mgr, &p);
    if (st == ER_OK) {
        int wait = (mgr->clock - 1) - p.arrival_time; /* clock advanced after treat */
        if (wait < 0) wait = 0;
        printf("  [OK] Now treating:\n");
        printf("       ID      : %d\n", p.id);
        printf("       Name    : %s\n", p.name);
        printf("       Age     : %d\n", p.age);
        printf("       Level   : %d (%s)\n", p.severity, SEVERITY_NAME[p.severity]);
        if (p.severity != p.original_severity) {
            printf("       Orig Lvl: %d (%s)  [AGED UP]\n",
                   p.original_severity, SEVERITY_NAME[p.original_severity]);
        }
        printf("       Wait    : %d min\n", wait);
        printf("  Clock is now %d.\n", mgr->clock);
    } else {
        print_status(st, "treat patient");
    }
}

/* -----------------------------------------------------------------------
 * do_peek
 * ----------------------------------------------------------------------- */
static void do_peek(ERManager *mgr)
{
    printf("\n  -- Peek at Next Patient --\n");
    Patient p;
    ERStatus st = er_peek_next(mgr, &p);
    if (st == ER_OK) {
        int wait = mgr->clock - p.arrival_time;
        if (wait < 0) wait = 0;
        printf("  Next to be treated:\n");
        printf("       ID      : %d\n", p.id);
        printf("       Name    : %s\n", p.name);
        printf("       Age     : %d\n", p.age);
        printf("       Level   : %d (%s)\n", p.severity, SEVERITY_NAME[p.severity]);
        printf("       Wait    : %d min (so far)\n", wait);
    } else {
        print_status(st, "peek");
    }
}

/* -----------------------------------------------------------------------
 * do_retriage
 * ----------------------------------------------------------------------- */
static void do_retriage(ERManager *mgr)
{
    printf("\n  -- Re-triage Patient --\n");
    int id  = read_int("  Patient ID to re-triage: ", 1, INT_MAX);
    int sev = read_int("  New severity (1-5): ", MIN_SEVERITY, MAX_SEVERITY);
    ERStatus st = er_retriage(mgr, id, sev);
    if (st == ER_OK) {
        printf("  [OK] Patient %d moved to Level %d (%s). Clock is now %d.\n",
               id, sev, SEVERITY_NAME[sev], mgr->clock);
    } else {
        print_status(st, "re-triage");
    }
}

/* -----------------------------------------------------------------------
 * do_discharge
 * ----------------------------------------------------------------------- */
static void do_discharge(ERManager *mgr)
{
    printf("\n  -- Discharge Patient --\n");
    int id = read_int("  Patient ID to discharge: ", 1, INT_MAX);
    Patient out;
    ERStatus st = er_discharge(mgr, id, &out);
    if (st == ER_OK) {
        printf("  [OK] Patient %d (%s) discharged. Clock is now %d.\n",
               out.id, out.name, mgr->clock);
    } else {
        print_status(st, "discharge");
    }
}

/* -----------------------------------------------------------------------
 * do_search
 * ----------------------------------------------------------------------- */
static void do_search(ERManager *mgr)
{
    printf("\n  -- Search for Patient --\n");
    int id = read_int("  Patient ID to search: ", 1, INT_MAX);
    ERStatus st = er_search(mgr, id);
    if (st != ER_OK) {
        print_status(st, "search");
    }
}

/* -----------------------------------------------------------------------
 * do_advance_time
 * ----------------------------------------------------------------------- */
static void do_advance_time(ERManager *mgr)
{
    printf("\n  -- Advance Simulated Time --\n");
    int minutes = read_int("  Advance by how many minutes (1-9999): ", 1, 9999);
    ERStatus st = er_advance_time(mgr, minutes);
    if (st == ER_OK) {
        printf("  [OK] Clock advanced to %d min. Aging applied.\n", mgr->clock);
    } else {
        print_status(st, "advance time");
    }
}

/* -----------------------------------------------------------------------
 * do_simulation
 * Purpose   : Auto-generate N random patients, admit them, apply aging,
 *             then treat them all in priority order.  Print a results table.
 *
 * Severity weighting (approximate):
 *   Level 1: ~5%    (rand % 20 == 0)
 *   Level 2: ~10%   (rand % 10 == 0)
 *   Level 3: ~25%   (first 5 of 20)
 *   Level 4: ~30%   (next 6 of 20)
 *   Level 5: ~30%   (remainder)
 * ----------------------------------------------------------------------- */
static void do_simulation(ERManager *mgr)
{
    printf("\n  -- Simulation Mode --\n");
    int n = read_int("  Number of patients to simulate (1-200): ", 1, SIM_MAX_PATIENTS);

    printf("  Use fixed random seed for reproducibility? (1=Yes, 0=No): ");
    int use_seed = read_int("", 0, 1);
    unsigned int seed;
    if (use_seed) {
        seed = (unsigned int)read_int("  Enter seed (0-99999): ", 0, 99999);
    } else {
        seed = (unsigned int)time(NULL);
    }
    srand(seed);
    printf("  Using seed: %u\n\n", seed);

    /* Save state so simulation doesn't permanently alter the manager */
    ERManager sim;
    er_init(&sim);
    /* We'll simulate into a fresh manager */

    /* --- Admit phase --- */
    static const char *first_names[] = {
        "Alice", "Bob", "Carol", "David", "Eva",
        "Frank", "Grace", "Henry", "Iris", "Jack",
        "Karen", "Leo", "Mia", "Nate", "Olivia",
        "Paul", "Quinn", "Rosa", "Sam", "Tina"
    };
    int name_count = 20;

    printf("  Admitting %d patients...\n", n);
    int admitted = 0;
    for (int k = 0; k < n; k++) {
        Patient p;
        patient_init(&p);

        p.id  = k + 1;
        int ni = rand() % name_count;
        snprintf(p.name, MAX_NAME_LEN, "%s_%d", first_names[ni], k + 1);
        p.age = rand() % (MAX_AGE + 1);

        /* Weighted severity */
        int r = rand() % 20;
        if      (r == 0)       p.severity = 1;  /* ~5%  */
        else if (r == 1)       p.severity = 2;  /* ~5%  */
        else if (r <= 5)       p.severity = 3;  /* ~25% */
        else if (r <= 11)      p.severity = 4;  /* ~30% */
        else                   p.severity = 5;  /* ~35% */

        /* Spread arrival times: each patient arrives 1-10 min apart */
        sim.clock += rand() % 10;
        p.arrival_time     = sim.clock;
        p.level_entry_time = sim.clock;
        p.original_severity = p.severity;

        int idx = p.severity - 1;
        if (!cq_is_full(&sim.queues[idx])) {
            cq_enqueue(&sim.queues[idx], &p);
            admitted++;
        }
    }
    printf("  Admitted: %d patients\n\n", admitted);

    /* --- Advance time to trigger aging mid-simulation --- */
    int time_jump = 70 + rand() % 80; /* 70-149 min jump to trigger aging */
    printf("  Advancing simulation clock by %d minutes (triggers aging)...\n\n", time_jump);
    er_advance_time(&sim, time_jump);

    /* --- Display before treating --- */
    printf("  Waiting room after aging:\n");
    er_display_waiting(&sim);

    /* --- Treat all patients --- */
    printf("  %-4s %-6s %-22s %-3s %-6s %-6s %-7s %-6s\n",
           "Ord", "ID", "Name", "Age", "CurLvl", "OrgLvl", "Wait(m)", "Aged?");
    printf("  %-4s %-6s %-22s %-3s %-6s %-6s %-7s %-6s\n",
           "---", "------", "----------------------", "---",
           "------", "------", "-------", "------");

    int order = 0;
    while (1) {
        Patient p;
        ERStatus st = er_treat_next(&sim, &p);
        if (st == ER_EMPTY) break;
        if (st != ER_OK) break;

        order++;
        int wait = (sim.clock - 1) - p.arrival_time;
        if (wait < 0) wait = 0;
        printf("  %-4d %-6d %-22s %-3d %-6d %-6d %-7d %-6s\n",
               order, p.id, p.name, p.age,
               p.severity, p.original_severity,
               wait,
               (p.severity != p.original_severity) ? "YES" : "no");
    }

    printf("\n  Simulation complete: %d patients treated.\n", order);
    printf("  (Simulation used a temporary manager; the main ER is unchanged.)\n\n");
    /* sim is on the stack; no dynamic memory to free */
    (void)mgr; /* main manager untouched */
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    ERManager mgr;
    er_init(&mgr);

    printf("\n  Welcome to the ER Patient Management System.\n");
    printf("  Time unit: minutes (simulated).\n");

    while (1) {
        print_menu(mgr.clock);

        char buf[INPUT_BUF];
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            printf("\nEOF detected. Exiting.\n");
            break;
        }
        if (strchr(buf, '\n') == NULL) flush_line();

        char *endptr;
        errno = 0;
        long choice = strtol(buf, &endptr, 10);
        while (*endptr != '\0' && isspace((unsigned char)*endptr)) endptr++;
        if (endptr == buf || *endptr != '\0' || errno == ERANGE ||
            choice < 0 || choice > 12) {
            printf("  [!] Please enter a number between 0 and 12.\n");
            continue;
        }

        switch ((int)choice) {
            case 1:  do_admit(&mgr);        break;
            case 2:  do_treat(&mgr);        break;
            case 3:  do_peek(&mgr);         break;
            case 4:  do_retriage(&mgr);     break;
            case 5:  do_discharge(&mgr);    break;
            case 6:  er_display_waiting(&mgr); break;
            case 7:  do_search(&mgr);       break;
            case 8:  er_statistics(&mgr);   break;
            case 9:  do_simulation(&mgr);   break;
            case 10: {
                ERStatus st = er_undo_last(&mgr);
                if (st == ER_OK) {
                    printf("  [OK] Last treatment undone. Patient returned to queue.\n");
                } else {
                    print_status(st, "undo last treatment");
                }
                break;
            }
            case 11: er_show_history(&mgr); break;
            case 12: do_advance_time(&mgr); break;
            case 0:
                printf("  Goodbye.\n");
                return 0;
            default:
                printf("  [!] Invalid choice.\n");
                break;
        }
    }
    return 0;
}
