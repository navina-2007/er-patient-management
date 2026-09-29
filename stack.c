/*
 * stack.c
 * =======
 * Implementation of the array-based Patient stack (LIFO).
 */

#include "stack.h"

/* -----------------------------------------------------------------------
 * stack_init
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
void stack_init(Stack *s)
{
    s->top = 0;
}

/* -----------------------------------------------------------------------
 * stack_is_empty
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
bool stack_is_empty(const Stack *s)
{
    return (s->top == 0);
}

/* -----------------------------------------------------------------------
 * stack_is_full
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
bool stack_is_full(const Stack *s)
{
    return (s->top == MAX_HISTORY);
}

/* -----------------------------------------------------------------------
 * stack_push
 * Purpose   : Write *p to data[top] then increment top.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
ERStatus stack_push(Stack *s, const Patient *p)
{
    if (stack_is_full(s)) {
        return ER_FULL;
    }
    s->data[s->top] = *p;
    s->top++;
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * stack_pop
 * Purpose   : Decrement top, then copy data[top] to *out.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
ERStatus stack_pop(Stack *s, Patient *out)
{
    if (stack_is_empty(s)) {
        return ER_EMPTY;
    }
    s->top--;
    *out = s->data[s->top];
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * stack_peek
 * Purpose   : Copy data[top-1] to *out without changing top.
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
ERStatus stack_peek(const Stack *s, Patient *out)
{
    if (stack_is_empty(s)) {
        return ER_EMPTY;
    }
    *out = s->data[s->top - 1];
    return ER_OK;
}

/* -----------------------------------------------------------------------
 * stack_size
 * Complexity: O(1)
 * ----------------------------------------------------------------------- */
int stack_size(const Stack *s)
{
    return s->top;
}
