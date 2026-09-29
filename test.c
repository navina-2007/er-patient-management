/*
 * test.c
 * ======
 * Automated unit tests for the ER Patient Management System.
 * 16 assert-based tests.  Compile separately:
 *   gcc -Wall -Wextra -Wpedantic -std=c11 -o test test.c patient.c cqueue.c stack.c ermanager.c
 * Run:
 *   ./test        (Linux/macOS)
 *   test.exe      (Windows)
 */

#include "patient.h"
#include "cqueue.h"
#include "stack.h"
#include "ermanager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* -----------------------------------------------------------------------
 * Test infrastructure
 * ----------------------------------------------------------------------- */
static int tests_run   = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("  [TEST %2d] %-60s ", ++tests_run, name); \
        fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL -- %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } while (0)

/* Helper: create a minimal valid patient */
static Patient make_patient(int id, int severity, int arrival)
{
    Patient p;
    patient_init(&p);
    p.id                = id;
    p.severity          = severity;
    p.original_severity = severity;
    p.age               = 30;
    p.arrival_time      = arrival;
    p.level_entry_time  = arrival;
    snprintf(p.name, MAX_NAME_LEN, "Patient_%d", id);
    return p;
}

/* -----------------------------------------------------------------------
 * Test 1: Empty system - treat and peek return ER_EMPTY
 * ----------------------------------------------------------------------- */
static void test01_empty_system(void)
{
    TEST("01: Empty system treat/peek => ER_EMPTY");
    ERManager mgr;
    er_init(&mgr);

    Patient out;
    ERStatus st_treat = er_treat_next(&mgr, &out);
    ERStatus st_peek  = er_peek_next(&mgr, &out);

    if (st_treat == ER_EMPTY && st_peek == ER_EMPTY) {
        PASS();
    } else {
        FAIL("treat or peek did not return ER_EMPTY on empty system");
    }
}

/* -----------------------------------------------------------------------
 * Test 2: Single admit then treat
 * ----------------------------------------------------------------------- */
