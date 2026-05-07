import tkinter as tk
from tkinter import messagebox, ttk
from typing import Dict, List, Optional

import requests


DEFAULT_REGEN_PCT = 15.0
DEFAULT_LAT = 14.5995
DEFAULT_LON = 120.9842
REQUEST_TIMEOUT_SECONDS = 8

# The project planner defines calculate_remaining_battery(available, used, regen)
# but remaining_pct also depends on total capacity. This value is set by get_user_input().
_CURRENT_CAPACITY_KWH = 0.0


def calculate_available_energy(battery_pct: float, capacity_kwh: float) -> float:
    return (battery_pct / 100.0) * capacity_kwh


def calculate_energy_used(consumption_rate: float, distance_km: float) -> float:
    return (consumption_rate / 100.0) * distance_km


def calculate_regen_recovery(energy_used: float, regen_pct: float) -> float:
    return energy_used * (regen_pct / 100.0)


def calculate_remaining_battery(available: float, used: float, regen: float) -> tuple[float, float]:
    remaining_kwh = available - used + regen
    if remaining_kwh < 0:
        remaining_kwh = 0.0

    if _CURRENT_CAPACITY_KWH <= 0:
        remaining_pct = 0.0
    else:
        remaining_pct = (remaining_kwh / _CURRENT_CAPACITY_KWH) * 100.0

    if remaining_pct < 0:
        remaining_pct = 0.0

    return remaining_kwh, remaining_pct


def generate_recommendation(remaining_pct: float, remaining_kwh: float) -> str:
    if remaining_pct < 20.0 or remaining_kwh <= 0:
        return "CHARGING RECOMMENDED - Battery low for arrival margin."
    return "Battery sufficient - proceed with trip."


def get_user_input(entries: Dict[str, tk.Entry]) -> Dict[str, float]:
    global _CURRENT_CAPACITY_KWH

    def read_float(field_name: str, required: bool = True) -> Optional[float]:
        raw = entries[field_name].get().strip()
        if not raw:
            if required:
                raise ValueError(f"{field_name} is required.")
            return None
        try:
            return float(raw)
        except ValueError as exc:
            raise ValueError(f"{field_name} must be numeric.") from exc

    battery_pct = read_float("battery_pct")
    capacity_kwh = read_float("capacity_kwh")
    consumption_rate = read_float("consumption_rate")
    distance_km = read_float("distance_km")

    regen_raw = entries["regen_pct"].get().strip()
    regen_pct = DEFAULT_REGEN_PCT if regen_raw == "" else float(regen_raw)

    lat_raw = entries["latitude"].get().strip()
    lon_raw = entries["longitude"].get().strip()
    latitude = DEFAULT_LAT if lat_raw == "" else float(lat_raw)
    longitude = DEFAULT_LON if lon_raw == "" else float(lon_raw)

    if battery_pct is None or battery_pct < 1 or battery_pct > 100:
        raise ValueError("Battery % must be between 1 and 100.")
    if capacity_kwh is None or capacity_kwh < 10 or capacity_kwh > 200:
        raise ValueError("Capacity kWh must be between 10 and 200.")
    if consumption_rate is None or consumption_rate < 5 or consumption_rate > 50:
        raise ValueError("Consumption rate must be between 5 and 50 kWh/100km.")
    if distance_km is None or distance_km <= 0:
        raise ValueError("Distance must be greater than 0 km.")
    if regen_pct < 0 or regen_pct > 100:
        raise ValueError("Regen efficiency must be between 0 and 100.")
    if latitude < -90 or latitude > 90:
        raise ValueError("Latitude must be between -90 and 90.")
    if longitude < -180 or longitude > 180:
        raise ValueError("Longitude must be between -180 and 180.")

    _CURRENT_CAPACITY_KWH = capacity_kwh

    return {
        "battery_pct": battery_pct,
        "capacity_kwh": capacity_kwh,
        "consumption_rate": consumption_rate,
        "distance_km": distance_km,
        "regen_pct": regen_pct,
        "latitude": latitude,
        "longitude": longitude,
    }


def _sample_stations() -> List[str]:
    return [
        "1. Tesla Supercharger - 450 Main St, Springfield [DC Fast, 250 kW]",
        "2. ChargePoint Station - Springfield Mall [Level 2, 7.2 kW]",
        "3. Electrify America - Route 9 & Commerce Dr [DC Fast, 150 kW]",
    ]


