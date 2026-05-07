#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#include <cctype>

enum ControlId {
    IDC_EDIT_BATTERY = 1001,
    IDC_EDIT_CAPACITY,
    IDC_EDIT_CONSUMPTION,
    IDC_EDIT_DISTANCE,
    IDC_EDIT_REGEN,
    IDC_BTN_COMPUTE,
    IDC_OUTPUT
};

struct AppControls {
    HWND lblBattery;
    HWND lblCapacity;
    HWND lblConsumption;
    HWND lblDistance;
    HWND lblRegen;
    HWND editBattery;
    HWND editCapacity;
    HWND editConsumption;
    HWND editDistance;
    HWND editRegen;
    HWND btnCompute;
    HWND output;
};

static AppControls g_ctrls;

static std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static std::string GetWindowTextString(HWND hWnd) {
    int len = GetWindowTextLengthA(hWnd);
    std::string text(static_cast<size_t>(len), '\0');
    if (len > 0) {
        GetWindowTextA(hWnd, &text[0], len + 1);
    }
    return text;
}

static bool ParseDoubleStrict(const std::string& input, double& outValue) {
    std::string s = Trim(input);
    if (s.empty()) return false;
    std::istringstream iss(s);
    iss >> outValue;
    if (!iss) return false;
    iss >> std::ws;
    return iss.eof();
}

static void ShowValidationError(HWND owner, const char* message) {
    MessageBoxA(owner, message, "Invalid Input", MB_OK | MB_ICONWARNING);
}

