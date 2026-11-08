/*
 * main.c - GTK 4 front-end for Study Planner.
 *
 * This file contains presentation code only. All planning logic (tasks,
 * urgency ranking, scheduling, statistics, persistence) lives in core/ and is
 * plain C with no GUI dependency.
 */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include "../core/planner.h"
#include "style.h"

#define APP_ID "io.github.studyplanner"

/* ------------------------------------------------------------------------ */
/* Application state                                                        */
/* ------------------------------------------------------------------------ */

typedef struct {
    GtkApplication *gtk_app;
    GtkWindow      *window;
    Planner        *planner;
    char           *data_path;
    gboolean        persist;      /* false in --demo mode */

    /* stat cards */
    GtkWidget *stat_open, *stat_open_sub;
    GtkWidget *stat_progress, *stat_progress_bar;
    GtkWidget *stat_hours, *stat_hours_sub;
    GtkWidget *stat_overdue, *stat_overdue_card, *stat_overdue_sub;

    /* tasks page */
    GtkWidget *search_entry;
    GtkWidget *show_done;
    GtkWidget *task_list;
    GtkWidget *task_stack;        /* "list" | "empty" */
    GtkWidget *empty_title, *empty_sub;

    /* schedule page */
    GtkWidget *sched_box;

    GtkWidget *daily_spin;
    gboolean   updating_ui;       /* suppress feedback loops */
} App;

static void refresh_all(App *app);

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static GtkWidget *make_label(const char *text, const char *css, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (css)
        gtk_widget_add_css_class(l, css);
    return l;
}

/* "1h 30m", "45m", "2h", "0m". Result is newly allocated. */
static char *fmt_minutes(int m)
{
    if (m < 0)
        m = 0;
    if (m < 60)
        return g_strdup_printf("%dm", m);
    if (m % 60 == 0)
        return g_strdup_printf("%dh", m / 60);
    return g_strdup_printf("%dh %dm", m / 60, m % 60);
}

static char *fmt_day_long(int day)
{
    static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, m, d;
    date_to_ymd(day, &y, &m, &d);
    return g_strdup_printf("%s, %d %s", wd[date_weekday(day)], d, mn[m - 1]);
}

static void show_error(App *app, const char *what, PlannerStatus st)
{
    GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", what);
    gtk_alert_dialog_set_detail(dlg, planner_strerror(st));
    gtk_alert_dialog_show(dlg, app->window);
    g_object_unref(dlg);
}

static void save_planner(App *app)
{
    if (!app->persist)
        return;
    PlannerStatus st = planner_save(app->planner, app->data_path);
    if (st != PLANNER_OK)
        show_error(app, "Could not save your planner", st);
}

/* Persist and redraw after any change. */
static void commit(App *app)
{
    save_planner(app);
    refresh_all(app);
}

/* ------------------------------------------------------------------------ */
/* Add / edit dialog                                                        */
/* ------------------------------------------------------------------------ */

typedef struct {
    App       *app;
    int        task_id;           /* 0 = new task */
    GtkWidget *window;
    GtkWidget *title, *subject, *priority, *due;
    GtkWidget *est_h, *est_m, *spent_h, *spent_m;
    GtkWidget *error;
} TaskDialog;

static int spin_minutes(GtkWidget *h, GtkWidget *m)
{
    return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(h)) * 60 +
           gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m));
}

static void set_spin_minutes(GtkWidget *h, GtkWidget *m, int minutes)
{
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(h), minutes / 60);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m), minutes % 60);
}

static void dialog_set_error(TaskDialog *dlg, const char *msg)
{
    gtk_label_set_text(GTK_LABEL(dlg->error), msg);
    gtk_widget_set_visible(dlg->error, TRUE);
}

static void on_dialog_save(GtkButton *btn, gpointer data)
{
    (void)btn;
    TaskDialog *dlg = data;
    App *app = dlg->app;

    const char *title = gtk_editable_get_text(GTK_EDITABLE(dlg->title));
    const char *subject = gtk_editable_get_text(GTK_EDITABLE(dlg->subject));
    const char *due_text = gtk_editable_get_text(GTK_EDITABLE(dlg->due));
    Priority pr = (Priority)(gtk_drop_down_get_selected(GTK_DROP_DOWN(dlg->priority)) + 1);
    int est = spin_minutes(dlg->est_h, dlg->est_m);

    char *trimmed = g_strstrip(g_strdup(title));
    gboolean title_ok = trimmed[0] != '\0';
    g_free(trimmed);
    if (!title_ok) {
        dialog_set_error(dlg, "Please give the task a title.");
        gtk_widget_grab_focus(dlg->title);
        return;
    }
    if (est <= 0) {
        dialog_set_error(dlg, "Estimated time must be greater than zero.");
        return;
    }
    int due = PLANNER_NO_DATE;
    char *due_trim = g_strstrip(g_strdup(due_text));
    if (due_trim[0] != '\0') {
        due = date_parse(due_trim);
        if (due == PLANNER_NO_DATE) {
            g_free(due_trim);
            dialog_set_error(dlg, "Deadline must look like 2026-12-31 (or be empty).");
            return;
        }
    }
    g_free(due_trim);

    PlannerStatus st;
    if (dlg->task_id == 0)
        st = planner_add(app->planner, title, subject, pr, due, est, NULL);
    else
        st = planner_update(app->planner, dlg->task_id, title, subject, pr, due, est,
                            spin_minutes(dlg->spent_h, dlg->spent_m));
    if (st != PLANNER_OK) {
        dialog_set_error(dlg, planner_strerror(st));
        return;
    }
    gtk_window_destroy(GTK_WINDOW(dlg->window));
    commit(app);
}

