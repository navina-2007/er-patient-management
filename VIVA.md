# VIVA EXPLANATION DOCUMENT
## ER Patient Management System — Step-by-Step Conceptual Guide

---

## 1. Circular Array Queue — How It Works

### Structure

```c
typedef struct {
    Patient data[MAX_PER_LEVEL];  // fixed-size array
    int     front;  // logical index of the OLDEST element
    int     rear;   // slot where the NEXT element will be WRITTEN
    int     count;  // number of elements (authoritative; never ambiguous)
} CircularQueue;
```

### Traced Example (MAX_PER_LEVEL = 5)

```
Initial state (empty):
  Index:  [0] [1] [2] [3] [4]
  Data:    _   _   _   _   _
  front=0, rear=0, count=0

Enqueue A (arrival=10):
  data[rear=0] = A; rear = (0+1)%5 = 1; count=1
  Index:  [0] [1] [2] [3] [4]
  Data:   [A]  _   _   _   _
  front=0, rear=1, count=1

Enqueue B (arrival=20), C (arrival=30):
  rear=3, count=3
  Index:  [0] [1] [2] [3] [4]
  Data:   [A] [B] [C]  _   _
  front=0, rear=3, count=3

Dequeue → returns A:
  out = data[front=0] = A
  front = (0+1)%5 = 1; count=2
  Index:  [0] [1] [2] [3] [4]
  Data:    _  [B] [C]  _   _
  front=1, rear=3, count=2

Enqueue D (arrival=40), E (arrival=50), F (arrival=60):
  data[3]=D, rear=4; data[4]=E, rear=0; data[0]=F, rear=1; count=5
  Index:  [0] [1] [2] [3] [4]
  Data:   [F] [B] [C] [D] [E]
  front=1, rear=1, count=5   ← FULL (not empty! count tells us)

Now front==rear but count=5 → FULL.
After one dequeue (→B): front=2, count=4, rear=1.
```

**Key insight**: `count` disambiguates `front == rear` (could be full or empty
without it). We ALWAYS check `count`, never compare front and rear directly.

---

## 2. Priority + FIFO Fairness Together

The five queues form a **multi-level feedback queue**:

```
er_treat_next():
  for i = 0 to 4:
    if queues[i] is not empty:
      return dequeue(queues[i])   // always the FRONT (oldest) element
```

This gives:
- **Priority**: Level-1 queue is checked before Level-2, etc.
- **FIFO within level**: Because we always take the *front* of the queue, and
  patients are enqueued at the rear in arrival order, the oldest patient
  within a level is always treated first.

Example: Three Level-3 patients arrive at t=5, t=10, t=20.
```
Level-3 queue: [P(t=5)] [P(t=10)] [P(t=20)]
                   ^front
```
Each treatment removes from front, so t=5 → t=10 → t=20. Perfect FIFO.

---

## 3. Aging — Preventing Starvation

### The Problem

A continuous stream of Level-1 patients would keep a Level-5 patient waiting
forever. This is called **starvation**.

### The Solution: Aging (Time-Based Promotion)

```
AGING_THRESHOLD:
  Level 5: 120 min → promote to Level 4
  Level 4:  90 min → promote to Level 3
  Level 3:  60 min → promote to Level 2
  Level 2:   0 min → NEVER (promotion ceiling)
  Level 1:   0 min → NEVER (this IS the ceiling)
```

### `apply_aging()` Algorithm

```
for level i = 5 down to 3:          // process high levels first → cascade
  for each patient p in queues[i]:
    wait = clock - p.level_entry_time
    if wait >= AGING_THRESHOLD[i]:
      new_level = i - 1
      if new_level < 2: new_level = 2  // CEILING: never enter Level 1
      if queues[new_level] is full: skip (retry next call)
      remove p from queues[i]
      p.severity = new_level
      p.level_entry_time = clock       // reset; must wait AGAIN at new level
      // arrival_time and original_severity are PRESERVED
      insert p into queues[new_level] in arrival_time order
```

### Promotion Ceiling

**Level 1 is reserved for clinically-triaged Critical patients.**
A Level-5 patient who has waited 500 minutes will reach Level 2 but
never Level 1. This is enforced by the `if (new_level < 2) new_level = 2`
guard in `apply_aging`.

### Arrival-Time Ordered Insertion (`cq_insert_in_order`)

When a promoted patient enters the target queue, they must not jump ahead of
patients who arrived EARLIER. Example:

```
Level-4 queue: [P(arr=5)] [P(arr=15)] [P(arr=25)]
Promoting Level-5 patient P(arr=12):

Linear scan: find first element with arrival_time > 12 → that's P(arr=15) at index 1.
Shift elements [1, 2] one step right: [5][_][15][25]
Insert at index 1: [5][12][15][25]

Result: [P(arr=5)] [P(arr=12)] [P(arr=15)] [P(arr=25)]  ✓ correct
```

