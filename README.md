# Study Planner

A desktop study planner with a **GTK 4** interface and a **pure C core**.
Add tasks with a deadline, priority and time estimate; the planner ranks them by
urgency and builds a day-by-day study schedule that fits your daily study time.

| Tasks | Study plan |
| --- | --- |
| ![Tasks](docs/tasks.png) | ![Study plan](docs/schedule.png) |

## Features

- **Tasks** with subject, priority, deadline and estimated effort
- **Urgency ranking** – overdue and near-deadline, high-priority work floats to the top
- **Automatic study plan** – earliest-deadline-first scheduling across days, honouring your
  daily study budget, with subjects interleaved when that does not risk a deadline
- **Deadline warnings** – tells you when work cannot fit before its deadline at your current pace
- **Progress tracking** – log time with one click (`+30 min`, or `Done` on a planned session);
  tasks auto-complete when the estimate is reached
- **Dashboard** – open tasks, overall progress, time left, overdue count
- Search, show/hide completed, edit and delete
- Data saved automatically to a small text file

## Architecture

```
core/planner.[ch]   All logic: tasks, dates, ranking, stats, scheduler, save/load (C11, no GUI)
ui/main.c           GTK 4 front-end: windows, dialogs, widgets (presentation only)
ui/style.h          Application CSS
tests/test_core.c   Unit tests for the core (no GTK needed)
```

The UI never computes anything itself – it calls the C core and renders the result.
The core has no dependency besides the C standard library, so it can be reused from a CLI,
tests or another front-end.

### Core API at a glance

```c
Planner *p = planner_new();
planner_add(p, "Calculus set 5", "Math", PRIORITY_HIGH, date_parse("2026-12-01"), 150, NULL);
planner_set_daily_minutes(p, 180);

Schedule *s = schedule_build(p, date_today(), 0);   // study plan
PlannerStats st = planner_stats(p, date_today());   // dashboard numbers

planner_save(p, "planner.dat");
schedule_free(s);
planner_free(p);
```

Dates are plain day numbers (days since 1970‑01‑01), so scheduling is immune to time zone
and DST quirks.

### How scheduling works

1. Open tasks are sorted by deadline (earliest first), then priority.
2. Each day, every task first gets up to 50 min so subjects are interleaved; any remaining
   daily capacity is then spent in deadline order.
3. Strict earliest-deadline-first is also computed, and the interleaved plan is only used
   if it misses no more deadlines than the strict one – so variety never costs a deadline.
4. Sessions after a task's deadline are flagged and the UI warns you.

## Quick start (beginners)

No programming experience needed. Pick your system.

### Windows 10 / 11

1. **Get the code.** Either
   - click the green **Code** button on GitHub, choose **Download ZIP**, then right-click the
     ZIP and **Extract All…**, or
   - if you have Git: `git clone https://github.com/vignesh1814/studyplanner.git`
2. Open the extracted `studyplanner` folder and **double-click `run_windows.bat`**.
3. The first time, it installs everything it needs (MSYS2, a C compiler and GTK 4 – a few
   hundred MB, takes several minutes). If Windows asks for permission, click **Yes**.
   When it finishes, the app opens.
4. Every later time, just double-click `run_windows.bat` again. It rebuilds only if the code
   changed, then starts the app.

> If the automatic install fails, install MSYS2 yourself from <https://www.msys2.org>
> (keep the default folder `C:\msys64`) and double-click `run_windows.bat` again.
> You can also double-click `setup_windows.bat` to redo just the installation step.

To try it with sample data (nothing is saved), open Command Prompt in the folder and run
`run_windows.bat --demo`.

### Linux / macOS

1. Get the code: `git clone https://github.com/vignesh1814/studyplanner.git` and
   `cd studyplanner` (or download the ZIP and extract it).
2. Install GTK 4 and a compiler (one command, pick yours):

   ```bash
   sudo apt install build-essential pkg-config libgtk-4-dev          # Ubuntu / Debian
   sudo dnf install gcc make pkgconf-pkg-config gtk4-devel           # Fedora
   sudo pacman -S base-devel gtk4                                    # Arch
   brew install gtk4 pkg-config                                      # macOS (Homebrew)
   ```
3. Run:

   ```bash
   ./run.sh
   ```

   It builds the app and starts it. Run the same command any time you want to open it again.

### Something went wrong?

| Problem | Fix |
| --- | --- |
| `GTK 4 not found` | Install GTK 4 (step 2 above, or run `setup_windows.bat` on Windows). |
| Windows: window flashes and closes | Run `run_windows.bat` from Command Prompt to see the error message. |
| `./run.sh: Permission denied` | Run `chmod +x run.sh`, then try again. |
| App starts but shows no data | Data lives in your user data folder (see [Usage](#usage)); try `--demo`. |

## Manual build (for developers)

You need a C11 compiler, `make`, `pkg-config` and the GTK 4 development files.

```bash
make          # build   -> build/studyplanner
make test     # run the core unit tests
make run      # build and launch
make clean
```

On Windows run these in an MSYS2 **UCRT64** shell and use `mingw32-make` instead of `make`
(`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-make`).

## Usage

```bash
./run.sh                      # Linux/macOS (Windows: run_windows.bat)
./build/studyplanner --demo   # try it with sample data (nothing is saved)
./build/studyplanner --data my.dat   # use a specific data file
```

Your tasks are stored in `planner.dat` inside the user data directory
(`%LOCALAPPDATA%\studyplanner` on Windows, `~/.local/share/studyplanner` on Linux), or at the
path in the `STUDYPLANNER_DATA` environment variable. If the file cannot be parsed it is
moved to `planner.dat.corrupt` instead of being overwritten.

## Tests

```bash
make test
```

Covers date maths, task operations and validation, ranking, statistics, scheduling
(capacity, deadlines, infeasible plans, horizon limits) and save/load round trips including
corrupt files.

## License

MIT
