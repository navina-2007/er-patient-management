# ER Patient Management System

## Problem Explanation (Plain English)

A hospital Emergency Room receives patients continuously. Some arrive in
ambulances with life-threatening injuries; others walk in with minor ailments.
Treating patients strictly in order of arrival would be dangerous — a critical
patient who arrives five minutes after a sprained ankle cannot wait behind
twenty mild cases.

This system manages a waiting room that ensures **the most urgent patients are
always seen first**, while still being **fair among patients of similar
urgency** (those who arrived earlier within the same urgency category are
treated first). It also prevents **starvation**: a non-urgent patient cannot
wait forever while a continuous stream of more-urgent arrivals keeps arriving
— the system gradually promotes long-waiting patients to higher urgency.

---

## Design Overview

```
Triage Level   Queue (Circular Array, FIFO)           Aging Rule
─────────────  ────────────────────────────────────   ─────────────────
 1 Critical    [P7]──[P2]──[P11]                      NEVER promoted further
               ^front                  ^rear           (promotion CEILING)
 2 Emergent    [P4]──[P9]                              NEVER promoted further
                                                       (promotion ceiling)
 3 Urgent      [P5]──[P1]──[P8]       ─────>  after 60 min → Level 2
 4 Semi-urgent [P3]──[P6]             ─────>  after 90 min → Level 3
 5 Non-urgent  [P10]                  ─────>  after 120 min → Level 4

                     Treatment order: highest-priority first (1→5),
                     FIFO within each level.

History Stack (array, LIFO):  [P2|P7|P4|P9...]  ← used for Undo
```

**PROMOTION CEILING**: Aging can promote a patient **at most to Level 2**.
Level 1 is strictly reserved for patients triaged as *Critical* by a nurse or
doctor. No amount of waiting can move a non-critical patient into Level 1.

---

## Data Structures — Chosen and Why

### 1. Circular Array Queue (`CircularQueue`)

```
 Index: [ 0 ][ 1 ][ 2 ][ 3 ][ 4 ]   (MAX_PER_LEVEL = 5 for illustration)
              [ A ][ B ][ C ]
                ^front         ^rear  (rear = next write slot)
         count = 3
```

- **Why circular?** Avoids shifting on dequeue. After dequeuing A, `front`
  advances to index 2; no data movement needed.
- **Wrap-around**: `rear = (rear + 1) % MAX_PER_LEVEL`
- **`count`** is the authoritative size. `front == rear` is ambiguous (full or
  empty), so we never use it alone.
- **No linked lists**: The course requires arrays. Linked lists would require
  dynamic memory (`malloc`/`free`), which adds complexity and memory-leak risk.

### 2. Array Stack (`Stack`)

A simple `data[MAX_HISTORY]` with a `top` index. `push` increments `top`;
`pop` decrements it. O(1) for both. Used for the undo/history feature.

---

## Priority Rule + FIFO Fairness

**Rule**: Always dequeue from the lowest-numbered non-empty queue.

**FIFO within a level**: Patients in the same queue are strictly ordered by
`arrival_time`. New patients are appended to the rear with `cq_enqueue` (O(1)).
If a patient is inserted after promotion or re-triage, `cq_insert_in_order`
(O(n)) places them at the correct arrival-time position.

### Worked Numeric Example

| Event | Clock | Action | Level-1 Queue | Level-3 Queue | Level-5 Queue |
|-------|-------|--------|--------------|--------------|--------------|
| 0 | 0 | Admit Alice (Lv5, arr=0) | — | — | Alice |
| 1 | 1 | Admit Bob (Lv3, arr=1) | — | Bob | Alice |
| 2 | 2 | Admit Carol (Lv1, arr=2) | Carol | Bob | Alice |
| 3 | 3 | Treat next | **Carol** treated (Lv1) | Bob | Alice |
| 4 | 4 | Treat next | — | **Bob** treated (Lv3) | Alice |
| 5 | 125 | Advance 121 min, Aging | — | Alice↑ promoted to Lv4 | — |
| 6 | 125 | Treat next | — | — | *Lv4: Alice* → treated |

