# EV Planner - Complete Setup & Compilation Guide

## Quick Start (For Immediate Use)

**Option 1: Use Existing Binary**
- An `EVPlanner.exe` is already in the project folder
- Simply double-click to run (Windows 7+ required)
- No installation needed

**Option 2: Rebuild From Source**
- See "Installation" section below

---

## Installation & Compilation

### Step 1: Install a C++ Compiler

Choose ONE of the following:

#### **Option A: MinGW-w64 (Recommended - Free & Lightweight)**

1. Download from: https://www.mingw-w64.org/downloads/
2. Choose version:
   - **Online installer** (recommended): `x86_64-w64-mingw32-gcc-latest.zip`
   - For 32-bit Windows: download `i686-w64-mingw32` instead

3. Installation steps:
   ```
   a) Extract ZIP to C:\mingw64 (or your preferred location)
   b) Add to PATH:
      - Right-click "This PC" → Properties
      - Click "Advanced system settings"
      - Click "Environment Variables"
      - Under "System variables", find "Path", click "Edit"
      - Click "New" and add: C:\mingw64\bin
      - Click OK, OK, OK
      - Restart terminal/command prompt
   ```

4. Verify installation:
   ```bash
   g++ --version
   ```
   Should show something like: `g++ (MinGW-W64 x86_64-posix-seh, built by Brecht Sanders)`

#### **Option B: Microsoft Visual C++ Build Tools (Professional)**

1. Download: https://visualstudio.microsoft.com/visual-cpp-build-tools/
2. Run installer and select "Desktop development with C++"
3. Installation size: ~4 GB
4. Use MSVC command line (see compilation section)

#### **Option C: Windows Subsystem for Linux (WSL)**

1. Install WSL2: https://docs.microsoft.com/en-us/windows/wsl/install
2. In WSL terminal: `sudo apt install g++ build-essential`
3. Navigate to Windows folder and compile there

---

### Step 2: Verify Source File Exists

The following file should be in your project directory:
- `ev_planner.cpp` - The complete source code

### Step 3: Compile the Application

#### **With MinGW (Command Prompt or PowerShell)**

```bash
cd "C:\Users\Ralph Simon Gaviola\Downloads\COPH"
g++ ev_planner.cpp -std=c++11 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner.exe
```

**Flags explained:**
- `ev_planner.cpp` - Source file
- `-std=c++11` - Use C++11 standard
- `-mwindows` - Create GUI (no console window)
- `-lgdi32` - Graphics library (built-in)
- `-luser32` - UI library (built-in)
- `-lwinhttp` - HTTP library (for API calls)
- `-o EVPlanner.exe` - Output filename

#### **With MSVC (Visual Studio Command Prompt)**

```bash
cl.exe /std:c++latest /EHsc ev_planner.cpp /link user32.lib gdi32.lib winhttp.lib /SUBSYSTEM:WINDOWS /OUT:EVPlanner.exe
```

#### **Using the Batch Script (Windows Only)**

```bash
double-click build_ev_planner.bat
```

The batch script automatically:
- Checks if g++ is installed
- Compiles the code
- Shows file size
- Offers to run the app

### Step 4: Run the Application

```bash
EVPlanner.exe
```

Or simply double-click `EVPlanner.exe` in File Explorer.

---

## Verification Checklist

After compilation, verify the executable works:

1. **File exists**: Check that `EVPlanner.exe` was created (should be 500-700 KB)
2. **Run the app**: Double-click `EVPlanner.exe`
3. **Window appears**: A GUI window with input fields should open
4. **Enter test values**:
   - Battery: 50
   - Capacity: 60
   - Consumption: 18
   - Distance: 120
   - Regen: 15
5. **Click "Compute Plan"** - Should show energy calculations
6. **Test "Clear" button** - Should reset form

---

## Troubleshooting

### "g++ is not recognized"

**Cause**: MinGW not installed or not in PATH

**Solution**:
1. Verify MinGW installation: Check `C:\mingw64\bin\g++.exe` exists
2. Add to PATH:
   - Settings → Environment Variables → PATH → Add `C:\mingw64\bin`
   - Restart terminal/PowerShell
   - Retest: `g++ --version`

### "windows.h not found"

**Cause**: Windows SDK not installed with MinGW

**Solution**:
- Download MinGW with headers: https://www.mingw-w64.org/downloads/
- Use the full package (not minimal installation)
- Verify: `C:\mingw64\x86_64-w64-mingw32\include\windows.h` exists

### "Cannot open output file"