static void test02_single_admit_treat(void)
{
    TEST("02: Single admit then treat");
    ERManager mgr;
    er_init(&mgr);

    Patient p = make_patient(1, 3, 0);
    ERStatus st = er_admit(&mgr, &p);
    if (st != ER_OK) { FAIL("admit failed"); return; }

    Patient out;
    st = er_treat_next(&mgr, &out);
    if (st != ER_OK)     { FAIL("treat failed"); return; }
    if (out.id != 1)     { FAIL("wrong patient treated"); return; }

    /* Queue should now be empty */
    Patient dummy;
    if (er_peek_next(&mgr, &dummy) != ER_EMPTY) {
        FAIL("queue should be empty after treating single patient");
        return;
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 3: Same-severity FIFO order preserved
 * ----------------------------------------------------------------------- */
static void test03_same_severity_fifo(void)
{
    TEST("03: Same-severity FIFO order");
    ERManager mgr;
    er_init(&mgr);

    /* Admit 3 patients at level 3, in order */
    for (int i = 1; i <= 3; i++) {
        Patient p = make_patient(i, 3, i * 10); /* arrival 10, 20, 30 */
        er_admit(&mgr, &p);
    }

    Patient out;
    for (int expected = 1; expected <= 3; expected++) {
        ERStatus st = er_treat_next(&mgr, &out);
        if (st != ER_OK || out.id != expected) {
            FAIL("FIFO order violated");
            return;
        }
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 4: Different severities: level 1 before level 5
 * ----------------------------------------------------------------------- */
static void test04_priority_order(void)
{
    TEST("04: Level 1 treated before Level 5");
    ERManager mgr;
    er_init(&mgr);

    /* Admit level 5 first (arrival earlier), then level 1 */
    Patient p5 = make_patient(5, 5, 0);
    Patient p1 = make_patient(1, 1, 10);

    /* Bypass er_admit clock side-effects by directly enqueueing */
    cq_enqueue(&mgr.queues[4], &p5);
    cq_enqueue(&mgr.queues[0], &p1);

    Patient out;
    er_treat_next(&mgr, &out);
    if (out.id != 1) {
        FAIL("Level 1 patient should be treated first");
        return;
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 5: Queue full - returns ER_FULL and loses nothing
 * ----------------------------------------------------------------------- */
static void test05_queue_full(void)
{
    TEST("05: Queue full => ER_FULL, no data loss");
    CircularQueue q;
    cq_init(&q);

    /* Fill to capacity */
    for (int i = 0; i < MAX_PER_LEVEL; i++) {
        Patient p = make_patient(i + 1, 1, i);
        ERStatus st = cq_enqueue(&q, &p);
        if (st != ER_OK) { FAIL("enqueue failed before full"); return; }
    }

    /* One more should fail */
    Patient extra = make_patient(MAX_PER_LEVEL + 1, 1, 9999);
    ERStatus st = cq_enqueue(&q, &extra);
    if (st != ER_FULL) { FAIL("expected ER_FULL"); return; }
    if (q.count != MAX_PER_LEVEL) { FAIL("count changed on failed enqueue"); return; }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 6: Circular wrap-around
 * ----------------------------------------------------------------------- */
static void test06_circular_wraparound(void)
{
    TEST("06: Circular wrap-around correctness");
    CircularQueue q;
    cq_init(&q);

    /* Fill with IDs 1..MAX_PER_LEVEL */
    for (int i = 1; i <= MAX_PER_LEVEL; i++) {
        Patient p = make_patient(i, 3, i);
        cq_enqueue(&q, &p);
    }

    /* Dequeue first 10 */
    Patient out;
    for (int i = 0; i < 10; i++) {
        cq_dequeue(&q, &out);
    }
    /* q.front is now at index 10, count = MAX-10 */

    /* Enqueue 5 more (IDs 101-105) - these wrap around */
    for (int i = 101; i <= 105; i++) {
        Patient p = make_patient(i, 3, i);
        ERStatus st = cq_enqueue(&q, &p);
        if (st != ER_OK) { FAIL("enqueue after dequeue failed"); return; }
    }

    /* Verify count */
    if (q.count != MAX_PER_LEVEL - 5) { FAIL("wrong count after wrap"); return; }

    /* Verify first element is ID 11 */
    Patient *front = cq_get_at(&q, 0);
    if (front == NULL || front->id != 11) { FAIL("front should be ID 11"); return; }

    /* Verify last 5 are IDs 101-105 */
    for (int i = 0; i < 5; i++) {
        Patient *tail = cq_get_at(&q, q.count - 5 + i);
        if (tail == NULL || tail->id != 101 + i) {
            FAIL("wrapped elements out of order");
            return;
        }
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 7: Duplicate ID rejected (including in history)
 * ----------------------------------------------------------------------- */
static void test07_duplicate_id(void)
{
    TEST("07: Duplicate ID rejected (queue + history)");
    ERManager mgr;
    er_init(&mgr);

    Patient p = make_patient(42, 3, 0);
    er_admit(&mgr, &p);

    /* Duplicate in queue */
    Patient dup = make_patient(42, 3, 5);
    ERStatus st = er_admit(&mgr, &dup);
    if (st != ER_DUPLICATE) { FAIL("dup in queue not caught"); return; }

    /* Treat the first, then try duplicate in history */
    Patient out;
    er_treat_next(&mgr, &out);

    Patient dup2 = make_patient(42, 3, 10);
    st = er_admit(&mgr, &dup2);
    if (st != ER_DUPLICATE) { FAIL("dup in history not caught"); return; }

    PASS();
}

/* -----------------------------------------------------------------------
 * Test 8: Aging - level 5 promoted to level 4 after threshold
 * ----------------------------------------------------------------------- */
static void test08_aging_single(void)
{
    TEST("08: Aging: level 5 => level 4 after threshold");
    ERManager mgr;
    er_init(&mgr);

    /* Place a level-5 patient with level_entry_time = 0 */
    Patient p = make_patient(1, 5, 0);
    cq_enqueue(&mgr.queues[4], &p);

    /* Advance clock past level-5 threshold (120 min) */
    mgr.clock = AGING_THRESHOLD[4] + 1; /* 121 */
    er_apply_aging(&mgr);

    if (mgr.queues[4].count != 0) { FAIL("patient still in level 5"); return; }
    if (mgr.queues[3].count != 1) { FAIL("patient not in level 4"); return; }

    Patient *promoted = cq_get_at(&mgr.queues[3], 0);
    if (promoted == NULL || promoted->severity != 4) {
        FAIL("promoted patient has wrong severity");
        return;
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 9: Aging cascade: 5 -> 4 -> 3 -> 2, never into 1
 * ----------------------------------------------------------------------- */
static void test09_aging_cascade(void)
{
    TEST("09: Aging cascade: 5->4->3->2, never into 1");
    ERManager mgr;
    er_init(&mgr);

    /* Place a level-5 patient, entry at time 0 */
    Patient p = make_patient(1, 5, 0);
    p.level_entry_time = 0;
    cq_enqueue(&mgr.queues[4], &p);

    /* Jump past all thresholds: 120 (5->4) + 90 (4->3) + 60 (3->2) = 270 */
    mgr.clock = 300;

    /*
     * One call to apply_aging should cascade 5->4->3->2 because:
     * - Level 5 scan: wait=300 >= 120, promote to 4. level_entry_time = 300.
     * - Level 4 scan: wait=300-300=0 < 90, NOT promoted yet in this call.
     * So a single call promotes one level at a time.
     * We need multiple calls to fully cascade.
     */
    er_apply_aging(&mgr); /* 5 -> 4, level_entry_time = 300 */

    /* Simulate time passing again */
    mgr.clock = 300 + 91; /* 391; wait at level 4 = 91 >= 90 */
    er_apply_aging(&mgr); /* 4 -> 3, level_entry_time = 391 */

    mgr.clock = 391 + 61; /* 452; wait at level 3 = 61 >= 60 */
    er_apply_aging(&mgr); /* 3 -> 2 */

    /* Patient should now be at level 2 */
    if (mgr.queues[1].count != 1) { FAIL("patient not in level 2"); return; }
    if (mgr.queues[0].count != 0) { FAIL("patient leaked into level 1"); return; }

    Patient *q2 = cq_get_at(&mgr.queues[1], 0);
    if (q2 == NULL || q2->severity != 2) { FAIL("severity not 2"); return; }
    if (q2->original_severity != 5) { FAIL("original_severity changed"); return; }

    /* Another advance: patient is at level 2, threshold = 0 => never promoted */
    mgr.clock = 10000;
    er_apply_aging(&mgr);
    if (mgr.queues[0].count != 0) { FAIL("patient entered level 1 (promotion ceiling violated)"); return; }

    PASS();
}

/* -----------------------------------------------------------------------
 * Test 10: Starvation prevention - level 5 patient eventually treated
 * ----------------------------------------------------------------------- */
static void test10_starvation_prevention(void)
{
    TEST("10: Starvation: level 5 patient eventually treated");
    ERManager mgr;
    er_init(&mgr);

    /* Admit one level-5 patient at t=0 */
    Patient p5 = make_patient(999, 5, 0);
    p5.level_entry_time = 0;
    cq_enqueue(&mgr.queues[4], &p5);

    /* Simulate continuous stream of level 1-2 patients */
    mgr.clock = 1;
    for (int i = 1; i <= 10; i++) {
        Patient p = make_patient(i, (i % 2) + 1, mgr.clock);  /* alternating 1 and 2 */
        p.level_entry_time = mgr.clock;
        cq_enqueue(&mgr.queues[p.severity - 1], &p);
        mgr.clock++;
    }

    /* Advance time past all thresholds */
    mgr.clock = 500;
    er_apply_aging(&mgr); /* cascade up to level 2 */
    /* apply again to cascade further if needed */
    mgr.clock = 700;
    er_apply_aging(&mgr);

    /* Now treat until we find patient 999 */
    bool found = false;
    for (int i = 0; i < 20; i++) {
        Patient out;
        if (er_treat_next(&mgr, &out) != ER_OK) break;
        if (out.id == 999) { found = true; break; }
    }

    if (found) {
        PASS();
    } else {
        FAIL("level-5 patient (ID 999) never treated");
    }
}

/* -----------------------------------------------------------------------
 * Test 11: Promoted patient inserted in arrival_time order
 * ----------------------------------------------------------------------- */
static void test11_promoted_arrival_order(void)
{
    TEST("11: Promoted patient inserted in arrival_time order");
    ERManager mgr;
    er_init(&mgr);

    /*
     * Clock will advance to AGING_THRESHOLD[4]+1 = 121.
     * Level-4 threshold = 90.  We set level_entry_time of the level-4 patients
     * to 50 so their wait = 121-50 = 71 < 90 (they do NOT age).
     * The level-5 patient has level_entry_time = 0; wait = 121 >= 120 (ages).
     */
    Patient early = make_patient(1, 4, 5);   /* arrival 5  */
    Patient late  = make_patient(2, 4, 15);  /* arrival 15 */
    early.level_entry_time = 50;  /* recent enough: 121-50=71 < 90 */
    late.level_entry_time  = 50;
    cq_enqueue(&mgr.queues[3], &early);
    cq_enqueue(&mgr.queues[3], &late);

    /* Level-5 patient arrives at t=10; level_entry_time=0 to trigger aging */
    Patient p5 = make_patient(3, 5, 10);
    p5.level_entry_time = 0;
    cq_enqueue(&mgr.queues[4], &p5);

    /* Advance clock to trigger level-5 aging */
    mgr.clock = AGING_THRESHOLD[4] + 1; /* 121 */
    er_apply_aging(&mgr);

    /* Level 4 should now contain: arrival 5, 10, 15 in that order */
    if (mgr.queues[3].count != 3) { FAIL("wrong count in level 4"); return; }

    Patient *a = cq_get_at(&mgr.queues[3], 0); /* arrival 5  */
    Patient *b = cq_get_at(&mgr.queues[3], 1); /* arrival 10 */
    Patient *c = cq_get_at(&mgr.queues[3], 2); /* arrival 15 */

    if (a == NULL || b == NULL || c == NULL) { FAIL("null pointer"); return; }
    if (a->arrival_time != 5 || b->arrival_time != 10 || c->arrival_time != 15) {
        FAIL("arrival_time order not preserved after promotion");
        return;
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 12: Re-triage moves patient to correct queue and position
 * ----------------------------------------------------------------------- */
static void test12_retriage(void)
{
    TEST("12: Re-triage moves patient to correct queue/position");
    ERManager mgr;
    er_init(&mgr);

    /* Admit two patients at level 3 */
    Patient p1 = make_patient(1, 3, 10);
    Patient p2 = make_patient(2, 3, 20);
    p1.level_entry_time = 10; p2.level_entry_time = 20;
    cq_enqueue(&mgr.queues[2], &p1);
    cq_enqueue(&mgr.queues[2], &p2);

    /* Admit one at level 1 */
    Patient p3 = make_patient(3, 1, 5);
    p3.level_entry_time = 5;
    cq_enqueue(&mgr.queues[0], &p3);

    mgr.clock = 50;

    /* Re-triage patient 2 from level 3 to level 1 */
    ERStatus st = er_retriage(&mgr, 2, 1);
    if (st != ER_OK) { FAIL("re-triage failed"); return; }

    /* Level 3 should have only patient 1 */
    if (mgr.queues[2].count != 1) { FAIL("level 3 count wrong"); return; }

    /* Level 1 should have 2 patients: ID 3 (arrival 5), ID 2 (arrival 20) */
    if (mgr.queues[0].count != 2) { FAIL("level 1 count wrong"); return; }

    Patient *first = cq_get_at(&mgr.queues[0], 0);
    Patient *second = cq_get_at(&mgr.queues[0], 1);
    if (first == NULL || first->id != 3)  { FAIL("level1[0] should be ID 3 (arrival 5)"); return; }
    if (second == NULL || second->id != 2) { FAIL("level1[1] should be ID 2 (arrival 20)"); return; }

    PASS();
}

/* -----------------------------------------------------------------------
 * Test 13: Remove front, rear and middle in wrapped circular queue
 * ----------------------------------------------------------------------- */
static void test13_remove_positions(void)
{
    TEST("13: Remove front, rear, middle in wrapped circular queue");
    CircularQueue q;
    cq_init(&q);

    /* Enqueue 8 patients (IDs 1-8) */
    for (int i = 1; i <= 8; i++) {
        Patient p = make_patient(i, 3, i);
        cq_enqueue(&q, &p);
    }
    /* Dequeue 3 from front so front wraps if MAX is small enough, but here it
       moves front to index 3 */
    Patient out;
    for (int i = 0; i < 3; i++) cq_dequeue(&q, &out);
    /* Now IDs 4-8 are in the queue */

    /* Enqueue 2 more (IDs 9, 10) - these wrap around the buffer */
    for (int i = 9; i <= 10; i++) {
        Patient p = make_patient(i, 3, i);
        cq_enqueue(&q, &p);
    }
    /* Queue: 4, 5, 6, 7, 8, 9, 10  (count=7) */

    /* Remove from front (ID 4) */
    ERStatus st = cq_remove_by_id(&q, 4, NULL);
    if (st != ER_OK) { FAIL("remove front failed"); return; }
    Patient *f = cq_get_at(&q, 0);
    if (f == NULL || f->id != 5) { FAIL("after remove front, new front should be ID 5"); return; }

    /* Remove from rear (ID 10) */
    st = cq_remove_by_id(&q, 10, NULL);
    if (st != ER_OK) { FAIL("remove rear failed"); return; }
    Patient *last = cq_get_at(&q, q.count - 1);
    if (last == NULL || last->id != 9) { FAIL("after remove rear, new rear should be ID 9"); return; }

    /* Remove from middle (ID 7; queue is 5,6,7,8,9) */
    st = cq_remove_by_id(&q, 7, NULL);
    if (st != ER_OK) { FAIL("remove middle failed"); return; }
    if (q.count != 4) { FAIL("wrong count after middle remove"); return; }

    /* Verify remaining order: 5, 6, 8, 9 */
    int expected[] = {5, 6, 8, 9};
    for (int i = 0; i < 4; i++) {
        Patient *pp = cq_get_at(&q, i);
        if (pp == NULL || pp->id != expected[i]) {
            FAIL("order wrong after middle remove");
            return;
        }
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 14: Undo restores patient to correct position
 * ----------------------------------------------------------------------- */
static void test14_undo(void)
{
    TEST("14: Undo restores patient; empty stack handled");
    ERManager mgr;
    er_init(&mgr);

    /* Undo on empty stack */
    ERStatus st = er_undo_last(&mgr);
    if (st != ER_EMPTY) { FAIL("empty stack undo should return ER_EMPTY"); return; }

    /* Admit two patients at level 3 */
    Patient p1 = make_patient(1, 3, 5);
    Patient p2 = make_patient(2, 3, 15);
    p1.level_entry_time = 5; p2.level_entry_time = 15;
    cq_enqueue(&mgr.queues[2], &p1);
    cq_enqueue(&mgr.queues[2], &p2);

    mgr.clock = 20;

    /* Treat patient 1 */
    Patient out;
    er_treat_next(&mgr, &out);
    if (out.id != 1) { FAIL("wrong patient treated"); return; }

    /* Undo: patient 1 should be back in level 3, BEFORE patient 2 */
    st = er_undo_last(&mgr);
    if (st != ER_OK) { FAIL("undo failed"); return; }
    if (mgr.queues[2].count != 2) { FAIL("queue count wrong after undo"); return; }

    /* Patient 1 (arrival 5) should be before patient 2 (arrival 15) */
    Patient *first = cq_get_at(&mgr.queues[2], 0);
    if (first == NULL || first->id != 1) {
        FAIL("undo: patient 1 should be at front (earlier arrival)");
        return;
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 15: Statistics correctness
 * ----------------------------------------------------------------------- */
static void test15_statistics(void)
{
    TEST("15: Statistics: counts and wait correctness");
    ERManager mgr;
    er_init(&mgr);

    /* Admit 3 patients: levels 1, 3, 5 */
    Patient p1 = make_patient(1, 1, 0);
    Patient p3 = make_patient(2, 3, 0);
    Patient p5 = make_patient(3, 5, 0);
    cq_enqueue(&mgr.queues[0], &p1);
    cq_enqueue(&mgr.queues[2], &p3);
    cq_enqueue(&mgr.queues[4], &p5);

    /* Treat two */
    Patient out;
    mgr.clock = 10;
    er_treat_next(&mgr, &out); /* treats level 1 patient */
    er_treat_next(&mgr, &out); /* treats level 3 patient */

    /* Verify totals */
    if (mgr.total_treated != 2) { FAIL("total_treated wrong"); return; }
    /* Level 5 patient still waiting */
    if (mgr.queues[4].count != 1) { FAIL("level 5 queue count wrong"); return; }

    PASS();
}

/* -----------------------------------------------------------------------
 * Test 16: Promotion skipped when target queue is full
 * ----------------------------------------------------------------------- */
static void test16_promotion_full_target(void)
{
    TEST("16: Promotion skipped safely when target queue is full");
    ERManager mgr;
    er_init(&mgr);

    /*
     * Clock will be set to AGING_THRESHOLD[4]+1 = 121.
     * Level-4 threshold is 90.
     * Set level_entry_time = 40 so wait = 121-40 = 81 < 90 (they don't age).
     * The level-5 patient has level_entry_time=0, wait=121 >= 120 (wants to age),
     * but the level-4 queue is full so promotion is skipped.
     */
    for (int i = 1; i <= MAX_PER_LEVEL; i++) {
        Patient p = make_patient(i, 4, i);
        p.level_entry_time = 40; /* 121 - 40 = 81 < 90: won't age */
        cq_enqueue(&mgr.queues[3], &p);
    }

    /* Add a level-5 patient that wants to be promoted to level 4 */
    Patient p5 = make_patient(MAX_PER_LEVEL + 1, 5, 0);
    p5.level_entry_time = 0;
    cq_enqueue(&mgr.queues[4], &p5);

    /* Advance clock past level-5 threshold */
    mgr.clock = AGING_THRESHOLD[4] + 1; /* 121 */
    er_apply_aging(&mgr);

    /* Patient should still be in level 5 (target full, promotion skipped) */
    if (mgr.queues[4].count != 1) {
        FAIL("patient lost or moved when target was full");
        return;
    }
    if (mgr.queues[3].count != MAX_PER_LEVEL) {
        FAIL("level 4 count changed unexpectedly");
        return;
    }
    PASS();
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    printf("\n  ============================================\n");
    printf("   ER Patient Management - Automated Tests\n");
    printf("  ============================================\n\n");

    test01_empty_system();
    test02_single_admit_treat();
    test03_same_severity_fifo();
    test04_priority_order();
    test05_queue_full();
    test06_circular_wraparound();
    test07_duplicate_id();
    test08_aging_single();
    test09_aging_cascade();
    test10_starvation_prevention();
    test11_promoted_arrival_order();
    test12_retriage();
    test13_remove_positions();
    test14_undo();
    test15_statistics();
    test16_promotion_full_target();

    printf("\n  ============================================\n");
    printf("  Results: %d passed, %d failed out of %d tests.\n",
           tests_passed, tests_failed, tests_run);
    printf("  ============================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
