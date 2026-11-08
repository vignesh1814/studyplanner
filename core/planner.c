/*
 * planner.c - Study Planner core logic (pure C, no GUI dependencies).
 */
#include "planner.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define DATA_MAGIC "STUDYPLANNER 1"
#define LINE_MAX_LEN 4096
#define DUE_SOON_DAYS 3

/* ======================================================================== */
/* Dates                                                                    */
/* ======================================================================== */

static int days_in_month(int y, int m)
{
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return (m == 2 && leap) ? 29 : dm[m - 1];
}

/* Howard Hinnant's civil-calendar algorithms. */
int date_from_ymd(int year, int month, int day)
{
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month(year, month))
        return PLANNER_NO_DATE;

    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void date_to_ymd(int day, int *year, int *month, int *d)
{
    int z = day + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int dd = doy - (153 * mp + 2) / 5 + 1;
    int mm = mp < 10 ? mp + 3 : mp - 9;
    if (mm <= 2)
        y++;
    if (year)  *year = y;
    if (month) *month = mm;
    if (d)     *d = dd;
}

int date_weekday(int day)
{
    /* 1970-01-01 was a Thursday (4). */
    return (int)((((long)day % 7) + 7 + 4) % 7);
}

int date_today(void)
{
    time_t now = time(NULL);
    struct tm lt;
#ifdef _WIN32
    if (localtime_s(&lt, &now) != 0)
        return 0;
#else
    if (localtime_r(&now, &lt) == NULL)
        return 0;
#endif
    return date_from_ymd(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

int date_parse(const char *text)
{
    int y, m, d, used = 0;
    if (!text)
        return PLANNER_NO_DATE;
    if (sscanf(text, "%4d-%2d-%2d%n", &y, &m, &d, &used) != 3 || text[used] != '\0')
        return PLANNER_NO_DATE;
    return date_from_ymd(y, m, d);
}

void date_format(int day, char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    if (day == PLANNER_NO_DATE) {
        buf[0] = '\0';
        return;
    }
    int y, m, d;
    date_to_ymd(day, &y, &m, &d);
    snprintf(buf, len, "%04d-%02d-%02d", y, m, d);
}

/* ======================================================================== */
/* Lifecycle and helpers                                                    */
/* ======================================================================== */

Planner *planner_new(void)
{
    Planner *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->next_id = 1;
    p->daily_minutes = PLANNER_DEFAULT_DAILY;
    return p;
}

void planner_free(Planner *p)
{
    if (!p)
        return;
    free(p->tasks);
    free(p);
}

/* Copies src into dst (size cap), trimming outer whitespace. */
static void copy_trimmed(char *dst, size_t cap, const char *src)
{
    while (*src && isspace((unsigned char)*src))
        src++;
    size_t n = strlen(src);
    while (n > 0 && isspace((unsigned char)src[n - 1]))
        n--;
    if (n >= cap)
        n = cap - 1;
    /* Do not cut a multi-byte UTF-8 sequence in half. */
    while (n > 0 && n < strlen(src) && ((unsigned char)src[n] & 0xC0) == 0x80)
        n--;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool valid_task_fields(const char *title, Priority pr, int estimate_min)
{
    if (!title)
        return false;
    while (*title && isspace((unsigned char)*title))
        title++;
    if (*title == '\0')
        return false;
    if (pr < PRIORITY_LOW || pr > PRIORITY_HIGH)
        return false;
    return estimate_min > 0;
}

static int find_index(const Planner *p, int id)
{
    for (size_t i = 0; i < p->count; i++)
        if (p->tasks[i].id == id)
            return (int)i;
    return -1;
}

Task *planner_find(Planner *p, int id)
{
    if (!p)
        return NULL;
    int i = find_index(p, id);
    return i < 0 ? NULL : &p->tasks[i];
}

const Task *planner_find_const(const Planner *p, int id)
{
    if (!p)
        return NULL;
    int i = find_index(p, id);
    return i < 0 ? NULL : &p->tasks[i];
}

static PlannerStatus push_task(Planner *p, const Task *t)
{
    if (p->count == p->capacity) {
        size_t ncap = p->capacity ? p->capacity * 2 : 16;
        Task *nt = realloc(p->tasks, ncap * sizeof *nt);
        if (!nt)
            return PLANNER_ERR_NOMEM;
        p->tasks = nt;
        p->capacity = ncap;
    }
    p->tasks[p->count++] = *t;
    return PLANNER_OK;
}

/* ======================================================================== */
/* Task management                                                          */
/* ======================================================================== */

PlannerStatus planner_add(Planner *p, const char *title, const char *subject,
                          Priority priority, int due, int estimate_min,
                          int *out_id)
{
    if (!p || !valid_task_fields(title, priority, estimate_min))
        return PLANNER_ERR_ARG;

    Task t;
    memset(&t, 0, sizeof t);
    t.id = p->next_id;
    copy_trimmed(t.title, sizeof t.title, title);
    copy_trimmed(t.subject, sizeof t.subject, subject ? subject : "");
    t.priority = priority;
    t.due = due;
    t.estimate_min = estimate_min;

    PlannerStatus st = push_task(p, &t);
    if (st != PLANNER_OK)
        return st;
    p->next_id++;
    if (out_id)
        *out_id = t.id;
    return PLANNER_OK;
}

PlannerStatus planner_update(Planner *p, int id, const char *title,
                             const char *subject, Priority priority, int due,
                             int estimate_min, int spent_min)
{
    if (!p || spent_min < 0 || !valid_task_fields(title, priority, estimate_min))
        return PLANNER_ERR_ARG;
    Task *t = planner_find(p, id);
    if (!t)
        return PLANNER_ERR_NOT_FOUND;

    copy_trimmed(t->title, sizeof t->title, title);
    copy_trimmed(t->subject, sizeof t->subject, subject ? subject : "");
    t->priority = priority;
    t->due = due;
    t->estimate_min = estimate_min;
    t->spent_min = spent_min;
    if (t->spent_min >= t->estimate_min)
        t->completed = true;
    return PLANNER_OK;
}

PlannerStatus planner_remove(Planner *p, int id)
{
    if (!p)
        return PLANNER_ERR_ARG;
    int i = find_index(p, id);
    if (i < 0)
        return PLANNER_ERR_NOT_FOUND;
    memmove(&p->tasks[i], &p->tasks[i + 1],
            (p->count - (size_t)i - 1) * sizeof p->tasks[0]);
    p->count--;
    return PLANNER_OK;
}

PlannerStatus planner_set_completed(Planner *p, int id, bool completed)
{
    Task *t = planner_find(p, id);
    if (!t)
        return p ? PLANNER_ERR_NOT_FOUND : PLANNER_ERR_ARG;
    t->completed = completed;
    if (completed && t->spent_min < t->estimate_min)
        t->spent_min = t->estimate_min;
    else if (!completed && t->spent_min >= t->estimate_min)
        t->spent_min = t->estimate_min > 1 ? t->estimate_min - 1 : 0; /* reopen with something left */
    return PLANNER_OK;
}

PlannerStatus planner_log_time(Planner *p, int id, int minutes)
{
    Task *t = planner_find(p, id);
    if (!t)
        return p ? PLANNER_ERR_NOT_FOUND : PLANNER_ERR_ARG;
    long v = (long)t->spent_min + minutes;
    if (v < 0)
        v = 0;
    if (v > INT_MAX / 2)
        v = INT_MAX / 2;
    t->spent_min = (int)v;
    if (t->spent_min >= t->estimate_min)
        t->completed = true;
    else if (minutes < 0)
        t->completed = false;
    return PLANNER_OK;
}

PlannerStatus planner_set_daily_minutes(Planner *p, int minutes)
{
    if (!p || minutes < 0 || minutes > 24 * 60)
        return PLANNER_ERR_ARG;
    p->daily_minutes = minutes;
    return PLANNER_OK;
}

/* ======================================================================== */
/* Analysis                                                                 */
/* ======================================================================== */

int task_remaining_min(const Task *t)
{
    if (!t || t->completed)
        return 0;
    int r = t->estimate_min - t->spent_min;
    return r > 0 ? r : 0;
}

double task_urgency(const Task *t, int today)
{
    if (!t || t->completed)
        return 0.0;

    double deadline;
    if (t->due == PLANNER_NO_DATE) {
        deadline = 1.0;
    } else {
        int left = t->due - today;
        if (left < 0)
            deadline = 30.0 + (left < -30 ? 30 : -left); /* overdue: top of the list */
        else
            deadline = 30.0 / (left + 1);
    }
    static const double weight[] = {0.0, 0.7, 1.0, 1.4};
    double effort = task_remaining_min(t) / 60.0;
    if (effort > 5.0)
        effort = 5.0;
    return deadline * weight[t->priority] + effort * 0.2;
}

typedef struct {
    const Planner *p;
    int today;
} RankCtx;

/* qsort has no context argument; the planner is single-threaded here and the
 * context is only live for the duration of the sort. */
static RankCtx g_rank;

static int rank_cmp(const void *a, const void *b)
{
    const Task *ta = &g_rank.p->tasks[*(const size_t *)a];
    const Task *tb = &g_rank.p->tasks[*(const size_t *)b];
    if (ta->completed != tb->completed)
        return ta->completed ? 1 : -1;
    double ua = task_urgency(ta, g_rank.today);
    double ub = task_urgency(tb, g_rank.today);
    if (ua != ub)
        return ua > ub ? -1 : 1;
    return ta->id - tb->id;
}

void planner_rank(const Planner *p, int today, size_t *order)
{
    if (!p || !order)
        return;
    for (size_t i = 0; i < p->count; i++)
        order[i] = i;
    g_rank.p = p;
    g_rank.today = today;
    qsort(order, p->count, sizeof order[0], rank_cmp);
    g_rank.p = NULL;
}

PlannerStats planner_stats(const Planner *p, int today)
{
    PlannerStats s;
    memset(&s, 0, sizeof s);
    if (!p)
        return s;

    double est_total = 0, est_done = 0;
    for (size_t i = 0; i < p->count; i++) {
        const Task *t = &p->tasks[i];
        s.total++;
        s.spent_min += t->spent_min;
        est_total += t->estimate_min;
        if (t->completed) {
            s.completed++;
            est_done += t->estimate_min;
            continue;
        }
        est_done += t->spent_min < t->estimate_min ? t->spent_min : t->estimate_min;
        s.remaining_min += task_remaining_min(t);
        if (t->due != PLANNER_NO_DATE) {
            if (t->due < today)
                s.overdue++;
            else if (t->due - today < DUE_SOON_DAYS)
                s.due_soon++;
        }
    }
    s.percent_done = est_total > 0 ? est_done * 100.0 / est_total : 0.0;
    return s;
}

/* ======================================================================== */
/* Scheduling                                                               */
/* ======================================================================== */

typedef struct {
    int idx;        /* index into planner tasks */
    int remaining;  /* minutes left to place */
    int due;        /* effective deadline (INT_MAX when none) */
    bool late;
} Work;

static const Planner *g_sched_p;

static int work_cmp_plain(const void *a, const void *b)
{
    const Work *wa = a, *wb = b;
    if (wa->due != wb->due)
        return wa->due < wb->due ? -1 : 1;
    const Task *ta = &g_sched_p->tasks[wa->idx];
    const Task *tb = &g_sched_p->tasks[wb->idx];
    if (ta->priority != tb->priority)
        return ta->priority > tb->priority ? -1 : 1;
    return ta->id - tb->id;
}

static PlannerStatus sched_push(Schedule *s, Session v)
{
    if (s->count == s->capacity) {
        size_t ncap = s->capacity ? s->capacity * 2 : 64;
        Session *ni = realloc(s->items, ncap * sizeof *ni);
        if (!ni)
            return PLANNER_ERR_NOMEM;
        s->items = ni;
        s->capacity = ncap;
    }
    s->items[s->count++] = v;
    return PLANNER_OK;
}

static int min_int(int a, int b) { return a < b ? a : b; }

static Schedule *build_schedule(const Planner *p, int start_day, int max_block)
{

    Schedule *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;

    Work *work = NULL;
    int *placed = NULL; /* minutes placed today, per work item */
    size_t n = 0;
    if (p->count > 0) {
        work = calloc(p->count, sizeof *work);
        placed = calloc(p->count, sizeof *placed);
        if (!work || !placed)
            goto fail;
    }
    for (size_t i = 0; i < p->count; i++) {
        int rem = task_remaining_min(&p->tasks[i]);
        if (rem <= 0)
            continue;
        work[n].idx = (int)i;
        work[n].remaining = rem;
        work[n].due = p->tasks[i].due == PLANNER_NO_DATE ? INT_MAX : p->tasks[i].due;
        n++;
    }
    g_sched_p = p;
    qsort(work, n, sizeof work[0], work_cmp_plain);
    g_sched_p = NULL;

    size_t open_work = n;
    for (int off = 0; off < PLANNER_MAX_HORIZON && open_work > 0 && p->daily_minutes > 0; off++) {
        int day = start_day + off;
        int cap = p->daily_minutes;
        memset(placed, 0, n * sizeof *placed);

        /* Pass 1: a bounded block per task (interleaves subjects).
         * Pass 2: spend any capacity left, still earliest deadline first. */
        for (int pass = 0; pass < 2 && cap > 0; pass++) {
            for (size_t k = 0; k < n && cap > 0; k++) {
                if (work[k].remaining <= 0)
                    continue;
                int give = min_int(work[k].remaining, cap);
                if (pass == 0)
                    give = min_int(give, max_block);
                if (give <= 0)
                    continue;
                work[k].remaining -= give;
                placed[k] += give;
                cap -= give;
                if (work[k].remaining == 0)
                    open_work--;
            }
        }
        for (size_t k = 0; k < n; k++) {
            if (placed[k] <= 0)
                continue;
            bool late = work[k].due != INT_MAX && day > work[k].due;
            if (late)
                work[k].late = true;
            Session v = {day, p->tasks[work[k].idx].id, placed[k], late};
            if (sched_push(s, v) != PLANNER_OK)
                goto fail;
        }
    }

    for (size_t k = 0; k < n; k++) {
        s->unscheduled_min += work[k].remaining;
        if (work[k].late || (work[k].remaining > 0 && work[k].due != INT_MAX))
            s->late_tasks++;
    }
    free(work);
    free(placed);
    return s;

fail:
    free(work);
    free(placed);
    schedule_free(s);
    return NULL;
}

Schedule *schedule_build(const Planner *p, int start_day, int max_block)
{
    if (!p)
        return NULL;
    if (max_block <= 0)
        max_block = PLANNER_DEFAULT_BLOCK;

    /* Interleaving subjects is nicer to study, but it must never cost a
     * deadline. Strict earliest-deadline-first minimises lateness, so build
     * both and keep the interleaved plan only if it is no worse. */
    Schedule *mixed = build_schedule(p, start_day, max_block);
    Schedule *strict = build_schedule(p, start_day, INT_MAX);
    if (!mixed || !strict) {
        schedule_free(mixed);
        schedule_free(strict);
        return NULL;
    }
    if (strict->late_tasks < mixed->late_tasks) {
        schedule_free(mixed);
        return strict;
    }
    schedule_free(strict);
    return mixed;
}

void schedule_free(Schedule *s)
{
    if (!s)
        return;
    free(s->items);
    free(s);
}

/* ======================================================================== */
/* Persistence                                                              */
/* ======================================================================== */

const char *planner_strerror(PlannerStatus s)
{
    switch (s) {
    case PLANNER_OK:            return "ok";
    case PLANNER_ERR_ARG:       return "invalid argument";
    case PLANNER_ERR_NOMEM:     return "out of memory";
    case PLANNER_ERR_NOT_FOUND: return "not found";
    case PLANNER_ERR_IO:        return "file error";
    case PLANNER_ERR_FORMAT:    return "corrupt data file";
    }
    return "unknown error";
}

static void write_escaped(FILE *f, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '\\': fputs("\\\\", f); break;
        case '\t': fputs("\\t", f);  break;
        case '\n': fputs("\\n", f);  break;
        case '\r': fputs("\\r", f);  break;
        default:   fputc(*s, f);
        }
    }
}

PlannerStatus planner_save(const Planner *p, const char *path)
{
    if (!p || !path)
        return PLANNER_ERR_ARG;

    size_t plen = strlen(path);
    char *tmp = malloc(plen + 5);
    if (!tmp)
        return PLANNER_ERR_NOMEM;
    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".tmp", 5);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(tmp);
        return PLANNER_ERR_IO;
    }
    bool ok = fprintf(f, "%s\nS\t%d\t%d\n", DATA_MAGIC, p->daily_minutes, p->next_id) > 0;
    for (size_t i = 0; ok && i < p->count; i++) {
        const Task *t = &p->tasks[i];
        ok = fprintf(f, "T\t%d\t%d\t%d\t%d\t%d\t%d\t", t->id, (int)t->priority,
                     t->due == PLANNER_NO_DATE ? -1 : t->due, t->estimate_min,
                     t->spent_min, t->completed ? 1 : 0) > 0;
        write_escaped(f, t->title);
        fputc('\t', f);
        write_escaped(f, t->subject);
        ok = ok && fputc('\n', f) != EOF;
    }
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        free(tmp);
        return PLANNER_ERR_IO;
    }

