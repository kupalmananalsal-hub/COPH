#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace planner {

const double kDefaultRegenEfficiencyPct = 15.0;
const double kSafetyThresholdPct = 20.0;

struct Inputs {
    double battery_pct;
    double battery_capacity_kwh;
    double consumption_kwh_per_100km;
    double trip_distance_km;
    double regen_efficiency_pct;

    Inputs()
        : battery_pct(0.0),
          battery_capacity_kwh(0.0),
          consumption_kwh_per_100km(0.0),
          trip_distance_km(0.0),
          regen_efficiency_pct(kDefaultRegenEfficiencyPct) {}
};

struct Result {
    double available_kwh;
    double gross_energy_used_kwh;
    double regen_recovered_kwh;
    double net_energy_used_kwh;
    double remaining_kwh;
    double remaining_pct;
    double max_range_km;
    bool charging_needed;

    Result()
        : available_kwh(0.0),
          gross_energy_used_kwh(0.0),
          regen_recovered_kwh(0.0),
          net_energy_used_kwh(0.0),
          remaining_kwh(0.0),
          remaining_pct(0.0),
          max_range_km(0.0),
          charging_needed(false) {}
};

double calculate_available_energy(double battery_pct, double capacity_kwh) {
    return (battery_pct / 100.0) * capacity_kwh;
}

double calculate_energy_used(double consumption_kwh_per_100km, double distance_km) {
    return (consumption_kwh_per_100km / 100.0) * distance_km;
}

double calculate_regen_recovery(double energy_used_kwh, double regen_efficiency_pct) {
    return energy_used_kwh * (regen_efficiency_pct / 100.0);
}

Result compute(const Inputs& in) {
    Result r;
    r.available_kwh = calculate_available_energy(in.battery_pct, in.battery_capacity_kwh);
    r.gross_energy_used_kwh = calculate_energy_used(in.consumption_kwh_per_100km, in.trip_distance_km);
    r.regen_recovered_kwh = calculate_regen_recovery(r.gross_energy_used_kwh, in.regen_efficiency_pct);
    r.regen_recovered_kwh = std::min(r.regen_recovered_kwh, r.gross_energy_used_kwh);
    r.net_energy_used_kwh = r.gross_energy_used_kwh - r.regen_recovered_kwh;
    r.remaining_kwh = std::max(0.0, r.available_kwh - r.net_energy_used_kwh);
    r.remaining_pct = std::max(0.0, (r.remaining_kwh / in.battery_capacity_kwh) * 100.0);
    r.max_range_km = r.available_kwh / (in.consumption_kwh_per_100km / 100.0);
    r.charging_needed = r.remaining_pct < kSafetyThresholdPct;
    return r;
}

}  // namespace planner

enum ControlId {
    IDC_BATTERY = 101,
    IDC_CAPACITY,
    IDC_CONSUMPTION,
    IDC_DISTANCE,
    IDC_REGEN_EFF,
    IDC_COMPUTE,
    IDC_RESULTS
};

HWND g_hEdits[5] = {0};
HWND g_hResults = NULL;

std::wstring s2ws(const std::string& s) {
    if (s.empty()) {
        return L"";
    }
    const int size_needed =
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), NULL, 0);
    std::wstring w(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], size_needed);
    return w;
}

std::string ws2s(const std::wstring& ws) {
    if (ws.empty()) { return ""; }
    const int size_needed =
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), NULL, 0, NULL, NULL);
    std::string s(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), &s[0], size_needed, NULL, NULL);
    return s;
}