static void on_dialog_cancel(GtkButton *btn, gpointer data)
{
    (void)btn;
    TaskDialog *dlg = data;
    gtk_window_destroy(GTK_WINDOW(dlg->window));
}

static void on_dialog_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    g_free(data);
}

static void on_calendar_day(GtkCalendar *cal, gpointer data)
{
    TaskDialog *dlg = data;
    GDateTime *dt = gtk_calendar_get_date(cal);
    char *s = g_date_time_format(dt, "%Y-%m-%d");
    gtk_editable_set_text(GTK_EDITABLE(dlg->due), s);
    g_free(s);
    g_date_time_unref(dt);
}

static GtkWidget *form_row(GtkWidget *grid, int row, const char *label, GtkWidget *w)
{
    gtk_grid_attach(GTK_GRID(grid), make_label(label, "form-label", 0), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
    return w;
}

static GtkWidget *duration_widget(GtkWidget **h, GtkWidget **m)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    *h = gtk_spin_button_new_with_range(0, 500, 1);
    *m = gtk_spin_button_new_with_range(0, 59, 5);
    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(*m), 5, 15);
    gtk_box_append(GTK_BOX(box), *h);
    gtk_box_append(GTK_BOX(box), make_label("h", NULL, 0));
    gtk_box_append(GTK_BOX(box), *m);
    gtk_box_append(GTK_BOX(box), make_label("min", NULL, 0));
    return box;
}

