    #include <cctype>
    #include <cstdlib>
    #include <iomanip>
    #include <iostream>
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

    std::string trim(const std::string& text) {
        std::size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
            ++start;
        }
        std::size_t end = text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
            --end;
        }
        return text.substr(start, end - start);
    }

    double parse_double_or_throw(const std::string& raw, const char* field_name) {
        if (raw.empty()) {
            throw std::runtime_error(std::string(field_name) + " is required.");
        }
        char* end_ptr = NULL;
        const double value = std::strtod(raw.c_str(), &end_ptr);
        if (end_ptr == raw.c_str()) {
            throw std::runtime_error(std::string(field_name) + " must be a valid number.");
        }
        while (*end_ptr != '\0' && std::isspace(static_cast<unsigned char>(*end_ptr))) {
            ++end_ptr;
        }
        if (*end_ptr != '\0') {
            throw std::runtime_error(std::string(field_name) + " must be a valid number.");
        }
        return value;
    }

    double prompt_bounded(const std::string& label, const char* field_name, double min_v, double max_v, bool allow_blank, double default_v) {
        while (true) {
            std::cout << label;
            std::string line;
            if (!std::getline(std::cin, line)) {
                throw std::runtime_error("Input stream closed unexpectedly.");
            }
            line = trim(line);

            if (line.empty()) {
                if (allow_blank) {
                    return default_v;
                }
                std::cout << "Invalid input: value cannot be blank.\n";
                continue;
            }

            double value = 0.0;
            try {
                value = parse_double_or_throw(line, field_name);
            } catch (const std::exception& ex) {
                std::cout << ex.what() << "\n";
                continue;
            }

            if (value < min_v || value > max_v) {
                std::cout << "Invalid input: " << field_name << " must be between " << min_v << " and " << max_v << ".\n";
                continue;
            }
            return value;
        }
    }

    bool prompt_yes_no(const std::string& label) {
        while (true) {
            std::cout << label;
            std::string line;
            if (!std::getline(std::cin, line)) {
                throw std::runtime_error("Input stream closed unexpectedly.");
            }
            line = trim(line);
            for (std::size_t i = 0; i < line.size(); ++i) {
                line[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));
            }
            if (line == "y" || line == "yes") {
                return true;
            }
            if (line == "n" || line == "no") {
                return false;
            }
            std::cout << "Please answer with y or n.\n";
        }
    }

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
        if (r.regen_recovered_kwh > r.gross_energy_used_kwh) {
            r.regen_recovered_kwh = r.gross_energy_used_kwh;
        }
        r.net_energy_used_kwh = r.gross_energy_used_kwh - r.regen_recovered_kwh;
        r.remaining_kwh = r.available_kwh - r.net_energy_used_kwh;
        if (r.remaining_kwh < 0.0) {
            r.remaining_kwh = 0.0;
        }
        r.remaining_pct = (r.remaining_kwh / in.battery_capacity_kwh) * 100.0;
        if (r.remaining_pct < 0.0) {
            r.remaining_pct = 0.0;
        }
        r.max_range_km = r.available_kwh / (in.consumption_kwh_per_100km / 100.0);
        r.charging_needed = r.remaining_pct < kSafetyThresholdPct;
        return r;
    }

    void print_result(const Result& r) {
        std::cout << "\n----------------------------------------------------\n";
        std::cout << "RESULTS\n";
        std::cout << "----------------------------------------------------\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Available energy                  : " << r.available_kwh << " kWh\n";
        std::cout << "Gross energy used                 : " << r.gross_energy_used_kwh << " kWh\n";
        std::cout << "Regen recovered                   : " << r.regen_recovered_kwh << " kWh\n";
        std::cout << "Net energy used                   : " << r.net_energy_used_kwh << " kWh\n";
        std::cout << "Predicted battery at destination  : " << r.remaining_pct << "% (" << r.remaining_kwh << " kWh)\n";
        std::cout << "Estimated max range               : " << r.max_range_km << " km\n";
        std::cout << "Charging needed?                  : " << (r.charging_needed ? "YES" : "NO") << "\n";
        if (r.charging_needed) {
            std::cout << "Status: Battery below 20% safety threshold on arrival.\n";
        } else {
            std::cout << "Status: Battery sufficient for this trip.\n";
        }
        std::cout << "----------------------------------------------------\n";
    }

    }  // namespace planner

    int main() {
        using namespace planner;
        std::cout << "====================================================\n";
        std::cout << "Electric Vehicle Charging & Range Planner (CLI)\n";
        std::cout << "Document-based six-function model\n";
        std::cout << "====================================================\n";

        try {
            while (true) {
                Inputs in;
                in.battery_pct = prompt_bounded("Enter current battery level (%) [1..100]: ", "battery %", 1.0, 100.0, false, 0.0);
                in.battery_capacity_kwh = prompt_bounded("Enter battery capacity (kWh) [10..200]: ", "capacity", 10.0, 200.0, false, 0.0);
                in.consumption_kwh_per_100km =
                    prompt_bounded("Enter energy consumption (kWh/100km) [5..50]: ", "consumption", 5.0, 50.0, false, 0.0);
                in.trip_distance_km = prompt_bounded("Enter trip distance (km) [>0..2000]: ", "distance", 0.001, 2000.0, false, 0.0);
                in.regen_efficiency_pct =
                    prompt_bounded("Enter regen efficiency (%) [0..100] (Enter for 15): ",
                                "regen efficiency",
                                0.0,
                                100.0,
                                true,
                                kDefaultRegenEfficiencyPct);

                const Result r = compute(in);
                print_result(r);

                if (!prompt_yes_no("Run another calculation? (y/n): ")) {
                    break;
                }
                std::cout << "\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "\nFatal error: " << ex.what() << "\n";
            return 1;
        } catch (...) {
            std::cerr << "\nUnknown fatal error.\n";
            return 1;
        }

        std::cout << "Goodbye. Drive safely!\n";
        return 0;
    }