def _fetch_nrel_stations(lat: float, lon: float, api_key: str) -> List[str]:
    # NREL API (primary, US):
    # url = "https://developer.nrel.gov/api/alt-fuel-stations/v1/nearest.json"
    # params = {
    #     "api_key": api_key,
    #     "fuel_type": "ELEC",
    #     "latitude": lat,
    #     "longitude": lon,
    #     "radius": 5,
    #     "limit": 3,
    #     "status": "E",
    # }
    # response = requests.get(url, params=params, timeout=REQUEST_TIMEOUT_SECONDS)
    # data = response.json()["fuel_stations"]
    # return data
    url = "https://developer.nrel.gov/api/alt-fuel-stations/v1/nearest.json"
    params = {
        "api_key": api_key,
        "fuel_type": "ELEC",
        "latitude": lat,
        "longitude": lon,
        "radius": 5,
        "limit": 3,
        "status": "E",
    }
    response = requests.get(url, params=params, timeout=REQUEST_TIMEOUT_SECONDS)
    response.raise_for_status()
    data = response.json().get("fuel_stations", [])

    stations = []
    for idx, station in enumerate(data[:3], start=1):
        name = station.get("station_name", "Unknown Station")
        address = station.get("street_address", "Address unavailable")
        dc_fast = station.get("ev_dc_fast_num") or station.get("ev_dc_fast_count") or 0
        lvl2 = station.get("ev_level2_evse_num") or 0
        charger_type = "DC Fast" if dc_fast else "Level 2"
        power_text = "Fast charging" if dc_fast else f"{lvl2} ports"
        stations.append(f"{idx}. {name} - {address} [{charger_type}, {power_text}]")
    return stations


def _fetch_ocm_stations(lat: float, lon: float, api_key: str) -> List[str]:
    # Open Charge Map fallback:
    # url = "https://api.openchargemap.io/v3/poi"
    # params = {
    #     "key": api_key,
    #     "latitude": lat,
    #     "longitude": lon,
    #     "distance": 8,
    #     "distanceunit": "KM",
    #     "maxresults": 3,
    # }
    # response = requests.get(url, params=params, timeout=REQUEST_TIMEOUT_SECONDS)
    # data = response.json()
    # return data
    url = "https://api.openchargemap.io/v3/poi"
    params = {
        "key": api_key,
        "latitude": lat,
        "longitude": lon,
        "distance": 8,
        "distanceunit": "KM",
        "maxresults": 3,
    }
    response = requests.get(url, params=params, timeout=REQUEST_TIMEOUT_SECONDS)
    response.raise_for_status()
    data = response.json()

    stations = []
    for idx, station in enumerate(data[:3], start=1):
        info = station.get("AddressInfo", {})
        title = info.get("Title", "Unknown Station")
        address = info.get("AddressLine1", "Address unavailable")
        power_kw = "N/A"
        connections = station.get("Connections", [])
        if connections:
            value = connections[0].get("PowerKW")
            if value is not None:
                power_kw = f"{value} kW"
        stations.append(f"{idx}. {title} - {address} [Power: {power_kw}]")
    return stations


def get_charging_stations(
    lat: float, lon: float, country_code: str, nrel_api_key: str, ocm_api_key: str
) -> tuple[str, List[str]]:
    configured = bool(nrel_api_key.strip()) or bool(ocm_api_key.strip())
    if not configured:
        return "API not configured - showing sample stations.", _sample_stations()

    # Fallback chain from planner document:
    # if country_code == "US": try NREL first, else use OCM
    if country_code.strip().upper() == "US" and nrel_api_key.strip():
        try:
            nrel_results = _fetch_nrel_stations(lat, lon, nrel_api_key.strip())
            if nrel_results:
                return "Nearest charging stations (via NREL API):", nrel_results
        except requests.RequestException:
            pass

    if ocm_api_key.strip():
        try:
            ocm_results = _fetch_ocm_stations(lat, lon, ocm_api_key.strip())
            if ocm_results:
                return "Nearest charging stations (via Open Charge Map):", ocm_results
        except requests.RequestException:
            pass

    return "API unavailable - showing sample stations.", _sample_stations()


class EVPlannerApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("EV Charging & Range Planner")
        self.root.geometry("880x760")
        self.root.minsize(840, 700)

        container = ttk.Frame(self.root, padding=14)
        container.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            container,
            text="EV Charging & Range Planner",
            font=("Segoe UI", 16, "bold"),
        ).pack(anchor=tk.W, pady=(0, 10))

        form = ttk.LabelFrame(container, text="Inputs", padding=10)
        form.pack(fill=tk.X)

        self.entries: Dict[str, tk.Entry] = {}
        fields = [
            ("Current battery level (%)", "battery_pct", "75"),
            ("Battery capacity (kWh)", "capacity_kwh", "60"),
            ("Consumption rate (kWh/100km)", "consumption_rate", "18"),
            ("Trip distance (km)", "distance_km", "120"),
            ("Regen efficiency (%)", "regen_pct", "15"),
            ("Latitude (optional)", "latitude", f"{DEFAULT_LAT}"),
            ("Longitude (optional)", "longitude", f"{DEFAULT_LON}"),
            ("Country code (US/PH/etc.)", "country_code", "PH"),
            ("NREL API key (optional)", "nrel_api_key", ""),
            ("Open Charge Map API key (optional)", "ocm_api_key", ""),
        ]

        for row, (label, key, default) in enumerate(fields):
            ttk.Label(form, text=label).grid(row=row, column=0, sticky=tk.W, padx=(0, 10), pady=4)
            entry = ttk.Entry(form, width=42)
            entry.insert(0, default)
            entry.grid(row=row, column=1, sticky=tk.W, pady=4)
            self.entries[key] = entry

        self.entries["nrel_api_key"].configure(show="*")
        self.entries["ocm_api_key"].configure(show="*")

        actions = ttk.Frame(container)
        actions.pack(fill=tk.X, pady=10)

        ttk.Button(actions, text="Compute Plan", command=self.compute_plan).pack(side=tk.LEFT)
        ttk.Button(actions, text="Reset", command=self.reset_form).pack(side=tk.LEFT, padx=(8, 0))

        self.error_var = tk.StringVar(value="")
        ttk.Label(container, textvariable=self.error_var, foreground="red").pack(anchor=tk.W)

        output_frame = ttk.LabelFrame(container, text="Results", padding=10)
        output_frame.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        self.output = tk.Text(
            output_frame,
            height=22,
            wrap=tk.WORD,
            font=("Consolas", 11),
            state=tk.DISABLED,
        )
        self.output.pack(fill=tk.BOTH, expand=True)

        self.reset_form()

    def _write_output(self, text: str) -> None:
        self.output.configure(state=tk.NORMAL)
        self.output.delete("1.0", tk.END)
        self.output.insert(tk.END, text)
        self.output.configure(state=tk.DISABLED)

    def compute_plan(self) -> None:
        self.error_var.set("")
        try:
            data = get_user_input(self.entries)
        except ValueError as exc:
            self.error_var.set(str(exc))
            messagebox.showerror("Invalid Input", str(exc))
            return

        available = calculate_available_energy(data["battery_pct"], data["capacity_kwh"])
        gross_used = calculate_energy_used(data["consumption_rate"], data["distance_km"])
        regen = calculate_regen_recovery(gross_used, data["regen_pct"])
        if regen > gross_used:
            regen = gross_used
        net_used = gross_used - regen
        remaining_kwh, remaining_pct = calculate_remaining_battery(available, gross_used, regen)
        max_range = available / (data["consumption_rate"] / 100.0)
        recommendation = generate_recommendation(remaining_pct, remaining_kwh)

        charging_needed = remaining_pct < 20.0
        exceeds_range = data["distance_km"] > max_range

        lines = [
            "----------------------------------------------------",
            "RESULTS",
            "----------------------------------------------------",
            f"Available energy: {available:.2f} kWh",
            f"Gross energy used: {gross_used:.2f} kWh",
            f"Regen recovered: {regen:.2f} kWh",
            f"Net energy used: {net_used:.2f} kWh",
            f"Remaining battery: {remaining_pct:.1f}% ({remaining_kwh:.2f} kWh)",
            f"Estimated max range: {max_range:.1f} km",
            f"Charging needed? {'YES' if charging_needed else 'NO'}",
            f"Status: {recommendation}",
        ]

        if exceeds_range:
            lines.append("ADVISORY: Trip exceeds current range. Plan charging before destination.")

        if remaining_kwh <= 0:
            lines.append("WARNING: Remaining battery clamped to 0%.")

        if charging_needed:
            lines.append("*** CHARGING RECOMMENDED ***")
            needed_to_20_pct_kwh = max(0.0, ((20.0 - remaining_pct) / 100.0) * data["capacity_kwh"])
            charging_hours_lvl2 = needed_to_20_pct_kwh / 7.2
            charging_minutes = round(charging_hours_lvl2 * 60)
            lines.append(f"Estimated charging time needed: ~{charging_minutes} min (Level 2)")

            label, stations = get_charging_stations(
                data["latitude"],
                data["longitude"],
                self.entries["country_code"].get().strip(),
                self.entries["nrel_api_key"].get().strip(),
                self.entries["ocm_api_key"].get().strip(),
            )
            lines.append(label)
            lines.extend(stations[:3])

        lines.append("----------------------------------------------------")
        self._write_output("\n".join(lines))

    def reset_form(self) -> None:
        defaults = {
            "battery_pct": "75",
            "capacity_kwh": "60",
            "consumption_rate": "18",
            "distance_km": "120",
            "regen_pct": "15",
            "latitude": f"{DEFAULT_LAT}",
            "longitude": f"{DEFAULT_LON}",
            "country_code": "PH",
            "nrel_api_key": "",
            "ocm_api_key": "",
        }
        for key, value in defaults.items():
            self.entries[key].delete(0, tk.END)
            self.entries[key].insert(0, value)
        self.error_var.set("")
        self._write_output("Enter trip details, then click Compute Plan.")


def main() -> None:
    root = tk.Tk()
    app = EVPlannerApp(root)
    _ = app
    root.mainloop()


if __name__ == "__main__":
    main()
