/*
 * ermanager.c
 * ===========
 * Implementation of all ERManager operations.
 * See ermanager.h for function-level documentation.
 */

#include "ermanager.h"
#include "patient.h"
#include "cqueue.h"
#include "stack.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Internal helper: compute total patients across all queues
 * ----------------------------------------------------------------------- */
static int total_waiting(const ERManager *mgr)
{
    int total = 0;
    for (int i = 0; i < NUM_LEVELS; i++) {
        total += mgr->queues[i].count;
    }
    return total;
}

/* -----------------------------------------------------------------------
 * er_init
 * Complexity: O(1) - initialises fixed-size arrays
 * ----------------------------------------------------------------------- */
void er_init(ERManager *mgr)
{
    for (int i = 0; i < NUM_LEVELS; i++) {
        cq_init(&mgr->queues[i]);
    }
    stack_init(&mgr->history);
    mgr->clock              = 0;
    mgr->total_treated      = 0;
    mgr->total_treated_wait = 0;
}

/* -----------------------------------------------------------------------
 * er_id_exists
 * Purpose  : Check queues + history for a given ID (for duplicate prevention).
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
bool er_id_exists(const ERManager *mgr, int id)
{
    /* Search all queues */
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (cq_find_by_id(&mgr->queues[i], id, NULL) == ER_OK) {
            return true;
        }
    }
    /* Search history stack */
    for (int i = 0; i < mgr->history.top; i++) {
        if (mgr->history.data[i].id == id) {
            return true;
        }
    }
    return false;
}

/* -----------------------------------------------------------------------
 * er_admit
 * Purpose  : Validate and admit a new patient.
 *            Uses cq_enqueue (O(1)) so new arrivals are always appended
 *            to the rear – this preserves admission FIFO order within a
 *            level, which is what we want for fresh admissions.
 * Complexity: O(n) due to duplicate check
 * ----------------------------------------------------------------------- */
ERStatus er_admit(ERManager *mgr, Patient *patient)
{
    if (!is_valid_severity(patient->severity)) {
        return ER_INVALID;
    }
    if (!is_valid_age(patient->age)) {
        return ER_INVALID;
    }
    if (patient->id <= 0) {
        return ER_INVALID;
    }
    if (patient->name[0] == '\0') {
        return ER_INVALID;
    }
    if (er_id_exists(mgr, patient->id)) {
        return ER_DUPLICATE;
    }

    int idx = patient->severity - 1; /* queues[0] = level 1, etc. */
    if (cq_is_full(&mgr->queues[idx])) {
        return ER_FULL;
    }

    /* Stamp arrival and level-entry times */
    patient->arrival_time      = mgr->clock;
    patient->level_entry_time  = mgr->clock;
    patient->original_severity = patient->severity;

    ERStatus st = cq_enqueue(&mgr->queues[idx], patient);
    if (st == ER_OK) {
        mgr->clock++;
    }
    return st;
}

/* -----------------------------------------------------------------------
 * er_treat_next
 * Purpose  : Apply aging, then dequeue from the highest-priority queue.
 * Complexity: O(n) for aging + O(1) for dequeue
 * ----------------------------------------------------------------------- */
ERStatus er_treat_next(ERManager *mgr, Patient *out)
{
    er_apply_aging(mgr);

    for (int i = 0; i < NUM_LEVELS; i++) {
        if (!cq_is_empty(&mgr->queues[i])) {
            Patient p;
            ERStatus st = cq_dequeue(&mgr->queues[i], &p);
            if (st != ER_OK) {
                return st;
            }
            /* Record statistics */
            int wait = mgr->clock - p.arrival_time;
            if (wait < 0) wait = 0;
            mgr->total_treated++;
            mgr->total_treated_wait += wait;

            /* Freeze the wait time at the moment of treatment */
            p.treatment_start_time = mgr->clock;

            /* Push to history (if stack is full the oldest entry is lost —
             * the stack is a bounded undo buffer, not a permanent record) */
            stack_push(&mgr->history, &p);

            if (out != NULL) {
                *out = p;
            }
            mgr->clock++;
            return ER_OK;
        }
    }
    return ER_EMPTY;
}

/* -----------------------------------------------------------------------
 * er_peek_next
 * Purpose  : Apply aging, peek at the highest-priority patient.
 * Complexity: O(n) for aging + O(1) for peek
 * ----------------------------------------------------------------------- */