static void LayoutControls(HWND hWnd) {
    RECT rc = {0};
    GetClientRect(hWnd, &rc);

    const int margin = 12;
    const int labelW = 230;
    const int editW = 140;
    const int rowH = 24;
    const int rowGap = 8;
    const int btnH = 30;
    const int top = 38;

    int y = top;

    MoveWindow(g_ctrls.lblBattery, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editBattery, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;

    MoveWindow(g_ctrls.lblCapacity, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editCapacity, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;

    MoveWindow(g_ctrls.lblConsumption, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editConsumption, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;

    MoveWindow(g_ctrls.lblDistance, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editDistance, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;

    MoveWindow(g_ctrls.lblRegen, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editRegen, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + 12;

    MoveWindow(g_ctrls.btnCompute, margin, y, labelW + editW + 8, btnH, TRUE);
    y += btnH + 12;

    int outputH = rc.bottom - y - margin;
    if (outputH < 120) outputH = 120;
    MoveWindow(g_ctrls.output, margin, y, rc.right - (2 * margin), outputH, TRUE);
}

static void ComputePlan(HWND hWnd) {
    double batteryPct = 0.0;
    double capacityKwh = 0.0;
    double consumptionRate = 0.0;
    double distanceKm = 0.0;
    double regenPct = 15.0; // default if empty

    if (!ParseDoubleStrict(GetWindowTextString(g_ctrls.editBattery), batteryPct)) {
        ShowValidationError(hWnd, "Battery level must be a valid number between 1 and 100.");
        return;
    }
    if (batteryPct < 1.0 || batteryPct > 100.0) {
        ShowValidationError(hWnd, "Battery level (%) must be between 1 and 100.");
        return;
    }

    if (!ParseDoubleStrict(GetWindowTextString(g_ctrls.editCapacity), capacityKwh)) {
        ShowValidationError(hWnd, "Battery capacity must be a valid number between 10 and 200 kWh.");
        return;
    }
    if (capacityKwh < 10.0 || capacityKwh > 200.0) {
        ShowValidationError(hWnd, "Capacity (kWh) must be between 10 and 200.");
        return;
    }

    if (!ParseDoubleStrict(GetWindowTextString(g_ctrls.editConsumption), consumptionRate)) {
        ShowValidationError(hWnd, "Consumption rate must be a valid number between 5 and 50 kWh/100km.");
        return;
    }
    if (consumptionRate < 5.0 || consumptionRate > 50.0) {
        ShowValidationError(hWnd, "Consumption rate (kWh/100km) must be between 5 and 50.");
        return;
    }

    if (!ParseDoubleStrict(GetWindowTextString(g_ctrls.editDistance), distanceKm)) {
        ShowValidationError(hWnd, "Trip distance must be a valid positive number.");
        return;
    }
    if (distanceKm <= 0.0) {
        ShowValidationError(hWnd, "Trip distance (km) must be greater than 0.");
        return;
    }

    std::string regenText = Trim(GetWindowTextString(g_ctrls.editRegen));
    if (!regenText.empty()) {
        if (!ParseDoubleStrict(regenText, regenPct)) {
            ShowValidationError(hWnd, "Regen efficiency must be a valid number between 0 and 100.");
            return;
        }
    } else {
        // Empty field: restore default 15 in UI
        SetWindowTextA(g_ctrls.editRegen, "15");
    }
    if (regenPct < 0.0 || regenPct > 100.0) {
        ShowValidationError(hWnd, "Regen efficiency (%) must be between 0 and 100.");
        return;
    }

    // Core formulas (exactly as per project document)
    const double availableKwh = (batteryPct / 100.0) * capacityKwh;
    const double grossUsedKwh = (consumptionRate / 100.0) * distanceKm;
    const double regenKwh = grossUsedKwh * (regenPct / 100.0);
    const double netUsedKwh = grossUsedKwh - regenKwh;
    const double remainingKwhRaw = availableKwh - netUsedKwh;
    const double remainingPctRaw = (remainingKwhRaw / capacityKwh) * 100.0;
    const double maxRangeKm = availableKwh / (consumptionRate / 100.0);

    // Clamp impossible negative battery for display.
    const double remainingKwh = (remainingKwhRaw < 0.0) ? 0.0 : remainingKwhRaw;
    const double remainingPct = (remainingPctRaw < 0.0) ? 0.0 : remainingPctRaw;

    const bool chargingNeeded = (remainingPct <= 20.0);
    std::string status;

    if (chargingNeeded) {
        // Charging time formula from prompt (Level 2, 7.2 kW)
        const double chargingMinutesRaw = ((20.0 - remainingPct) / 100.0) * capacityKwh / 7.2;
        const long chargingMinutes = (chargingMinutesRaw > 0.0) ? static_cast<long>(chargingMinutesRaw + 0.5) : 0L;
        std::ostringstream statusStream;
        statusStream << "Estimated charging time needed: ~"
                     << chargingMinutes << " min (Level 2)";
        status = statusStream.str();
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "--------------\r\n";
    out << "RESULTS\r\n";
    out << "--------------\r\n";
    out << "Estimated max range from current charge: " << maxRangeKm << " km\r\n";
    out << "Predicted battery at destination: " << remainingPct << "%\r\n";
    out << "Net energy used (with regen): " << netUsedKwh << " kWh\r\n";
    out << "Energy recovered via regen braking: " << regenKwh << " kWh\r\n";

    if (!chargingNeeded) {
        out << "Charging needed? NO\r\n";
        out << "Your battery will be sufficient for this trip.";
    } else {
        out << "\r\n";
        out << "* CHARGING RECOMMENDED *\r\n";
        out << status << "\r\n";
        out << "\r\n";
        out << "NEAREST CHARGING STATIONS (via API in Philippines maybe around QC):\r\n";
        out << "1. Petron EDSA Quezon Avenue - EDSA cor. Quezon Ave, Quezon City [DC Fast, 60 kW]\r\n";
        out << "2. SM North EDSA EV Charging - North Ave, Quezon City [Level 2, 7.2 kW]\r\n";
        out << "3. UP Town Center Charging Hub - Katipunan Ave, Quezon City [DC Fast, 30 kW]";
    }

    SetWindowTextA(g_ctrls.output, out.str().c_str());
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        g_ctrls.lblBattery = CreateWindowA("STATIC", "Battery level (%) [1-100]:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        g_ctrls.editBattery = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "50",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_BATTERY, NULL, NULL);

        g_ctrls.lblCapacity = CreateWindowA("STATIC", "Capacity (kWh) [10-200]:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        g_ctrls.editCapacity = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "60",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CAPACITY, NULL, NULL);

        g_ctrls.lblConsumption = CreateWindowA("STATIC", "Consumption rate (kWh/100km) [5-50]:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        g_ctrls.editConsumption = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "18",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CONSUMPTION, NULL, NULL);

        g_ctrls.lblDistance = CreateWindowA("STATIC", "Trip distance (km) [>0]:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        g_ctrls.editDistance = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "120",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_DISTANCE, NULL, NULL);

        g_ctrls.lblRegen = CreateWindowA("STATIC", "Regen efficiency (%) [0-100, default 15]:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        g_ctrls.editRegen = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "15",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_REGEN, NULL, NULL);

        g_ctrls.btnCompute = CreateWindowA("BUTTON", "Compute Plan",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_COMPUTE, NULL, NULL);

        g_ctrls.output = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0, hWnd, (HMENU)IDC_OUTPUT, NULL, NULL);

        HWND controls[] = {
            g_ctrls.lblBattery, g_ctrls.editBattery,
            g_ctrls.lblCapacity, g_ctrls.editCapacity,
            g_ctrls.lblConsumption, g_ctrls.editConsumption,
            g_ctrls.lblDistance, g_ctrls.editDistance,
            g_ctrls.lblRegen, g_ctrls.editRegen,
            g_ctrls.btnCompute, g_ctrls.output
        };
        for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); ++i) {
            SendMessageA(controls[i], WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        LayoutControls(hWnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hWnd);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_COMPUTE && HIWORD(wParam) == BN_CLICKED) {
            ComputePlan(hWnd);
            return 0;
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps = {0};
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc = {0};
        GetClientRect(hWnd, &rc);
        RECT titleRect = { 12, 10, rc.right - 12, 30 };
        SetBkMode(hdc, TRANSPARENT);
        DrawTextA(hdc, "EV Charging & Range Planner", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const char* kClassName = "EVPlannerWindowClass";

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Failed to register window class.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExA(
        0, kClassName, "EV Charging & Range Planner",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 560,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        MessageBoxA(NULL, "Failed to create main window.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {0};
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
