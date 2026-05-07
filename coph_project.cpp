#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_ROUTE_STATIONS 10

enum ControlId {
    IDC_EDIT_BATTERY = 1001,
    IDC_EDIT_CAPACITY,
    IDC_EDIT_CONSUMPTION,
    IDC_EDIT_DISTANCE,
    IDC_EDIT_REGEN,
    IDC_EDIT_CHARGING_NEEDED,
    IDC_EDIT_CUR_STREET,
    IDC_EDIT_CUR_CITY,
    IDC_EDIT_CUR_PROVINCE,
    IDC_EDIT_CUR_REGION,
    IDC_EDIT_DEST_STREET,
    IDC_EDIT_DEST_CITY,
    IDC_EDIT_DEST_PROVINCE,
    IDC_EDIT_DEST_REGION,
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
    int charging_needed_manual;
    char cur_street[128];
    char cur_city[64];
    char cur_province[64];
    char cur_region[64];
    char dest_street[128];
    char dest_city[64];
    char dest_province[64];
    char dest_region[64];
    double cur_lat;
    double cur_lon;
    double dest_lat;
    double dest_lon;
    char cur_city_label[64];
    char dest_city_label[64];
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
    HWND lblChargingNeeded;
    HWND lblCurStreet;
    HWND lblCurCity;
    HWND lblCurProvince;
    HWND lblCurRegion;
    HWND lblDestStreet;
    HWND lblDestCity;
    HWND lblDestProvince;
    HWND lblDestRegion;
    HWND editBattery;
    HWND editCapacity;
    HWND editConsumption;
    HWND editDistance;
    HWND editRegen;
    HWND editChargingNeeded;
    HWND editCurStreet;
    HWND editCurCity;
    HWND editCurProvince;
    HWND editCurRegion;
    HWND editDestStreet;
    HWND editDestCity;
    HWND editDestProvince;
    HWND editDestRegion;
    HWND btnCompute;
    HWND btnReset;
    HWND btnOpenMap;
    HWND output;
} AppControls;

// Centralized station and city datasets.
#include "ph_locations_data.h"

static AppControls g_ctrls;
static double g_capacity_for_percentage = 0.0;
static int g_user_wants_charging = 0;
static char g_google_map_url[4096] = "";
static char g_route_map_path[MAX_PATH] = "";

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
    if (!src || !dst || dstSize == 0) return;
    for (i = 0; i + 1 < dstSize && src[i] != '\0'; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void UrlEncodeSimple(const char* src, char* dst, size_t dstSize) {
    size_t i = 0;
    size_t j = 0;
    if (!src || !dst || dstSize == 0) return;
    while (src[i] != '\0' && j + 1 < dstSize) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            dst[j++] = (char)c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else if (j + 3 < dstSize) {
            static const char* hex = "0123456789ABCDEF";
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0x0F];
            dst[j++] = hex[c & 0x0F];
        } else {
            break;
        }
        ++i;
    }
    dst[j] = '\0';
}

static void GetWindowTextBuffer(HWND hWnd, char* out, int outSize) {
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    GetWindowTextA(hWnd, out, outSize);
}

