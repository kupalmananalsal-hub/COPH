# COPH
Physics Project

## Native Windows App (C++)

- File: `ev_range_planner_winapp.cpp`
- Type: Win32 desktop application (no HTML/web UI)
- Project is now single-source-file (all backend + GUI logic in this one file)

Build with Dev-C++ MinGW:

`g++ ev_range_planner_winapp.cpp -std=gnu++11 -mwindows -lgdi32 -luser32 -o EVPlanner.exe`

Run:

`EVPlanner.exe`