static void open_task_dialog(App *app, int task_id)
{
    const Task *existing = task_id ? planner_find_const(app->planner, task_id) : NULL;
    if (task_id && !existing)
        return;

    TaskDialog *dlg = g_new0(TaskDialog, 1);
    dlg->app = app;
    dlg->task_id = task_id;

    dlg->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg->window), task_id ? "Edit task" : "New task");
    gtk_window_set_modal(GTK_WINDOW(dlg->window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dlg->window), app->window);
    gtk_window_set_resizable(GTK_WINDOW(dlg->window), FALSE);
    g_signal_connect(dlg->window, "destroy", G_CALLBACK(on_dialog_destroy), dlg);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(root, 22);
    gtk_widget_set_margin_bottom(root, 20);
    gtk_widget_set_margin_start(root, 24);
    gtk_widget_set_margin_end(root, 24);
    gtk_window_set_child(GTK_WINDOW(dlg->window), root);

    gtk_box_append(GTK_BOX(root),
                   make_label(task_id ? "Edit task" : "What do you need to study?",
                              "dialog-heading", 0));

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_box_append(GTK_BOX(root), grid);

    int row = 0;
    dlg->title = form_row(grid, row++, "TITLE", gtk_entry_new());
    gtk_widget_set_size_request(dlg->title, 340, -1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(dlg->title), "e.g. Revise chapter 4");
    gtk_entry_set_max_length(GTK_ENTRY(dlg->title), PLANNER_MAX_TITLE - 1);
    gtk_entry_set_activates_default(GTK_ENTRY(dlg->title), TRUE);

    dlg->subject = form_row(grid, row++, "SUBJECT", gtk_entry_new());
    gtk_entry_set_placeholder_text(GTK_ENTRY(dlg->subject), "e.g. Physics");
    gtk_entry_set_max_length(GTK_ENTRY(dlg->subject), PLANNER_MAX_SUBJECT - 1);

    const char *prios[] = {"Low", "Medium", "High", NULL};
    dlg->priority = form_row(grid, row++, "PRIORITY", gtk_drop_down_new_from_strings(prios));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dlg->priority), 1);

    /* deadline: text entry + calendar popover */
    GtkWidget *due_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    dlg->due = gtk_entry_new();
    gtk_widget_set_hexpand(dlg->due, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(dlg->due), "YYYY-MM-DD (optional)");
    GtkWidget *cal_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(cal_btn), "x-office-calendar-symbolic");
    GtkWidget *pop = gtk_popover_new();
    GtkWidget *cal = gtk_calendar_new();
    gtk_popover_set_child(GTK_POPOVER(pop), cal);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(cal_btn), pop);
    g_signal_connect(cal, "day-selected", G_CALLBACK(on_calendar_day), dlg);
    gtk_box_append(GTK_BOX(due_box), dlg->due);
    gtk_box_append(GTK_BOX(due_box), cal_btn);
    form_row(grid, row++, "DEADLINE", due_box);

    form_row(grid, row++, "ESTIMATE", duration_widget(&dlg->est_h, &dlg->est_m));
    set_spin_minutes(dlg->est_h, dlg->est_m, 60);

    if (existing) {
        form_row(grid, row++, "TIME SPENT", duration_widget(&dlg->spent_h, &dlg->spent_m));
        gtk_editable_set_text(GTK_EDITABLE(dlg->title), existing->title);
        gtk_editable_set_text(GTK_EDITABLE(dlg->subject), existing->subject);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dlg->priority), (guint)existing->priority - 1);
        if (existing->due != PLANNER_NO_DATE) {
            char buf[16];
            date_format(existing->due, buf, sizeof buf);
            gtk_editable_set_text(GTK_EDITABLE(dlg->due), buf);
            int y, m, d;
            date_to_ymd(existing->due, &y, &m, &d);
            GDateTime *dt = g_date_time_new_local(y, m, d, 0, 0, 0);
            if (dt) {
                gtk_calendar_set_date(GTK_CALENDAR(cal), dt);
                g_date_time_unref(dt);
            }
            /* selecting a day fires the handler; keep the original text */
            gtk_editable_set_text(GTK_EDITABLE(dlg->due), buf);
        }
        set_spin_minutes(dlg->est_h, dlg->est_m, existing->estimate_min);
        set_spin_minutes(dlg->spent_h, dlg->spent_m, existing->spent_min);
    }

    dlg->error = make_label("", "form-error", 0);
    gtk_widget_set_visible(dlg->error, FALSE);
    gtk_box_append(GTK_BOX(root), dlg->error);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btns, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *save = gtk_button_new_with_label(task_id ? "Save changes" : "Add task");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(btns), cancel);
    gtk_box_append(GTK_BOX(btns), save);
    gtk_box_append(GTK_BOX(root), btns);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_dialog_cancel), dlg);
    g_signal_connect(save, "clicked", G_CALLBACK(on_dialog_save), dlg);
    gtk_window_set_default_widget(GTK_WINDOW(dlg->window), save);

    gtk_window_present(GTK_WINDOW(dlg->window));
    gtk_widget_grab_focus(dlg->title);
}

/* ------------------------------------------------------------------------ */
/* Task rows                                                                */
/* ------------------------------------------------------------------------ */

static void on_task_toggled(GtkCheckButton *chk, gpointer data)
{
    App *app = g_object_get_data(G_OBJECT(chk), "app");
    int id = GPOINTER_TO_INT(data);
    planner_set_completed(app->planner, id, gtk_check_button_get_active(chk));
    commit(app);
}

static void on_task_log(GtkButton *btn, gpointer data)
{
    App *app = g_object_get_data(G_OBJECT(btn), "app");
    planner_log_time(app->planner, GPOINTER_TO_INT(data), 30);
    commit(app);
}

static void on_task_edit(GtkButton *btn, gpointer data)
{
    App *app = g_object_get_data(G_OBJECT(btn), "app");
    open_task_dialog(app, GPOINTER_TO_INT(data));
}

static void on_delete_answer(GObject *src, GAsyncResult *res, gpointer data)
{
    App *app = g_object_get_data(G_OBJECT(src), "app");
    GError *err = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &err);
    g_clear_error(&err);
    if (choice == 1) {
        planner_remove(app->planner, GPOINTER_TO_INT(data));
        commit(app);
    }
}

static void on_task_delete(GtkButton *btn, gpointer data)
{
    App *app = g_object_get_data(G_OBJECT(btn), "app");
    const Task *t = planner_find_const(app->planner, GPOINTER_TO_INT(data));
    if (!t)
        return;
    GtkAlertDialog *dlg = gtk_alert_dialog_new("Delete \"%s\"?", t->title);
    gtk_alert_dialog_set_detail(dlg, "This task and its logged study time will be removed.");
    const char *buttons[] = {"Cancel", "Delete", NULL};
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_default_button(dlg, 0);
    g_object_set_data(G_OBJECT(dlg), "app", app);
    gtk_alert_dialog_choose(dlg, app->window, NULL, on_delete_answer, data);
    g_object_unref(dlg);
}