#ifdef _WIN32
    ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    ok = rename(tmp, path) == 0;
#endif
    if (!ok)
        remove(tmp);
    free(tmp);
    return ok ? PLANNER_OK : PLANNER_ERR_IO;
}

/* In-place unescape of a field. Returns false on a dangling escape. */
static bool unescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r != '\\') {
            *w++ = *r;
            continue;
        }
        r++;
        switch (*r) {
        case '\\': *w++ = '\\'; break;
        case 't':  *w++ = '\t'; break;
        case 'n':  *w++ = '\n'; break;
        case 'r':  *w++ = '\r'; break;
        default:   return false;
        }
    }
    *w = '\0';
    return true;
}

/* Splits `line` on tabs in place; returns number of fields found (<= max). */
static int split_tabs(char *line, char **fields, int max)
{
    int n = 0;
    char *cur = line;
    while (n < max) {
        fields[n++] = cur;
        char *tab = strchr(cur, '\t');
        if (!tab)
            break;
        *tab = '\0';
        cur = tab + 1;
    }
    return n;
}

static bool parse_int(const char *s, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
        return false;
    *out = (int)v;
    return true;
}

PlannerStatus planner_load(Planner *p, const char *path)
{
    if (!p || !path)
        return PLANNER_ERR_ARG;

    FILE *f = fopen(path, "rb");
    if (!f)
        return PLANNER_ERR_NOT_FOUND;

    Planner *tmp = planner_new();
    if (!tmp) {
        fclose(f);
        return PLANNER_ERR_NOMEM;
    }

    PlannerStatus st = PLANNER_OK;
    char line[LINE_MAX_LEN];
    bool header = false;
    int max_id = 0;

    while (st == PLANNER_OK && fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            st = PLANNER_ERR_FORMAT; /* line too long */
            break;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;

        if (!header) {
            if (strcmp(line, DATA_MAGIC) != 0)
                st = PLANNER_ERR_FORMAT;
            header = true;
            continue;
        }

        char *fld[12];
        int nf = split_tabs(line, fld, 12);
        if (strcmp(fld[0], "S") == 0) {
            int daily, next;
            if (nf != 3 || !parse_int(fld[1], &daily) || !parse_int(fld[2], &next) ||
                planner_set_daily_minutes(tmp, daily) != PLANNER_OK || next < 1)
                st = PLANNER_ERR_FORMAT;
            else
                tmp->next_id = next;
        } else if (strcmp(fld[0], "T") == 0) {
            Task t;
            int pr, due, done;
            memset(&t, 0, sizeof t);
            if (nf != 9 || !parse_int(fld[1], &t.id) || !parse_int(fld[2], &pr) ||
                !parse_int(fld[3], &due) || !parse_int(fld[4], &t.estimate_min) ||
                !parse_int(fld[5], &t.spent_min) || !parse_int(fld[6], &done) ||
                !unescape(fld[7]) || !unescape(fld[8]) || t.id < 1 ||
                t.spent_min < 0 || (done != 0 && done != 1) ||
                !valid_task_fields(fld[7], (Priority)pr, t.estimate_min) ||
                find_index(tmp, t.id) >= 0) {
                st = PLANNER_ERR_FORMAT;
            } else {
                t.priority = (Priority)pr;
                t.due = due == -1 ? PLANNER_NO_DATE : due;
                t.completed = done == 1;
                copy_trimmed(t.title, sizeof t.title, fld[7]);
                copy_trimmed(t.subject, sizeof t.subject, fld[8]);
                st = push_task(tmp, &t);
                if (t.id > max_id)
                    max_id = t.id;
            }
        } else {
            st = PLANNER_ERR_FORMAT;
        }
    }
    if (st == PLANNER_OK && (ferror(f) || !header))
        st = ferror(f) ? PLANNER_ERR_IO : PLANNER_ERR_FORMAT;
    fclose(f);

    if (st == PLANNER_OK) {
        if (tmp->next_id <= max_id)
            tmp->next_id = max_id + 1;
        Planner swap = *p;
        *p = *tmp;
        *tmp = swap;
    }
    planner_free(tmp);
    return st;
}
