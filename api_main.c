/*
 * api_main.c
 * ==========
 * Second main() entry-point for the ER web backend.
 *
 * Protocol:
 *   - Reads one command line from stdin.
 *   - Writes exactly ONE line of JSON to stdout, then fflush(stdout).
 *   - Never writes to stderr (so Node.js can parse cleanly).
 *
 * Commands:
 *   ADMIT id|name|age|severity
 *   TREAT
 *   PEEK
 *   RETRIAGE id|newSeverity
 *   DISCHARGE id
 *   SEARCH id
 *   UNDO
 *   ADVANCE n
 *   STATS
 *   HISTORY
 *   SIMULATE n|seed
 *   RESET
 *   STATE
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 api_main.c patient.c cqueue.c stack.c ermanager.c -o er_web.exe
 */

#include "ermanager.h"
#include "patient.h"
#include "cqueue.h"
#include "stack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

/* -----------------------------------------------------------------------
 * Global ERManager (lives for the entire process lifetime)
 * ----------------------------------------------------------------------- */
static ERManager mgr;

/* -----------------------------------------------------------------------
 * JSON string escaping helper
 * Writes the escaped version of src into dst (dst must be large enough).
 * Escapes: \ -> \\, " -> \", control chars -> \uXXXX
 * ----------------------------------------------------------------------- */
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 6 < dst_size; si++) {
        unsigned char c = (unsigned char)src[si];
        if (c == '\\') {
            dst[di++] = '\\'; dst[di++] = '\\';
        } else if (c == '"') {
            dst[di++] = '\\'; dst[di++] = '"';
        } else if (c == '\n') {
            dst[di++] = '\\'; dst[di++] = 'n';
        } else if (c == '\r') {
            dst[di++] = '\\'; dst[di++] = 'r';
        } else if (c == '\t') {
            dst[di++] = '\\'; dst[di++] = 't';
        } else if (c < 0x20) {
            di += (size_t)snprintf(dst + di, dst_size - di, "\\u%04x", (unsigned)c);
        } else {
            dst[di++] = (char)c;
        }
    }
    if (di < dst_size) dst[di] = '\0';
    else dst[dst_size - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * Snapshot structure for promotion detection
 * Before er_apply_aging() we record each patient's id + current level.
 * After aging we diff the snapshot to find promotions.
 * ----------------------------------------------------------------------- */
#define MAX_SNAPSHOT (NUM_LEVELS * MAX_PER_LEVEL)
typedef struct { int id; int level; } PatientSnap;

static int take_snapshot(PatientSnap *snap)
{
    int n = 0;
    for (int i = 0; i < NUM_LEVELS && n < MAX_SNAPSHOT; i++) {
        for (int j = 0; j < mgr.queues[i].count && n < MAX_SNAPSHOT; j++) {
            Patient *p = cq_get_at(&mgr.queues[i], j);
            if (p) { snap[n].id = p->id; snap[n].level = i + 1; n++; }
        }
    }
    return n;
}

/* -----------------------------------------------------------------------
 * Emit a JSON patient object.
 * pos = overall treatment order position (1-based); -1 = unknown.
 * ----------------------------------------------------------------------- */
static void emit_patient_obj(const Patient *p, int pos)
{
    char esc_name[MAX_NAME_LEN * 2 + 4];
    json_escape(p->name, esc_name, sizeof(esc_name));

    int wait = mgr.clock - p->arrival_time;
    if (wait < 0) wait = 0;

    int level_wait = mgr.clock - p->level_entry_time;
    if (level_wait < 0) level_wait = 0;

    bool aged = (p->severity != p->original_severity);

    printf("{\"id\":%d,\"name\":\"%s\",\"age\":%d,"
           "\"level\":%d,\"orig\":%d,\"wait\":%d,\"levelWait\":%d,"
           "\"aged\":%s,\"position\":%d}",
           p->id, esc_name, p->age,
           p->severity, p->original_severity, wait, level_wait,
           aged ? "true" : "false",
           pos);
}


/* -----------------------------------------------------------------------
 * Build the full state JSON blob and print it (no trailing newline –
 * caller adds it after the closing brace).
 *
 * Shape:
 *   "clock": N,
 *   "queues": [[...], [...], [...], [...], [...]],
 *   "next": {...} | null,
 *   "stats": {...},
 *   "history": [...],
 *   "lastTreated": {...} | null,
 *   "promotions": [{"id":N,"from":F,"to":T,"clock":C}, ...]
 * ----------------------------------------------------------------------- */
static void emit_state(void)
{
    /* Snapshot levels BEFORE aging so we can detect promotions */
    PatientSnap before[MAX_SNAPSHOT];
    int before_n = take_snapshot(before);

    /* Apply aging so everything is fresh */
    er_apply_aging(&mgr);

    /* Snapshot AFTER aging */
    PatientSnap after[MAX_SNAPSHOT];
    int after_n = take_snapshot(after);

    /* ---- clock ---- */
    printf("\"clock\":%d,", mgr.clock);

    /* ---- queues ---- */
    printf("\"queues\":[");
    int overall_pos = 0;
    for (int lvl = 0; lvl < NUM_LEVELS; lvl++) {
        printf("[");
        for (int j = 0; j < mgr.queues[lvl].count; j++) {
            Patient *p = cq_get_at(&mgr.queues[lvl], j);
            if (p == NULL) continue;
            overall_pos++;
            if (j > 0) printf(",");
            emit_patient_obj(p, overall_pos);
        }
        printf("]");
        if (lvl < NUM_LEVELS - 1) printf(",");
    }
    printf("],");

    /* ---- next ---- */
    /* Find the front of the highest-priority non-empty queue */
    printf("\"next\":");
    bool found_next = false;
    int next_pos = 0;
    for (int lvl = 0; lvl < NUM_LEVELS && !found_next; lvl++) {
        /* Count all patients in higher-priority queues */
        for (int k = 0; k < lvl; k++) next_pos += mgr.queues[k].count;
        if (!cq_is_empty(&mgr.queues[lvl])) {
            Patient tmp;
            if (cq_peek_front(&mgr.queues[lvl], &tmp) == ER_OK) {
                next_pos++;
                emit_patient_obj(&tmp, next_pos);
                found_next = true;
            }
        }
        next_pos = 0; /* reset for next iteration */
    }
    if (!found_next) printf("null");
    printf(",");

    /* ---- stats ---- */
    int tw = 0;
    int level_counts[NUM_LEVELS];
    int max_wait = 0;
    int max_id   = -1;
    char max_name[MAX_NAME_LEN];
    max_name[0] = '\0';
    long sum_wait = 0;

    for (int i = 0; i < NUM_LEVELS; i++) {
        level_counts[i] = mgr.queues[i].count;
        tw += mgr.queues[i].count;
        for (int j = 0; j < mgr.queues[i].count; j++) {
            Patient *p = cq_get_at(&mgr.queues[i], j);
            if (p == NULL) continue;
            int w = mgr.clock - p->arrival_time;
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

    double avg_wait_waiting = (tw > 0) ? (double)sum_wait / tw : 0.0;
    double avg_wait_treated = (mgr.total_treated > 0)
                              ? (double)mgr.total_treated_wait / mgr.total_treated
                              : 0.0;

    char esc_max[MAX_NAME_LEN * 2 + 4];
    json_escape(max_name, esc_max, sizeof(esc_max));

    printf("\"stats\":{\"totalWaiting\":%d,"
           "\"countPerLevel\":[%d,%d,%d,%d,%d],"
           "\"avgWait\":%.2f,"
           "\"maxWait\":%d,"
           "\"maxWaitId\":%d,"
           "\"maxWaitName\":\"%s\","
           "\"totalTreated\":%d,"
           "\"avgWaitTreated\":%.2f},",
           tw,
           level_counts[0], level_counts[1], level_counts[2],
           level_counts[3], level_counts[4],
           avg_wait_waiting,
           max_wait,
           max_id,
           esc_max,
           mgr.total_treated,
           avg_wait_treated);

    /* ---- history (most recent first) ----
     * For treated patients the wait time is frozen at the moment
     * treatment started: treatment_start_time - arrival_time.
     * We fall back to clock - arrival_time only if treatment_start_time
     * was never set (legacy / undo path). */
    printf("\"history\":[");
    for (int i = mgr.history.top - 1; i >= 0; i--) {
        Patient *p = &mgr.history.data[i];
        if (i < mgr.history.top - 1) printf(",");
        char en[MAX_NAME_LEN * 2 + 4];
        json_escape(p->name, en, sizeof(en));
        int w = (p->treatment_start_time > 0)
                ? (p->treatment_start_time - p->arrival_time)
                : (mgr.clock - p->arrival_time);
        if (w < 0) w = 0;
        printf("{\"id\":%d,\"name\":\"%s\",\"age\":%d,"
               "\"level\":%d,\"orig\":%d,\"wait\":%d}",
               p->id, en, p->age,
               p->severity, p->original_severity, w);
    }
    printf("],");

    /* ---- lastTreated ---- */
    printf("\"lastTreated\":");
    if (mgr.history.top > 0) {
        Patient *p = &mgr.history.data[mgr.history.top - 1];
        char en[MAX_NAME_LEN * 2 + 4];
        json_escape(p->name, en, sizeof(en));
        int w = (p->treatment_start_time > 0)
                ? (p->treatment_start_time - p->arrival_time)
                : (mgr.clock - p->arrival_time);
        if (w < 0) w = 0;
        printf("{\"id\":%d,\"name\":\"%s\",\"age\":%d,"
               "\"level\":%d,\"orig\":%d,\"wait\":%d}",
               p->id, en, p->age,
               p->severity, p->original_severity, w);
    } else {
        printf("null");
    }
    printf(",");

    /* ---- promotions ----
     * Diff the before/after snapshots to find patients whose level changed.
     * These are the patients that er_apply_aging() just promoted. */
    printf("\"promotions\":[");
    bool first_prom = true;
    for (int a = 0; a < after_n; a++) {
        int aid     = after[a].id;
        int new_lvl = after[a].level;
        for (int b = 0; b < before_n; b++) {
            if (before[b].id == aid && before[b].level != new_lvl) {
                if (!first_prom) printf(",");
                printf("{\"id\":%d,\"from\":%d,\"to\":%d,\"clock\":%d}",
                       aid, before[b].level, new_lvl, mgr.clock);
                first_prom = false;
                break;
            }
        }
    }
    printf("]");
}

/* -----------------------------------------------------------------------
 * emit_response – print a complete JSON response line.
 *   ok  : 0 or 1
 *   msg : human-readable message (will be JSON-escaped)
 * Caller must have already emitted any extra fields into the buffer
 * that is flushed here.
 * ----------------------------------------------------------------------- */
static void emit_response(int ok, const char *msg)
{
    char esc_msg[512];
    json_escape(msg, esc_msg, sizeof(esc_msg));
    printf("{\"ok\":%d,\"msg\":\"%s\",", ok, esc_msg);
    emit_state();
    printf("}\n");
    fflush(stdout);
}

/* -----------------------------------------------------------------------
 * Simple pseudo-random number generator (LCG) for SIMULATE
 * ----------------------------------------------------------------------- */
static unsigned int rng_state = 12345u;

static void rng_seed(unsigned int seed)
{
    rng_state = seed ? seed : 1u;
}

static unsigned int rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

/* -----------------------------------------------------------------------
 * Adjectives and nouns for random name generation
 * ----------------------------------------------------------------------- */
static const char *first_names[] = {
    "Alice", "Bob", "Carol", "Dan", "Eve", "Frank", "Grace", "Hank",
    "Iris", "Jack", "Karen", "Leo", "Mia", "Nick", "Olivia", "Paul",
    "Quinn", "Rose", "Sam", "Tina", "Uma", "Vince", "Wendy", "Xander",
    "Yara", "Zane"
};
#define FIRST_NAMES_COUNT 26

static const char *last_names[] = {
    "Smith", "Jones", "Brown", "Taylor", "Wilson", "Davis", "White",
    "Harris", "Martin", "Thomas", "Moore", "Jackson", "Lee", "Lewis",
    "Clark", "Hall", "Allen", "Young", "King", "Wright"
};
#define LAST_NAMES_COUNT 20

/* -----------------------------------------------------------------------
 * trim_nl – remove trailing \r\n from a line buffer
 * ----------------------------------------------------------------------- */
static void trim_nl(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

/* -----------------------------------------------------------------------
 * Command handlers
 * ----------------------------------------------------------------------- */

/* ADMIT id|name|age|severity */
static void cmd_admit(const char *args)
{
    int id, age, severity;
    char name[MAX_NAME_LEN];

    /* Parse pipe-delimited args */
    char buf[256];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "|");
    if (tok == NULL) { emit_response(0, "ADMIT: missing id"); return; }
    id = atoi(tok);

    tok = strtok(NULL, "|");
    if (tok == NULL) { emit_response(0, "ADMIT: missing name"); return; }
    strncpy(name, tok, MAX_NAME_LEN - 1);
    name[MAX_NAME_LEN - 1] = '\0';
    /* trim */
    trim_string(name);

    tok = strtok(NULL, "|");
    if (tok == NULL) { emit_response(0, "ADMIT: missing age"); return; }
    age = atoi(tok);

    tok = strtok(NULL, "|");
    if (tok == NULL) { emit_response(0, "ADMIT: missing severity"); return; }
    severity = atoi(tok);

    Patient p;
    patient_init(&p);
    p.id       = id;
    p.age      = age;
    p.severity = severity;
    strncpy(p.name, name, MAX_NAME_LEN - 1);
    p.name[MAX_NAME_LEN - 1] = '\0';

    ERStatus st = er_admit(&mgr, &p);
    switch (st) {
        case ER_OK:
            emit_response(1, "Patient admitted");
            break;
        case ER_DUPLICATE:
            emit_response(0, "Duplicate ID: patient already exists");
            break;
        case ER_FULL:
            emit_response(0, "Queue full for that severity level");
            break;
        case ER_INVALID:
            emit_response(0, "Invalid parameters (id<=0, bad age/severity, empty name)");
            break;
        default:
            emit_response(0, "Admit failed");
            break;
    }
}

/* TREAT */
static void cmd_treat(void)
{
    Patient out;
    patient_init(&out);
    ERStatus st = er_treat_next(&mgr, &out);
    if (st == ER_OK) {
        char buf[128];
        char esc[MAX_NAME_LEN * 2 + 4];
        json_escape(out.name, esc, sizeof(esc));
        snprintf(buf, sizeof(buf), "Treated: %s (ID %d)", esc, out.id);
        emit_response(1, buf);
    } else if (st == ER_EMPTY) {
        emit_response(0, "No patients waiting");
    } else {
        emit_response(0, "Treat failed");
    }
}

/* PEEK */
static void cmd_peek(void)
{
    Patient out;
    patient_init(&out);
    ERStatus st = er_peek_next(&mgr, &out);
    if (st == ER_OK) {
        char buf[128];
        char esc[MAX_NAME_LEN * 2 + 4];
        json_escape(out.name, esc, sizeof(esc));
        snprintf(buf, sizeof(buf), "Next: %s (ID %d, Level %d)", esc, out.id, out.severity);
        emit_response(1, buf);
    } else if (st == ER_EMPTY) {
        emit_response(0, "No patients waiting");
    } else {
        emit_response(0, "Peek failed");
    }
}

/* RETRIAGE id|newSeverity */
static void cmd_retriage(const char *args)
{
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "|");
    if (tok == NULL) { emit_response(0, "RETRIAGE: missing id"); return; }
    int id = atoi(tok);

    tok = strtok(NULL, "|");
    if (tok == NULL) { emit_response(0, "RETRIAGE: missing newSeverity"); return; }
    int new_sev = atoi(tok);

    ERStatus st = er_retriage(&mgr, id, new_sev);
    switch (st) {
        case ER_OK:        emit_response(1, "Patient re-triaged"); break;
        case ER_NOT_FOUND: emit_response(0, "Patient ID not found"); break;
        case ER_FULL:      emit_response(0, "Target queue full"); break;
        case ER_INVALID:   emit_response(0, "Invalid severity"); break;
        default:           emit_response(0, "Retriage failed"); break;
    }
}

/* DISCHARGE id */
static void cmd_discharge(const char *args)
{
    int id = atoi(args);
    Patient out;
    patient_init(&out);
    ERStatus st = er_discharge(&mgr, id, &out);
    if (st == ER_OK) {
        char buf[128];
        char esc[MAX_NAME_LEN * 2 + 4];
        json_escape(out.name, esc, sizeof(esc));
        snprintf(buf, sizeof(buf), "Discharged: %s (ID %d)", esc, out.id);
        emit_response(1, buf);
    } else if (st == ER_NOT_FOUND) {
        emit_response(0, "Patient ID not found");
    } else {
        emit_response(0, "Discharge failed");
    }
}

/* SEARCH id */
static void cmd_search(const char *args)
{
    int id = atoi(args);

    er_apply_aging(&mgr);

    int overall_pos = 0;
    for (int i = 0; i < NUM_LEVELS; i++) {
        for (int j = 0; j < mgr.queues[i].count; j++) {
            overall_pos++;
            Patient *p = cq_get_at(&mgr.queues[i], j);
            if (p != NULL && p->id == id) {
                char buf[256];
                char esc[MAX_NAME_LEN * 2 + 4];
                json_escape(p->name, esc, sizeof(esc));
                int wait = mgr.clock - p->arrival_time;
                if (wait < 0) wait = 0;
                snprintf(buf, sizeof(buf),
                         "Found: %s (ID %d) Level %d pos %d/%d wait %d min",
                         esc, p->id, p->severity, overall_pos,
                         /* total waiting */ mgr.queues[0].count + mgr.queues[1].count +
                         mgr.queues[2].count + mgr.queues[3].count + mgr.queues[4].count,
                         wait);
                emit_response(1, buf);
                return;
            }
        }
    }
    emit_response(0, "Patient ID not found");
}

/* UNDO */
static void cmd_undo(void)
{
    ERStatus st = er_undo_last(&mgr);
    switch (st) {
        case ER_OK:    emit_response(1, "Last treatment undone"); break;
        case ER_EMPTY: emit_response(0, "Nothing to undo"); break;
        case ER_FULL:  emit_response(0, "Cannot undo: queue full"); break;
        default:       emit_response(0, "Undo failed"); break;
    }
}

/* ADVANCE n */
static void cmd_advance(const char *args)
{
    int n = atoi(args);
    ERStatus st = er_advance_time(&mgr, n);
    if (st == ER_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Clock advanced by %d min (now %d)", n, mgr.clock);
        emit_response(1, buf);
    } else {
        emit_response(0, "ADVANCE: n must be > 0");
    }
}

/* STATS */
static void cmd_stats(void)
{
    emit_response(1, "Statistics computed");
}

/* HISTORY */
static void cmd_history(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "History: %d treated", mgr.total_treated);
    emit_response(1, buf);
}

/* SIMULATE n|seed */
static void cmd_simulate(const char *args)
{
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "|");
    if (tok == NULL) { emit_response(0, "SIMULATE: missing count"); return; }
    int n = atoi(tok);

    tok = strtok(NULL, "|");
    unsigned int seed = (tok != NULL) ? (unsigned int)atoi(tok) : 42u;

    if (n <= 0 || n > 50) {
        emit_response(0, "SIMULATE: count must be 1..50");
        return;
    }

    rng_seed(seed);

    int admitted = 0, treated = 0, failed = 0;
    int next_id = 9000 + (int)(rng_next() % 1000);

    for (int i = 0; i < n; i++) {
        /* Random patient */
        Patient p;
        patient_init(&p);

        /* Find a unique ID */
        int attempts = 0;
        do {
            next_id = (next_id + (int)(rng_next() % 97) + 1);
            if (next_id > 99999) next_id = 1001;
            attempts++;
        } while (er_id_exists(&mgr, next_id) && attempts < 200);

        if (er_id_exists(&mgr, next_id)) { failed++; continue; }

        p.id       = next_id;
        p.age      = (int)(rng_next() % 100) + 1;
        p.severity = (int)(rng_next() % NUM_LEVELS) + 1;

        const char *fn = first_names[rng_next() % FIRST_NAMES_COUNT];
        const char *ln = last_names[rng_next() % LAST_NAMES_COUNT];
        snprintf(p.name, MAX_NAME_LEN, "%s %s", fn, ln);

        ERStatus st = er_admit(&mgr, &p);
        if (st == ER_OK) admitted++;
        else failed++;

        /* Occasionally treat */
        if ((rng_next() % 3) == 0) {
            Patient out;
            patient_init(&out);
            if (er_treat_next(&mgr, &out) == ER_OK) treated++;
        }

        /* Occasionally advance time */
        if ((rng_next() % 5) == 0) {
            int adv = (int)(rng_next() % 30) + 1;
            er_advance_time(&mgr, adv);
        }
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Simulation done: %d admitted, %d treated, %d failed",
             admitted, treated, failed);
    emit_response(1, msg);
}