static GtkWidget *icon_button(App *app, const char *icon, const char *tip, int id,
                              GCallback cb)
{
    GtkWidget *b = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_set_tooltip_text(b, tip);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(b), "app", app);
    g_signal_connect(b, "clicked", cb, GINT_TO_POINTER(id));
    return b;
}

static GtkWidget *build_due_label(const Task *t, int today)
{
    char text[96];
    const char *css = NULL;

    if (t->due == PLANNER_NO_DATE) {
        g_snprintf(text, sizeof text, "No deadline");
    } else {
        int left = t->due - today;
        char *dl = fmt_day_long(t->due);
        if (t->completed)
            g_snprintf(text, sizeof text, "Due %s", dl);
        else if (left < 0)
            g_snprintf(text, sizeof text, "Overdue by %d day%s · %s", -left,
                       left == -1 ? "" : "s", dl);
        else if (left == 0)
            g_snprintf(text, sizeof text, "Due today · %s", dl);
        else if (left == 1)
            g_snprintf(text, sizeof text, "Due tomorrow · %s", dl);
        else
            g_snprintf(text, sizeof text, "Due in %d days · %s", left, dl);
        g_free(dl);
        if (t->completed)
            css = "done";
        else if (left < 0)
            css = "overdue";
        else if (left < 3)
            css = "soon";
    }
    GtkWidget *l = make_label(text, "due", 0);
    if (css)
        gtk_widget_add_css_class(l, css);
    return l;
}

static GtkWidget *build_task_row(App *app, const Task *t, int today)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(row, "task-row");
    gtk_widget_add_css_class(row, t->priority == PRIORITY_HIGH   ? "prio-high"
                                  : t->priority == PRIORITY_LOW ? "prio-low"
                                                                : "prio-medium");

    GtkWidget *chk = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk), t->completed);
    gtk_widget_set_valign(chk, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(chk, "Mark as done");
    g_object_set_data(G_OBJECT(chk), "app", app);
    g_signal_connect(chk, "toggled", G_CALLBACK(on_task_toggled), GINT_TO_POINTER(t->id));
    gtk_box_append(GTK_BOX(row), chk);

    /* title + meta */
    GtkWidget *mid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(mid, TRUE);
    gtk_widget_set_valign(mid, GTK_ALIGN_CENTER);
    GtkWidget *title = make_label(t->title, "task-title", 0);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    if (t->completed)
        gtk_widget_add_css_class(title, "done");
    gtk_box_append(GTK_BOX(mid), title);

    GtkWidget *meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    if (t->subject[0]) {
        GtkWidget *chip = make_label(t->subject, "chip", 0.5f);
        gtk_label_set_ellipsize(GTK_LABEL(chip), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(chip), 22);
        gtk_box_append(GTK_BOX(meta), chip);
    }
    gtk_box_append(GTK_BOX(meta), build_due_label(t, today));
    gtk_box_append(GTK_BOX(mid), meta);
    gtk_box_append(GTK_BOX(row), mid);

    /* progress */
    GtkWidget *prog = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(prog, 170, -1);
    gtk_widget_set_valign(prog, GTK_ALIGN_CENTER);
    char *spent = fmt_minutes(t->completed ? t->estimate_min : t->spent_min);
    char *est = fmt_minutes(t->estimate_min);
    char *ptxt = g_strdup_printf("%s of %s", spent, est);
    gtk_box_append(GTK_BOX(prog), make_label(ptxt, "task-progress-label", 0));
    g_free(spent);
    g_free(est);
    g_free(ptxt);
    GtkWidget *bar = gtk_progress_bar_new();
    double frac = t->completed ? 1.0 : (double)t->spent_min / t->estimate_min;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), frac > 1.0 ? 1.0 : frac);
    if (t->completed)
        gtk_widget_add_css_class(bar, "complete");
    gtk_box_append(GTK_BOX(prog), bar);
    gtk_box_append(GTK_BOX(row), prog);

    /* actions */
    if (!t->completed) {
        GtkWidget *log = gtk_button_new_with_label("+30 min");
        gtk_widget_add_css_class(log, "pill");
        gtk_widget_set_valign(log, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(log, "Log 30 minutes of study");
        g_object_set_data(G_OBJECT(log), "app", app);
        g_signal_connect(log, "clicked", G_CALLBACK(on_task_log), GINT_TO_POINTER(t->id));
        gtk_box_append(GTK_BOX(row), log);
    }
    gtk_box_append(GTK_BOX(row), icon_button(app, "document-edit-symbolic", "Edit", t->id,
                                             G_CALLBACK(on_task_edit)));
    gtk_box_append(GTK_BOX(row), icon_button(app, "user-trash-symbolic", "Delete", t->id,
                                             G_CALLBACK(on_task_delete)));
    return row;
}

