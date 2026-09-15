# ITU Course Picker (Native C++)

A native Windows C++ helper for preparing and submitting İTÜ course registration requests through WinHTTP. It contains a registration program and a small interactive setup utility.

The program is intended for your own authorized account. Use it only in accordance with İTÜ registration rules and service limits.

## Features

- Native WinHTTP requests with a persistent session.
- Native authentication flow, including identity selection when required.
- Server clock sampling through HTTP `Date` headers.
- Hybrid wait loop using sleep followed by a high precision spin near the target.
- `.env` support for credentials.
- Separate setup utility for credentials, courses, and registration time.
- `--dry-run` mode to authenticate and prepare the request without sending it.

## Prerequisites

- Windows 10 or later.
- MinGW-w64 UCRT64 or MSVC.
- CMake 3.10 or later.
- Internet access to the İTÜ OBS service.

WinHTTP and `winmm` are Windows system libraries. No separate installation is required when using the supported Windows toolchain.

## Build With CMake

From the project directory:

```powershell
cmake -S . -B build
cmake --build build --parallel 2
```

The two executables are written to the project directory:

- `main.exe`: registration program.
- `setup.exe`: interactive configuration utility.

Run both programs from the project directory so their relative paths resolve correctly.

## Configuration

Create `.env` in the project directory. It is ignored by Git and should contain your credentials:

```env
ITU_USERNAME=your_itu_username
ITU_PASSWORD=your_itu_password
```

Create `data/config.json` with the target time and courses. The time is interpreted using the computer's local timezone.

```json
{
  "time": {
    "year": 2026,
    "month": 6,
    "day": 22,
    "hour": 15,
    "minute": 33,
    "second": 0,
    "millisecond": 0,
    "lead_millisecond": 0
  },
  "courses": {
    "crn": ["30335"],
    "scrn": []
  }
}
```

`crn` contains courses to add and `scrn` contains courses to drop. 

For a registration system that rejects early requests, keep `lead_millisecond` at `0`. A network latency estimate cannot guarantee that a request will arrive after the opening time, so using a positive lead can trigger an early request and cooldown.

## Setup Utility

Run `setup.exe` from the project directory and select one of the menu options:

1. Update `.env` credentials. The utility hides password input and replaces existing `ITU_USERNAME` and `ITU_PASSWORD` entries.
2. Update the date, time, lead, add CRNs, and drop CRNs in `data/config.json`.

Optional path arguments are available:

```powershell
setup.exe --config-path data/config.json --env-path .env
```


## Registration Program

```powershell
main.exe
```

Available flags:

- `--logs`: enable verbose diagnostic output, including authentication details and raw response output. Avoid sharing logs because they may contain sensitive information.
- `--test`: skip the final wait and send immediately. Use only with a safe test configuration.
- `--local`: skip server clock sampling and use local system time.
- `--dry-run`: authenticate, build the payload, and prepare the final request, then exit without sending a registration request.

The normal flow performs clock sampling, obtains the authentication token before the target time, prepares the request, waits until the configured deadline, and sends one request. The clock sampler uses the lowest round-trip sample from seven HTTP requests, but HTTP `Date` headers have only whole-second precision and do not provide a guaranteed server-receipt time.

## Timing Notes

The local wait loop uses `steady_clock`, which avoids problems when Windows adjusts the wall clock during the final wait. The process also requests a 1 ms Windows timer period and higher process/thread priority where Windows permits it.

These measures reduce local scheduling delay. They cannot guarantee the time at which the university server receives or processes a request because network routing, TLS connection state, queues, and server load remain outside the program's control. Keep the connection and authentication preparation close to the target time and use `--dry-run` before a real registration attempt.

## Acknowledgments

Server result code mappings and the original course selection behavior were adapted from the Python implementation by [AtaTrkgl](https://github.com/AtaTrkgl/itu-ders-secici).

This project uses [nlohmann/json](https://github.com/nlohmann/json), a header-only JSON library.