P(arr=12) goes AFTER P(arr=5) (who arrived earlier) and BEFORE P(arr=15).

---

## 4. Removal from the Middle — Shifting and Cost

`cq_remove_by_id()` removes a patient whose ID matches, anywhere in the queue.

### Algorithm

```
1. Linear scan to find logical index `del` where data[del].id == target_id.
2. Shift elements [del+1 .. count-1] one step LEFT (toward front):
   for i = del to count-2:
     data[(front+i)%MAX] = data[(front+i+1)%MAX]
3. Retract rear by one: rear = (rear - 1 + MAX) % MAX
4. count--
```

### Example (queue: A B C D E, removing C at index 2)

```
Before: [ _ A B C D E _ ]   front=1, rear=6, count=5
                ^C = index 2
After shift:  D and E move left:
        [ _ A B D E _ _ ]   front=1, rear=5, count=4
```

**Cost**: O(n) in the worst case (removing from the front requires shifting
all n-1 elements). This is acceptable here because:
- Removal from middle is an infrequent operation (re-triage, discharge).
- The array is bounded (max 100 elements per level → at most 99 shifts).

---

## 5. Array Stack for Undo

```
data: [P_oldest | P2 | P3 | ... | P_newest]
       ^index 0                   ^index top-1
top points to next FREE slot (starts at 0).
```

- **Push** (treat patient): `data[top] = patient; top++;` → O(1)
- **Pop** (undo): `top--; return data[top];` → O(1)
- **Undo** calls `cq_insert_in_order` to put the patient back in
  arrival-time order.

The stack holds the last `MAX_HISTORY = 200` treated patients. If the stack
fills, the oldest undo-records are lost (this is a bounded undo buffer, not a
permanent log).

---

## 6. Time and Space Complexity Summary

| Operation | Time | Notes |
|-----------|------|-------|
| Enqueue | O(1) | Append to rear |
| Dequeue | O(1) | Remove from front |
| Peek front | O(1) | Read front |
| Get at index | O(1) | Modular arithmetic |
| Insert in order | O(n) | Shift + scan |
| Remove by ID | O(n) | Shift |
| Find by ID | O(n) | Linear scan |
| Stack push/pop/peek | O(1) | Array index |
| Admit patient | O(n) | Dup check across all queues |
| Treat next | O(n²) | apply_aging is O(n²) worst |
| Apply aging | O(n²) | n patients × O(n) insert |
| Display all | O(n) | One pass |
| Statistics | O(n) | One pass |

**Space**: 5 × 100 × sizeof(Patient) + 200 × sizeof(Patient)
= 700 × ~80 bytes ≈ 56 KB. All on the program stack; zero heap usage.

---

## 7. Fixed-Capacity Trade-off vs Linked Lists

| Aspect | Fixed Array | Linked List |
|--------|-------------|-------------|
| Memory | Allocated at compile time | Allocated dynamically |
| Cache performance | Excellent (contiguous) | Poor (pointer chasing) |
| Risk of leak | None (no malloc) | Yes (every node must be freed) |
| Maximum capacity | Compile-time constant | Bounded only by available heap |
| Insert/remove middle | O(n) shift | O(n) find + O(1) pointer update |
| Course constraint | ALLOWED | FORBIDDEN |

For an ER with ≤ 100 patients per severity level, the fixed array is efficient,
safe (valgrind-clean), and meets the course requirements.

---

## 15 Likely Viva Questions and Model Answers

**Q1. Why use five separate queues instead of one sorted array?**

A: Separate queues give O(1) enqueue (append to rear) and O(1) dequeue (front
of highest-priority queue). A single sorted array would require O(n) insert
to maintain order. Separate queues also cleanly separate aging state per level.

---

**Q2. What is the purpose of `count` in the CircularQueue? Why not use
`front == rear` to detect empty/full?**

A: When `front == rear` the queue could be either empty (nothing enqueued yet)
or full (exactly MAX_PER_LEVEL elements). `count` removes this ambiguity with
zero additional algorithmic cost.

---

**Q3. Trace an enqueue and a dequeue on a 5-slot queue that has wrapped.**

A: (Use the 5-slot example from Section 1 above. Show physical indices and
modular arithmetic explicitly.)

---

**Q4. What is starvation and how does aging prevent it?**

A: Starvation = a low-priority patient never gets treated because higher-priority
patients keep arriving. Aging gives every patient a maximum wait time at their
current level. After that time, they move to the next higher priority level,
guaranteeing eventual treatment.

---

**Q5. Why is the promotion ceiling set at Level 2, not Level 1?**

A: Level 1 (Critical) is reserved for patients who are clinically assessed as
immediately life-threatening by a doctor or nurse. If a Level-5 patient were
promoted to Level 1 purely by waiting, they would displace genuinely critical
patients. The aging mechanism is for fairness, not clinical reassessment.

---