static gboolean matches_search(const Task *t, const char *needle)
{
    if (!needle || !*needle)
        return TRUE;
    char *n = g_utf8_casefold(needle, -1);
    char *a = g_utf8_casefold(t->title, -1);
    char *b = g_utf8_casefold(t->subject, -1);
    gboolean hit = strstr(a, n) || strstr(b, n);
    g_free(n);
    g_free(a);
    g_free(b);
    return hit;
}

static void refresh_tasks(App *app, int today)
{
    gtk_list_box_remove_all(GTK_LIST_BOX(app->task_list));

    const Planner *p = app->planner;
    const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->search_entry));
    gboolean show_done = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->show_done));

    size_t *order = p->count ? g_new(size_t, p->count) : NULL;
    if (order)
        planner_rank(p, today, order);

    int shown = 0;
    for (size_t i = 0; order && i < p->count; i++) {
        const Task *t = &p->tasks[order[i]];
        if (t->completed && !show_done)
            continue;
        if (!matches_search(t, needle))
            continue;
        gtk_list_box_append(GTK_LIST_BOX(app->task_list), build_task_row(app, t, today));
        shown++;
    }
    g_free(order);

    if (shown > 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->task_stack), "list");
    } else {
        gboolean filtering = (needle && *needle) || p->count > 0;
        gtk_label_set_text(GTK_LABEL(app->empty_title),
                           filtering ? "Nothing to show" : "No tasks yet");
        gtk_label_set_text(GTK_LABEL(app->empty_sub),
                           filtering ? "Everything is done, or no task matches your search."
                                     : "Add your first task and the planner will build a schedule for you.");
        gtk_stack_set_visible_child_name(GTK_STACK(app->task_stack), "empty");
    }
}

/* ------------------------------------------------------------------------ */
/* Stats                                                                    */
/* ------------------------------------------------------------------------ */

static void refresh_stats(App *app, int today)
{
    PlannerStats s = planner_stats(app->planner, today);
    char *t;

    t = g_strdup_printf("%d", s.total - s.completed);
    gtk_label_set_text(GTK_LABEL(app->stat_open), t);
    g_free(t);
    t = g_strdup_printf("%d done · %d due soon", s.completed, s.due_soon);
    gtk_label_set_text(GTK_LABEL(app->stat_open_sub), t);
    g_free(t);

    t = g_strdup_printf("%.0f%%", s.percent_done);
    gtk_label_set_text(GTK_LABEL(app->stat_progress), t);
    g_free(t);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->stat_progress_bar), s.percent_done / 100.0);

    t = fmt_minutes(s.remaining_min);
    gtk_label_set_text(GTK_LABEL(app->stat_hours), t);
    g_free(t);
    char *sp = fmt_minutes(s.spent_min);
    t = g_strdup_printf("%s studied so far", sp);
    gtk_label_set_text(GTK_LABEL(app->stat_hours_sub), t);
    g_free(t);
    g_free(sp);

    t = g_strdup_printf("%d", s.overdue);
    gtk_label_set_text(GTK_LABEL(app->stat_overdue), t);
    g_free(t);
    gtk_label_set_text(GTK_LABEL(app->stat_overdue_sub),
                       s.overdue ? "needs attention" : "all on time");
    if (s.overdue) {
        gtk_widget_add_css_class(app->stat_overdue_card, "danger");
        gtk_widget_remove_css_class(app->stat_overdue_card, "good");
    } else {
        gtk_widget_remove_css_class(app->stat_overdue_card, "danger");
        gtk_widget_add_css_class(app->stat_overdue_card, "good");
    }
}

/* ------------------------------------------------------------------------ */
/* Schedule page                                                            */
/* ------------------------------------------------------------------------ */

static void on_session_done(GtkButton *btn, gpointer data)
{
    App *app = g_object_get_data(G_OBJECT(btn), "app");
    int minutes = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "minutes"));
    planner_log_time(app->planner, GPOINTER_TO_INT(data), minutes);
    commit(app);
}

static GtkWidget *build_banner(const char *text, const char *css)
{
    GtkWidget *l = make_label(text, NULL, 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "banner");
    if (css)
        gtk_widget_add_css_class(box, css);
    gtk_box_append(GTK_BOX(box), l);
    return box;
}

