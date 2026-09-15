#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <chrono>

class SystemClock {
private:
    long long offset_ms = 0; // The difference: Server Time - Local Time

    // Helper to parse HTTP Date header (RFC 1123)
    std::time_t parse_http_date(const std::wstring& date_str);

public:
    SystemClock();
    
    // Connects to ITU server, reads the Date header, and calculates drift
    void sync_with_server(HINTERNET hConnect);

    // High-precision wait loop (Sleeps then Spins)
    // Takes the target time from config (e.g., 14:00:00)
    void wait_until(int year, int month, int day, int hour, int minute, int second = 0, int millisecond = 0, int lead_millisecond = 0);

    // specific getter for debug purposes
    long long get_offset() const { return offset_ms; }
};
