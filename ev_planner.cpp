#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace planner {

const double kDefaultRegenEfficiencyPct = 15.0;
const double kSafetyThresholdPct = 20.0;
const double kLevel2ChargingKW = 7.2;  // Level 2 charger at 7.2 kW

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
    int charging_time_minutes;

    Result()
        : available_kwh(0.0),
          gross_energy_used_kwh(0.0),
          regen_recovered_kwh(0.0),
          net_energy_used_kwh(0.0),
          remaining_kwh(0.0),
          remaining_pct(0.0),
          max_range_km(0.0),
          charging_needed(false),
          charging_time_minutes(0) {}
};

struct ChargingStation {
    std::string name;
    std::string address;
    std::string city;
    std::string charger_type;
    double power_kw;
    double latitude;
    double longitude;
    double distance_km;
    
    ChargingStation() : power_kw(0.0), latitude(0.0), longitude(0.0), distance_km(0.0) {}
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

double calculate_remaining_battery(double available_kwh, double net_energy_used_kwh) {
    return std::max(0.0, available_kwh - net_energy_used_kwh);
}

int calculate_charging_time(double remaining_pct, double capacity_kwh) {
    if (remaining_pct >= kSafetyThresholdPct) {
        return 0;
    }
    // Time to charge from remaining_pct to 20%: ((20 - remaining_pct) / 100) * capacity / 7.2 * 60
    double energy_needed = ((kSafetyThresholdPct - remaining_pct) / 100.0) * capacity_kwh;
    double time_hours = energy_needed / kLevel2ChargingKW;
    return static_cast<int>(std::round(time_hours * 60.0));
}

Result compute(const Inputs& in) {
    Result r;
    r.available_kwh = calculate_available_energy(in.battery_pct, in.battery_capacity_kwh);
    r.gross_energy_used_kwh = calculate_energy_used(in.consumption_kwh_per_100km, in.trip_distance_km);
    r.regen_recovered_kwh = calculate_regen_recovery(r.gross_energy_used_kwh, in.regen_efficiency_pct);
    r.regen_recovered_kwh = std::min(r.regen_recovered_kwh, r.gross_energy_used_kwh);
    r.net_energy_used_kwh = r.gross_energy_used_kwh - r.regen_recovered_kwh;
    r.remaining_kwh = calculate_remaining_battery(r.available_kwh, r.net_energy_used_kwh);
    r.remaining_pct = std::max(0.0, (r.remaining_kwh / in.battery_capacity_kwh) * 100.0);
    r.max_range_km = r.available_kwh / (in.consumption_kwh_per_100km / 100.0);
    r.charging_needed = r.remaining_pct < kSafetyThresholdPct;
    r.charging_time_minutes = calculate_charging_time(r.remaining_pct, in.battery_capacity_kwh);
    return r;
}

}  // namespace planner

enum ControlId {
    IDC_BATTERY = 101,
    IDC_CAPACITY,
    IDC_CONSUMPTION,
    IDC_DISTANCE,
    IDC_REGEN_EFF,
    IDC_CITY,
    IDC_COMPUTE,
    IDC_CLEAR,
    IDC_RESULTS
};

HWND g_hEdits[6] = {0};
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
    if (ws.empty()) { 
        return ""; 
    }
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), NULL, 0, NULL, NULL);
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

