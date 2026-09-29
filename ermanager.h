/*
 * ermanager.h
 * ===========
 * ERManager: the top-level controller that owns all queues, the history
 * stack, and the simulated clock.
 *
 * Queue indexing convention:
 *   manager.queues[0] => triage level 1 (Critical)
 *   manager.queues[1] => triage level 2 (Emergent)
 *   manager.queues[2] => triage level 3 (Urgent)
 *   manager.queues[3] => triage level 4 (Semi-urgent)
 *   manager.queues[4] => triage level 5 (Non-urgent)
 *   Access: queues[severity - 1]
 */

#ifndef ERMANAGER_H
#define ERMANAGER_H

#include "cqueue.h"
#include "stack.h"
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * ERManager struct
 * ----------------------------------------------------------------------- */
typedef struct {
    CircularQueue queues[NUM_LEVELS]; /* queues[0..4] for levels 1..5       */
    Stack         history;            /* stack of treated patients           */
    int           clock;              /* simulated time in minutes           */
    int           total_treated;      /* cumulative count of treated patients*/
    long          total_treated_wait; /* sum of all treated patients' waits  */
} ERManager;

/* -----------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------- */

/*
 * er_init
 * Purpose   : Zero-initialise the ERManager (all queues empty, clock = 0).
 * Complexity: O(1)
 */
void er_init(ERManager *mgr);

/*
 * er_id_exists
 * Purpose   : Check whether a given id already exists in ANY queue OR the
 *             history stack (to prevent duplicate IDs).
 * Returns   : true if found anywhere, false otherwise.
 * Complexity: O(n) where n = total patients across all queues + history
 */
bool er_id_exists(const ERManager *mgr, int id);

/*
 * er_admit
 * Purpose   : Validate and admit a new patient.
 *             Sets patient->severity as the queue level, arrival_time =
 *             clock, level_entry_time = clock; enqueues using cq_enqueue
 *             (O(1), simple FIFO append for new admissions).
 *             Clock advances by 1 on success.
 * Returns   : ER_OK, ER_FULL, ER_DUPLICATE, or ER_INVALID.
 * Complexity: O(n) for duplicate check, O(1) for enqueue
 */
ERStatus er_admit(ERManager *mgr, Patient *patient);

/*
 * er_treat_next
 * Purpose   : Apply aging, then dequeue from the lowest-numbered non-empty
 *             queue.  Push the patient onto the history stack and update
 *             statistics.  Clock advances by 1 on success.
 * Parameters: mgr - the manager.
 *             out - pointer where the treated Patient is written (may be NULL).
 * Returns   : ER_OK on success, ER_EMPTY if no patients are waiting.
 * Complexity: O(n) for aging + O(1) for dequeue
 */
ERStatus er_treat_next(ERManager *mgr, Patient *out);

/*
 * er_peek_next
 * Purpose   : Apply aging and return the identity of the next patient to be
 *             treated WITHOUT removing them.
 * Returns   : ER_OK, ER_EMPTY.
 * Complexity: O(n) for aging + O(1) for peek
 */
ERStatus er_peek_next(ERManager *mgr, Patient *out);

/*
 * er_retriage
 * Purpose   : Find a patient by id, move them to `new_severity` queue,
 *             insert in arrival_time order, reset severity fields and
 *             level_entry_time.  Clock advances by 1 on success.
 * Returns   : ER_OK, ER_NOT_FOUND, ER_FULL, ER_INVALID.
 * Complexity: O(n)
 */
ERStatus er_retriage(ERManager *mgr, int id, int new_severity);

/*
 * er_discharge
 * Purpose   : Remove a specific patient by id from whichever queue they
 *             are in.  Clock advances by 1 on success.
 * Parameters: mgr - the manager.
 *             id  - patient id to remove.
 *             out - if non-NULL, the removed Patient is written here.
 * Returns   : ER_OK, ER_NOT_FOUND.
 * Complexity: O(n)
 */
ERStatus er_discharge(ERManager *mgr, int id, Patient *out);

/*
 * er_search
 * Purpose   : Find a patient by id across all queues. Print their details,
 *             their level, position within level, overall treatment position,
 *             and current wait time.
 * Returns   : ER_OK if found, ER_NOT_FOUND otherwise.
 * Complexity: O(n)
 */
ERStatus er_search(ERManager *mgr, int id);

/*
 * er_apply_aging
 * Purpose   : Scan levels 5 down to 3 and promote patients that have waited
 *             longer than AGING_THRESHOLD[level-1] at their current level.
 *             Promotion ceiling: patients are promoted at most to level 2.
 *             Promoted patients are inserted into the target queue in
 *             arrival_time order (cq_insert_in_order).
 *             If the target queue is full, the promotion is skipped and
 *             retried on the next call (patient is never lost).
 * Complexity: O(n^2) worst case (scan + insert per level)
 */
void er_apply_aging(ERManager *mgr);

/*
 * er_display_waiting
 * Purpose   : Apply aging then print all waiting patients in exact treatment
 *             order (level 1 first, FIFO within each level).
 * Complexity: O(n)
 */
void er_display_waiting(ERManager *mgr);

/*
 * er_statistics
 * Purpose   : Apply aging, then print aggregate statistics:
 *             total waiting, per-level counts with a bar chart, average and
 *             maximum wait time, total treated, average wait of treated.
 * Complexity: O(n)
 */
void er_statistics(ERManager *mgr);

/*
 * er_undo_last
 * Purpose   : Pop the most recently treated patient from the history stack
 *             and re-insert them into the appropriate queue in arrival_time
 *             order.  Clock does NOT advance (undo is not a treatment).
 * Returns   : ER_OK, ER_EMPTY (nothing to undo), ER_FULL (queue full).
 * Complexity: O(n)
 */
ERStatus er_undo_last(ERManager *mgr);

/*
 * er_show_history
 * Purpose   : Print all treated patients from the stack (most recent first).
 * Complexity: O(n)
 */
void er_show_history(ERManager *mgr);

/*
 * er_advance_time
 * Purpose   : Advance the simulated clock by `minutes` and trigger aging.
 * Returns   : ER_INVALID if minutes <= 0.
 * Complexity: O(n^2) (due to aging)
 */
ERStatus er_advance_time(ERManager *mgr, int minutes);

#endif /* ERMANAGER_H */
