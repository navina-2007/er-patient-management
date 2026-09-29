/*
 * stack.h
 * =======
 * Array-based stack of Patient structs, used for undo / history.
 * Maximum capacity is MAX_HISTORY (defined in patient.h).
 *
 * Stack layout:
 *   data[0] = oldest treated patient (bottom of stack)
 *   data[top-1] = most recently treated patient (top of stack)
 *   top = next free index; starts at 0 (empty).
 */

#ifndef STACK_H
#define STACK_H

#include "patient.h"
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Stack struct
 * ----------------------------------------------------------------------- */
typedef struct {
    Patient data[MAX_HISTORY];
    int     top;   /* index of the next free slot; 0 when empty            */
} Stack;

/* -----------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------- */

/*
 * stack_init
 * Purpose   : Initialise an empty stack (top = 0).
 * Complexity: O(1)
 */
void stack_init(Stack *s);

/*
 * stack_is_empty
 * Returns   : true iff top == 0.
 * Complexity: O(1)
 */
bool stack_is_empty(const Stack *s);

/*
 * stack_is_full
 * Returns   : true iff top == MAX_HISTORY.
 * Complexity: O(1)
 */
bool stack_is_full(const Stack *s);

/*
 * stack_push
 * Purpose   : Push p onto the top of the stack.
 * Returns   : ER_OK on success, ER_FULL if at capacity.
 * Complexity: O(1)
 */
ERStatus stack_push(Stack *s, const Patient *p);

/*
 * stack_pop
 * Purpose   : Remove and return the top element.
 * Parameters: s   - the stack.
 *             out - pointer where the popped Patient is written.
 * Returns   : ER_OK on success, ER_EMPTY if the stack is empty.
 * Complexity: O(1)
 */
ERStatus stack_pop(Stack *s, Patient *out);

/*
 * stack_peek
 * Purpose   : Read the top element without removing it.
 * Returns   : ER_OK on success, ER_EMPTY if the stack is empty.
 * Complexity: O(1)
 */
ERStatus stack_peek(const Stack *s, Patient *out);

/*
 * stack_size
 * Returns   : the number of elements currently on the stack.
 * Complexity: O(1)
 */
int stack_size(const Stack *s);

#endif /* STACK_H */
