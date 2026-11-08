/* Unit tests for the Study Planner core. Build & run with `make test`. */
#include "../core/planner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failed++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static void test_dates(void)
{
    CHECK(date_from_ymd(1970, 1, 1) == 0);
    CHECK(date_from_ymd(2000, 3, 1) == 11017);
    CHECK(date_from_ymd(2024, 2, 29) != PLANNER_NO_DATE);
    CHECK(date_from_ymd(2023, 2, 29) == PLANNER_NO_DATE);
    CHECK(date_from_ymd(2100, 2, 29) == PLANNER_NO_DATE);
    CHECK(date_from_ymd(2024, 13, 1) == PLANNER_NO_DATE);
    CHECK(date_from_ymd(2024, 4, 31) == PLANNER_NO_DATE);

    /* round trip over many years */
    for (int d = -719000; d < 2930000; d += 997) {
        int y, m, dd;
        date_to_ymd(d, &y, &m, &dd);
        CHECK(date_from_ymd(y, m, dd) == d);
    }
    CHECK(date_weekday(0) == 4);                              /* Thu */
    CHECK(date_weekday(date_from_ymd(2026, 11, 8)) == 0);     /* Sun */
    CHECK(date_weekday(-1) == 3);                             /* Wed */

    char buf[16];
    date_format(date_from_ymd(2026, 3, 7), buf, sizeof buf);
    CHECK(strcmp(buf, "2026-03-07") == 0);
    date_format(PLANNER_NO_DATE, buf, sizeof buf);
    CHECK(buf[0] == '\0');

    CHECK(date_parse("2026-03-07") == date_from_ymd(2026, 3, 7));
    CHECK(date_parse("2026-3-7") == date_from_ymd(2026, 3, 7));
    CHECK(date_parse("2026-02-30") == PLANNER_NO_DATE);
    CHECK(date_parse("2026-03-07x") == PLANNER_NO_DATE);
    CHECK(date_parse("garbage") == PLANNER_NO_DATE);
    CHECK(date_parse("") == PLANNER_NO_DATE);
    CHECK(date_parse(NULL) == PLANNER_NO_DATE);
    CHECK(date_today() > date_from_ymd(2020, 1, 1));
}

