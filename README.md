# Radix — Number Base Converter

Convert numbers between any two bases (2–36), including binary, octal,
decimal, and hexadecimal. The frontend is plain HTML/CSS/JS; the backend
is a single-file C++17 program that does the actual arithmetic and serves
the page.

## What's inside

```
base-converter/
├── server.cpp       C++17 backend: tiny HTTP server + base conversion logic
└── public/
    ├── index.html   Page structure
    ├── style.css    Visual design
    └── script.js    Talks to the backend via fetch()
```

## How it works

- **Backend (`server.cpp`)** opens a raw TCP socket and speaks just enough
  HTTP/1.1 to serve static files and one JSON API endpoint. Numbers are
  parsed and converted using arbitrary-precision (string-based) arithmetic,
  so it isn't limited to 64-bit integers — try a 30-digit number.
- **Frontend** sends the value you type to the backend and renders whatever
  comes back. No conversion math happens in JavaScript — it all happens in
  C++.

### API

```
GET /api/convert?value=FF&from=16&to=2
```

| Param   | Required | Meaning                                  |
|---------|----------|-------------------------------------------|
| `value` | yes      | The number to convert, as a string        |
| `from`  | yes      | The base `value` is currently written in (2–36) |
| `to`    | no       | An extra base to convert into, on top of the default bin/oct/dec/hex (2–36) |

Response:
```json
{
  "ok": true,
  "decimal": "255",
  "outputs": { "2": "11111111", "8": "377", "10": "255", "16": "FF" }
}
```
On invalid input: `{"ok": false, "error": "..."}`

## Build & run

You need a C++17 compiler. No third-party libraries — only the standard
library and sockets (POSIX on Linux/macOS, Winsock on Windows; `server.cpp`
picks the right one automatically).

**Linux / macOS:**
```bash
g++ -O2 -std=c++17 -o server server.cpp
./server          # starts on http://localhost:8080
./server 3000     # or choose a port
```

**Windows (MinGW-w64 / MSYS2 g++):**
```powershell
g++ -O2 -std=c++17 -o server.exe server.cpp -lws2_32
.\server.exe
```
> The `-lws2_32` flag is required on Windows to link the Winsock library.
> If you're using MSVC's `cl.exe` instead, it picks up the library
> automatically via the `#pragma comment(lib, "ws2_32.lib")` in the source.

Then open **http://localhost:8080** (or whichever port you chose) in your
browser. The server serves `public/index.html` and friends directly, so
there's no separate step to "deploy" the frontend — just keep `server`
running in the `base-converter/` folder, next to `public/`.

## Notes

- Digits beyond 9 use letters A–Z, case-insensitive on input, uppercase on
  output (so base-36 covers 0–9 and A–Z).
- Negative numbers are supported with a leading `-`.
- This server is intended for local/demo use — it handles one request per
  connection synchronously and has no authentication, TLS, or hardening
  for production traffic.