static GtkWidget *build_day_card(App *app, const Schedule *s, size_t first, size_t last,
                                 int today)
{
    int day = s->items[first].day;
    int total = 0;
    for (size_t i = first; i < last; i++)
        total += s->items[i].minutes;

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(card, "day-card");
    if (day == today)
        gtk_widget_add_css_class(card, "today");

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    char *dl = fmt_day_long(day);
    char *title = day == today       ? g_strdup_printf("Today · %s", dl)
                  : day == today + 1 ? g_strdup_printf("Tomorrow · %s", dl)
                                     : g_strdup(dl);
    GtkWidget *tl = make_label(title, "day-title", 0);
    gtk_widget_set_hexpand(tl, TRUE);
    gtk_box_append(GTK_BOX(head), tl);
    char *tot = fmt_minutes(total);
    gtk_box_append(GTK_BOX(head), make_label(tot, "day-total", 1));
    gtk_box_append(GTK_BOX(card), head);
    g_free(dl);
    g_free(title);
    g_free(tot);

    for (size_t i = first; i < last; i++) {
        const Session *ss = &s->items[i];
        const Task *t = planner_find_const(app->planner, ss->task_id);
        if (!t)
            continue;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "session-row");
        char *m = fmt_minutes(ss->minutes);
        gtk_box_append(GTK_BOX(row), make_label(m, "session-time", 0.5f));
        g_free(m);

        GtkWidget *name = make_label(t->title, NULL, 0);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_box_append(GTK_BOX(row), name);
        if (t->subject[0])
            gtk_box_append(GTK_BOX(row), make_label(t->subject, "chip", 0.5f));
        if (ss->late)
            gtk_box_append(GTK_BOX(row), make_label("after deadline", "late-tag", 0.5f));

        if (day == today) {
            GtkWidget *done = gtk_button_new_with_label("Done");
            gtk_widget_add_css_class(done, "pill");
            gtk_widget_set_tooltip_text(done, "Log this session as studied");
            g_object_set_data(G_OBJECT(done), "app", app);
            g_object_set_data(G_OBJECT(done), "minutes", GINT_TO_POINTER(ss->minutes));
            g_signal_connect(done, "clicked", G_CALLBACK(on_session_done),
                             GINT_TO_POINTER(ss->task_id));
            gtk_box_append(GTK_BOX(row), done);
        }
        gtk_box_append(GTK_BOX(card), row);
    }
    return card;
}

#define SCHEDULE_DAYS_SHOWN 21

static void refresh_schedule(App *app, int today)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(app->sched_box)) != NULL)
        gtk_box_remove(GTK_BOX(app->sched_box), child);

    Schedule *s = schedule_build(app->planner, today, 0);
    if (!s) {
        gtk_box_append(GTK_BOX(app->sched_box),
                       build_banner("Could not build a schedule (out of memory).", "warn"));
        return;
    }

    char *msg;
    if (s->count == 0 && s->unscheduled_min == 0) {
        gtk_box_append(GTK_BOX(app->sched_box),
                       build_banner("Nothing to schedule. Add a task and your study plan appears here.",
                                    NULL));
    } else if (app->planner->daily_minutes == 0) {
        gtk_box_append(GTK_BOX(app->sched_box),
                       build_banner("Your daily study time is 0. Raise it in the top bar to get a plan.",
                                    "warn"));
    } else if (s->late_tasks > 0) {
        msg = g_strdup_printf("%d task%s can't be finished before %s deadline at your current daily "
                              "study time. Raise the daily hours or move the deadline%s.",
                              s->late_tasks, s->late_tasks == 1 ? "" : "s",
                              s->late_tasks == 1 ? "its" : "their", s->late_tasks == 1 ? "" : "s");
        gtk_box_append(GTK_BOX(app->sched_box), build_banner(msg, "warn"));
        g_free(msg);
    } else {
        char *end = fmt_day_long(s->items[s->count - 1].day);
        msg = g_strdup_printf("On track — everything fits before its deadline. Last session: %s.", end);
        gtk_box_append(GTK_BOX(app->sched_box), build_banner(msg, "good"));
        g_free(msg);
        g_free(end);
    }

    int days = 0;
    size_t i = 0;
    while (i < s->count && days < SCHEDULE_DAYS_SHOWN) {
        size_t j = i;
        while (j < s->count && s->items[j].day == s->items[i].day)
            j++;
        gtk_box_append(GTK_BOX(app->sched_box), build_day_card(app, s, i, j, today));
        days++;
        i = j;
    }
    if (i < s->count) {
        msg = g_strdup_printf("Showing the next %d study days.", SCHEDULE_DAYS_SHOWN);
        gtk_box_append(GTK_BOX(app->sched_box), make_label(msg, "empty-sub", 0.5f));
        g_free(msg);
    }
    schedule_free(s);
}

/* ------------------------------------------------------------------------ */
/* Refresh                                                                  */
/* ------------------------------------------------------------------------ */

static void refresh_all(App *app)
{
    int today = date_today();
    app->updating_ui = TRUE;
    refresh_stats(app, today);
    refresh_tasks(app, today);
    refresh_schedule(app, today);
    app->updating_ui = FALSE;
}