static int ParseDoubleStrict(const char* input, double* outValue) {
    char tmp[128];
    char* endPtr = NULL;
    double value = 0.0;
    if (!input || !outValue) return 0;
    strncpy(tmp, input, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    TrimInPlace(tmp);
    if (tmp[0] == '\0') return 0;
    value = strtod(tmp, &endPtr);
    if (endPtr == tmp) return 0;
    while (*endPtr != '\0') {
        if (!isspace((unsigned char)*endPtr)) return 0;
        ++endPtr;
    }
    *outValue = value;
    return 1;
}

static void ShowValidationError(HWND owner, const char* message) {
    MessageBoxA(owner, message, "Invalid Input", MB_OK | MB_ICONWARNING);
}

static void AppendLine(char* dst, size_t dstSize, const char* line) {
    if (!dst || !line || dstSize == 0) return;
    strncat(dst, line, dstSize - strlen(dst) - 1);
    strncat(dst, "\r\n", dstSize - strlen(dst) - 1);
}

static int ResolveCityCoordinates(const char* cityInput, char* matchedCity, size_t matchedSize, double* lat, double* lon) {
    char lower[128];
    size_t i;
    ToLowerCopy(cityInput, lower, sizeof(lower));
    for (i = 0; i < sizeof(kPhilippineCityCoords) / sizeof(kPhilippineCityCoords[0]); ++i) {
        if (strstr(lower, kPhilippineCityCoords[i].token) != NULL) {
            if (matchedCity && matchedSize > 0) {
                strncpy(matchedCity, kPhilippineCityCoords[i].label, matchedSize - 1);
                matchedCity[matchedSize - 1] = '\0';
            }
            *lat = kPhilippineCityCoords[i].lat;
            *lon = kPhilippineCityCoords[i].lon;
            return 1;
        }
    }
    // Fallback: accept any Philippine city input even if not in presets.
    if (matchedCity && matchedSize > 0) {
        strncpy(matchedCity, cityInput, matchedSize - 1);
        matchedCity[matchedSize - 1] = '\0';
    }
    // Philippines geographic center (approx) to keep route logic functional.
    *lat = 12.8797;
    *lon = 121.7740;
    return 1;
}

static void BuildAddress(char* out, size_t outSize, const char* street, const char* city, const char* province, const char* region) {
    out[0] = '\0';
    strncat(out, street, outSize - strlen(out) - 1);
    if (strlen(out) > 0) strncat(out, ", ", outSize - strlen(out) - 1);
    strncat(out, city, outSize - strlen(out) - 1);
    if (strlen(province) > 0) {
        strncat(out, ", ", outSize - strlen(out) - 1);
        strncat(out, province, outSize - strlen(out) - 1);
    }
    if (strlen(region) > 0) {
        strncat(out, ", ", outSize - strlen(out) - 1);
        strncat(out, region, outSize - strlen(out) - 1);
    }
    strncat(out, ", Philippines", outSize - strlen(out) - 1);
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

static double point_to_segment_km(double px, double py, double ax, double ay, double bx, double by, double* outT) {
    double abx = bx - ax;
    double aby = by - ay;
    double apx = px - ax;
    double apy = py - ay;
    double ab2 = abx * abx + aby * aby;
    double t = 0.0;
    double cx;
    double cy;
    if (ab2 > 0.0000001) {
        t = (apx * abx + apy * aby) / ab2;
    }
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    cx = ax + t * abx;
    cy = ay + t * aby;
    if (outT) *outT = t;
    return sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
}

static void FindStationsAlongRoute(double startLat, double startLon, double endLat, double endLon, int* outIdx, double* outDist, int maxCount) {
    int i, k;
    double midLat = (startLat + endLat) / 2.0;
    double kmPerDegLat = 111.32;
    double kmPerDegLon = 111.32 * cos(midLat * 3.141592653589793 / 180.0);
    double ax = startLon * kmPerDegLon;
    double ay = startLat * kmPerDegLat;
    double bx = endLon * kmPerDegLon;
    double by = endLat * kmPerDegLat;

    for (k = 0; k < maxCount; ++k) {
        outIdx[k] = -1;
        outDist[k] = 1e12;
    }

    for (i = 0; i < (int)(sizeof(kPhilippineStations) / sizeof(kPhilippineStations[0])); ++i) {
        double t;
        double px = kPhilippineStations[i].lon * kmPerDegLon;
        double py = kPhilippineStations[i].lat * kmPerDegLat;
        double corridor = point_to_segment_km(px, py, ax, ay, bx, by, &t);
        double routeStartDist = distance_km(startLat, startLon, kPhilippineStations[i].lat, kPhilippineStations[i].lon);
        double score = corridor + routeStartDist * 0.12;
        if (t < 0.0 || t > 1.0) {
            score += 1000.0;
        }

        for (k = 0; k < maxCount; ++k) {
            if (score < outDist[k]) {
                int shift;
                for (shift = maxCount - 1; shift > k; --shift) {
                    outDist[shift] = outDist[shift - 1];
                    outIdx[shift] = outIdx[shift - 1];
                }
                outDist[k] = score;
                outIdx[k] = i;
                break;
            }
        }
    }
}

static void BuildGoogleMapUrl(const char* originAddress, const char* destinationAddress, const int* idx, int stationCount) {
    char originEnc[512], destEnc[512], wp[1024];
    int i;
    UrlEncodeSimple(originAddress, originEnc, sizeof(originEnc));
    UrlEncodeSimple(destinationAddress, destEnc, sizeof(destEnc));
    wp[0] = '\0';

    for (i = 0; i < stationCount; ++i) {
        char part[96];
        if (idx[i] < 0) continue;
        if (wp[0] != '\0') strncat(wp, "%7C", sizeof(wp) - strlen(wp) - 1);
        snprintf(part, sizeof(part), "%.6f%%2C%.6f", kPhilippineStations[idx[i]].lat, kPhilippineStations[idx[i]].lon);
        strncat(wp, part, sizeof(wp) - strlen(wp) - 1);
    }

    snprintf(
        g_google_map_url,
        sizeof(g_google_map_url),
        "https://www.google.com/maps/dir/?api=1&origin=%s&destination=%s&travelmode=driving&waypoints=%s",
        originEnc,
        destEnc,
        wp
    );
}

static int WriteRouteMapHtml(
    const char* outputPath,
    double startLat, double startLon,
    double endLat, double endLon,
    const char* startLabel, const char* endLabel,
    const int* stationIdx, int stationCount
) {
    FILE* f = fopen(outputPath, "w");
    int i;
    if (!f) return 0;

    fprintf(f, "<!doctype html><html><head><meta charset=\"utf-8\"/><title>EV Route Pins</title>");
    fprintf(f, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>");
    fprintf(f, "<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.css\"/>");
    fprintf(f, "<style>html,body,#map{height:100%%;margin:0;} .legend{position:absolute;top:10px;left:10px;background:#fff;padding:10px;border-radius:8px;z-index:999;font-family:Segoe UI;font-size:13px;box-shadow:0 2px 8px rgba(0,0,0,.2);} .tracker{background:#0A84FF;color:#fff;padding:3px 8px;border-radius:999px;font-weight:700;border:2px solid #fff;box-shadow:0 2px 6px rgba(0,0,0,.3);} .ev-dot{width:16px;height:16px;border-radius:50%%;background:#FF0000;border:3px solid #8B0000;box-shadow:0 0 0 2px #FFFFFF,0 2px 8px rgba(0,0,0,.45);}</style>");
    fprintf(f, "</head><body><div class=\"legend\"><b>EV Charging Route Map</b><br/>Big red circles = charging stations<br/>Blue tracker = route progress</div><div id=\"map\"></div>");
    fprintf(f, "<script src=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.js\"></script><script>");
    fprintf(f, "var map=L.map('map');");
    fprintf(f, "var osm=L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'&copy; OpenStreetMap'});");
    fprintf(f, "var carto=L.tileLayer('https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png',{maxZoom:20,subdomains:'abcd',attribution:'&copy; OpenStreetMap &copy; CARTO'});");
    fprintf(f, "osm.addTo(map);");
    fprintf(f, "osm.on('tileerror',function(){if(!map.hasLayer(carto)){map.addLayer(carto);}});");
    fprintf(f, "var start=[%.6f,%.6f],end=[%.6f,%.6f];", startLat, startLon, endLat, endLon);
    fprintf(f, "var pts=[start,end];");
    fprintf(f, "var fullRoute=[];");
    fprintf(f, "var routePoints=[start];");
    fprintf(f, "var startMarker=L.marker(start).addTo(map).bindPopup('Current Location: %s');", startLabel);
    fprintf(f, "var endMarker=L.marker(end).addTo(map).bindPopup('Destination: %s');", endLabel);
    fprintf(f, "var routeLayer=L.layerGroup().addTo(map);");
    fprintf(f, "var trackerIcon=L.divIcon({className:'',html:'<div class=\"tracker\">TRACK</div>',iconSize:[58,24],iconAnchor:[29,12]});");
    fprintf(f, "var tracker=L.marker(start,{icon:trackerIcon}).addTo(map).bindPopup('Tracking from current location to destination');");

    for (i = 0; i < stationCount; ++i) {
        int id = stationIdx[i];
        if (id < 0) continue;
        fprintf(f, "var evIcon%d=L.divIcon({className:'',html:'<div class=\"ev-dot\"></div>',iconSize:[22,22],iconAnchor:[11,11]});", i + 1);
        fprintf(
            f,
            "L.marker([%.6f,%.6f],{icon:evIcon%d}).addTo(map).bindPopup('Station %d: %s<br/>%s (%s)<br/>%.1f kW');",
            kPhilippineStations[id].lat, kPhilippineStations[id].lon,
            i + 1,
            i + 1, kPhilippineStations[id].name, kPhilippineStations[id].address, kPhilippineStations[id].type, kPhilippineStations[id].power_kw
        );
        fprintf(f, "pts.push([%.6f,%.6f]);", kPhilippineStations[id].lat, kPhilippineStations[id].lon);
        fprintf(f, "routePoints.push([%.6f,%.6f]);", kPhilippineStations[id].lat, kPhilippineStations[id].lon);
    }

    fprintf(f, "routePoints.push(end);");
    fprintf(f, "async function drawRoadRoute(){");
    fprintf(f, "for(var i=0;i<routePoints.length-1;i++){");
    fprintf(f, "var a=routePoints[i],b=routePoints[i+1];");
    fprintf(f, "var fallback=false;");
    fprintf(f, "try{");
    fprintf(f, "var url='https://router.project-osrm.org/route/v1/driving/'+a[1]+','+a[0]+';'+b[1]+','+b[0]+'?overview=full&geometries=geojson';");
    fprintf(f, "var res=await fetch(url);");
    fprintf(f, "if(!res.ok) fallback=true;");
    fprintf(f, "else{");
    fprintf(f, "var data=await res.json();");
    fprintf(f, "if(!data.routes||!data.routes.length) fallback=true;");
    fprintf(f, "else{");
    fprintf(f, "var coords=data.routes[0].geometry.coordinates.map(function(c){return [c[1],c[0]];});");
    fprintf(f, "L.polyline(coords,{color:'#0066FF',weight:5,opacity:0.95}).addTo(routeLayer);");
    fprintf(f, "for(var j=0;j<coords.length;j++){pts.push(coords[j]);}");
    fprintf(f, "for(var j=0;j<coords.length;j++){fullRoute.push(coords[j]);}");
    fprintf(f, "}");
    fprintf(f, "}");
    fprintf(f, "}catch(e){fallback=true;}");
    fprintf(f, "if(fallback){L.polyline([a,b],{color:'#0066FF',weight:5,opacity:0.95}).addTo(routeLayer);pts.push(a);pts.push(b);fullRoute.push(a);fullRoute.push(b);}");
    fprintf(f, "}");
    fprintf(f, "map.fitBounds(L.latLngBounds(pts),{padding:[30,30]});");
    fprintf(f, "if(fullRoute.length===0){fullRoute=[start,end];}");
    fprintf(f, "var idx=0;");
    fprintf(f, "setInterval(function(){");
    fprintf(f, "if(!fullRoute.length) return;");
    fprintf(f, "tracker.setLatLng(fullRoute[idx]);");
    fprintf(f, "if(idx===fullRoute.length-1){idx=0;}else{idx++;}");
    fprintf(f, "},600);");
    fprintf(f, "}");
    fprintf(f, "drawRoadRoute();");
    fprintf(f, "</script></body></html>");
    fclose(f);
    return 1;
}

static void OpenNearestMap(void) {
    if (g_route_map_path[0] != '\0') {
        ShellExecuteA(NULL, "open", g_route_map_path, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    if (g_google_map_url[0] != '\0') {
        ShellExecuteA(NULL, "open", g_google_map_url, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    MessageBoxA(NULL, "No map is ready yet. Compute a plan first.", "Map", MB_OK | MB_ICONINFORMATION);
}

static UserInput get_user_input(void) {
    UserInput input;
    char batteryText[64], capacityText[64], consumptionText[64], distanceText[64], regenText[64];
    char chargeText[32];
    char lowerCharge[32];
    input.is_valid = 0;
    input.error[0] = '\0';
    input.battery_pct = 0.0;
    input.capacity_kwh = 0.0;
    input.consumption_rate = 0.0;
    input.distance_km = 0.0;
    input.regen_pct = 15.0;
    input.charging_needed_manual = 0;
    input.cur_lat = input.cur_lon = 0.0;
    input.dest_lat = input.dest_lon = 0.0;
    input.cur_city_label[0] = '\0';
    input.dest_city_label[0] = '\0';

    GetWindowTextBuffer(g_ctrls.editBattery, batteryText, (int)sizeof(batteryText));
    GetWindowTextBuffer(g_ctrls.editCapacity, capacityText, (int)sizeof(capacityText));
    GetWindowTextBuffer(g_ctrls.editConsumption, consumptionText, (int)sizeof(consumptionText));
    GetWindowTextBuffer(g_ctrls.editDistance, distanceText, (int)sizeof(distanceText));
    GetWindowTextBuffer(g_ctrls.editRegen, regenText, (int)sizeof(regenText));
    GetWindowTextBuffer(g_ctrls.editChargingNeeded, chargeText, (int)sizeof(chargeText));

    GetWindowTextBuffer(g_ctrls.editCurStreet, input.cur_street, (int)sizeof(input.cur_street));
    GetWindowTextBuffer(g_ctrls.editCurCity, input.cur_city, (int)sizeof(input.cur_city));
    GetWindowTextBuffer(g_ctrls.editCurProvince, input.cur_province, (int)sizeof(input.cur_province));
    GetWindowTextBuffer(g_ctrls.editCurRegion, input.cur_region, (int)sizeof(input.cur_region));
    GetWindowTextBuffer(g_ctrls.editDestStreet, input.dest_street, (int)sizeof(input.dest_street));
    GetWindowTextBuffer(g_ctrls.editDestCity, input.dest_city, (int)sizeof(input.dest_city));
    GetWindowTextBuffer(g_ctrls.editDestProvince, input.dest_province, (int)sizeof(input.dest_province));
    GetWindowTextBuffer(g_ctrls.editDestRegion, input.dest_region, (int)sizeof(input.dest_region));

    TrimInPlace(input.cur_street);
    TrimInPlace(input.cur_city);
    TrimInPlace(input.cur_province);
    TrimInPlace(input.cur_region);
    TrimInPlace(input.dest_street);
    TrimInPlace(input.dest_city);
    TrimInPlace(input.dest_province);
    TrimInPlace(input.dest_region);
    TrimInPlace(chargeText);

    if (input.cur_street[0] == '\0' || input.cur_city[0] == '\0') {
        strncpy(input.error, "Current location requires at least street and city.", sizeof(input.error) - 1);
        return input;
    }
    if (input.dest_street[0] == '\0' || input.dest_city[0] == '\0') {
        strncpy(input.error, "Destination requires at least street and city.", sizeof(input.error) - 1);
        return input;
    }
    ResolveCityCoordinates(input.cur_city, input.cur_city_label, sizeof(input.cur_city_label), &input.cur_lat, &input.cur_lon);
    ResolveCityCoordinates(input.dest_city, input.dest_city_label, sizeof(input.dest_city_label), &input.dest_lat, &input.dest_lon);

    ToLowerCopy(chargeText, lowerCharge, sizeof(lowerCharge));
    if (strcmp(lowerCharge, "yes") == 0 || strcmp(lowerCharge, "y") == 0) {
        input.charging_needed_manual = 1;
    } else if (strcmp(lowerCharge, "no") == 0 || strcmp(lowerCharge, "n") == 0) {
        input.charging_needed_manual = 0;
    } else {
        strncpy(input.error, "Charging needed input must be YES or NO.", sizeof(input.error) - 1);
        return input;
    }

    if (!ParseDoubleStrict(batteryText, &input.battery_pct) || input.battery_pct < 1.0 || input.battery_pct > 100.0) {
        strncpy(input.error, "Battery %% must be between 1 and 100.", sizeof(input.error) - 1);
        return input;
    }
    if (!ParseDoubleStrict(capacityText, &input.capacity_kwh) || input.capacity_kwh < 10.0 || input.capacity_kwh > 200.0) {
        strncpy(input.error, "Capacity kWh must be between 10 and 200.", sizeof(input.error) - 1);
        return input;
    }
    if (!ParseDoubleStrict(consumptionText, &input.consumption_rate) || input.consumption_rate < 5.0 || input.consumption_rate > 50.0) {
        strncpy(input.error, "Consumption rate must be between 5 and 50 kWh/100km.", sizeof(input.error) - 1);
        return input;
    }
    if (!ParseDoubleStrict(distanceText, &input.distance_km) || input.distance_km <= 0.0) {
        strncpy(input.error, "Distance must be greater than 0 km.", sizeof(input.error) - 1);
        return input;
    }

    TrimInPlace(regenText);
    if (regenText[0] != '\0') {
        if (!ParseDoubleStrict(regenText, &input.regen_pct)) {
            strncpy(input.error, "Regen efficiency must be numeric.", sizeof(input.error) - 1);
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
    if (out.remaining_pct < 0.0) out.remaining_pct = 0.0;
    return out;
}

static const char* generate_recommendation(double remaining_pct, double remaining_kwh) {
    (void)remaining_pct;
    (void)remaining_kwh;
    if (g_user_wants_charging) return "User requested charging assistance.";
    return "User selected no charging needed.";
}

static void reset_form(void) {
    SetWindowTextA(g_ctrls.editBattery, "75");
    SetWindowTextA(g_ctrls.editCapacity, "60");
    SetWindowTextA(g_ctrls.editConsumption, "18");
    SetWindowTextA(g_ctrls.editDistance, "120");
    SetWindowTextA(g_ctrls.editRegen, "15");
    SetWindowTextA(g_ctrls.editChargingNeeded, "Yes");
    SetWindowTextA(g_ctrls.editCurStreet, "Commonwealth Ave");
    SetWindowTextA(g_ctrls.editCurCity, "Quezon City");
    SetWindowTextA(g_ctrls.editCurProvince, "Metro Manila");
    SetWindowTextA(g_ctrls.editCurRegion, "NCR");
    SetWindowTextA(g_ctrls.editDestStreet, "Session Road");
    SetWindowTextA(g_ctrls.editDestCity, "Baguio");
    SetWindowTextA(g_ctrls.editDestProvince, "Benguet");
    SetWindowTextA(g_ctrls.editDestRegion, "CAR");
    g_google_map_url[0] = '\0';
    g_route_map_path[0] = '\0';
    SetWindowTextA(g_ctrls.output, "Fill in addresses and EV fields, then click Compute Plan.");
}

static void ComputePlan(HWND hWnd) {
    UserInput in = get_user_input();
    RemainingBattery rem;
    char output[16384];
    char line[512];
    char originAddress[512], destinationAddress[512];
    double available, gross_used, regen, net_used, max_range;
    int alongIdx[MAX_ROUTE_STATIONS];
    double alongScore[MAX_ROUTE_STATIONS];
    int i;
    const char* recommendation;

    if (!in.is_valid) {
        ShowValidationError(hWnd, in.error);
        return;
    }

    g_user_wants_charging = in.charging_needed_manual;
    g_capacity_for_percentage = in.capacity_kwh;

    available = calculate_available_energy(in.battery_pct, in.capacity_kwh);
    gross_used = calculate_energy_used(in.consumption_rate, in.distance_km);
    regen = calculate_regen_recovery(gross_used, in.regen_pct);
    if (regen > gross_used) regen = gross_used;
    net_used = gross_used - regen;
    rem = calculate_remaining_battery(available, gross_used, regen);
    max_range = available / (in.consumption_rate / 100.0);
    recommendation = generate_recommendation(rem.remaining_pct, rem.remaining_kwh);

    BuildAddress(originAddress, sizeof(originAddress), in.cur_street, in.cur_city, in.cur_province, in.cur_region);
    BuildAddress(destinationAddress, sizeof(destinationAddress), in.dest_street, in.dest_city, in.dest_province, in.dest_region);

    for (i = 0; i < MAX_ROUTE_STATIONS; ++i) {
        alongIdx[i] = -1;
        alongScore[i] = 0.0;
    }
    FindStationsAlongRoute(in.cur_lat, in.cur_lon, in.dest_lat, in.dest_lon, alongIdx, alongScore, MAX_ROUTE_STATIONS);
    BuildGoogleMapUrl(originAddress, destinationAddress, alongIdx, MAX_ROUTE_STATIONS);

    {
        char exePath[MAX_PATH];
        char* slash;
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        slash = strrchr(exePath, '\\');
        if (slash) {
            *(slash + 1) = '\0';
            snprintf(g_route_map_path, sizeof(g_route_map_path), "%sev_route_map.html", exePath);
        } else {
            snprintf(g_route_map_path, sizeof(g_route_map_path), "ev_route_map.html");
        }
    }

    WriteRouteMapHtml(
        g_route_map_path,
        in.cur_lat, in.cur_lon,
        in.dest_lat, in.dest_lon,
        in.cur_city_label, in.dest_city_label,
        alongIdx, MAX_ROUTE_STATIONS
    );

    output[0] = '\0';
    AppendLine(output, sizeof(output), "----------------------------------------------------");
    AppendLine(output, sizeof(output), "RESULTS");
    AppendLine(output, sizeof(output), "----------------------------------------------------");
    snprintf(line, sizeof(line), "Current location: %s", originAddress);
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Destination: %s", destinationAddress);
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
    snprintf(line, sizeof(line), "Charging needed? %s (user input)", g_user_wants_charging ? "YES" : "NO");
    AppendLine(output, sizeof(output), line);
    snprintf(line, sizeof(line), "Status: %s", recommendation);
    AppendLine(output, sizeof(output), line);

    if (rem.clamped_to_zero) {
        AppendLine(output, sizeof(output), "WARNING: Remaining battery clamped to 0%.");
    }
    if (in.distance_km > max_range) {
        AppendLine(output, sizeof(output), "ADVISORY: Trip exceeds estimated max range.");
    }

    AppendLine(output, sizeof(output), "EV charging stations selected along route:");
    for (i = 0; i < MAX_ROUTE_STATIONS; ++i) {
        if (alongIdx[i] >= 0) {
            const ChargingStation* s = &kPhilippineStations[alongIdx[i]];
            snprintf(
                line, sizeof(line),
                "%d. %s - %s, %s [%s, %.1f kW]",
                i + 1, s->name, s->address, s->city, s->type, s->power_kw
            );
            AppendLine(output, sizeof(output), line);
        }
    }

    AppendLine(output, sizeof(output), "Map opened with large red circle pins.");
    AppendLine(output, sizeof(output), "----------------------------------------------------");
    SetWindowTextA(g_ctrls.output, output);
    OpenNearestMap();
}

static void LayoutControls(HWND hWnd) {
    RECT rc = {0};
    int margin = 12;
    int labelW = 250;
    int editW = 300;
    int rowH = 22;
    int rowGap = 6;
    int btnH = 30;
    int top = 36;
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
    MoveWindow(g_ctrls.lblChargingNeeded, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editChargingNeeded, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap + 3;

    MoveWindow(g_ctrls.lblCurStreet, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editCurStreet, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;
    MoveWindow(g_ctrls.lblCurCity, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editCurCity, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;
    MoveWindow(g_ctrls.lblCurProvince, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editCurProvince, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;
    MoveWindow(g_ctrls.lblCurRegion, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editCurRegion, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap + 3;

    MoveWindow(g_ctrls.lblDestStreet, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editDestStreet, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;
    MoveWindow(g_ctrls.lblDestCity, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editDestCity, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;
    MoveWindow(g_ctrls.lblDestProvince, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editDestProvince, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + rowGap;
    MoveWindow(g_ctrls.lblDestRegion, margin, y, labelW, rowH, TRUE);
    MoveWindow(g_ctrls.editDestRegion, margin + labelW + 8, y, editW, rowH, TRUE);
    y += rowH + 10;

    MoveWindow(g_ctrls.btnCompute, margin, y, 170, btnH, TRUE);
    MoveWindow(g_ctrls.btnReset, margin + 178, y, 120, btnH, TRUE);
    MoveWindow(g_ctrls.btnOpenMap, margin + 306, y, 170, btnH, TRUE);
    y += btnH + 10;

    outputH = rc.bottom - y - margin;
    if (outputH < 150) outputH = 150;
    MoveWindow(g_ctrls.output, margin, y, rc.right - (2 * margin), outputH, TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND controls[32];
            int i;
            int n = 0;

            g_ctrls.lblBattery = CreateWindowA("STATIC", "Battery level (%) [1-100]:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editBattery = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "75", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_BATTERY, NULL, NULL);
            g_ctrls.lblCapacity = CreateWindowA("STATIC", "Capacity (kWh) [10-200]:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editCapacity = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "60", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CAPACITY, NULL, NULL);
            g_ctrls.lblConsumption = CreateWindowA("STATIC", "Consumption rate (kWh/100km) [5-50]:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editConsumption = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "18", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CONSUMPTION, NULL, NULL);
            g_ctrls.lblDistance = CreateWindowA("STATIC", "Trip distance (km) [>0]:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editDistance = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "120", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_DISTANCE, NULL, NULL);
            g_ctrls.lblRegen = CreateWindowA("STATIC", "Regen efficiency (%) [0-100, default 15]:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editRegen = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "15", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_REGEN, NULL, NULL);
            g_ctrls.lblChargingNeeded = CreateWindowA("STATIC", "Charging needed? (YES/NO, user input):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editChargingNeeded = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "Yes", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CHARGING_NEEDED, NULL, NULL);

            g_ctrls.lblCurStreet = CreateWindowA("STATIC", "Current location - Street:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editCurStreet = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CUR_STREET, NULL, NULL);
            g_ctrls.lblCurCity = CreateWindowA("STATIC", "Current location - City:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editCurCity = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CUR_CITY, NULL, NULL);
            g_ctrls.lblCurProvince = CreateWindowA("STATIC", "Current location - Province:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editCurProvince = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CUR_PROVINCE, NULL, NULL);
            g_ctrls.lblCurRegion = CreateWindowA("STATIC", "Current location - Region:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editCurRegion = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_CUR_REGION, NULL, NULL);

            g_ctrls.lblDestStreet = CreateWindowA("STATIC", "Destination - Street:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editDestStreet = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_DEST_STREET, NULL, NULL);
            g_ctrls.lblDestCity = CreateWindowA("STATIC", "Destination - City:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editDestCity = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_DEST_CITY, NULL, NULL);
            g_ctrls.lblDestProvince = CreateWindowA("STATIC", "Destination - Province:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editDestProvince = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_DEST_PROVINCE, NULL, NULL);
            g_ctrls.lblDestRegion = CreateWindowA("STATIC", "Destination - Region:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
            g_ctrls.editDestRegion = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_DEST_REGION, NULL, NULL);

            g_ctrls.btnCompute = CreateWindowA("BUTTON", "Compute Plan", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_COMPUTE, NULL, NULL);
            g_ctrls.btnReset = CreateWindowA("BUTTON", "Reset", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_RESET, NULL, NULL);
            g_ctrls.btnOpenMap = CreateWindowA("BUTTON", "Open Map Pins", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_OPEN_MAP, NULL, NULL);
            g_ctrls.output = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_OUTPUT, NULL, NULL);

            controls[n++] = g_ctrls.lblBattery; controls[n++] = g_ctrls.editBattery;
            controls[n++] = g_ctrls.lblCapacity; controls[n++] = g_ctrls.editCapacity;
            controls[n++] = g_ctrls.lblConsumption; controls[n++] = g_ctrls.editConsumption;
            controls[n++] = g_ctrls.lblDistance; controls[n++] = g_ctrls.editDistance;
            controls[n++] = g_ctrls.lblRegen; controls[n++] = g_ctrls.editRegen;
            controls[n++] = g_ctrls.lblChargingNeeded; controls[n++] = g_ctrls.editChargingNeeded;
            controls[n++] = g_ctrls.lblCurStreet; controls[n++] = g_ctrls.editCurStreet;
            controls[n++] = g_ctrls.lblCurCity; controls[n++] = g_ctrls.editCurCity;
            controls[n++] = g_ctrls.lblCurProvince; controls[n++] = g_ctrls.editCurProvince;
            controls[n++] = g_ctrls.lblCurRegion; controls[n++] = g_ctrls.editCurRegion;
            controls[n++] = g_ctrls.lblDestStreet; controls[n++] = g_ctrls.editDestStreet;
            controls[n++] = g_ctrls.lblDestCity; controls[n++] = g_ctrls.editDestCity;
            controls[n++] = g_ctrls.lblDestProvince; controls[n++] = g_ctrls.editDestProvince;
            controls[n++] = g_ctrls.lblDestRegion; controls[n++] = g_ctrls.editDestRegion;
            controls[n++] = g_ctrls.btnCompute; controls[n++] = g_ctrls.btnReset;
            controls[n++] = g_ctrls.btnOpenMap; controls[n++] = g_ctrls.output;

            for (i = 0; i < n; ++i) SendMessageA(controls[i], WM_SETFONT, (WPARAM)hFont, TRUE);
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
            RECT rc = {0}, titleRect;
            GetClientRect(hWnd, &rc);
            titleRect.left = 12;
            titleRect.top = 8;
            titleRect.right = rc.right - 12;
            titleRect.bottom = 30;
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, "EV Charging & Range Planner - Route Stations with Big Pins", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 860,
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
