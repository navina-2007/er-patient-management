/*
 * patient.h
 * =========
 * Defines the Patient struct, shared constants (capacity limits, severity
 * range, aging thresholds), the ERStatus return-code enum, and declarations
 * for patient helper functions.
 *
 * Design note:
 *   queues[] is indexed 0..4 in the implementation (queues[severity-1]).
 *   "level" always refers to triage severity 1..5 throughout the codebase.
 *   AGING_THRESHOLD[i] is the maximum wait (minutes) a patient may sit at
 *   level (i+1) before being promoted.  Index 0 (level 1) and index 1
 *   (level 2) are 0 meaning "never promoted from this level" because level 1
 *   is the promotion ceiling (reserved for truly critical patients) and
 *   level 2 is one step below that ceiling.
 */

#ifndef PATIENT_H
#define PATIENT_H

#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Compile-time capacities
 * ----------------------------------------------------------------------- */
#define MAX_PER_LEVEL   100   /* max patients per severity queue            */
#define MAX_HISTORY     200   /* max entries in the treated-patient stack   */
#define NUM_LEVELS        5   /* triage levels 1..5                         */
#define MAX_NAME_LEN     50   /* including NUL terminator                   */
#define MAX_AGE         120
#define MIN_AGE           0
#define MIN_SEVERITY      1
#define MAX_SEVERITY      5

/*
 * AGING_THRESHOLD[i] = max wait time (minutes) at level (i+1) before
 * promotion.  0 means "never promoted".
 *
 *   Index 0 -> level 1 (Critical)    : 0  (never promoted; this IS the ceiling)
 *   Index 1 -> level 2 (Emergent)    : 0  (promotion ceiling; patients stop here)
 *   Index 2 -> level 3 (Urgent)      : 60 (promoted to level 2 after 60 min)
 *   Index 3 -> level 4 (Semi-urgent) : 90 (promoted to level 3 after 90 min)
 *   Index 4 -> level 5 (Non-urgent)  :120 (promoted to level 4 after 120 min)
 *
 * PROMOTION CEILING NOTE:
 *   Aging may promote a patient AT MOST to level 2.  A promoted non-urgent
 *   patient can NEVER enter level 1.  Level 1 is reserved for patients
 *   triaged as Critical by clinical staff.
 */
extern const int AGING_THRESHOLD[NUM_LEVELS];

/* -----------------------------------------------------------------------
 * Severity level names (for display)
 * ----------------------------------------------------------------------- */
extern const char *SEVERITY_NAME[NUM_LEVELS + 1]; /* index 1..5 used */

/* -----------------------------------------------------------------------
 * Return status codes
 * Every non-printing module returns one of these; only main.c (and the
 * simulation) converts them to human-readable messages.
 * ----------------------------------------------------------------------- */
typedef enum {
    ER_OK = 0,
    ER_FULL,        /* target queue or stack is at capacity               */
    ER_EMPTY,       /* source queue or stack has no elements              */
    ER_NOT_FOUND,   /* patient ID not present in the searched scope       */
    ER_DUPLICATE,   /* patient ID already exists (queues or history)      */
    ER_INVALID      /* bad argument (severity out of range, etc.)         */
} ERStatus;

/* -----------------------------------------------------------------------
 * Patient struct
 * ----------------------------------------------------------------------- */
typedef struct {
    int  id;               /* unique positive identifier                   */
    char name[MAX_NAME_LEN];
    int  age;              /* 0..120                                        */
    int  severity;         /* CURRENT queue level (1..5); changes via aging/
                              re-triage                                     */
    int  original_severity;/* level assigned at admission or last manual
                              re-triage (never changed by aging)            */
    int  arrival_time;     /* simulated clock when admitted (immutable)     */
    int  level_entry_time; /* simulated clock when patient entered the
                              CURRENT level; reset on every level change    */
    int  treatment_start_time; /* clock value when TREAT was called;
                              set by er_treat_next; 0 for waiting patients.
                              wait_final = treatment_start_time - arrival_time */
} Patient;

/* -----------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------- */

/*
 * patient_init
 * Initialise all fields of *p to safe zero-like values.
 * Parameters : p - pointer to the Patient to initialise (must not be NULL)
 * Returns    : nothing
 * Complexity : O(1)
 */
void patient_init(Patient *p);

/*
 * patient_print
 * Print one Patient row to stdout with aligned columns.
 * Parameters : p        - pointer to the Patient to print
 *              position - 1-based overall treatment position (use 0 to skip)
 *              clock    - current simulated time (to compute wait time)
 * Returns    : nothing
 * Complexity : O(1)
 */
void patient_print(const Patient *p, int position, int clock);

/*
 * patient_print_header
 * Print the column header row that matches patient_print output.
 * Complexity : O(1)
 */
void patient_print_header(void);

/*
 * is_valid_severity
 * Returns true iff sev is in [MIN_SEVERITY, MAX_SEVERITY].
 * Complexity : O(1)
 */
bool is_valid_severity(int sev);

/*
 * is_valid_age
 * Returns true iff age is in [MIN_AGE, MAX_AGE].
 * Complexity : O(1)
 */
bool is_valid_age(int age);

/*
 * trim_string
 * Remove leading and trailing whitespace from s in-place.
 * Returns true if the result is non-empty, false otherwise.
 * Complexity : O(n) where n = strlen(s)
 */
bool trim_string(char *s);

#endif /* PATIENT_H */