**Cause**: EVPlanner.exe is currently running

**Solution**:
1. Close the running EVPlanner.exe window
2. Recompile

### Compilation produces many warnings

**What to expect**: Some warnings about unused parameters are normal in Win32 code

**Not an error**: Application will still compile and run correctly

### Application crashes immediately

**Likely cause**: 64-bit vs 32-bit mismatch

**Solution**:
- For 64-bit Windows (most common): Ensure you're using `x86_64-w64-mingw32` compiler
- For 32-bit Windows: Use `i686-w64-mingw32` compiler
- Check: `g++ -v` and look for architecture info

---

## Alternative: Online Compiler (Testing Only)

If you cannot install MinGW locally, test the code online:

1. Visit: https://www.onlinegdb.com/
2. Select Language: C++
3. Paste the code from `ev_planner.cpp`
4. Note: Win32 GUI features won't work in online compiler

---

## File Structure

```
COPH/
├── ev_planner.cpp                 ← Main source code
├── EVPlanner.exe                  ← Compiled executable
├── build_ev_planner.bat           ← Windows batch build script
├── BUILD_INSTRUCTIONS.md          ← Detailed documentation
├── SETUP_GUIDE.md                 ← This file
└── README.md                      ← Project overview
```

---

## Development & Compilation Commands Reference

### Build with MinGW (Default)
```bash
g++ ev_planner.cpp -std=c++11 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner.exe
```

### Build with MSVC
```bash
cl.exe /std:c++latest /EHsc ev_planner.cpp /link user32.lib gdi32.lib winhttp.lib /SUBSYSTEM:WINDOWS
```

### Build with debugging symbols (MinGW)
```bash
g++ ev_planner.cpp -std=c++11 -mwindows -g -lgdi32 -luser32 -lwinhttp -o EVPlanner_debug.exe
```

### Build 32-bit version (MinGW)
```bash
i686-w64-mingw32-g++ ev_planner.cpp -std=c++11 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner_x86.exe
```

### Build with optimizations (MinGW)
```bash
g++ ev_planner.cpp -std=c++11 -O3 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner_optimized.exe
```

---

## Runtime Requirements

The compiled `EVPlanner.exe` requires:
- **Windows**: 7, 8, 10, 11 (32-bit or 64-bit)
- **Libraries** (usually pre-installed):
  - `gdi32.dll` - Graphics Device Interface
  - `user32.dll` - Windows UI
  - `winhttp.dll` - HTTP functionality
  
No manual installation needed - all Windows system DLLs.

---

## Performance Characteristics

| Operation | Time |
|-----------|------|
| App startup | < 1 second |
| Input validation | Instant |
| Calculation | < 10 ms |
| Station lookup | 2-5 seconds (if using real API) |

Compiled size: ~500-700 KB (single executable, no dependencies)

---

## Getting Help

1. **Compilation issues**: Verify MinGW installation and PATH
2. **Runtime issues**: Check Windows version (7+)
3. **Feature requests**: See BUILD_INSTRUCTIONS.md for API configuration

---

## Next Steps

1. **Install a compiler** (MinGW recommended)
2. **Verify installation**: Run `g++ --version`
3. **Build the project**: Use build_ev_planner.bat or command above
4. **Run**: Double-click EVPlanner.exe
5. **Customize** (optional): Edit ev_planner.cpp and recompile

---

## Code Structure Summary

### Six Core Physics Functions
These implement the exact formulas specified in the project:

```cpp
// Available energy at current battery level
double calculate_available_energy(double battery_pct, double capacity_kwh)

// Total energy needed for distance
double calculate_energy_used(double consumption_kwh_per_100km, double distance_km)

// Energy recovered from regenerative braking
double calculate_regen_recovery(double energy_used_kwh, double regen_efficiency_pct)

// Energy remaining after trip
double calculate_remaining_battery(double available_kwh, double net_energy_used_kwh)

// Time to charge to safe level
int calculate_charging_time(double remaining_pct, double capacity_kwh)

// Orchestrator: runs all calculations
Result compute(const Inputs& in)
```

### GUI Components
- **Input validation**: read_double_or_throw()
- **Window creation**: WndProc(), CreateWindowExW()
- **Button handlers**: run_compute(), clear_inputs()
- **Data display**: Multi-line text output

### API Integration (Optional)
- **fetch_charging_stations()** - Queries charging networks
- **get_charging_stations_sample()** - Fallback data
- Framework ready for Open Charge Map API integration

---

For detailed technical information, see BUILD_INSTRUCTIONS.md