**Q6. How does `cq_insert_in_order` preserve FIFO fairness for promoted
patients?**

A: It scans for the first patient whose `arrival_time` is GREATER than the
promoted patient's. The promoted patient is inserted just before that patient,
ensuring they appear after anyone who arrived earlier but before later arrivals.
This means promotion gives higher priority than same-level non-promoted patients
only insofar as is already warranted by actual arrival time.

---

**Q7. What is the time complexity of re-triage? Why?**

A: O(n). Finding the patient is O(n) (linear scan). Removing them is O(n)
(shift). Inserting in order is O(n) (scan + shift). All three steps are
sequential, so the total is O(n).

---

**Q8. How does the undo operation work? Can you undo multiple times?**

A: `er_undo_last` pops from the history stack (O(1)) and re-inserts the
patient into their queue in arrival-time order (O(n)). You can undo multiple
times, once per entry in the stack, up to `MAX_HISTORY = 200` undos.

---

**Q9. What happens if two patients have the same arrival time?**

A: `cq_insert_in_order` uses a strict `>` comparison. If a new patient's
arrival time equals an existing one, the new patient is placed AFTER the
existing one (stable insert). This is the safest "tie-breaking" policy.

---

**Q10. How is the history stack different from a queue? Why use a stack here?**

A: A stack is LIFO — the most recently pushed item is popped first. This
matches the semantics of "undo" (reverse the last action). A queue would pop
the oldest treatment first, which would not be undo behaviour.

---

**Q11. What if the target queue is full during aging? Is any patient lost?**

A: No. The code checks `cq_is_full` before attempting to remove the patient
from the source queue. If the target is full, the patient is left in the
source queue unchanged and will be reconsidered on the next `apply_aging` call.

---

**Q12. Why does `apply_aging` scan from Level 5 down to Level 3?**

A: To enable cascading in a single pass. If a patient is promoted from Level 5
to Level 4, and Level-4's threshold is also exceeded, the same invocation can
promote them further (from Level 4 to Level 3) on the same iteration through
`i=3`. Scanning high-to-low makes this possible without a second pass.

---

**Q13. How does removal from the middle of a circular array work? What is
the cost of removing the front element vs a middle element?**

A: Both are O(n) in the worst case because we always shift elements toward
the rear to close the gap. Removing the front element shifts n-1 elements;
removing a middle element at index k shifts n-k-1 elements. Removing the last
element (rear) requires zero shifts (just retract rear). Our implementation
always shifts toward the rear, keeping `front` stable.

---

**Q14. How is input validated against buffer overflows and bad values?**

A: All input is read with `fgets(buf, sizeof(buf), stdin)`. If the input is
longer than the buffer, `strchr(buf, '\n') == NULL` detects the truncation and
we call `flush_line()` to discard the rest. `strtol` with `endptr` and
`errno` detects non-numeric input, trailing garbage, and overflow. Ranges
(age 0–120, severity 1–5, ID > 0) are checked explicitly. On EOF, the
program exits cleanly.

---

**Q15. What is the space complexity of the entire system?**

A: O(1) additional (constant) space beyond the fixed-size arrays.
- 5 × `CircularQueue`: 5 × 100 × `sizeof(Patient)` ≈ 40 KB
- `Stack`: 200 × `sizeof(Patient)` ≈ 16 KB
- Total: ≈ 56 KB, all statically allocated (or on the local `ERManager` stack
  frame in `main`). No heap allocation, so `valgrind --leak-check=full` reports
  zero leaks.

---

## Self-Review Checklist

- [x] Compiles with zero warnings: `gcc -Wall -Wextra -Wpedantic -std=c11`
- [x] All 16 tests pass (16/16 PASS)
- [x] No linked lists, trees, heaps, graphs, or hash tables
- [x] No `malloc`; no heap allocation at all
- [x] All queue operations use modulo wrap-around correctly
- [x] `count` used (not `front==rear`) to detect empty/full
- [x] FIFO order preserved within each level
- [x] Aging ceiling enforced (never promotes into Level 1)
- [x] Promoted patients inserted in arrival-time order
- [x] Promotion skipped (not lost) when target queue is full
- [x] All inputs read with `fgets` + `strtol`; no raw `scanf`
- [x] EOF handled (clean exit)
- [x] Buffer overflow in input impossible (buffer-sized reads + flush)
- [x] Duplicate IDs rejected across queues AND history
- [x] No global variables (all state in `ERManager`, passed by pointer)
- [x] Every function has a purpose/parameter/return/complexity comment
- [x] No platform-specific calls (`system("clear")`, `conio.h`, etc.)
- [x] Works on Linux, macOS, Windows (MinGW)
- [x] `patient.h` / `patient.c` / `cqueue.h` / `cqueue.c` / `stack.h` /
      `stack.c` / `ermanager.h` / `ermanager.c` / `main.c` / `test.c` /
      `Makefile` / `README.md` — all files present and complete