ERStatus er_peek_next(ERManager *mgr, Patient *out)
{
    er_apply_aging(mgr);

    for (int i = 0; i < NUM_LEVELS; i++) {
        if (!cq_is_empty(&mgr->queues[i])) {
            return cq_peek_front(&mgr->queues[i], out);
        }
    }
    return ER_EMPTY;
}

/* -----------------------------------------------------------------------
 * er_retriage
 * Purpose  : Move a patient to a new severity queue in arrival_time order.
 *            If the target queue is full, the patient is left untouched.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus er_retriage(ERManager *mgr, int id, int new_severity)
{
    if (!is_valid_severity(new_severity)) {
        return ER_INVALID;
    }

    /* Find the patient */
    int found_level = -1;
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (cq_find_by_id(&mgr->queues[i], id, NULL) == ER_OK) {
            found_level = i;
            break;
        }
    }
    if (found_level == -1) {
        return ER_NOT_FOUND;
    }

    int target_idx = new_severity - 1;

    /* Check target capacity BEFORE removing from source
     * (same level re-triage is allowed but doesn't gain a slot) */
    if (found_level != target_idx && cq_is_full(&mgr->queues[target_idx])) {
        return ER_FULL;
    }

    /* Remove from current queue */
    Patient p;
    ERStatus st = cq_remove_by_id(&mgr->queues[found_level], id, &p);
    if (st != ER_OK) {
        return st;
    }

    /* Update severity fields */
    p.severity          = new_severity;
    p.original_severity = new_severity;
    p.level_entry_time  = mgr->clock;

    /* Insert into target queue in arrival_time order */
    st = cq_insert_in_order(&mgr->queues[target_idx], &p);
    if (st != ER_OK) {
        /*
         * This should not happen because we checked capacity above,
         * but if it does (same-level re-triage edge case), try plain enqueue.
         * As a safety net, re-insert back into the original queue.
         */
        p.severity         = found_level + 1;
        p.original_severity = found_level + 1;
        p.level_entry_time = mgr->clock;
        cq_insert_in_order(&mgr->queues[found_level], &p);
        return ER_FULL;
    }

    mgr->clock++;
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * er_discharge
 * Purpose  : Remove a patient by ID without treating them.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus er_discharge(ERManager *mgr, int id, Patient *out)
{
    for (int i = 0; i < NUM_LEVELS; i++) {
        ERStatus st = cq_remove_by_id(&mgr->queues[i], id, out);
        if (st == ER_OK) {
            mgr->clock++;
            return ER_OK;
        }
    }
    return ER_NOT_FOUND;
}

/* -----------------------------------------------------------------------
 * er_search
 * Purpose  : Find a patient by ID and print their full status.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus er_search(ERManager *mgr, int id)
{
    er_apply_aging(mgr);

    int overall_pos = 0;
    for (int i = 0; i < NUM_LEVELS; i++) {
        for (int j = 0; j < mgr->queues[i].count; j++) {
            overall_pos++;
            Patient *p = cq_get_at(&mgr->queues[i], j);
            if (p != NULL && p->id == id) {
                int wait = mgr->clock - p->arrival_time;
                printf("\n  Patient found:\n");
                printf("  ID              : %d\n",   p->id);
                printf("  Name            : %s\n",   p->name);
                printf("  Age             : %d\n",   p->age);
                printf("  Current level   : %d (%s)\n",
                       p->severity, SEVERITY_NAME[p->severity]);
                printf("  Original level  : %d (%s)\n",
                       p->original_severity, SEVERITY_NAME[p->original_severity]);
                printf("  Position in lvl : %d of %d\n",
                       j + 1, mgr->queues[i].count);
                printf("  Overall position: %d of %d\n",
                       overall_pos, total_waiting(mgr));
                printf("  Wait time       : %d min\n", wait < 0 ? 0 : wait);
                printf("  Aged up?        : %s\n",
                       (p->severity != p->original_severity) ? "Yes" : "No");
                return ER_OK;
            }
        }
    }
    return ER_NOT_FOUND;
}

/* -----------------------------------------------------------------------
 * er_apply_aging
 * Purpose   : Promote patients who have waited too long at their level.
 *
 * Algorithm :
 *   Iterate levels from 5 down to 3 (indices 4 down to 2).
 *   For each patient in the level whose wait >= threshold, attempt to
 *   move them one level up (level - 1, i.e. index - 1).
 *   Promotion ceiling: target level is always >= 2 (index >= 1).
 *   Inserted in arrival_time order into the target queue.
 *   If target is full, skip (retry next call).
 *   We iterate from high index to low so a patient that gets promoted into
 *   level 3 can ALSO be processed in the same call if the level-3 threshold
 *   is met (cascade).  We use a stable inner scan so the result is correct.
 *
 * PROMOTION CEILING COMMENT:
 *   The minimum target level for aging is 2.  A promoted non-urgent patient
 *   can never enter level 1.  Level 1 is reserved for patients triaged as
 *   Critical by clinical staff.
 *
 * Complexity: O(n^2) worst case
 * ----------------------------------------------------------------------- */