static void on_filter_changed(GtkWidget *w, gpointer data)
{
    (void)w;
    refresh_all(data);
}

static void on_daily_changed(GtkSpinButton *spin, gpointer data)
{
    App *app = data;
    if (app->updating_ui)
        return;
    int minutes = (int)lround(gtk_spin_button_get_value(spin) * 60.0);
    if (planner_set_daily_minutes(app->planner, minutes) == PLANNER_OK)
        commit(app);
}

static void on_add_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    open_task_dialog(data, 0);
}

/* ------------------------------------------------------------------------ */
/* Window construction                                                      */
/* ------------------------------------------------------------------------ */

static GtkWidget *make_stat_card(const char *caption, GtkWidget **value, GtkWidget **sub,
                                 const char *css)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(card, "stat-card");
    if (css)
        gtk_widget_add_css_class(card, css);
    gtk_widget_set_hexpand(card, TRUE);
    gtk_box_append(GTK_BOX(card), make_label(caption, "stat-label", 0));
    *value = make_label("0", "stat-value", 0);
    gtk_box_append(GTK_BOX(card), *value);
    if (sub) {
        *sub = make_label("", "stat-sub", 0);
        gtk_box_append(GTK_BOX(card), *sub);
    }
    return card;
}

static GtkWidget *wrap_scroller(GtkWidget *child)
{
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), child);
    gtk_widget_set_vexpand(sw, TRUE);
    return sw;
}

static GtkWidget *build_tasks_page(App *app)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app->search_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(app->search_entry, TRUE);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(app->search_entry),
                                          "Search tasks or subjects");
    g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(on_filter_changed), app);
    app->show_done = gtk_check_button_new_with_label("Show completed");
    g_signal_connect(app->show_done, "toggled", G_CALLBACK(on_filter_changed), app);
    gtk_box_append(GTK_BOX(bar), app->search_entry);
    gtk_box_append(GTK_BOX(bar), app->show_done);
    gtk_box_append(GTK_BOX(page), bar);

    app->task_stack = gtk_stack_new();
    gtk_widget_set_vexpand(app->task_stack, TRUE);

    app->task_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->task_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(app->task_list, "task-list");
    gtk_stack_add_named(GTK_STACK(app->task_stack), wrap_scroller(app->task_list), "list");

    GtkWidget *empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
    app->empty_title = make_label("No tasks yet", "empty-title", 0.5f);
    app->empty_sub = make_label("", "empty-sub", 0.5f);
    gtk_box_append(GTK_BOX(empty), app->empty_title);
    gtk_box_append(GTK_BOX(empty), app->empty_sub);
    gtk_stack_add_named(GTK_STACK(app->task_stack), empty, "empty");

    gtk_box_append(GTK_BOX(page), app->task_stack);
    return page;
}

static GtkWidget *build_schedule_page(App *app)
{
    app->sched_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_end(app->sched_box, 6);
    return wrap_scroller(app->sched_box);
}

static void build_window(App *app)
{
    GtkWidget *win = gtk_application_window_new(app->gtk_app);
    app->window = GTK_WINDOW(win);
    gtk_window_set_title(app->window, "Study Planner");
    gtk_window_set_default_size(app->window, 1020, 740);
    gtk_widget_add_css_class(win, "sp-window");

    /* header bar */
    GtkWidget *header = gtk_header_bar_new();
    gtk_window_set_titlebar(app->window, header);

    GtkWidget *add = gtk_button_new_with_label("＋ New task");
    gtk_widget_add_css_class(add, "suggested-action");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_clicked), app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), add);

    GtkWidget *daily = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(daily), make_label("Study per day", NULL, 1));
    app->daily_spin = gtk_spin_button_new_with_range(0, 16, 0.5);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->daily_spin), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->daily_spin), app->planner->daily_minutes / 60.0);
    gtk_widget_set_tooltip_text(app->daily_spin, "Hours you can study each day");
    g_signal_connect(app->daily_spin, "value-changed", G_CALLBACK(on_daily_changed), app);
    gtk_box_append(GTK_BOX(daily), app->daily_spin);
    gtk_box_append(GTK_BOX(daily), make_label("h", NULL, 0));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), daily);

    /* body */
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(body, 18);
    gtk_widget_set_margin_bottom(body, 18);
    gtk_widget_set_margin_start(body, 22);
    gtk_widget_set_margin_end(body, 22);
    gtk_window_set_child(app->window, body);

    GtkWidget *cards = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_box_set_homogeneous(GTK_BOX(cards), TRUE);
    gtk_box_append(GTK_BOX(cards),
                   make_stat_card("OPEN TASKS", &app->stat_open, &app->stat_open_sub, "accent"));

    GtkWidget *prog_card = make_stat_card("OVERALL PROGRESS", &app->stat_progress, NULL, NULL);
    app->stat_progress_bar = gtk_progress_bar_new();
    gtk_widget_set_margin_top(app->stat_progress_bar, 6);
    gtk_box_append(GTK_BOX(prog_card), app->stat_progress_bar);
    gtk_box_append(GTK_BOX(cards), prog_card);

    gtk_box_append(GTK_BOX(cards),
                   make_stat_card("TIME LEFT", &app->stat_hours, &app->stat_hours_sub, NULL));
    app->stat_overdue_card = make_stat_card("OVERDUE", &app->stat_overdue, &app->stat_overdue_sub, "good");
    gtk_box_append(GTK_BOX(cards), app->stat_overdue_card);
    gtk_box_append(GTK_BOX(body), cards);

    /* pages */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_stack_add_titled(GTK_STACK(stack), build_tasks_page(app), "tasks", "Tasks");
    gtk_stack_add_titled(GTK_STACK(stack), build_schedule_page(app), "schedule", "Study plan");

    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), switcher);
    gtk_box_append(GTK_BOX(body), stack);

    refresh_all(app);
}