static void test_task_management(void)
{
    Planner *p = planner_new();
    CHECK(p != NULL);
    int a, b;

    CHECK(planner_add(p, "  Read ch.1  ", " Math ", PRIORITY_HIGH, 100, 120, &a) == PLANNER_OK);
    CHECK(planner_add(p, "Essay", "", PRIORITY_LOW, PLANNER_NO_DATE, 60, &b) == PLANNER_OK);
    CHECK(a != b && p->count == 2);
    CHECK(strcmp(p->tasks[0].title, "Read ch.1") == 0);
    CHECK(strcmp(p->tasks[0].subject, "Math") == 0);

    /* invalid input */
    CHECK(planner_add(p, "", "x", PRIORITY_LOW, 0, 10, NULL) == PLANNER_ERR_ARG);
    CHECK(planner_add(p, "   ", "x", PRIORITY_LOW, 0, 10, NULL) == PLANNER_ERR_ARG);
    CHECK(planner_add(p, "t", "x", PRIORITY_LOW, 0, 0, NULL) == PLANNER_ERR_ARG);
    CHECK(planner_add(p, "t", "x", (Priority)9, 0, 10, NULL) == PLANNER_ERR_ARG);
    CHECK(planner_add(NULL, "t", "x", PRIORITY_LOW, 0, 10, NULL) == PLANNER_ERR_ARG);
    CHECK(p->count == 2);

    /* log time & auto-complete */
    CHECK(planner_log_time(p, a, 30) == PLANNER_OK);
    CHECK(planner_find(p, a)->spent_min == 30 && !planner_find(p, a)->completed);
    CHECK(task_remaining_min(planner_find(p, a)) == 90);
    CHECK(planner_log_time(p, a, 90) == PLANNER_OK);
    CHECK(planner_find(p, a)->completed);
    CHECK(task_remaining_min(planner_find(p, a)) == 0);
    CHECK(planner_log_time(p, a, -500) == PLANNER_OK);
    CHECK(planner_find(p, a)->spent_min == 0 && !planner_find(p, a)->completed);
    CHECK(planner_log_time(p, 9999, 5) == PLANNER_ERR_NOT_FOUND);

    /* completion toggle */
    CHECK(planner_set_completed(p, b, true) == PLANNER_OK);
    CHECK(planner_find(p, b)->spent_min == 60);
    CHECK(planner_set_completed(p, b, false) == PLANNER_OK);
    CHECK(!planner_find(p, b)->completed && task_remaining_min(planner_find(p, b)) > 0);

    /* update */
    CHECK(planner_update(p, a, "New", "Phys", PRIORITY_MEDIUM, 5, 200, 20) == PLANNER_OK);
    CHECK(strcmp(planner_find(p, a)->title, "New") == 0 && planner_find(p, a)->estimate_min == 200);
    CHECK(planner_update(p, a, "", "Phys", PRIORITY_MEDIUM, 5, 200, 20) == PLANNER_ERR_ARG);
    CHECK(planner_update(p, 424242, "x", "", PRIORITY_LOW, 0, 5, 0) == PLANNER_ERR_NOT_FOUND);

    /* remove keeps order and ids stable */
    CHECK(planner_remove(p, a) == PLANNER_OK);
    CHECK(p->count == 1 && p->tasks[0].id == b);
    CHECK(planner_remove(p, a) == PLANNER_ERR_NOT_FOUND);
    int c;
    CHECK(planner_add(p, "again", "", PRIORITY_LOW, 0, 5, &c) == PLANNER_OK && c != a && c != b);

    /* long titles are truncated safely */
    char big[600];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    CHECK(planner_add(p, big, big, PRIORITY_LOW, 0, 5, &c) == PLANNER_OK);
    CHECK(strlen(planner_find(p, c)->title) == PLANNER_MAX_TITLE - 1);
    CHECK(strlen(planner_find(p, c)->subject) == PLANNER_MAX_SUBJECT - 1);

    CHECK(planner_set_daily_minutes(p, 90) == PLANNER_OK && p->daily_minutes == 90);
    CHECK(planner_set_daily_minutes(p, -1) == PLANNER_ERR_ARG);
    CHECK(planner_set_daily_minutes(p, 24 * 60 + 1) == PLANNER_ERR_ARG);
    planner_free(p);
}

static void test_ranking_and_stats(void)
{
    Planner *p = planner_new();
    int today = date_from_ymd(2026, 6, 10);
    int far, soon, over, none, done;
    planner_add(p, "far", "", PRIORITY_MEDIUM, today + 30, 60, &far);
    planner_add(p, "soon", "", PRIORITY_MEDIUM, today + 1, 60, &soon);
    planner_add(p, "over", "", PRIORITY_MEDIUM, today - 2, 60, &over);
    planner_add(p, "none", "", PRIORITY_MEDIUM, PLANNER_NO_DATE, 60, &none);
    planner_add(p, "done", "", PRIORITY_HIGH, today, 60, &done);
    planner_set_completed(p, done, true);

    size_t order[5];
    planner_rank(p, today, order);
    CHECK(p->tasks[order[0]].id == over);
    CHECK(p->tasks[order[1]].id == soon);
    CHECK(p->tasks[order[4]].id == done);          /* completed sinks */
    CHECK(task_urgency(planner_find(p, done), today) == 0.0);

    /* priority matters when dates tie */
    int hi, lo;
    planner_add(p, "hi", "", PRIORITY_HIGH, today + 5, 60, &hi);
    planner_add(p, "lo", "", PRIORITY_LOW, today + 5, 60, &lo);
    CHECK(task_urgency(planner_find(p, hi), today) > task_urgency(planner_find(p, lo), today));

    PlannerStats s = planner_stats(p, today);
    CHECK(s.total == 7 && s.completed == 1);
    CHECK(s.overdue == 1);            /* "over" */
    CHECK(s.due_soon == 1);           /* "soon" (done task excluded) */
    CHECK(s.remaining_min == 6 * 60);
    CHECK(s.percent_done > 0 && s.percent_done < 100);

    planner_log_time(p, far, 30);
    PlannerStats s2 = planner_stats(p, today);
    CHECK(s2.percent_done > s.percent_done);
    CHECK(s2.spent_min == s.spent_min + 30);

    Planner *empty = planner_new();
    PlannerStats se = planner_stats(empty, today);
    CHECK(se.total == 0 && se.percent_done == 0.0);
    planner_free(empty);
    planner_free(p);
}