void er_apply_aging(ERManager *mgr)
{
    /*
     * Process from level 5 down to level 3 (indices 4 to 2).
     * We need to be careful: when we promote a patient, the queue shrinks,
     * so we loop using a while with explicit index management.
     */
    for (int i = NUM_LEVELS - 1; i >= 2; i--) {  /* i = 4, 3, 2 */
        int level         = i + 1;  /* actual triage level (3, 4, 5)       */
        int threshold     = AGING_THRESHOLD[i];

        if (threshold == 0) continue; /* safety: should not happen for i>=2 */

        /* Scan all patients in this queue */
        int j = 0;
        while (j < mgr->queues[i].count) {
            Patient *p = cq_get_at(&mgr->queues[i], j);
            if (p == NULL) { j++; continue; }

            int wait_at_level = mgr->clock - p->level_entry_time;

            if (wait_at_level >= threshold) {
                /* Enforce promotion ceiling: never promote into level 1 */
                int new_level = level - 1;
                if (new_level < 2) new_level = 2; /* ceiling */

                int new_idx = new_level - 1;

                if (cq_is_full(&mgr->queues[new_idx])) {
                    /* Skip; retry next call */
                    j++;
                    continue;
                }

                /* Remove from current queue */
                Patient promoted;
                if (cq_remove_by_id(&mgr->queues[i], p->id, &promoted) != ER_OK) {
                    j++;
                    continue;
                }

                /* Update patient fields */
                promoted.severity        = new_level;
                promoted.level_entry_time = mgr->clock;
                /* NOTE: arrival_time and original_severity are preserved */

                /* Insert in arrival_time order into target queue */
                cq_insert_in_order(&mgr->queues[new_idx], &promoted);

                /*
                 * Do NOT increment j: the patient at index j is now the
                 * one that used to be at j+1 (the queue shrunk).
                 */
            } else {
                j++;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * er_display_waiting
 * Purpose  : Print all waiting patients in treatment order.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
void er_display_waiting(ERManager *mgr)
{
    er_apply_aging(mgr);

    int tw = total_waiting(mgr);
    printf("\n  === Waiting Room (Clock: %d min) | Total: %d patients ===\n\n",
           mgr->clock, tw);

    if (tw == 0) {
        printf("  No patients currently waiting.\n\n");
        return;
    }

    patient_print_header();

    int pos = 0;
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (!cq_is_empty(&mgr->queues[i])) {
            printf("  -- Level %d: %s (%d patients) --\n",
                   i + 1, SEVERITY_NAME[i + 1], mgr->queues[i].count);
            for (int j = 0; j < mgr->queues[i].count; j++) {
                Patient *p = cq_get_at(&mgr->queues[i], j);
                if (p != NULL) {
                    pos++;
                    patient_print(p, pos, mgr->clock);
                }
            }
        }
    }
    printf("\n");
}

/* -----------------------------------------------------------------------
 * er_statistics
 * Purpose  : Print aggregate statistics with a bar chart per level.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
void er_statistics(ERManager *mgr)
{
    er_apply_aging(mgr);

    int tw = total_waiting(mgr);
    printf("\n  === ER Statistics (Clock: %d min) ===\n\n", mgr->clock);

    /* Per-level counts and bar chart */
    printf("  Patients per level:\n");
    for (int i = 0; i < NUM_LEVELS; i++) {
        int cnt = mgr->queues[i].count;
        printf("  Level %d (%s): %3d  [", i + 1, SEVERITY_NAME[i + 1], cnt);
        int bars = cnt > 40 ? 40 : cnt;
        for (int b = 0; b < bars; b++) putchar('#');
        for (int b = bars; b < 40; b++) putchar(' ');
        printf("]\n");
    }

    printf("\n  Total waiting          : %d\n", tw);

    /* Compute wait stats from current queues */
    int max_wait    = 0;
    long sum_wait   = 0;
    int max_id      = -1;
    char max_name[MAX_NAME_LEN];
    max_name[0] = '\0';

    for (int i = 0; i < NUM_LEVELS; i++) {
        for (int j = 0; j < mgr->queues[i].count; j++) {
            Patient *p = cq_get_at(&mgr->queues[i], j);
            if (p == NULL) continue;
            int w = mgr->clock - p->arrival_time;
            if (w < 0) w = 0;
            sum_wait += w;
            if (w > max_wait) {
                max_wait = w;
                max_id   = p->id;
                strncpy(max_name, p->name, MAX_NAME_LEN - 1);
                max_name[MAX_NAME_LEN - 1] = '\0';
            }
        }
    }

    if (tw > 0) {
        printf("  Average wait (waiting) : %.1f min\n", (double)sum_wait / tw);
        if (max_id >= 0) {
            printf("  Longest wait           : %d min (ID %d - %s)\n",
                   max_wait, max_id, max_name);
        }
    } else {
        printf("  Average wait (waiting) : N/A\n");
        printf("  Longest wait           : N/A\n");
    }

    printf("\n  Total treated          : %d\n", mgr->total_treated);
    if (mgr->total_treated > 0) {
        printf("  Average wait (treated) : %.1f min\n",
               (double)mgr->total_treated_wait / mgr->total_treated);
    } else {
        printf("  Average wait (treated) : N/A\n");
    }
    printf("\n");
}

/* -----------------------------------------------------------------------
 * er_undo_last
 * Purpose  : Pop last treated patient and re-admit them into their queue.
 *            Uses cq_insert_in_order so the patient lands at the correct
 *            arrival_time position.
 *            The statistics counters are rolled back.
 *            Clock does NOT advance (undo is a correction, not an action).
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus er_undo_last(ERManager *mgr)
{
    if (stack_is_empty(&mgr->history)) {
        return ER_EMPTY;
    }

    Patient p;
    /* Peek first to check if the target queue has room */
    if (stack_peek(&mgr->history, &p) != ER_OK) {
        return ER_EMPTY;
    }

    int idx = p.severity - 1;
    if (cq_is_full(&mgr->queues[idx])) {
        return ER_FULL;
    }

    /* Now actually pop */
    stack_pop(&mgr->history, &p);

    /* Roll back statistics */
    int wait = mgr->clock - p.arrival_time;
    if (wait < 0) wait = 0;
    mgr->total_treated--;
    mgr->total_treated_wait -= wait;
    if (mgr->total_treated < 0) mgr->total_treated = 0;
    if (mgr->total_treated_wait < 0) mgr->total_treated_wait = 0;

    /* Reset level_entry_time to now so aging restarts from current clock */
    p.level_entry_time = mgr->clock;

    return cq_insert_in_order(&mgr->queues[idx], &p);
}

/* -----------------------------------------------------------------------
 * er_show_history
 * Purpose  : Print the history stack from top (most recent) to bottom.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
void er_show_history(ERManager *mgr)
{
    printf("\n  === Treatment History (most recent first) ===\n");
    printf("  Total treated so far: %d\n\n", mgr->total_treated);

    if (stack_is_empty(&mgr->history)) {
        printf("  No treated patients in memory.\n\n");
        return;
    }

    patient_print_header();

    for (int i = mgr->history.top - 1; i >= 0; i--) {
        Patient *p = &mgr->history.data[i];
        printf("  [%d] ID=%-4d %-20s Age=%-3d Level=%d(%s) Orig=%d(%s) Wait=%d min\n",
               mgr->history.top - i,
               p->id, p->name, p->age,
               p->severity, SEVERITY_NAME[p->severity],
               p->original_severity, SEVERITY_NAME[p->original_severity],
               mgr->clock - p->arrival_time);
    }
    printf("\n");
}

/* -----------------------------------------------------------------------
 * er_advance_time
 * Purpose  : Advance clock by `minutes` and trigger aging.
 * Returns  : ER_INVALID if minutes <= 0.
 * Complexity: O(n^2) due to aging
 * ----------------------------------------------------------------------- */
ERStatus er_advance_time(ERManager *mgr, int minutes)
{
    if (minutes <= 0) {
        return ER_INVALID;
    }
    mgr->clock += minutes;
    er_apply_aging(mgr);
    return ER_OK;
}
