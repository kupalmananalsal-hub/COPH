#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

enum ControlId {
    IDC_EDIT_BATTERY = 1001,
    IDC_EDIT_CAPACITY,
    IDC_EDIT_CONSUMPTION,
    IDC_EDIT_DISTANCE,
    IDC_EDIT_REGEN,
    IDC_EDIT_CITY,
    IDC_BTN_COMPUTE,
    IDC_BTN_RESET,
    IDC_BTN_OPEN_MAP,
    IDC_OUTPUT
};

typedef struct ChargingStation {
    const char* name;
    const char* address;
    const char* city;
    const char* type;
    double power_kw;
    double lat;
    double lon;
} ChargingStation;

typedef struct CityCoord {
    const char* token;
    const char* label;
    double lat;
    double lon;
} CityCoord;

typedef struct UserInput {
    int is_valid;
    char error[256];
    double battery_pct;
    double capacity_kwh;
    double consumption_rate;
    double distance_km;
    double regen_pct;
    char city_input[128];
    char matched_city[64];
    double user_lat;
    double user_lon;
} UserInput;

typedef struct RemainingBattery {
    double remaining_kwh;
    double remaining_pct;
    int clamped_to_zero;
} RemainingBattery;

typedef struct AppControls {
    HWND lblBattery;
    HWND lblCapacity;
    HWND lblConsumption;
    HWND lblDistance;
    HWND lblRegen;
    HWND lblCity;
    HWND editBattery;
    HWND editCapacity;
    HWND editConsumption;
    HWND editDistance;
    HWND editRegen;
    HWND editCity;
    HWND btnCompute;
    HWND btnReset;
    HWND btnOpenMap;
    HWND output;
} AppControls;

static const ChargingStation kLuzonStations[] = {
    {"Meralco eVolve Center", "Ortigas Ave, Pasig", "Pasig", "DC Fast", 60.0, 14.5877, 121.0635},
    {"SM Megamall EV Charging", "EDSA, Mandaluyong", "Mandaluyong", "Level 2", 7.2, 14.5849, 121.0560},
    {"SM North EDSA EV Charging", "North Ave, Quezon City", "Quezon City", "Level 2", 7.2, 14.6547, 121.0306},
    {"UP Town Center Charging", "Katipunan Ave, Quezon City", "Quezon City", "DC Fast", 30.0, 14.6495, 121.0740},
    {"BGC Central EV Hub", "Bonifacio Global City, Taguig", "Taguig", "DC Fast", 60.0, 14.5513, 121.0480},
    {"NAIA EV Station", "NAIA Terminal Area, Pasay", "Pasay", "DC Fast", 50.0, 14.5095, 121.0198},
    {"SCTEX NLEX Shell Recharge", "San Fernando Exit, Pampanga", "San Fernando", "DC Fast", 60.0, 15.0370, 120.6890},
    {"Marquee Mall EV Charging", "Angeles, Pampanga", "Angeles", "Level 2", 7.2, 15.1450, 120.5887},
    {"Subic Bay EV Station", "Subic Bay Freeport", "Olongapo", "DC Fast", 50.0, 14.8272, 120.2820},
    {"SM City Baguio EV Charging", "Luneta Hill, Baguio", "Baguio", "Level 2", 7.2, 16.4088, 120.5960},
    {"SM City Clark EV Charging", "Clark Freeport, Pampanga", "Mabalacat", "Level 2", 7.2, 15.1680, 120.5420},
    {"Lipa EV Hub", "Lipa City, Batangas", "Lipa", "DC Fast", 50.0, 13.9411, 121.1626},
    {"SM City Batangas EV Charging", "Batangas City", "Batangas", "Level 2", 7.2, 13.7565, 121.0583},
    {"Naga City EV Station", "Naga City, Camarines Sur", "Naga", "DC Fast", 30.0, 13.6218, 123.1948},
    {"Legazpi EV Charging Point", "Legazpi City, Albay", "Legazpi", "Level 2", 7.2, 13.1391, 123.7438}
};