// Haversine formula to calculate distance between two lat/lon points (in km)
double calculate_distance(double lat1, double lon1, double lat2, double lon2) {
    const double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
    const double EARTH_RADIUS_KM = 6371.0;
    
    double lat1_rad = lat1 * DEG_TO_RAD;
    double lat2_rad = lat2 * DEG_TO_RAD;
    double delta_lat = (lat2 - lat1) * DEG_TO_RAD;
    double delta_lon = (lon2 - lon1) * DEG_TO_RAD;
    
    double a = std::sin(delta_lat / 2.0) * std::sin(delta_lat / 2.0) +
               std::cos(lat1_rad) * std::cos(lat2_rad) *
               std::sin(delta_lon / 2.0) * std::sin(delta_lon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    
    return EARTH_RADIUS_KM * c;
}

// City coordinates mapping for Luzon
struct CityCoord {
    std::string city;
    double latitude;
    double longitude;
};

std::vector<CityCoord> get_city_coordinates() {
    std::vector<CityCoord> cities;
    
    CityCoord c1; c1.city = "Manila"; c1.latitude = 14.5995; c1.longitude = 120.9842; cities.push_back(c1);
    CityCoord c2; c2.city = "Quezon City"; c2.latitude = 14.6760; c2.longitude = 121.0437; cities.push_back(c2);
    CityCoord c3; c3.city = "Makati"; c3.latitude = 14.5547; c3.longitude = 121.0244; cities.push_back(c3);
    CityCoord c4; c4.city = "Pasig"; c4.latitude = 14.5794; c4.longitude = 121.0582; cities.push_back(c4);
    CityCoord c5; c5.city = "Taguig"; c5.latitude = 14.5162; c5.longitude = 121.0313; cities.push_back(c5);
    CityCoord c6; c6.city = "Las Piñas"; c6.latitude = 14.3531; c6.longitude = 120.9268; cities.push_back(c6);
    CityCoord c7; c7.city = "Caloocan"; c7.latitude = 14.6348; c7.longitude = 120.9803; cities.push_back(c7);
    CityCoord c8; c8.city = "Marikina"; c8.latitude = 14.6459; c8.longitude = 121.1100; cities.push_back(c8);
    CityCoord c9; c9.city = "Cavite"; c9.latitude = 14.3159; c9.longitude = 120.9075; cities.push_back(c9);
    CityCoord c10; c10.city = "Laguna"; c10.latitude = 14.3122; c10.longitude = 121.4312; cities.push_back(c10);
    CityCoord c11; c11.city = "Batangas"; c11.latitude = 13.7521; c11.longitude = 121.0128; cities.push_back(c11);
    CityCoord c12; c12.city = "Pampanga"; c12.latitude = 15.0955; c12.longitude = 120.8373; cities.push_back(c12);
    CityCoord c13; c13.city = "Nueva Ecija"; c13.latitude = 15.3306; c13.longitude = 121.0066; cities.push_back(c13);
    CityCoord c14; c14.city = "Rizal"; c14.latitude = 14.6280; c14.longitude = 121.3118; cities.push_back(c14);
    CityCoord c15; c15.city = "Bulacan"; c15.latitude = 14.7597; c15.longitude = 120.8241; cities.push_back(c15);
    CityCoord c16; c16.city = "Cabanatuan"; c16.latitude = 15.4857; c16.longitude = 121.1294; cities.push_back(c16);
    CityCoord c17; c17.city = "Baguio"; c17.latitude = 16.4023; c17.longitude = 120.5960; cities.push_back(c17);
    CityCoord c18; c18.city = "Pangasinan"; c18.latitude = 16.0194; c18.longitude = 120.3328; cities.push_back(c18);
    
    return cities;
}

// Get user's city coordinates (case-insensitive)
bool get_city_coords(const std::string& city_input, double& out_lat, double& out_lon) {
    std::string city_lower = city_input;
    // Convert to lowercase for comparison
    for (size_t i = 0; i < city_lower.length(); ++i) {
        city_lower[i] = std::tolower(static_cast<unsigned char>(city_lower[i]));
    }
    
    std::vector<CityCoord> cities = get_city_coordinates();
    for (size_t i = 0; i < cities.size(); ++i) {
        std::string city_name = cities[i].city;
        for (size_t j = 0; j < city_name.length(); ++j) {
            city_name[j] = std::tolower(static_cast<unsigned char>(city_name[j]));
        }
        if (city_name == city_lower) {
            out_lat = cities[i].latitude;
            out_lon = cities[i].longitude;
            return true;
        }
    }
    return false;
}

// EV Charging stations across Luzon with coordinates
std::vector<planner::ChargingStation> get_luzon_charging_stations() {
    std::vector<planner::ChargingStation> stations;
    
    // Metro Manila & Surrounding Areas
    planner::ChargingStation s1; s1.name = "Petron SM Mall of Asia"; s1.city = "Manila"; 
    s1.address = "Mall of Asia Complex, Pasay"; s1.charger_type = "DC Fast (50 kW)"; s1.power_kw = 50.0; 
    s1.latitude = 14.5529; s1.longitude = 120.9403; stations.push_back(s1);
    
    planner::ChargingStation s2; s2.name = "Petron Araneta Center"; s2.city = "Quezon City"; 
    s2.address = "Araneta Center, Cubao"; s2.charger_type = "DC Fast (30 kW)"; s2.power_kw = 30.0; 
    s2.latitude = 14.6296; s2.longitude = 121.0429; stations.push_back(s2);
    
    planner::ChargingStation s3; s3.name = "Greenergy Makati Office"; s3.city = "Makati"; 
    s3.address = "Ayala Avenue, Makati"; s3.charger_type = "Level 2 (7.2 kW)"; s3.power_kw = 7.2; 
    s3.latitude = 14.5565; s3.longitude = 121.0248; stations.push_back(s3);
    
    planner::ChargingStation s4; s4.name = "BGC Financial Center"; s4.city = "Taguig"; 
    s4.address = "Bonifacio Global City"; s4.charger_type = "DC Fast (50 kW)"; s4.power_kw = 50.0; 
    s4.latitude = 14.5589; s4.longitude = 121.0355; stations.push_back(s4);
    
    planner::ChargingStation s5; s5.name = "Pasig Rizal Park Station"; s5.city = "Pasig"; 
    s5.address = "Rizal Park, Pasig"; s5.charger_type = "Level 2 (7.2 kW)"; s5.power_kw = 7.2; 
    s5.latitude = 14.5841; s5.longitude = 121.0603; stations.push_back(s5);
    
    planner::ChargingStation s6; s6.name = "Las Piñas Mall Charging"; s6.city = "Las Piñas"; 
    s6.address = "SM Mall Las Piñas"; s6.charger_type = "DC Fast (30 kW)"; s6.power_kw = 30.0; 
    s6.latitude = 14.3551; s6.longitude = 120.9298; stations.push_back(s6);
    
    planner::ChargingStation s7; s7.name = "Caloocan EDSA Hub"; s7.city = "Caloocan"; 
    s7.address = "EDSA, Caloocan"; s7.charger_type = "Level 2 (7.2 kW)"; s7.power_kw = 7.2; 
    s7.latitude = 14.6348; s7.longitude = 120.9803; stations.push_back(s7);
    
    planner::ChargingStation s8; s8.name = "Marikina Corporate Park"; s8.city = "Marikina"; 
    s8.address = "Corporate Park, Marikina"; s8.charger_type = "DC Fast (30 kW)"; s8.power_kw = 30.0; 
    s8.latitude = 14.6459; s8.longitude = 121.1100; stations.push_back(s8);
    
    // Cavite
    planner::ChargingStation s9; s9.name = "Cavite Power Station"; s9.city = "Cavite"; 
    s9.address = "Kawit Highway, Cavite"; s9.charger_type = "DC Fast (50 kW)"; s9.power_kw = 50.0; 
    s9.latitude = 14.3159; s9.longitude = 120.9075; stations.push_back(s9);
    
    planner::ChargingStation s10; s10.name = "Dasmarinas Charging Hub"; s10.city = "Cavite"; 
    s10.address = "Aguinaldo Highway, Dasmarinas"; s10.charger_type = "Level 2 (7.2 kW)"; s10.power_kw = 7.2; 
    s10.latitude = 14.2969; s10.longitude = 120.8729; stations.push_back(s10);
    
    // Laguna
    planner::ChargingStation s11; s11.name = "Laguna Business Park Charger"; s11.city = "Laguna"; 
    s11.address = "Laguna Technopark, Santa Rosa"; s11.charger_type = "DC Fast (30 kW)"; s11.power_kw = 30.0; 
    s11.latitude = 14.3122; s11.longitude = 121.4312; stations.push_back(s11);
    
    planner::ChargingStation s12; s12.name = "Calamba EV Station"; s12.city = "Laguna"; 
    s12.address = "Tunasan, Calamba"; s12.charger_type = "Level 2 (7.2 kW)"; s12.power_kw = 7.2; 
    s12.latitude = 14.1994; s12.longitude = 121.1747; stations.push_back(s12);
    
    // Batangas
    planner::ChargingStation s13; s13.name = "Batangas City Charging"; s13.city = "Batangas"; 
    s13.address = "Batangas City Port Area"; s13.charger_type = "DC Fast (30 kW)"; s13.power_kw = 30.0; 
    s13.latitude = 13.7521; s13.longitude = 121.0128; stations.push_back(s13);
    
    planner::ChargingStation s14; s14.name = "Tagaytay EV Rest Stop"; s14.city = "Batangas"; 
    s14.address = "Tagaytay-Nasugbu Road"; s14.charger_type = "Level 2 (7.2 kW)"; s14.power_kw = 7.2; 
    s14.latitude = 13.8753; s14.longitude = 120.9569; stations.push_back(s14);
    
    // Pampanga
    planner::ChargingStation s15; s15.name = "Pampanga Industrial Zone"; s15.city = "Pampanga"; 
    s15.address = "San Fernando, Pampanga"; s15.charger_type = "DC Fast (50 kW)"; s15.power_kw = 50.0; 
    s15.latitude = 15.0955; s15.longitude = 120.8373; stations.push_back(s15);
    
    planner::ChargingStation s16; s16.name = "Clark Freeport Charging"; s16.city = "Pampanga"; 
    s16.address = "Clark Freeport Zone"; s16.charger_type = "Level 2 (7.2 kW)"; s16.power_kw = 7.2; 
    s16.latitude = 15.1797; s16.longitude = 120.5430; stations.push_back(s16);
    
    // Nueva Ecija
    planner::ChargingStation s17; s17.name = "Cabanatuan City Hub"; s17.city = "Nueva Ecija"; 
    s17.address = "Cabanatuan City Center"; s17.charger_type = "DC Fast (30 kW)"; s17.power_kw = 30.0; 
    s17.latitude = 15.4857; s17.longitude = 121.1294; stations.push_back(s17);
    
    planner::ChargingStation s18; s18.name = "San Fernando EV Pit Stop"; s18.city = "Nueva Ecija"; 
    s18.address = "San Fernando, Nueva Ecija"; s18.charger_type = "Level 2 (7.2 kW)"; s18.power_kw = 7.2; 
    s18.latitude = 15.6181; s18.longitude = 120.9847; stations.push_back(s18);
    
    // Rizal
    planner::ChargingStation s19; s19.name = "Cainta EV Station"; s19.city = "Rizal"; 
    s19.address = "Cainta, Rizal"; s19.charger_type = "Level 2 (7.2 kW)"; s19.power_kw = 7.2; 
    s19.latitude = 14.5683; s19.longitude = 121.3264; stations.push_back(s19);
    
    planner::ChargingStation s20; s20.name = "Antipolo Charging Hub"; s20.city = "Rizal"; 
    s20.address = "Antipolo City"; s20.charger_type = "DC Fast (30 kW)"; s20.power_kw = 30.0; 
    s20.latitude = 14.5945; s20.longitude = 121.1751; stations.push_back(s20);
    
    // Bulacan
    planner::ChargingStation s21; s21.name = "Meycauayan Port Charging"; s21.city = "Bulacan"; 
    s21.address = "Meycauayan, Bulacan"; s21.charger_type = "Level 2 (7.2 kW)"; s21.power_kw = 7.2; 
    s21.latitude = 14.7559; s21.longitude = 120.9631; stations.push_back(s21);
    
    planner::ChargingStation s22; s22.name = "Bulacan Business District"; s22.city = "Bulacan"; 
    s22.address = "Santa Maria, Bulacan"; s22.charger_type = "DC Fast (30 kW)"; s22.power_kw = 30.0; 
    s22.latitude = 14.8141; s22.longitude = 120.9158; stations.push_back(s22);
    
    // Baguio
    planner::ChargingStation s23; s23.name = "Baguio City Main Station"; s23.city = "Baguio"; 
    s23.address = "Session Road, Baguio"; s23.charger_type = "DC Fast (30 kW)"; s23.power_kw = 30.0; 
    s23.latitude = 16.4023; s23.longitude = 120.5960; stations.push_back(s23);
    
    // Pangasinan
    planner::ChargingStation s24; s24.name = "Dagupan City EV Hub"; s24.city = "Pangasinan"; 
    s24.address = "Dagupan City"; s24.charger_type = "Level 2 (7.2 kW)"; s24.power_kw = 7.2; 
    s24.latitude = 16.0194; s24.longitude = 120.3328; stations.push_back(s24);
    
    // Quezon Province
    planner::ChargingStation s25; s25.name = "Lucena City Charging"; s25.city = "Quezon"; 
    s25.address = "Lucena City"; s25.charger_type = "DC Fast (30 kW)"; s25.power_kw = 30.0; 
    s25.latitude = 13.9314; s25.longitude = 121.6145; stations.push_back(s25);
    
    return stations;
}

// Find 3 nearest charging stations based on user's city
std::vector<planner::ChargingStation> find_nearest_stations(const std::string& user_city) {
    double user_lat, user_lon;
    std::vector<planner::ChargingStation> result;
    
    // Get user's city coordinates
    if (!get_city_coords(user_city, user_lat, user_lon)) {
        // City not found, return empty
        return result;
    }
    
    std::vector<planner::ChargingStation> all_stations = get_luzon_charging_stations();
    
    // Calculate distance for each station
    for (size_t i = 0; i < all_stations.size(); ++i) {
        all_stations[i].distance_km = calculate_distance(user_lat, user_lon, 
                                                         all_stations[i].latitude, 
                                                         all_stations[i].longitude);
    }
    
    // Sort by distance
    for (size_t i = 0; i < all_stations.size(); ++i) {
        for (size_t j = i + 1; j < all_stations.size(); ++j) {
            if (all_stations[j].distance_km < all_stations[i].distance_km) {
                planner::ChargingStation temp = all_stations[i];
                all_stations[i] = all_stations[j];
                all_stations[j] = temp;
            }
        }
    }
    
    // Return top 3
    for (size_t i = 0; i < 3 && i < all_stations.size(); ++i) {
        result.push_back(all_stations[i]);
    }
    
    return result;
}

void set_default_text() {
    SetWindowTextA(g_hEdits[0], "50");
    SetWindowTextA(g_hEdits[1], "60");
    SetWindowTextA(g_hEdits[2], "18");
    SetWindowTextA(g_hEdits[3], "120");
    SetWindowTextA(g_hEdits[4], "15");
    SetWindowTextA(g_hEdits[5], "Manila");
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
        
        // Get city input
        std::string user_city = ws2s(get_window_text(g_hEdits[5]));
        if (user_city.empty()) {
            throw std::runtime_error("City address is required for finding charging stations.");
        }

        const planner::Result r = planner::compute(in);

        std::ostringstream out;
        out << std::fixed << std::setprecision(2);
        
        out << "=== ENERGY CALCULATION ===\r\n";
        out << "Available energy: " << r.available_kwh << " kWh\r\n";
        out << "Gross energy used: " << r.gross_energy_used_kwh << " kWh\r\n";
        out << "Regen recovered: " << r.regen_recovered_kwh << " kWh\r\n";
        out << "Net energy used: " << r.net_energy_used_kwh << " kWh\r\n";
        
        out << "\r\n=== BATTERY STATUS ===\r\n";
        out << "Remaining battery: " << r.remaining_pct << "% (" << r.remaining_kwh << " kWh)\r\n";
        out << "Estimated max range: " << r.max_range_km << " km\r\n";
        
        if (in.trip_distance_km > r.max_range_km) {
            out << "\r\n*** ADVISORY: Trip distance (" << in.trip_distance_km << " km) exceeds max range! ***\r\n";
        }
        
        out << "\r\n=== CHARGING RECOMMENDATION ===\r\n";
        out << "Charging needed? " << (r.charging_needed ? "YES" : "NO") << "\r\n";
        
        if (r.charging_needed) {
            out << "Status: CHARGING RECOMMENDED\r\n";
            out << "Reason: Battery below 20% safety threshold on arrival.\r\n";
            out << "Estimated charging time: " << r.charging_time_minutes << " minutes (Level 2 @ 7.2 kW)\r\n";
            
            // Find nearest charging stations based on user's city
            std::vector<planner::ChargingStation> stations = find_nearest_stations(user_city);
            
            if (stations.empty()) {
                out << "\r\n=== NEARBY CHARGING STATIONS ===\r\n";
                out << "City '" << user_city << "' not found in database.\r\n";
                out << "Supported cities: Manila, Quezon City, Makati, Taguig, Pasig, Las Piñas,\r\n";
                out << "Caloocan, Marikina, Cavite, Laguna, Batangas, Pampanga, Nueva Ecija,\r\n";
                out << "Rizal, Bulacan, Baguio, Pangasinan, Quezon\r\n";
            } else {
                out << "\r\n=== NEAREST CHARGING STATIONS IN LUZON ===\r\n";
                out << "Your location: " << user_city << "\r\n\r\n";
                
                for (size_t i = 0; i < stations.size(); ++i) {
                    out << "[Station " << (i + 1) << " - " << std::setprecision(1) 
                        << stations[i].distance_km << " km away]\r\n";
                    out << std::setprecision(2);
                    out << "  Name: " << stations[i].name << "\r\n";
                    out << "  City: " << stations[i].city << "\r\n";
                    out << "  Address: " << stations[i].address << "\r\n";
                    out << "  Type: " << stations[i].charger_type << "\r\n";
                    out << "  Power: " << stations[i].power_kw << " kW\r\n";
                    if (i < stations.size() - 1) {
                        out << "\r\n";
                    }
                }
            }
        } else {
            out << "Status: Battery sufficient for this trip.\r\n";
            out << "Safety margin: " << (r.remaining_pct - planner::kSafetyThresholdPct) 
                << "% above minimum threshold.\r\n";
        }

        SetWindowTextW(g_hResults, s2ws(out.str()).c_str());
    } catch (const std::exception& ex) {
        MessageBoxW(hWnd, s2ws(ex.what()).c_str(), L"Input Error", MB_OK | MB_ICONWARNING);
    }
}