/* RESET */
static void cmd_reset(void)
{
    er_init(&mgr);
    emit_response(1, "System reset");
}

/* STATE – just dump current state */
static void cmd_state(void)
{
    emit_response(1, "State retrieved");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    er_init(&mgr);

    /* Disable buffering so Node.js readline gets each response immediately */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin,  NULL, _IONBF, 0);

    char line[512];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        trim_nl(line);

        /* Split command from args at first space */
        char *sp = strchr(line, ' ');
        char cmd[64];
        const char *args = "";

        if (sp != NULL) {
            size_t cmd_len = (size_t)(sp - line);
            if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
            strncpy(cmd, line, cmd_len);
            cmd[cmd_len] = '\0';
            args = sp + 1;
        } else {
            strncpy(cmd, line, sizeof(cmd) - 1);
            cmd[sizeof(cmd) - 1] = '\0';
        }

        /* Dispatch */
        if      (strcmp(cmd, "ADMIT")    == 0) cmd_admit(args);
        else if (strcmp(cmd, "TREAT")    == 0) cmd_treat();
        else if (strcmp(cmd, "PEEK")     == 0) cmd_peek();
        else if (strcmp(cmd, "RETRIAGE") == 0) cmd_retriage(args);
        else if (strcmp(cmd, "DISCHARGE")== 0) cmd_discharge(args);
        else if (strcmp(cmd, "SEARCH")   == 0) cmd_search(args);
        else if (strcmp(cmd, "UNDO")     == 0) cmd_undo();
        else if (strcmp(cmd, "ADVANCE")  == 0) cmd_advance(args);
        else if (strcmp(cmd, "STATS")    == 0) cmd_stats();
        else if (strcmp(cmd, "HISTORY")  == 0) cmd_history();
        else if (strcmp(cmd, "SIMULATE") == 0) cmd_simulate(args);
        else if (strcmp(cmd, "RESET")    == 0) cmd_reset();
        else if (strcmp(cmd, "STATE")    == 0) cmd_state();
        else if (cmd[0] == '\0')               ; /* ignore blank lines */
        else {
            char err[256];
            char esc[200];
            json_escape(cmd, esc, sizeof(esc));
            snprintf(err, sizeof(err), "Unknown command: %s", esc);
            emit_response(0, err);
        }
    }

    return 0;
}
