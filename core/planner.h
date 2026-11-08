/*
 * planner.h - Study Planner core logic (pure C, no GUI dependencies).
 *
 * Dates are represented as "day numbers": days since 1970-01-01 (proleptic
 * Gregorian calendar). This keeps all scheduling arithmetic free of time
 * zone and DST problems. PLANNER_NO_DATE means "no deadline".
 */
#ifndef STUDYPLANNER_PLANNER_H
#define STUDYPLANNER_PLANNER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLANNER_NO_DATE        (-2147483647 - 1)
#define PLANNER_MAX_TITLE      128
#define PLANNER_MAX_SUBJECT    64
#define PLANNER_DEFAULT_DAILY  180   /* minutes of study per day */
#define PLANNER_DEFAULT_BLOCK  50    /* max minutes of one task per day-block */
#define PLANNER_MAX_HORIZON    366   /* never schedule further than this many days */

typedef enum {
    PLANNER_OK = 0,
    PLANNER_ERR_ARG,        /* invalid argument */
    PLANNER_ERR_NOMEM,
    PLANNER_ERR_NOT_FOUND,
    PLANNER_ERR_IO,
    PLANNER_ERR_FORMAT      /* corrupt data file */
} PlannerStatus;

typedef enum {
    PRIORITY_LOW = 1,
    PRIORITY_MEDIUM = 2,
    PRIORITY_HIGH = 3
} Priority;

typedef struct {
    int      id;
    char     title[PLANNER_MAX_TITLE];
    char     subject[PLANNER_MAX_SUBJECT];
    Priority priority;
    int      due;             /* day number or PLANNER_NO_DATE */
    int      estimate_min;    /* total effort estimated, minutes (>0) */
    int      spent_min;       /* effort logged so far, minutes (>=0) */
    bool     completed;
} Task;

typedef struct {
    Task *tasks;
    size_t count;
    size_t capacity;
    int next_id;
    int daily_minutes;        /* study budget per day, minutes */
} Planner;

/* One block of work in a generated schedule. */
typedef struct {
    int  day;                 /* day number */
    int  task_id;
    int  minutes;
    bool late;                /* scheduled after the task's deadline */
} Session;

typedef struct {
    Session *items;
    size_t count;
    size_t capacity;
    int unscheduled_min;      /* work that did not fit inside the horizon */
    int late_tasks;           /* tasks that cannot finish before their deadline */
} Schedule;

typedef struct {
    int    total;
    int    completed;
    int    overdue;           /* open tasks whose due date has passed */
    int    due_soon;          /* open tasks due within 3 days (incl. today) */
    int    remaining_min;     /* sum of remaining effort of open tasks */
    int    spent_min;         /* sum of all logged effort */
    double percent_done;      /* effort-weighted completion, 0..100 */
} PlannerStats;

/* ---- dates ------------------------------------------------------------ */
int         date_from_ymd(int year, int month, int day);   /* PLANNER_NO_DATE if invalid */
void        date_to_ymd(int day, int *year, int *month, int *d);
int         date_weekday(int day);                          /* 0 = Sunday */
int         date_today(void);
/* Parses "YYYY-MM-DD". Returns PLANNER_NO_DATE on failure. */
int         date_parse(const char *text);
/* Formats into buf as "YYYY-MM-DD"; "" for PLANNER_NO_DATE. */
void        date_format(int day, char *buf, size_t len);

/* ---- lifecycle -------------------------------------------------------- */
Planner    *planner_new(void);
void        planner_free(Planner *p);

/* ---- task management -------------------------------------------------- */
/* Adds a task; stores new id in *out_id if non-NULL. */
PlannerStatus planner_add(Planner *p, const char *title, const char *subject,
                          Priority priority, int due, int estimate_min,
                          int *out_id);
PlannerStatus planner_update(Planner *p, int id, const char *title,
                             const char *subject, Priority priority, int due,
                             int estimate_min, int spent_min);
PlannerStatus planner_remove(Planner *p, int id);
PlannerStatus planner_set_completed(Planner *p, int id, bool completed);
/* Adds (or, if negative, subtracts) logged minutes; auto-completes when the
 * estimate is reached. */
PlannerStatus planner_log_time(Planner *p, int id, int minutes);
PlannerStatus planner_set_daily_minutes(Planner *p, int minutes);
Task       *planner_find(Planner *p, int id);
const Task *planner_find_const(const Planner *p, int id);

/* ---- analysis --------------------------------------------------------- */
int          task_remaining_min(const Task *t);
/* Urgency score: higher = do it sooner. Combines deadline closeness,
 * priority and remaining effort. Completed tasks score 0. */
double       task_urgency(const Task *t, int today);
/* Fills `order` (caller supplies room for p->count ints) with task indexes
 * sorted by: open before done, then urgency descending. */
void         planner_rank(const Planner *p, int today, size_t *order);
PlannerStats planner_stats(const Planner *p, int today);

/* ---- scheduling ------------------------------------------------------- */
/* Builds a study schedule starting at `start_day` using earliest-deadline-
 * first with priority tie-breaking. Each day, every task first gets up to
 * `max_block` minutes (<=0 selects the default) so subjects are interleaved;
 * leftover capacity is then filled in the same order. There is at most one
 * session per task per day and a day never exceeds p->daily_minutes.
 * Returns NULL on allocation failure. */
Schedule    *schedule_build(const Planner *p, int start_day, int max_block);
void         schedule_free(Schedule *s);
/* ---- persistence ------------------------------------------------------ */
PlannerStatus planner_save(const Planner *p, const char *path);
/* Replaces the content of *p. If the file does not exist, returns
 * PLANNER_ERR_NOT_FOUND and leaves *p untouched. */
PlannerStatus planner_load(Planner *p, const char *path);

const char  *planner_strerror(PlannerStatus s);

#ifdef __cplusplus
}
#endif

#endif /* STUDYPLANNER_PLANNER_H */