static const CityCoord kCityCoords[] = {
    {"quezon city", "Quezon City", 14.6760, 121.0437},
    {"manila", "Manila", 14.5995, 120.9842},
    {"makati", "Makati", 14.5547, 121.0244},
    {"pasig", "Pasig", 14.5764, 121.0851},
    {"mandaluyong", "Mandaluyong", 14.5794, 121.0359},
    {"taguig", "Taguig", 14.5176, 121.0509},
    {"pasay", "Pasay", 14.5378, 120.9876},
    {"paranaque", "Paranaque", 14.4793, 121.0198},
    {"muntinlupa", "Muntinlupa", 14.4081, 121.0415},
    {"caloocan", "Caloocan", 14.7566, 121.0453},
    {"angeles", "Angeles", 15.1450, 120.5887},
    {"san fernando", "San Fernando", 15.0343, 120.6840},
    {"olongapo", "Olongapo", 14.8386, 120.2842},
    {"baguio", "Baguio", 16.4023, 120.5960},
    {"tarlac", "Tarlac City", 15.4755, 120.5963},
    {"cabanatuan", "Cabanatuan", 15.4909, 120.9674},
    {"balanga", "Balanga", 14.6762, 120.5363},
    {"batangas", "Batangas City", 13.7565, 121.0583},
    {"lipa", "Lipa", 13.9411, 121.1626},
    {"naga", "Naga", 13.6218, 123.1948},
    {"legazpi", "Legazpi", 13.1391, 123.7438}
};

static AppControls g_ctrls;
static double g_capacity_for_percentage = 0.0;
static char g_map_url[2048] = "";

static void TrimInPlace(char* s) {
    size_t start = 0;
    size_t len = strlen(s);
    while (start < len && isspace((unsigned char)s[start])) {
        ++start;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        --len;
    }
}

