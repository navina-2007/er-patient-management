/*
 * patient.c
 * =========
 * Implementation of Patient helpers: initialisation, printing, validation,
 * and string trimming.
 */

#include "patient.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Compile-time constants (definitions)
 * ----------------------------------------------------------------------- */

/*
 * AGING_THRESHOLD[i] => threshold for level (i+1).
 * 0 means "never promote from this level".
 * See patient.h for the full explanation.
 */
const int AGING_THRESHOLD[NUM_LEVELS] = {
    0,   /* index 0 -> level 1: promotion ceiling, never promoted        */
    0,   /* index 1 -> level 2: promotion ceiling, never promoted        */
    60,  /* index 2 -> level 3: promote to level 2 after 60 min         */
    90,  /* index 3 -> level 4: promote to level 3 after 90 min         */
    120  /* index 4 -> level 5: promote to level 4 after 120 min        */
};

/*
 * Human-readable names for each severity level.
 * Index 0 is unused (kept for safety); indices 1..5 correspond to levels.
 */
const char *SEVERITY_NAME[NUM_LEVELS + 1] = {
    "Unknown",      /* 0 - unused                */
    "Critical",     /* 1                          */
    "Emergent",     /* 2                          */
    "Urgent",       /* 3                          */
    "Semi-urgent",  /* 4                          */
    "Non-urgent"    /* 5                          */
};

/* -----------------------------------------------------------------------
 * patient_init
 * Purpose   : Zero-initialise a Patient struct to known-safe values.
 * Parameters: p - non-NULL pointer to the Patient to initialise.
 * Returns   : nothing
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
void patient_init(Patient *p)
{
    p->id               = 0;
    p->name[0]          = '\0';
    p->age              = 0;
    p->severity         = 0;
    p->original_severity = 0;
    p->arrival_time     = 0;
    p->level_entry_time = 0;
    p->treatment_start_time = 0;
}

/* -----------------------------------------------------------------------
 * patient_print_header
 * Purpose   : Print the column header for the patient table.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
void patient_print_header(void)
{
    printf("%-4s %-6s %-20s %-4s %-12s %-12s %-8s %-10s %s\n",
           "Pos", "ID", "Name", "Age",
           "CurLevel", "OrigLevel", "Wait(m)", "Status", "Aged?");
    printf("%-4s %-6s %-20s %-4s %-12s %-12s %-8s %-10s %s\n",
           "---", "------", "--------------------", "----",
           "------------", "------------", "-------", "----------", "-----");
}

/* -----------------------------------------------------------------------
 * patient_print
 * Purpose   : Print one formatted row for the given patient.
 * Parameters: p        - non-NULL pointer to the Patient to display.
 *             position - 1-based overall treatment position; 0 => skip col.
 *             clock    - current simulated time, used to compute wait time.
 * Returns   : nothing
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
void patient_print(const Patient *p, int position, int clock)
{
    int wait = clock - p->arrival_time;
    const char *aged_marker = (p->severity != p->original_severity)
                              ? "(aged)" : "";
    char pos_buf[16];
    if (position > 0) {
        snprintf(pos_buf, sizeof(pos_buf), "%d", position);
    } else {
        pos_buf[0] = '-'; pos_buf[1] = '\0';
    }

    /* Truncate name display to 20 chars if needed */
    char name_disp[21];
    strncpy(name_disp, p->name, 20);
    name_disp[20] = '\0';

    printf("%-4s %-6d %-20s %-4d %-12s %-12s %-8d %-10s %s\n",
           pos_buf,
           p->id,
           name_disp,
           p->age,
           SEVERITY_NAME[p->severity],
           SEVERITY_NAME[p->original_severity],
           wait < 0 ? 0 : wait,
           (p->severity <= 2) ? "HIGH PRI" : "waiting",
           aged_marker);
}

/* -----------------------------------------------------------------------
 * is_valid_severity
 * Purpose   : Return true iff sev is a valid triage level (1..5).
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
bool is_valid_severity(int sev)
{
    return (sev >= MIN_SEVERITY && sev <= MAX_SEVERITY);
}

/* -----------------------------------------------------------------------
 * is_valid_age
 * Purpose   : Return true iff age is in the acceptable range [0..120].
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
bool is_valid_age(int age)
{
    return (age >= MIN_AGE && age <= MAX_AGE);
}

/* -----------------------------------------------------------------------
 * trim_string
 * Purpose   : Remove leading and trailing whitespace from s in-place.
 * Parameters: s - NUL-terminated string to trim.
 * Returns   : true if the trimmed string is non-empty, false otherwise.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
bool trim_string(char *s)
{
    if (s == NULL) return false;

    /* Remove trailing whitespace (including '\n' from fgets) */
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }

    /* Skip leading whitespace */
    int start = 0;
    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }

    if (start > 0) {
        memmove(s, s + start, (size_t)(len - start + 1));
    }

    return (s[0] != '\0');
}
