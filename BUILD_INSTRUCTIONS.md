# EV Charging & Range Planner - Build Instructions

## Overview
This is a native Windows GUI application built with Win32 API and C++ Standard Library. It calculates EV energy consumption, battery status, and charging recommendations with integration for finding nearby charging stations.

## Prerequisites

### Option 1: MinGW-w64 (Recommended for Development)
- Download: https://www.mingw-w64.org/downloads/
- Install MinGW with gcc/g++ toolchain
- Add bin folder to PATH

### Option 2: Microsoft Visual C++ Build Tools
- Download: https://visualstudio.microsoft.com/visual-cpp-build-tools/
- Install the C++ workload

## Compilation Commands

### MinGW-w64 (Windows)
```bash
g++ ev_planner.cpp -std=c++11 -mwindows -lgdi32 -luser32 -lwinhttp -o EVPlanner.exe
```

**Explanation:**
- `ev_planner.cpp` - Source file
- `-std=c++11` - C++11 standard (supports std::vector, std::string, etc.)
- `-mwindows` - Creates GUI window (no console)
- `-lgdi32 -luser32` - Standard Windows GUI libraries
- `-lwinhttp` - Windows HTTP API (for future API integration)
- `-o EVPlanner.exe` - Output executable name

### MSVC Compiler
```bash
cl.exe /std:c++latest ev_planner.cpp /link user32.lib gdi32.lib winhttp.lib /SUBSYSTEM:WINDOWS /OUT:EVPlanner.exe
```

### Dev-C++ (with MinGW built-in)
1. File → New → Project → Windows Application
2. Copy `ev_planner.cpp` into the project
3. Project → Project Options:
   - Set Compiler: `MinGW`
   - Add include: `-std=c++11`
   - Add libraries: `-lwinhttp`
4. Build → Compile & Run

## Running the Application

```bash
EVPlanner.exe
```

The application opens a window with:
- **Input fields** for battery %, capacity, consumption rate, distance, and regen efficiency
- **Compute Plan button** to run calculations
- **Clear button** to reset inputs and results
- **Results area** showing detailed energy calculations and charging recommendations

## Features

### Core Calculations
1. **Available Energy** = (Battery % / 100) × Capacity (kWh)
2. **Gross Energy Used** = (Consumption Rate / 100) × Distance (km)
3. **Regen Recovered** = Gross Energy Used × (Regen % / 100)
4. **Net Energy Used** = Gross Energy Used - Regen Recovered
5. **Remaining Battery** = Available - Net Used
6. **Remaining %** = (Remaining kWh / Capacity) × 100
7. **Max Range** = Available Energy / (Consumption Rate / 100)

### Input Validation
- **Battery %**: 1–100 (rejects 0)
- **Capacity**: 10–200 kWh
- **Consumption Rate**: 5–50 kWh/100km
- **Distance**: > 0 km
- **Regen Efficiency**: 0–100% (defaults to 15% if using default)

### Charging Recommendation
- If remaining battery < 20%: "CHARGING RECOMMENDED"
- Calculates charging time at Level 2 (7.2 kW)
  - Formula: minutes = ((20 - remaining_pct) / 100) × capacity / 7.2 × 60
- Shows 3 nearest charging stations (currently sample data; API integration ready)
- Trip distance vs. max range advisory

### API Integration (Optional)
The code includes framework for Open Charge Map API integration:
- API Endpoint: `https://api.openchargemap.io/v3/poi`
- Parameters: latitude, longitude, distance, API key
- To enable:
  1. Get free API key from https://openchargemap.org
  2. Uncomment API section in `fetch_charging_stations()` function
  3. Replace `YOUR_API_KEY_HERE` with your actual key
  4. Recompile

Current implementation uses sample Philippine charging stations for demonstration.

## Default Values
- Battery: 50%
- Capacity: 60 kWh
- Consumption: 18 kWh/100km
- Distance: 120 km
- Regen: 15%

## Library Dependencies
- **winhttp.lib** - Windows HTTP API (for charging station lookups)
- **gdi32.lib** - Graphics Device Interface (built-in Windows)
- **user32.lib** - User interface controls (built-in Windows)

All are part of Windows SDK and included with MinGW/MSVC.

## Troubleshooting

### "Error: winhttp.h not found"
- Ensure Windows SDK is installed
- MinGW: Install with `mingw-w64-x86_64-headers` package
- Add SDK include path to compiler if needed

### Application won't start
- Ensure you compiled with `-mwindows` flag (prevents console window)
- Check that all `.lib` files are linked properly

### Charging station API not responding
- Application gracefully falls back to sample data
- Check firewall/proxy settings if real API is needed
- Verify API key is valid

## Testing the Application

1. **Basic Calculation**
   - Input: Battery=50%, Capacity=60kWh, Consumption=18, Distance=120km, Regen=15%
   - Expected: Available ~27kWh, sufficient battery for trip

2. **Charging Scenario**
   - Input: Battery=15%, Capacity=60kWh, Consumption=18, Distance=200km, Regen=15%
   - Expected: "CHARGING RECOMMENDED" message with charging time

3. **Range Exceeded**
   - Input: Battery=50%, Capacity=60kWh, Consumption=18, Distance=500km, Regen=15%
   - Expected: Advisory warning about exceeding max range

## Code Structure

### Six Core Functions (Physics Module)
```cpp
double calculate_available_energy(double battery_pct, double capacity_kwh);
double calculate_energy_used(double consumption_kwh_per_100km, double distance_km);
double calculate_regen_recovery(double energy_used_kwh, double regen_efficiency_pct);
double calculate_remaining_battery(double available_kwh, double net_energy_used_kwh);
int calculate_charging_time(double remaining_pct, double capacity_kwh);
Result compute(const Inputs& in);  // Main computation orchestrator
```

### GUI Functions
- `run_compute()` - Validates input and triggers calculations
- `clear_inputs()` - Resets form to defaults
- `WndProc()` - Window message handler
- `create_label()` / `create_edit()` - UI element creators

### API Functions
- `fetch_charging_stations()` - Queries charging station data
- `get_charging_stations_sample()` - Returns sample data for offline use

## Output Format
Results display includes:
- Available/gross/regen/net energy (kWh)
- Remaining battery % and kWh
- Max estimated range (km)
- Charging recommendation (YES/NO)
- Charging time (if needed)
- Nearby stations (name, address, charger type, power)

## Performance
- **Startup**: < 1 second
- **Calculation**: Instant (< 10ms)
- **Station lookup**: ~2-5 seconds (if using real API)

## Single File Distribution
The compiled `EVPlanner.exe` can be distributed standalone with no external dependencies beyond Windows system libraries (GDI32.dll, User32.dll, WinHTTP.dll - all included in Windows).

## Version
- **Language**: C++11
- **Target**: Windows 7+
- **Architecture**: x86 or x64 (compile accordingly)