static void ToLowerCopy(const char* src, char* dst, size_t dstSize) {
    size_t i = 0;
    if (!src || !dst || dstSize == 0) {
        return;
    }
    for (i = 0; i + 1 < dstSize && src[i] != '\0'; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void GetWindowTextBuffer(HWND hWnd, char* out, int outSize) {
    if (!out || outSize <= 0) {
        return;
    }
    out[0] = '\0';
    GetWindowTextA(hWnd, out, outSize);
}

static int ParseDoubleStrict(const char* input, double* outValue) {
    char tmp[128];
    char* endPtr = NULL;
    double value = 0.0;
    if (!input || !outValue) {
        return 0;
    }
    strncpy(tmp, input, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    TrimInPlace(tmp);
    if (tmp[0] == '\0') {
        return 0;
    }
    value = strtod(tmp, &endPtr);
    if (endPtr == tmp) {
        return 0;
    }
    while (*endPtr != '\0') {
        if (!isspace((unsigned char)*endPtr)) {
            return 0;
        }
        ++endPtr;
    }
    *outValue = value;
    return 1;
}

static void ShowValidationError(HWND owner, const char* message) {
    MessageBoxA(owner, message, "Invalid Input", MB_OK | MB_ICONWARNING);
}

static void AppendLine(char* dst, size_t dstSize, const char* line) {
    if (!dst || !line || dstSize == 0) {
        return;
    }
    strncat(dst, line, dstSize - strlen(dst) - 1);
    strncat(dst, "\r\n", dstSize - strlen(dst) - 1);
}

static int ResolveCityCoordinates(const char* cityInput, char* matchedCity, size_t matchedSize, double* lat, double* lon) {
    char lower[128];
    size_t i;
    ToLowerCopy(cityInput, lower, sizeof(lower));
    for (i = 0; i < sizeof(kCityCoords) / sizeof(kCityCoords[0]); ++i) {
        if (strstr(lower, kCityCoords[i].token) != NULL) {
            if (matchedCity && matchedSize > 0) {
                strncpy(matchedCity, kCityCoords[i].label, matchedSize - 1);
                matchedCity[matchedSize - 1] = '\0';
            }
            *lat = kCityCoords[i].lat;
            *lon = kCityCoords[i].lon;
            return 1;
        }
    }
    return 0;
}

static double distance_km(double lat1, double lon1, double lat2, double lon2) {
    const double earth_km = 6371.0;
    const double d2r = 3.141592653589793 / 180.0;
    double dLat = (lat2 - lat1) * d2r;
    double dLon = (lon2 - lon1) * d2r;
    double a = sin(dLat / 2.0) * sin(dLat / 2.0)
        + cos(lat1 * d2r) * cos(lat2 * d2r) * sin(dLon / 2.0) * sin(dLon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return earth_km * c;
}

static void FindNearestStations(double userLat, double userLon, int* outIdx, double* outDist, int maxCount) {
    int i;
    int k;
    for (k = 0; k < maxCount; ++k) {
        outIdx[k] = -1;
        outDist[k] = 1e12;
    }
    for (i = 0; i < (int)(sizeof(kLuzonStations) / sizeof(kLuzonStations[0])); ++i) {
        double d = distance_km(userLat, userLon, kLuzonStations[i].lat, kLuzonStations[i].lon);
        for (k = 0; k < maxCount; ++k) {
            if (d < outDist[k]) {
                int shift;
                for (shift = maxCount - 1; shift > k; --shift) {
                    outDist[shift] = outDist[shift - 1];
                    outIdx[shift] = outIdx[shift - 1];
                }
                outDist[k] = d;
                outIdx[k] = i;
                break;
            }
        }
    }
}

static void BuildMapUrl(double userLat, double userLon, const int* idx, int count) {
    if (count <= 0 || idx[0] < 0) {
        g_map_url[0] = '\0';
        return;
    }

    if (count == 1 || idx[1] < 0) {
        snprintf(
            g_map_url,
            sizeof(g_map_url),
            "https://www.google.com/maps/dir/?api=1&origin=%.6f,%.6f&destination=%.6f,%.6f&travelmode=driving",
            userLat, userLon, kLuzonStations[idx[0]].lat, kLuzonStations[idx[0]].lon
        );
        return;
    }

    if (count == 2 || idx[2] < 0) {
        snprintf(
            g_map_url,
            sizeof(g_map_url),
            "https://www.google.com/maps/dir/?api=1&origin=%.6f,%.6f&destination=%.6f,%.6f&travelmode=driving&waypoints=%.6f,%.6f",
            userLat, userLon,
            kLuzonStations[idx[0]].lat, kLuzonStations[idx[0]].lon,
            kLuzonStations[idx[1]].lat, kLuzonStations[idx[1]].lon
        );
        return;
    }

    snprintf(
        g_map_url,
        sizeof(g_map_url),
        "https://www.google.com/maps/dir/?api=1&origin=%.6f,%.6f&destination=%.6f,%.6f&travelmode=driving&waypoints=%.6f,%.6f%%7C%.6f,%.6f",
        userLat, userLon,
        kLuzonStations[idx[0]].lat, kLuzonStations[idx[0]].lon,
        kLuzonStations[idx[1]].lat, kLuzonStations[idx[1]].lon,
        kLuzonStations[idx[2]].lat, kLuzonStations[idx[2]].lon
    );
}

static void OpenNearestMap(void) {
    if (g_map_url[0] == '\0') {
        MessageBoxA(NULL, "No map is ready yet. Compute a plan first.", "Map", MB_OK | MB_ICONINFORMATION);
        return;
    }
    ShellExecuteA(NULL, "open", g_map_url, NULL, NULL, SW_SHOWNORMAL);
}

static UserInput get_user_input(void) {
    UserInput input;
    char batteryText[64], capacityText[64], consumptionText[64], distanceText[64], regenText[64], cityText[128];
    input.is_valid = 0;
    input.error[0] = '\0';
    input.matched_city[0] = '\0';
    input.battery_pct = 0.0;
    input.capacity_kwh = 0.0;
    input.consumption_rate = 0.0;
    input.distance_km = 0.0;
    input.regen_pct = 15.0;
    input.user_lat = 0.0;
    input.user_lon = 0.0;

    GetWindowTextBuffer(g_ctrls.editBattery, batteryText, (int)sizeof(batteryText));
    GetWindowTextBuffer(g_ctrls.editCapacity, capacityText, (int)sizeof(capacityText));
    GetWindowTextBuffer(g_ctrls.editConsumption, consumptionText, (int)sizeof(consumptionText));
    GetWindowTextBuffer(g_ctrls.editDistance, distanceText, (int)sizeof(distanceText));
    GetWindowTextBuffer(g_ctrls.editRegen, regenText, (int)sizeof(regenText));
    GetWindowTextBuffer(g_ctrls.editCity, cityText, (int)sizeof(cityText));
    TrimInPlace(cityText);

    if (cityText[0] == '\0') {
        strncpy(input.error, "City address is required (example: Quezon City).", sizeof(input.error) - 1);
        return input;
    }
    strncpy(input.city_input, cityText, sizeof(input.city_input) - 1);
    input.city_input[sizeof(input.city_input) - 1] = '\0';
    if (!ResolveCityCoordinates(cityText, input.matched_city, sizeof(input.matched_city), &input.user_lat, &input.user_lon)) {
        strncpy(input.error, "City not recognized. Use a Luzon city (e.g., Manila, Quezon City, Baguio, Naga).", sizeof(input.error) - 1);
        return input;
    }

    if (!ParseDoubleStrict(batteryText, &input.battery_pct)) {
        strncpy(input.error, "Battery % must be numeric and between 1 and 100.", sizeof(input.error) - 1);
        return input;
    }
    if (input.battery_pct < 1.0 || input.battery_pct > 100.0) {
        strncpy(input.error, "Battery % must be between 1 and 100 (0 is not allowed).", sizeof(input.error) - 1);
        return input;
    }

    if (!ParseDoubleStrict(capacityText, &input.capacity_kwh)) {
        strncpy(input.error, "Capacity kWh must be numeric and between 10 and 200.", sizeof(input.error) - 1);
        return input;
    }
    if (input.capacity_kwh < 10.0 || input.capacity_kwh > 200.0) {
        strncpy(input.error, "Capacity kWh must be between 10 and 200.", sizeof(input.error) - 1);
        return input;
    }

    if (!ParseDoubleStrict(consumptionText, &input.consumption_rate)) {
        strncpy(input.error, "Consumption rate must be numeric and between 5 and 50 kWh/100km.", sizeof(input.error) - 1);
        return input;
    }
    if (input.consumption_rate < 5.0 || input.consumption_rate > 50.0) {
        strncpy(input.error, "Consumption rate must be between 5 and 50 kWh/100km.", sizeof(input.error) - 1);
        return input;
    }

    if (!ParseDoubleStrict(distanceText, &input.distance_km)) {
        strncpy(input.error, "Distance must be numeric and greater than 0 km.", sizeof(input.error) - 1);
        return input;
    }
    if (input.distance_km <= 0.0) {
        strncpy(input.error, "Distance must be greater than 0 km.", sizeof(input.error) - 1);
        return input;
    }

    TrimInPlace(regenText);
    if (regenText[0] != '\0') {
        if (!ParseDoubleStrict(regenText, &input.regen_pct)) {
            strncpy(input.error, "Regen efficiency must be numeric and between 0 and 100.", sizeof(input.error) - 1);
            return input;
        }
    } else {
        input.regen_pct = 15.0;
        SetWindowTextA(g_ctrls.editRegen, "15");
    }
    if (input.regen_pct < 0.0 || input.regen_pct > 100.0) {
        strncpy(input.error, "Regen efficiency must be between 0 and 100.", sizeof(input.error) - 1);
        return input;
    }

    input.is_valid = 1;
    return input;
}

static double calculate_available_energy(double battery_pct, double capacity_kwh) {
    return (battery_pct / 100.0) * capacity_kwh;
}

static double calculate_energy_used(double consumption_rate, double distance_km) {
    return (consumption_rate / 100.0) * distance_km;
}

static double calculate_regen_recovery(double energy_used, double regen_pct) {
    return energy_used * (regen_pct / 100.0);
}

static RemainingBattery calculate_remaining_battery(double available, double used, double regen) {
    RemainingBattery out;
    double net_used = used - regen;
    out.remaining_kwh = available - net_used;
    out.clamped_to_zero = 0;
    if (out.remaining_kwh < 0.0) {
        out.remaining_kwh = 0.0;
        out.clamped_to_zero = 1;
    }
    if (g_capacity_for_percentage > 0.0) {
        out.remaining_pct = (out.remaining_kwh / g_capacity_for_percentage) * 100.0;
    } else {
        out.remaining_pct = 0.0;
    }
    if (out.remaining_pct < 0.0) {
        out.remaining_pct = 0.0;
    }
    return out;
}

static const char* generate_recommendation(double remaining_pct, double remaining_kwh) {
    if (remaining_pct < 20.0 || remaining_kwh <= 0.0) {
        return "CHARGING RECOMMENDED";
    }
    return "Battery sufficient - proceed with trip.";
}

static void reset_form(void) {
    SetWindowTextA(g_ctrls.editBattery, "75");
    SetWindowTextA(g_ctrls.editCapacity, "60");
    SetWindowTextA(g_ctrls.editConsumption, "18");
    SetWindowTextA(g_ctrls.editDistance, "120");
    SetWindowTextA(g_ctrls.editRegen, "15");
    SetWindowTextA(g_ctrls.editCity, "Quezon City");
    g_map_url[0] = '\0';
    SetWindowTextA(g_ctrls.output, "Enter values and click Compute Plan.");
}

static void ComputePlan(HWND hWnd) {
    UserInput in = get_user_input();
    RemainingBattery rem;
    char output[8192];
    char line[512];
    double available = 0.0;
    double gross_used = 0.0;
    double regen = 0.0;
    double net_used = 0.0;
    double max_range = 0.0;
    double charging_kwh_to_20 = 0.0;
    double charging_minutes = 0.0;
    int trip_exceeds_range = 0;
    int nearestIdx[3] = {-1, -1, -1};
    double nearestDist[3] = {0.0, 0.0, 0.0};
    const char* recommendation;
    int i;

    if (!in.is_valid) {
        ShowValidationError(hWnd, in.error);
        return;
    }

    g_capacity_for_percentage = in.capacity_kwh;
    available = calculate_available_energy(in.battery_pct, in.capacity_kwh);
    gross_used = calculate_energy_used(in.consumption_rate, in.distance_km);
    regen = calculate_regen_recovery(gross_used, in.regen_pct);
    if (regen > gross_used) {
        regen = gross_used;
    }
    net_used = gross_used - regen;
    rem = calculate_remaining_battery(available, gross_used, regen);
    max_range = available / (in.consumption_rate / 100.0);
    recommendation = generate_recommendation(rem.remaining_pct, rem.remaining_kwh);
    trip_exceeds_range = (in.distance_km > max_range) ? 1 : 0;

    FindNearestStations(in.user_lat, in.user_lon, nearestIdx, nearestDist, 3);
    BuildMapUrl(in.user_lat, in.user_lon, nearestIdx, 3);

    output[0] = '\0';
    AppendLine(output, sizeof(output), "----------------------------------------------------");
    AppendLine(output, sizeof(output), "RESULTS");
    AppendLine(output, sizeof(output), "----------------------------------------------------");
    snprintf(line, sizeof(line), "Input city: %s (matched: %s)", in.city_input, in.matched_city);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Available energy: %.2f kWh", available);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Gross energy used: %.2f kWh", gross_used);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Regen recovered: %.2f kWh", regen);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Net energy used: %.2f kWh", net_used);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Remaining battery: %.1f%% (%.2f kWh)", rem.remaining_pct, rem.remaining_kwh);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Estimated max range: %.1f km", max_range);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Charging needed? %s", (rem.remaining_pct < 20.0) ? "YES" : "NO");
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Status: %s", recommendation);
    AppendLine(output, sizeof(output), line);

    if (trip_exceeds_range) {
        AppendLine(output, sizeof(output), "ADVISORY: Trip exceeds current range.");
    }
    if (rem.clamped_to_zero) {
        AppendLine(output, sizeof(output), "WARNING: Remaining battery clamped to 0%.");
    }

    if (rem.remaining_pct < 20.0) {
        AppendLine(output, sizeof(output), "*** CHARGING RECOMMENDED ***");
        charging_kwh_to_20 = ((20.0 - rem.remaining_pct) / 100.0) * in.capacity_kwh;
        if (charging_kwh_to_20 < 0.0) {
            charging_kwh_to_20 = 0.0;
        }
        charging_minutes = (charging_kwh_to_20 / 7.2) * 60.0;
        snprintf(line, sizeof(line), "Estimated charging time needed: ~%.0f min (Level 2)", charging_minutes);
        AppendLine(output, sizeof(output), line);
    }

    AppendLine(output, sizeof(output), "Nearest EV charging stations in Luzon:");
    for (i = 0; i < 3; ++i) {
        if (nearestIdx[i] >= 0) {
            const ChargingStation* s = &kLuzonStations[nearestIdx[i]];
            snprintf(
                line,
                sizeof(line),
                "%d. %s - %s, %s [%s, %.1f kW] (%.1f km)",
                i + 1, s->name, s->address, s->city, s->type, s->power_kw, nearestDist[i]
            );
            AppendLine(output, sizeof(output), line);
        }
    }

    AppendLine(output, sizeof(output), "Map pins are ready. Click 'Open Map Pins' to view highlighted nearest stations.");
    AppendLine(output, sizeof(output), "----------------------------------------------------");
    SetWindowTextA(g_ctrls.output, output);

    if (rem.remaining_pct < 20.0 && g_map_url[0] != '\0') {
        OpenNearestMap();
    }
}

static void LayoutControls(HWND hWnd) {
    RECT rc = {0};
    int margin = 12;
    int labelW = 260;
    int editW = 250;
    int rowH = 24;
    int rowGap = 8;
    int btnH = 30;
    int top = 38;
    int y;
    int outputH;

    GetClientRect(hWnd, &rc);
    y = top;

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
    y += rowH + rowGap;

    MoveWindow(g_ctrls.lblCity, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editCity, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + 12;

    MoveWindow(g_ctrls.btnCompute, margin, y, 170, btnH, TRUE);
    MoveWindow(g_ctrls.btnReset, margin + 178, y, 120, btnH, TRUE);
    MoveWindow(g_ctrls.btnOpenMap, margin + 306, y, 170, btnH, TRUE);
    y += btnH + 12;

    outputH = rc.bottom - y - margin;
    if (outputH < 180) {
        outputH = 180;
    }
    MoveWindow(g_ctrls.output, margin, y, rc.right - (2 * margin), outputH, TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND controls[16];
            int i;

            g_ctrls.lblBattery = CreateWindowA("STATIC", "Battery level (%) [1-100]:",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editBattery = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "75",
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

            g_ctrls.lblCity = CreateWindowA("STATIC", "City address (Luzon):",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editCity = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "Quezon City",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CITY, NULL, NULL);

            g_ctrls.btnCompute = CreateWindowA("BUTTON", "Compute Plan",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_COMPUTE, NULL, NULL);
            g_ctrls.btnReset = CreateWindowA("BUTTON", "Reset",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_RESET, NULL, NULL);
            g_ctrls.btnOpenMap = CreateWindowA("BUTTON", "Open Map Pins",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_OPEN_MAP, NULL, NULL);

            g_ctrls.output = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                0, 0, 0, 0, hWnd, (HMENU)IDC_OUTPUT, NULL, NULL);

            controls[0] = g_ctrls.lblBattery; controls[1] = g_ctrls.editBattery;
            controls[2] = g_ctrls.lblCapacity; controls[3] = g_ctrls.editCapacity;
            controls[4] = g_ctrls.lblConsumption; controls[5] = g_ctrls.editConsumption;
            controls[6] = g_ctrls.lblDistance; controls[7] = g_ctrls.editDistance;
            controls[8] = g_ctrls.lblRegen; controls[9] = g_ctrls.editRegen;
            controls[10] = g_ctrls.lblCity; controls[11] = g_ctrls.editCity;
            controls[12] = g_ctrls.btnCompute; controls[13] = g_ctrls.btnReset;
            controls[14] = g_ctrls.btnOpenMap; controls[15] = g_ctrls.output;

            for (i = 0; i < 16; ++i) {
                SendMessageA(controls[i], WM_SETFONT, (WPARAM)hFont, TRUE);
            }

            LayoutControls(hWnd);
            reset_form();
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
            if (LOWORD(wParam) == IDC_BTN_RESET && HIWORD(wParam) == BN_CLICKED) {
                reset_form();
                return 0;
            }
            if (LOWORD(wParam) == IDC_BTN_OPEN_MAP && HIWORD(wParam) == BN_CLICKED) {
                OpenNearestMap();
                return 0;
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc = {0};
            RECT titleRect;
            GetClientRect(hWnd, &rc);
            titleRect.left = 12;
            titleRect.top = 10;
            titleRect.right = rc.right - 12;
            titleRect.bottom = 30;
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, "EV Charging & Range Planner - Luzon Stations", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* kClassName = "EVPlannerWindowClass";
    WNDCLASSA wc = {0};
    HWND hWnd;
    MSG msg;
    (void)hPrevInstance;
    (void)lpCmdLine;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Failed to register window class.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    hWnd = CreateWindowExA(
        0, kClassName, "EV Charging & Range Planner",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 700,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        MessageBoxA(NULL, "Failed to create main window.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