Note: at step 5, Alice has waited 121 minutes at Level 5 (threshold = 120),
so she is promoted to Level 4 with her original `arrival_time = 0` preserved.

---

## Anti-Starvation (Aging) — How It Works

Threshold array (`AGING_THRESHOLD[i]` where i = level-1):

| Level | Index | Threshold | Meaning |
|-------|-------|-----------|---------|
| 1 | 0 | 0 | Never promoted (ceiling) |
| 2 | 1 | 0 | Never promoted (ceiling) |
| 3 | 2 | 60 min | Promoted to Level 2 after 60 min |
| 4 | 3 | 90 min | Promoted to Level 3 after 90 min |
| 5 | 4 | 120 min | Promoted to Level 4 after 120 min |

`apply_aging()` is called **before every treat, peek, display, and statistics**
operation.  It scans levels 5 → 3 (outer loop i = 4, 3, 2) so that a patient
promoted within one call can be checked again at the new level in the same
invocation (cascade).

---

## Complexity Table

| Operation | Time | Space |
|-----------|------|-------|
| `cq_enqueue` | O(1) | O(1) |
| `cq_dequeue` | O(1) | O(1) |
| `cq_peek_front` | O(1) | O(1) |
| `cq_get_at` | O(1) | O(1) |
| `cq_insert_in_order` | O(n) | O(1) |
| `cq_remove_by_id` | O(n) | O(1) |
| `cq_find_by_id` | O(n) | O(1) |
| `stack_push/pop/peek` | O(1) | O(1) |
| `er_admit` | O(n) (dup check) | O(1) |
| `er_treat_next` | O(n) (aging) | O(1) |
| `er_peek_next` | O(n) (aging) | O(1) |
| `er_retriage` | O(n) | O(1) |
| `er_discharge` | O(n) | O(1) |
| `er_search` | O(n) | O(1) |
| `er_apply_aging` | O(n²) worst | O(1) |
| `er_display_waiting` | O(n) | O(1) |
| `er_statistics` | O(n) | O(1) |
| `er_undo_last` | O(n) | O(1) |
| `er_show_history` | O(n) | O(1) |

**Total space**: O(N) where N = MAX_PER_LEVEL × 5 + MAX_HISTORY = 700 Patients
on the stack, each ~80 bytes → ≈56 KB. No heap allocation.

---

## Fixed-Capacity Limitation and Handling

Each queue holds at most `MAX_PER_LEVEL = 100` patients. The history stack
holds at most `MAX_HISTORY = 200` entries. When a queue is full:

- `er_admit` returns `ER_FULL` with a user-friendly message.
- `er_retriage` leaves the patient untouched in the original queue.
- Aging skips the promotion and retries on the next `apply_aging` call.

**No patient is ever silently lost.**

---

## Alternatives Considered and Why Rejected

| Alternative | Problem |
|-------------|---------|
| Single sorted array | O(n) insertion to maintain order — similar cost but loses the per-level separation needed for aging and priority display |
| Single unsorted array, scan for minimum | O(n) every dequeue — correct but no level structure, hard to implement aging fairly |
| Plain FIFO (single queue) | Completely ignores urgency — a critical patient waits behind 100 non-urgent ones |
| Linked list | Dynamic allocation with `malloc`/`free`; not permitted by course constraints; also has pointer-chasing overhead |
| Binary heap | O(log n) insert and dequeue, automatic priority — but forbidden (no tree-based structures); also loses FIFO fairness within same priority |

---

## Sample Run

```
  Welcome to the ER Patient Management System.
  Time unit: minutes (simulated).

  +==================================================+
  |  ER PATIENT MANAGEMENT SYSTEM  [Time:    0 min]  |
  +==================================================+
  |  1. Admit new patient                            |
  ...
  Choice: 1

  -- Admit New Patient --
  Patient ID (positive integer): 101
  Patient name: John Smith
  Age (0-120): 45
  Severity (1-5): 3
  [OK] Patient 101 (John Smith) admitted at Level 3 (Urgent). Clock is now 1.

  Choice: 12
  -- Advance Simulated Time --
  Advance by how many minutes: 65
  [OK] Clock advanced to 66 min. Aging applied.

  Choice: 6
  === Waiting Room (Clock: 66 min) | Total: 1 patients ===
  Pos  ID     Name                 Age  CurLevel     OrigLevel    Wait(m)  Status     Aged?
  -- Level 2: Emergent (1 patients) --
  1    101    John Smith           45   Emergent      Urgent       65       HIGH PRI   (aged)
```