std::wstring get_window_text(HWND h) {
    const int len = GetWindowTextLengthW(h);
    std::wstring out(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(h, &out[0], len + 1);
    out.resize(static_cast<std::size_t>(len));
    return out;
}

double read_double_or_throw(HWND hEdit, const char* field, double min, double max) {
    const std::string raw = ws2s(get_window_text(hEdit));
    if (raw.empty()) {
        throw std::runtime_error(std::string(field) + " is required.");
    }

    char* end_ptr = NULL;
    const double value = std::strtod(raw.c_str(), &end_ptr);
    if (end_ptr == raw.c_str()) {
        throw std::runtime_error(std::string(field) + " must be a valid number.");
    }
    while (*end_ptr != '\0' && std::isspace(static_cast<unsigned char>(*end_ptr))) {
        ++end_ptr;
    }
    if (*end_ptr != '\0') {
        throw std::runtime_error(std::string(field) + " must be a valid number.");
    }
    if (value < min || value > max) {
        std::ostringstream oss;
        oss << field << " must be between " << min << " and " << max << ".";
        throw std::runtime_error(oss.str());
    }
    return value;
}

void set_default_text() {
    SetWindowTextA(g_hEdits[0], "45");
    SetWindowTextA(g_hEdits[1], "60");
    SetWindowTextA(g_hEdits[2], "18");
    SetWindowTextA(g_hEdits[3], "120");
    SetWindowTextA(g_hEdits[4], "15");
}

void apply_font(HWND hWnd) {
    (void)hWnd;
}

HWND create_label(HWND parent, int x, int y, int w, int height, const wchar_t* text) {
    HWND ctrl = CreateWindowExW(
        0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, height, parent, NULL, NULL, NULL);
    apply_font(ctrl);
    return ctrl;
}

HWND create_edit(HWND parent, int id, int x, int y, int w, int height) {
    HWND ctrl = CreateWindowExW(WS_EX_CLIENTEDGE,
                                L"EDIT",
                                L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                x,
                                y,
                                w,
                                height,
                                parent,
                                reinterpret_cast<HMENU>(id),
                                NULL,
                                NULL);
    apply_font(ctrl);
    return ctrl;
}

void run_compute(HWND hWnd) {
    try {
        planner::Inputs in;
        in.battery_pct = read_double_or_throw(g_hEdits[0], "Battery %", 1.0, 100.0);
        in.battery_capacity_kwh = read_double_or_throw(g_hEdits[1], "Capacity (kWh)", 10.0, 200.0);
        in.consumption_kwh_per_100km =
            read_double_or_throw(g_hEdits[2], "Consumption (kWh/100km)", 5.0, 50.0);
        in.trip_distance_km = read_double_or_throw(g_hEdits[3], "Distance (km)", 0.001, 2000.0);
        in.regen_efficiency_pct = read_double_or_throw(g_hEdits[4], "Regen efficiency %", 0.0, 100.0);

        const planner::Result r = planner::compute(in);

        std::ostringstream out;
        out << std::fixed << std::setprecision(2);
        out << "Available energy: " << r.available_kwh << " kWh\r\n";
        out << "Gross energy used: " << r.gross_energy_used_kwh << " kWh\r\n";
        out << "Regen recovered: " << r.regen_recovered_kwh << " kWh\r\n";
        out << "Net energy used: " << r.net_energy_used_kwh << " kWh\r\n";
        out << "Remaining battery: " << r.remaining_pct << "% (" << r.remaining_kwh << " kWh)\r\n";
        out << "Estimated max range: " << r.max_range_km << " km\r\n";
        out << "Charging needed? " << (r.charging_needed ? "YES" : "NO") << "\r\n";
        if (r.charging_needed) {
            out << "Status: Battery below 20% safety threshold on arrival.\r\n";
        } else {
            out << "Status: Battery sufficient for this trip.\r\n";
        }

        SetWindowTextW(g_hResults, s2ws(out.str()).c_str());
    } catch (const std::exception& ex) {
        MessageBoxW(hWnd, s2ws(ex.what()).c_str(), L"Input Error", MB_OK | MB_ICONWARNING);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            create_label(hWnd, 20, 18, 620, 24, L"Electric Vehicle Charging & Range Planner (Document-Based Model)");

            const wchar_t* labels[5] = {L"Battery (%)",
                                        L"Capacity (kWh)",
                                        L"Consumption (kWh/100km)",
                                        L"Distance (km)",
                                        L"Regen efficiency (%)"};

            int y = 56;
            for (int i = 0; i < 5; ++i) {
                create_label(hWnd, 20, y + 4, 210, 22, labels[i]);
                g_hEdits[i] = create_edit(hWnd, IDC_BATTERY + i, 240, y, 150, 24);
                y += 32;
            }

            HWND hCompute = CreateWindowExW(0,
                                            L"BUTTON",
                                            L"Compute Plan",
                                            WS_CHILD | WS_VISIBLE,
                                            20,
                                            230,
                                            150,
                                            34,
                                            hWnd,
                                            reinterpret_cast<HMENU>(IDC_COMPUTE),
                                            NULL,
                                            NULL);
            apply_font(hCompute);

            g_hResults = CreateWindowExW(WS_EX_CLIENTEDGE,
                                         L"EDIT",
                                         L"",
                                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                                             WS_VSCROLL,
                                         20,
                                         280,
                                         740,
                                         230,
                                         hWnd,
                                         reinterpret_cast<HMENU>(IDC_RESULTS),
                                         NULL,
                                         NULL);
            apply_font(g_hResults);

            set_default_text();
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_COMPUTE && HIWORD(wParam) == BN_CLICKED) {
                run_compute(hWnd);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"EVRangePlannerWindowClass";

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class.", L"Startup Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExW(0,
                                CLASS_NAME,
                                L"EV Charging & Range Planner",
                                WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                800,
                                580,
                                NULL,
                                NULL,
                                hInstance,
                                NULL);
    if (hWnd == NULL) {
        MessageBoxW(NULL, L"Failed to create application window.", L"Startup Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}