/* ------------------------------------------------------------------------ */
/* Demo data & startup                                                      */
/* ------------------------------------------------------------------------ */

static void load_demo(Planner *p)
{
    int t = date_today();
    planner_set_daily_minutes(p, 240);
    int id;
    planner_add(p, "Calculus problem set 5", "Math", PRIORITY_HIGH, t + 1, 150, &id);
    planner_log_time(p, id, 45);
    planner_add(p, "Revise organic chemistry reactions", "Chemistry", PRIORITY_HIGH, t + 3, 240, &id);
    planner_log_time(p, id, 60);
    planner_add(p, "Lab report: titration", "Chemistry", PRIORITY_MEDIUM, t + 6, 180, NULL);
    planner_add(p, "Read chapters 7–9", "History", PRIORITY_MEDIUM, t + 9, 210, &id);
    planner_log_time(p, id, 30);
    planner_add(p, "Essay outline: causes of WWI", "History", PRIORITY_LOW, t + 12, 90, NULL);
    planner_add(p, "Practice past paper", "Physics", PRIORITY_HIGH, t - 1, 120, &id);
    planner_log_time(p, id, 30);
    planner_add(p, "Flashcards: vocabulary unit 4", "Spanish", PRIORITY_LOW, PLANNER_NO_DATE, 60, &id);
    planner_log_time(p, id, 60);
    planner_add(p, "Summarise lecture notes", "Physics", PRIORITY_MEDIUM, t - 3, 60, &id);
    planner_set_completed(p, id, TRUE);
}

static void on_activate(GtkApplication *gapp, gpointer data)
{
    App *app = data;
    app->gtk_app = gapp;

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, STYLE_CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    build_window(app);
    gtk_window_present(app->window);
}

int main(int argc, char **argv)
{
    App app;
    memset(&app, 0, sizeof app);
    app.persist = TRUE;

    /* Option parsing is done before GTK sees argv. */
    const char *data_override = g_getenv("STUDYPLANNER_DATA");
    gboolean demo = FALSE;
    int out = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0)
            demo = TRUE;
        else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            data_override = argv[++i];
        else
            argv[out++] = argv[i];
    }
    argc = out;

    app.planner = planner_new();
    if (!app.planner) {
        g_printerr("out of memory\n");
        return 1;
    }

    if (data_override) {
        app.data_path = g_strdup(data_override);
    } else {
        char *dir = g_build_filename(g_get_user_data_dir(), "studyplanner", NULL);
        g_mkdir_with_parents(dir, 0700);
        app.data_path = g_build_filename(dir, "planner.dat", NULL);
        g_free(dir);
    }

    if (demo) {
        app.persist = FALSE;
        load_demo(app.planner);
    } else {
        PlannerStatus st = planner_load(app.planner, app.data_path);
        if (st == PLANNER_ERR_FORMAT || st == PLANNER_ERR_IO) {
            /* Keep the unreadable file as a backup rather than overwriting it. */
            char *bak = g_strconcat(app.data_path, ".corrupt", NULL);
            g_rename(app.data_path, bak);
            g_printerr("Could not read %s (%s); moved to %s\n", app.data_path,
                       planner_strerror(st), bak);
            g_free(bak);
        }
    }

    GtkApplication *gapp = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gapp, "activate", G_CALLBACK(on_activate), &app);
    int status = g_application_run(G_APPLICATION(gapp), argc, argv);
    g_object_unref(gapp);

    planner_free(app.planner);
    g_free(app.data_path);
    return status;
}
