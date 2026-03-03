#include "gps_stripper.h"

#include <iostream>
#include <stdexcept>
#include <string>

/**
 * gps-stripper — Privacy-strip GPS start/end zones from a FIT file.
 *
 * Usage:
 *   gps-stripper <input.fit> <output.fit> <trim_start_meters> <trim_end_meters>
 *
 * Stdout (JSON):
 *   {"status":"ok","stripped_start":N,"stripped_end":M}
 *   {"status":"error","message":"..."}
 */
int main(int argc, char* argv[])
{
    if (argc != 5) {
        std::cerr << "{\"status\":\"error\",\"message\":\"Usage: gps-stripper <input.fit> <output.fit> <trim_start_m> <trim_end_m>\"}\n";
        return 1;
    }

    const std::string inputFile  = argv[1];
    const std::string outputFile = argv[2];
    double trimStart = 0.0;
    double trimEnd   = 0.0;

    try {
        trimStart = std::stod(argv[3]);
        trimEnd   = std::stod(argv[4]);
    } catch (const std::exception& e) {
        std::cout << "{\"status\":\"error\",\"message\":\"Invalid trim distance: " << e.what() << "\"}\n";
        return 1;
    }

    try {
        GpsStripper stripper(inputFile);
        auto [nStart, nEnd] = stripper.strip(outputFile, trimStart, trimEnd);

        std::cout << "{\"status\":\"ok\","
                  << "\"stripped_start\":" << nStart << ","
                  << "\"stripped_end\":" << nEnd << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "{\"status\":\"error\",\"message\":\"" << e.what() << "\"}\n";
        return 1;
    }
}
