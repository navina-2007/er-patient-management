/*
 * cqueue.h
 * ========
 * Circular-array queue of Patient structs, one per severity level.
 *
 * Storage layout (example with MAX_PER_LEVEL = 5):
 *
 *   data: [ _ | A | B | C | _ ]
 *               ^           ^
 *             front        rear (next write position)
 *   front = 1, rear = 4, count = 3
 *
 * Wrap-around formula:
 *   enqueue: data[rear] = item; rear = (rear + 1) % MAX_PER_LEVEL;
 *   dequeue: item = data[front]; front = (front + 1) % MAX_PER_LEVEL;
 *
 * count is the authoritative size; front==rear is ambiguous (could be full
 * or empty), so we always test count instead.
 */

#ifndef CQUEUE_H
#define CQUEUE_H

#include "patient.h"
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * CircularQueue struct
 * ----------------------------------------------------------------------- */
typedef struct {
    Patient data[MAX_PER_LEVEL];
    int     front;  /* index of the first (oldest) element                */
    int     rear;   /* index where the NEXT element will be written       */
    int     count;  /* number of elements currently stored                */
} CircularQueue;

/* -----------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------- */

/*
 * cq_init
 * Purpose   : Initialise an empty CircularQueue.
 * Parameters: q - non-NULL pointer to the queue.
 * Returns   : nothing
 * Complexity: O(1)
 */
void cq_init(CircularQueue *q);

/*
 * cq_is_empty
 * Returns   : true iff count == 0.
 * Complexity: O(1)
 */
bool cq_is_empty(const CircularQueue *q);

/*
 * cq_is_full
 * Returns   : true iff count == MAX_PER_LEVEL.
 * Complexity: O(1)
 */
bool cq_is_full(const CircularQueue *q);

/*
 * cq_enqueue
 * Purpose   : Append p to the rear of the queue (standard FIFO insert).
 * Returns   : ER_OK on success, ER_FULL if the queue has no room.
 * Complexity: O(1)
 */
ERStatus cq_enqueue(CircularQueue *q, const Patient *p);

/*
 * cq_dequeue
 * Purpose   : Remove and return the front element.
 * Parameters: q   - the queue.
 *             out - pointer where the removed Patient is written.
 * Returns   : ER_OK on success, ER_EMPTY if nothing to remove.
 * Complexity: O(1)
 */
ERStatus cq_dequeue(CircularQueue *q, Patient *out);

/*
 * cq_peek_front
 * Purpose   : Read the front element without removing it.
 * Parameters: q   - the queue.
 *             out - pointer where the peeked Patient is written.
 * Returns   : ER_OK on success, ER_EMPTY if the queue is empty.
 * Complexity: O(1)
 */
ERStatus cq_peek_front(const CircularQueue *q, Patient *out);

/*
 * cq_get_at
 * Purpose   : Return a pointer to the patient at logical index i
 *             (0 = front, count-1 = rear).  No bounds checking beyond
 *             returning NULL when i is out of range.
 * Returns   : pointer into q->data on success, NULL if out of range.
 * Complexity: O(1)
 */
Patient *cq_get_at(CircularQueue *q, int i);

/*
 * cq_insert_in_order
 * Purpose   : Insert p into the queue maintaining ascending arrival_time
 *             order.  If arrival times are equal the new patient is placed
 *             AFTER existing patients with the same time (stable insert).
 *             Existing elements are shifted (within the circular array) to
 *             make room, so the insert position is found by linear scan and
 *             elements moved one step toward the rear.
 * Returns   : ER_OK on success, ER_FULL if there is no room.
 * Complexity: O(n) where n = q->count
 */
ERStatus cq_insert_in_order(CircularQueue *q, const Patient *p);

/*
 * cq_remove_by_id
 * Purpose   : Find the patient with the given id, remove it, and shift the
 *             remaining elements to close the gap (toward the rear, so
 *             front stays stable).
 * Parameters: q   - the queue.
 *             id  - the patient ID to find and remove.
 *             out - if non-NULL, the removed Patient is written here.
 * Returns   : ER_OK on success, ER_NOT_FOUND if no patient has that id.
 * Complexity: O(n)
 */
ERStatus cq_remove_by_id(CircularQueue *q, int id, Patient *out);

/*
 * cq_find_by_id
 * Purpose   : Find a patient by ID and return its logical index.
 * Parameters: q        - the queue.
 *             id       - the patient ID to find.
 *             out_idx  - if non-NULL, receives the logical index (0-based).
 * Returns   : ER_OK if found, ER_NOT_FOUND otherwise.
 * Complexity: O(n)
 */
ERStatus cq_find_by_id(const CircularQueue *q, int id, int *out_idx);

#endif /* CQUEUE_H */
