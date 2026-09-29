/*
 * cqueue.c
 * ========
 * Implementation of the circular-array queue for Patient structs.
 * All indexing is done with modulo arithmetic:
 *   physical_index = (front + logical_index) % MAX_PER_LEVEL
 */

#include "cqueue.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * cq_init
 * Purpose   : Set front=0, rear=0, count=0 (empty queue).
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
void cq_init(CircularQueue *q)
{
    q->front = 0;
    q->rear  = 0;
    q->count = 0;
}

/* -----------------------------------------------------------------------
 * cq_is_empty
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
bool cq_is_empty(const CircularQueue *q)
{
    return (q->count == 0);
}

/* -----------------------------------------------------------------------
 * cq_is_full
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
bool cq_is_full(const CircularQueue *q)
{
    return (q->count == MAX_PER_LEVEL);
}

/* -----------------------------------------------------------------------
 * cq_enqueue
 * Purpose   : Append p at the rear of the queue (O(1) amortised FIFO push).
 * Algorithm :
 *   1. Check full.
 *   2. Write to data[rear].
 *   3. Advance rear = (rear+1) % MAX_PER_LEVEL.
 *   4. Increment count.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
ERStatus cq_enqueue(CircularQueue *q, const Patient *p)
{
    if (cq_is_full(q)) {
        return ER_FULL;
    }
    q->data[q->rear] = *p;
    q->rear = (q->rear + 1) % MAX_PER_LEVEL;
    q->count++;
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * cq_dequeue
 * Purpose   : Remove and return the front element.
 * Algorithm :
 *   1. Check empty.
 *   2. Copy data[front] to *out.
 *   3. Advance front = (front+1) % MAX_PER_LEVEL.
 *   4. Decrement count.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
ERStatus cq_dequeue(CircularQueue *q, Patient *out)
{
    if (cq_is_empty(q)) {
        return ER_EMPTY;
    }
    *out = q->data[q->front];
    q->front = (q->front + 1) % MAX_PER_LEVEL;
    q->count--;
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * cq_peek_front
 * Purpose   : Return a copy of the front element without removing it.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
ERStatus cq_peek_front(const CircularQueue *q, Patient *out)
{
    if (cq_is_empty(q)) {
        return ER_EMPTY;
    }
    *out = q->data[q->front];
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * cq_get_at
 * Purpose   : Return pointer to element at logical index i (0 = front).
 *             Physical index = (front + i) % MAX_PER_LEVEL.
 * Returns   : NULL if i < 0 or i >= count.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
Patient *cq_get_at(CircularQueue *q, int i)
{
    if (i < 0 || i >= q->count) {
        return NULL;
    }
    int phys = (q->front + i) % MAX_PER_LEVEL;
    return &q->data[phys];
}

/* -----------------------------------------------------------------------
 * cq_insert_in_order
 * Purpose   : Insert p maintaining ascending arrival_time order.
 *             Equal arrival times: new patient goes AFTER existing ones
 *             (stable / FIFO fairness within the same arrival minute).
 *
 * Algorithm (shifting within the circular array):
 *   1. Return ER_FULL if no room.
 *   2. Linear scan from the rear backward to find the insertion point:
 *      the first logical position where data[pos].arrival_time > p->arrival_time.
 *      Let ins = that logical index (0-based from front).
 *      If no such position found, ins = count (append at rear).
 *   3. Shift elements at logical positions [ins .. count-1] one step toward
 *      the rear (rear advances, then elements are copied outward).
 *   4. Write p to logical index ins.
 *   5. Advance rear, increment count.
 *
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus cq_insert_in_order(CircularQueue *q, const Patient *p)
{
    if (cq_is_full(q)) {
        return ER_FULL;
    }

    /* Find insertion point: first index where existing arrival_time > p's */
    int ins = q->count; /* default: append at end */
    for (int i = 0; i < q->count; i++) {
        int phys = (q->front + i) % MAX_PER_LEVEL;
        if (q->data[phys].arrival_time > p->arrival_time) {
            ins = i;
            break;
        }
    }

    /*
     * Shift elements from logical [ins .. count-1] one position toward rear.
     * We extend the rear first (conceptually), then copy from the old rear
     * inward toward ins.
     */
    for (int i = q->count; i > ins; i--) {
        int dst = (q->front + i)     % MAX_PER_LEVEL;
        int src = (q->front + i - 1) % MAX_PER_LEVEL;
        q->data[dst] = q->data[src];
    }

    /* Write the new patient at the insertion point */
    int ins_phys = (q->front + ins) % MAX_PER_LEVEL;
    q->data[ins_phys] = *p;

    q->rear = (q->rear + 1) % MAX_PER_LEVEL;
    q->count++;
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * cq_remove_by_id
 * Purpose   : Find patient with `id`, remove and (optionally) return it.
 *             Elements after the gap are shifted toward the rear so that
 *             front stays stable and order is preserved.
 *
 * Algorithm:
 *   1. Linear scan to find logical index `del` of the patient with id.
 *   2. Shift elements at logical [del+1 .. count-1] one step toward front
 *      (overwrite del, then del+1, ...).
 *   3. Retract rear by one, decrement count.
 *
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus cq_remove_by_id(CircularQueue *q, int id, Patient *out)
{
    int del = -1;
    for (int i = 0; i < q->count; i++) {
        int phys = (q->front + i) % MAX_PER_LEVEL;
        if (q->data[phys].id == id) {
            del = i;
            if (out != NULL) {
                *out = q->data[phys];
            }
            break;
        }
    }
    if (del == -1) {
        return ER_NOT_FOUND;
    }

    /* Shift elements [del+1 .. count-1] one step toward front */
    for (int i = del; i < q->count - 1; i++) {
        int dst = (q->front + i)     % MAX_PER_LEVEL;
        int src = (q->front + i + 1) % MAX_PER_LEVEL;
        q->data[dst] = q->data[src];
    }

    /* Retract rear */
    q->rear = (q->rear - 1 + MAX_PER_LEVEL) % MAX_PER_LEVEL;
    q->count--;
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * cq_find_by_id
 * Purpose   : Locate a patient by id and return their logical index.
 * Returns   : ER_OK with *out_idx set, or ER_NOT_FOUND.
 * Complexity: O(n)
 * ----------------------------------------------------------------------- */
ERStatus cq_find_by_id(const CircularQueue *q, int id, int *out_idx)
{
    for (int i = 0; i < q->count; i++) {
        int phys = (q->front + i) % MAX_PER_LEVEL;
        if (q->data[phys].id == id) {
            if (out_idx != NULL) {
                *out_idx = i;
            }
            return ER_OK;
        }
    }
    return ER_NOT_FOUND;
}
