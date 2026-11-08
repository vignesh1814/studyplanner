/* Application stylesheet (GTK CSS). Neutral tones use currentColor so the
 * look adapts to both light and dark system themes. */
#ifndef STUDYPLANNER_STYLE_H
#define STUDYPLANNER_STYLE_H

static const char STYLE_CSS[] =
    "window.sp-window { }\n"

    /* ---- stat cards ---- */
    ".stat-card { background: alpha(currentColor, 0.06); border-radius: 16px;"
    "  padding: 14px 18px; }\n"
    ".stat-value { font-size: 28px; font-weight: 800; }\n"
    ".stat-label { font-size: 11px; font-weight: 600; opacity: 0.6; letter-spacing: 1px; }\n"
    ".stat-sub { font-size: 12px; opacity: 0.6; }\n"
    ".stat-card.danger .stat-value { color: #e5484d; }\n"
    ".stat-card.good .stat-value { color: #30a46c; }\n"
    ".stat-card.accent .stat-value { color: #6e7bff; }\n"

    /* ---- task list ---- */
    "list.task-list { background: transparent; }\n"
    "list.task-list > row { background: none; padding: 0; margin: 0; }\n"
    "list.task-list > row:hover { background: none; }\n"
    ".task-row { background: alpha(currentColor, 0.05); border-radius: 14px;"
    "  padding: 12px 16px 12px 14px; margin: 4px 2px; border-left: 5px solid transparent; }\n"
    ".task-row:hover { background: alpha(currentColor, 0.085); }\n"
    ".task-row.prio-high { border-left-color: #e5484d; }\n"
    ".task-row.prio-medium { border-left-color: #f5a524; }\n"
    ".task-row.prio-low { border-left-color: #30a46c; }\n"
    ".task-title { font-size: 15px; font-weight: 600; }\n"
    ".task-title.done { text-decoration: line-through; opacity: 0.45; }\n"
    ".task-progress-label { font-size: 12px; opacity: 0.7; }\n"
    ".chip { background: alpha(#6e7bff, 0.18); color: #6e7bff; border-radius: 99px;"
    "  padding: 1px 10px; font-size: 11px; font-weight: 700; }\n"
    ".due { font-size: 12px; opacity: 0.7; }\n"
    ".due.overdue { color: #e5484d; opacity: 1; font-weight: 700; }\n"
    ".due.soon { color: #d98a00; opacity: 1; font-weight: 700; }\n"
    ".due.done { opacity: 0.4; }\n"
    "progressbar > trough { min-height: 7px; border-radius: 99px; background: alpha(currentColor, 0.12); }\n"
    "progressbar > trough > progress { min-height: 7px; border-radius: 99px; background: #6e7bff; }\n"
    "progressbar.complete > trough > progress { background: #30a46c; }\n"
    ".pill { border-radius: 99px; padding: 3px 12px; font-size: 12px; font-weight: 600; }\n"

    /* ---- empty state ---- */
    ".empty-title { font-size: 20px; font-weight: 700; }\n"
    ".empty-sub { opacity: 0.6; }\n"

    /* ---- schedule ---- */
    ".banner { background: alpha(#6e7bff, 0.14); border-radius: 14px; padding: 12px 16px; }\n"
    ".banner.warn { background: alpha(#e5484d, 0.14); }\n"
    ".banner.good { background: alpha(#30a46c, 0.16); }\n"
    ".day-card { background: alpha(currentColor, 0.05); border-radius: 16px; padding: 14px 16px; }\n"
    ".day-card.today { background: alpha(#6e7bff, 0.13); }\n"
    ".day-title { font-size: 15px; font-weight: 800; }\n"
    ".day-total { font-size: 12px; opacity: 0.65; }\n"
    ".session-row { padding: 6px 0; }\n"
    ".session-time { background: alpha(currentColor, 0.1); border-radius: 8px;"
    "  padding: 2px 9px; font-size: 12px; font-weight: 700; min-width: 48px; }\n"
    ".late-tag { background: alpha(#e5484d, 0.18); color: #e5484d; border-radius: 99px;"
    "  padding: 1px 9px; font-size: 11px; font-weight: 700; }\n"

    /* ---- dialog ---- */
    ".dialog-heading { font-size: 18px; font-weight: 800; }\n"
    ".form-label { font-size: 12px; font-weight: 700; opacity: 0.7; }\n"
    ".form-error { color: #e5484d; font-weight: 600; }\n"
    ".section-title { font-size: 12px; font-weight: 800; opacity: 0.55; letter-spacing: 1px; }\n";

#endif