static int minutes_on(const Schedule *s, int day)
{
    int m = 0;
    for (size_t i = 0; i < s->count; i++)
        if (s->items[i].day == day)
            m += s->items[i].minutes;
    return m;
}

static int minutes_for(const Schedule *s, int id)
{
    int m = 0;
    for (size_t i = 0; i < s->count; i++)
        if (s->items[i].task_id == id)
            m += s->items[i].minutes;
    return m;
}

static void test_schedule(void)
{
    int today = date_from_ymd(2026, 6, 10);
    Planner *p = planner_new();
    planner_set_daily_minutes(p, 120);

    int a, b, c;
    planner_add(p, "A", "", PRIORITY_MEDIUM, today + 2, 200, &a);
    planner_add(p, "B", "", PRIORITY_HIGH, today + 5, 100, &b);
    planner_add(p, "C", "", PRIORITY_LOW, PLANNER_NO_DATE, 50, &c);

    Schedule *s = schedule_build(p, today, 50);
    CHECK(s != NULL);
    if (!s) { planner_free(p); return; }

    /* all work is placed, nothing exceeds the daily budget */
    CHECK(minutes_for(s, a) == 200 && minutes_for(s, b) == 100 && minutes_for(s, c) == 50);
    CHECK(s->unscheduled_min == 0);
    int last_day = 0;
    for (size_t i = 0; i < s->count; i++) {
        CHECK(minutes_on(s, s->items[i].day) <= 120);
        CHECK(s->items[i].minutes > 0);
        CHECK(s->items[i].day >= today);
        if (i > 0) CHECK(s->items[i].day >= s->items[i - 1].day);   /* sorted by day */
        last_day = s->items[i].day;
    }
    /* total 350 min at 120/day -> done within 3 days */
    CHECK(last_day == today + 2);
    CHECK(s->late_tasks == 0);
    /* A (earliest deadline) must be finished by its due date */
    for (size_t i = 0; i < s->count; i++)
        if (s->items[i].task_id == a)
            CHECK(s->items[i].day <= today + 2 && !s->items[i].late);
    /* one session per task per day */
    for (size_t i = 0; i < s->count; i++)
        for (size_t j = i + 1; j < s->count; j++)
            CHECK(!(s->items[i].day == s->items[j].day &&
                    s->items[i].task_id == s->items[j].task_id));
    schedule_free(s);

    /* infeasible: deadline tomorrow, 600 min of work, 60 min/day */
    planner_set_daily_minutes(p, 60);
    int d;
    planner_add(p, "Crunch", "", PRIORITY_HIGH, today + 1, 600, &d);
    s = schedule_build(p, today, 0);
    CHECK(s != NULL && s->late_tasks >= 1);
    bool saw_late = false;
    for (size_t i = 0; s && i < s->count; i++)
        if (s->items[i].task_id == d && s->items[i].late) saw_late = true;
    CHECK(saw_late);
    CHECK(s && minutes_for(s, d) == 600);
    schedule_free(s);

    /* interleaving must never cost a deadline that strict EDF can meet */
    Planner *g = planner_new();
    planner_set_daily_minutes(g, 100);
    int ga, gb;
    planner_add(g, "A", "", PRIORITY_LOW, today + 1, 200, &ga);
    planner_add(g, "B", "", PRIORITY_HIGH, today + 5, 100, &gb);
    Schedule *gs = schedule_build(g, today, 50);
    CHECK(gs && gs->late_tasks == 0);
    schedule_free(gs);
    planner_free(g);

    /* zero budget: nothing scheduled, everything reported unscheduled */
    planner_set_daily_minutes(p, 0);
    s = schedule_build(p, today, 0);
    CHECK(s && s->count == 0 && s->unscheduled_min == 200 + 100 + 50 + 600);
    schedule_free(s);

    /* completed tasks are ignored; empty planner works */
    Planner *e = planner_new();
    s = schedule_build(e, today, 0);
    CHECK(s && s->count == 0 && s->unscheduled_min == 0 && s->late_tasks == 0);
    schedule_free(s);
    int x;
    planner_add(e, "x", "", PRIORITY_LOW, today, 10, &x);
    planner_set_completed(e, x, true);
    s = schedule_build(e, today, 0);
    CHECK(s && s->count == 0);
    schedule_free(s);
    planner_free(e);

    /* a huge task is bounded by the horizon rather than looping forever */
    Planner *h = planner_new();
    planner_set_daily_minutes(h, 10);
    planner_add(h, "huge", "", PRIORITY_LOW, PLANNER_NO_DATE, 1000000, &x);
    s = schedule_build(h, today, 0);
    CHECK(s && s->count == PLANNER_MAX_HORIZON && s->unscheduled_min == 1000000 - 10 * PLANNER_MAX_HORIZON);
    schedule_free(s);
    planner_free(h);

    schedule_free(NULL);
    planner_free(p);
}