void clear_inputs(HWND hWnd) {
    (void)hWnd;
    set_default_text();
    SetWindowTextW(g_hResults, L"");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            create_label(hWnd, 20, 18, 740, 24, L"Electric Vehicle Charging & Range Planner");

            const wchar_t* labels[6] = {L"Battery (%)",
                                        L"Capacity (kWh)",
                                        L"Consumption (kWh/100km)",
                                        L"Distance (km)",
                                        L"Regen efficiency (%)",
                                        L"City Address"};

            int y = 56;
            for (int i = 0; i < 6; ++i) {
                create_label(hWnd, 20, y + 4, 210, 22, labels[i]);
                g_hEdits[i] = create_edit(hWnd, IDC_BATTERY + i, 240, y, 150, 24);
                y += 32;
            }

            HWND hCompute = CreateWindowExW(0,
                                            L"BUTTON",
                                            L"Compute Plan",
                                            WS_CHILD | WS_VISIBLE,
                                            20,
                                            246,
                                            150,
                                            34,
                                            hWnd,
                                            reinterpret_cast<HMENU>(IDC_COMPUTE),
                                            NULL,
                                            NULL);
            apply_font(hCompute);

            HWND hClear = CreateWindowExW(0,
                                          L"BUTTON",
                                          L"Clear",
                                          WS_CHILD | WS_VISIBLE,
                                          180,
                                          246,
                                          150,
                                          34,
                                          hWnd,
                                          reinterpret_cast<HMENU>(IDC_CLEAR),
                                          NULL,
                                          NULL);
            apply_font(hClear);

            g_hResults = CreateWindowExW(WS_EX_CLIENTEDGE,
                                         L"EDIT",
                                         L"",
                                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                                             WS_VSCROLL,
                                         20,
                                         296,
                                         740,
                                         314,
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
            } else if (id == IDC_CLEAR && HIWORD(wParam) == BN_CLICKED) {
                clear_inputs(hWnd);
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
                                700,
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