---

## How to Build, Run, Test

```bash
# Prerequisites: GCC (MinGW on Windows, or GCC on Linux/macOS)

# Build the main application
make all
# or manually:
gcc -Wall -Wextra -Wpedantic -std=c11 -o er_patient_mgmt \
    patient.c cqueue.c stack.c ermanager.c main.c

# Run
make run
# or:
./er_patient_mgmt      # Linux/macOS
er_patient_mgmt.exe    # Windows

# Build and run tests
make test
# or manually:
gcc -Wall -Wextra -Wpedantic -std=c11 -o run_tests \
    patient.c cqueue.c stack.c ermanager.c test.c
./run_tests

# Valgrind (Linux/macOS only)
make valgrind
# or:
valgrind --leak-check=full ./run_tests
```

---

## Assumptions

1. `int` is at least 32 bits (standard for all target platforms).
2. `arrival_time` is always non-negative (clock starts at 0 and only increases).
3. Patient IDs are positive integers; 0 and negatives are rejected.
4. The history stack is a bounded undo buffer; if it fills up, the oldest
   entry is silently overwritten by a new one (this is acceptable since the
   stack is primarily for undo, not permanent archiving).
5. Simulation mode creates a temporary `ERManager` on the stack and does not
   affect the main manager.

## Possible Future Improvements

- Dynamic capacity using `malloc` / `realloc` for unbounded queues.
- Persistent storage: save/load queue state to a file.
- Multi-threading: lock-free circular buffer for concurrent producers.
- GUI front-end (ncurses or web).
- Real-time clock integration instead of a simulated clock.

---

## Web Demo

### Prerequisites
- `er_web.exe` already built (see **How to Build** above)
- Node.js 18+

### Run Steps

```bash
# From the er_patient_mgmt directory:
node server.js
# Server starts on http://127.0.0.1:3000/
# Open that URL in any modern browser.
```

### Demo Script (5-minute walkthrough)

1. **Load demo data** — click *Tools → Load Demo* to admit 8 pre-built patients across all 5 triage levels.

2. **Inspect queues** — five color-coded columns populate instantly. Each card shows patient ID, name, age, current wait, and an aging progress bar.

3. **Admit manually** — fill *Admit New Patient*, click ⚡ Auto to generate an ID, pick a severity button, then **+ Admit Patient**. A toast confirms and the card animates in.

4. **Advance time** — click **+60** twice (120 min total). Watch Level 4/5 cards develop orange/red aging bars as patients near promotion thresholds.

5. **Treat & Undo** — the *Next to Treat* banner shows the highest-priority patient. Press **T** to treat; press **U** to undo and watch them reappear.

6. **Retriage** — on any card, click a colored level button (e.g. L1) to move a patient to a higher-priority queue.

7. **Discharge** — click **✕ Discharge** on a card, confirm the dialog.

8. **List view** — toggle *☰ Waiting List* for a sortable table. Click any column to sort; use **/** to focus the search box and filter by ID or name.

9. **Statistics tab** — live bar chart per level, average/max wait, total treated.

10. **Simulate** — *Tools → Run Simulation*, count 20, seed 42. Backend generates patients, treats some, advances time.

11. **Import CSV** — click *Import CSV*, paste:
    ```
    id,name,age,severity
    3001,Grace Hall,34,2
    3002,Hank Allen,55,4
    ```
    Report shows loaded/skipped with reasons.

12. **Export CSV** — downloads the current waiting list as a dated `.csv` file.

13. **Dark mode** — click 🌙 Dark in the top bar; preference persists in `localStorage`.

14. **Reset** — click ↺ Reset (confirmation required) to wipe all state.

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `T` | Treat next patient |
| `U` | Undo last treatment |
| `A` | Focus Admit form |
| `/` | Focus search box |
| `?` | Open help / shortcuts popover |
| `Esc` | Close dialogs |