static void test_persistence(void)
{
    const char *path = "test_planner_tmp.dat";
    Planner *p = planner_new();
    int id;
    planner_set_daily_minutes(p, 135);
    planner_add(p, "Tab\there\\ and\nnewline", "Sub\tject", PRIORITY_HIGH, 20000, 90, &id);
    planner_log_time(p, id, 15);
    planner_add(p, "Ünïcode ✓ title", "Café", PRIORITY_LOW, PLANNER_NO_DATE, 45, &id);
    planner_set_completed(p, id, true);
    planner_remove(p, 1);   /* leave a gap in the ids */
    planner_add(p, "third", "", PRIORITY_MEDIUM, 5, 30, &id);

    CHECK(planner_save(p, path) == PLANNER_OK);

    Planner *q = planner_new();
    CHECK(planner_load(q, path) == PLANNER_OK);
    CHECK(q->count == p->count && q->daily_minutes == 135 && q->next_id == p->next_id);
    for (size_t i = 0; i < p->count && i < q->count; i++) {
        CHECK(p->tasks[i].id == q->tasks[i].id);
        CHECK(strcmp(p->tasks[i].title, q->tasks[i].title) == 0);
        CHECK(strcmp(p->tasks[i].subject, q->tasks[i].subject) == 0);
        CHECK(p->tasks[i].priority == q->tasks[i].priority);
        CHECK(p->tasks[i].due == q->tasks[i].due);
        CHECK(p->tasks[i].estimate_min == q->tasks[i].estimate_min);
        CHECK(p->tasks[i].spent_min == q->tasks[i].spent_min);
        CHECK(p->tasks[i].completed == q->tasks[i].completed);
    }
    /* overwrite works (rename over existing file) */
    CHECK(planner_save(q, path) == PLANNER_OK);

    /* missing file leaves planner untouched */
    size_t before = q->count;
    CHECK(planner_load(q, "definitely_missing_file.dat") == PLANNER_ERR_NOT_FOUND);
    CHECK(q->count == before);

    /* corrupt files are rejected and do not clobber the planner */
    const char *bad[] = {
        "NOT A PLANNER FILE\n",
        "STUDYPLANNER 1\nT\t1\t9\t-1\t10\t0\t0\tx\ty\n",          /* bad priority */
        "STUDYPLANNER 1\nT\t1\t1\t-1\t0\t0\t0\tx\ty\n",           /* zero estimate */
        "STUDYPLANNER 1\nT\t1\t1\t-1\t10\t0\t0\n",                /* too few fields */
        "STUDYPLANNER 1\nT\t1\t1\t-1\t10\t0\t0\tx\\q\ty\n",       /* bad escape */
        "STUDYPLANNER 1\nT\t1\t1\t-1\t10\t0\t0\tx\ty\nT\t1\t1\t-1\t10\t0\t0\tz\ty\n", /* dup id */
        "STUDYPLANNER 1\nQ\twhat\n",
        "",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        FILE *f = fopen(path, "wb");
        fputs(bad[i], f);
        fclose(f);
        CHECK(planner_load(q, path) == PLANNER_ERR_FORMAT);
        CHECK(q->count == before);
    }

    CHECK(planner_save(p, "no_such_dir_xyz/file.dat") == PLANNER_ERR_IO);
    CHECK(planner_save(NULL, path) == PLANNER_ERR_ARG);

    remove(path);
    planner_free(q);
    planner_free(p);
}

int main(void)
{
    test_dates();
    test_task_management();
    test_ranking_and_stats();
    test_schedule();
    test_persistence();

    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
