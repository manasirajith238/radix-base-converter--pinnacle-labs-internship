// server.cpp
// A small, dependency-free C++17 HTTP server that powers a number-base
// converter. It serves the static frontend (HTML/CSS/JS) from ./public
// and exposes a JSON API at /api/convert that does the actual conversion
// math in C++, using arbitrary-precision (string-based) arithmetic so it
// isn't limited to 64-bit integers.
//
// Build:  g++ -O2 -std=c++17 -o server server.cpp
// Run:    ./server [port]      (defaults to 8080)
// Then open http://localhost:8080 in a browser.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  // Windows: use Winsock2. Link with -lws2_32.
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socklen_t = int;
  using SocketHandle = SOCKET;
  static const SocketHandle kInvalidSocket = INVALID_SOCKET;
  static void closeSocket(SocketHandle fd) { closesocket(fd); }
#else
  // Linux / macOS: standard POSIX sockets.
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using SocketHandle = int;
  static const SocketHandle kInvalidSocket = -1;
  static void closeSocket(SocketHandle fd) { close(fd); }
#endif

// ---------------------------------------------------------------------
// Arbitrary-precision base conversion
// ---------------------------------------------------------------------
// We represent a non-negative integer internally as a vector of "limbs"
// in base 10,000 (little-endian) purely to make decimal output trivial.
// Converting an arbitrary input base to decimal is done by repeated
// "multiply existing decimal value by inputBase, then add next digit".
// Converting decimal to an arbitrary output base is done by repeated
// division of the decimal value by the output base, collecting remainders.

namespace bigint {

using Decimal = std::vector<int>; // base-10000 limbs, little endian

Decimal fromInt(long long v) {
    Decimal d;
    if (v == 0) { d.push_back(0); return d; }
    while (v > 0) { d.push_back(static_cast<int>(v % 10000)); v /= 10000; }
    return d;
}

void trim(Decimal &d) {
    while (d.size() > 1 && d.back() == 0) d.pop_back();
}

bool isZero(const Decimal &d) {
    return d.size() == 1 && d[0] == 0;
}

// d = d * mul + add   (mul, add are small ints, e.g. a base and a digit)
void mulAdd(Decimal &d, int mul, int add) {
    long long carry = add;
    for (size_t i = 0; i < d.size(); ++i) {
        long long cur = static_cast<long long>(d[i]) * mul + carry;
        d[i] = static_cast<int>(cur % 10000);
        carry = cur / 10000;
    }
    while (carry > 0) {
        d.push_back(static_cast<int>(carry % 10000));
        carry /= 10000;
    }
    trim(d);
}

// Divide d by small divisor in place, return remainder.
int divModSmall(Decimal &d, int divisor) {
    long long rem = 0;
    for (int i = static_cast<int>(d.size()) - 1; i >= 0; --i) {
        long long cur = rem * 10000 + d[i];
        d[i] = static_cast<int>(cur / divisor);
        rem = cur % divisor;
    }
    trim(d);
    return static_cast<int>(rem);
}

std::string toDecimalString(const Decimal &d) {
    std::ostringstream oss;
    oss << d.back();
    for (int i = static_cast<int>(d.size()) - 2; i >= 0; --i) {
        oss.width(4);
        oss.fill('0');
        oss << d[i];
    }
    return oss.str();
}

} // namespace bigint

const char *DIGITS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

int digitValue(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

struct ConvertResult {
    bool ok = false;
    std::string error;
    std::string decimal;          // value in base 10, as a string
    std::map<int, std::string> outputs; // base -> digits string
};

// Validate + parse `value` (given in `fromBase`) into a bigint::Decimal.
bool parseToDecimal(const std::string &raw, int fromBase, bigint::Decimal &outDecimal,
                     bool &negative, std::string &error) {
    if (fromBase < 2 || fromBase > 36) {
        error = "Base must be between 2 and 36.";
        return false;
    }
    std::string value = raw;
    // trim whitespace
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();

    negative = false;
    size_t start = 0;
    if (!value.empty() && (value[0] == '+' || value[0] == '-')) {
        negative = (value[0] == '-');
        start = 1;
    }
    if (start >= value.size()) {
        error = "Enter a number to convert.";
        return false;
    }

    bigint::Decimal dec = bigint::fromInt(0);
    bool sawDigit = false;
    for (size_t i = start; i < value.size(); ++i) {
        char c = value[i];
        int dv = digitValue(c);
        if (dv < 0 || dv >= fromBase) {
            error = std::string("'") + c + "' is not a valid digit in base " + std::to_string(fromBase) + ".";
            return false;
        }
        bigint::mulAdd(dec, fromBase, dv);
        sawDigit = true;
    }
    if (!sawDigit) {
        error = "Enter a number to convert.";
        return false;
    }
    outDecimal = dec;
    return true;
}

std::string decimalToBase(bigint::Decimal dec, int base) {
    if (bigint::isZero(dec)) return "0";
    std::string out;
    while (!bigint::isZero(dec)) {
        int rem = bigint::divModSmall(dec, base);
        out.push_back(DIGITS[rem]);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

ConvertResult convert(const std::string &value, int fromBase, const std::vector<int> &targetBases) {
    ConvertResult result;
    bigint::Decimal dec;
    bool negative;
    std::string error;
    if (!parseToDecimal(value, fromBase, dec, negative, error)) {
        result.ok = false;
        result.error = error;
        return result;
    }
    result.ok = true;
    std::string decStr = bigint::toDecimalString(dec);
    result.decimal = (negative && decStr != "0" ? "-" : "") + decStr;
    for (int b : targetBases) {
        if (b < 2 || b > 36) continue;
        bigint::Decimal copy = dec;
        std::string digits = decimalToBase(copy, b);
        result.outputs[b] = (negative && digits != "0" ? "-" : "") + digits;
    }
    return result;
}

// ---------------------------------------------------------------------
// Minimal JSON helpers (just enough for this API's needs)
// ---------------------------------------------------------------------

std::string jsonEscape(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------
// Tiny HTTP server
// ---------------------------------------------------------------------

std::string urlDecode(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = digitValue(s[i + 1]);
            int lo = digitValue(s[i + 2]);
            if (hi >= 0 && hi < 16 && lo >= 0 && lo < 16) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += (s[i] == '+') ? ' ' : s[i];
    }
    return out;
}

std::map<std::string, std::string> parseQuery(const std::string &query) {
    std::map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        std::string key = urlDecode(pair.substr(0, eq));
        std::string val = urlDecode(pair.substr(eq + 1));
        params[key] = val;
    }
    return params;
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
};

bool parseRequestLine(const std::string &line, HttpRequest &req) {
    std::istringstream iss(line);
    std::string fullPath;
    if (!(iss >> req.method >> fullPath)) return false;
    auto q = fullPath.find('?');
    if (q == std::string::npos) {
        req.path = fullPath;
    } else {
        req.path = fullPath.substr(0, q);
        req.query = parseQuery(fullPath.substr(q + 1));
    }
    return true;
}

std::string contentTypeFor(const std::string &path) {
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".html") == 0) return "text/html; charset=utf-8";
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".css") == 0) return "text/css; charset=utf-8";
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".js") == 0) return "application/javascript; charset=utf-8";
    return "application/octet-stream";
}

bool readFile(const std::string &path, std::string &contents) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    contents = oss.str();
    return true;
}

void sendResponse(SocketHandle client, int statusCode, const std::string &statusText,
                   const std::string &contentType, const std::string &body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Connection: close\r\n\r\n";
    std::string full = header.str() + body;
    size_t sent = 0;
    while (sent < full.size()) {
        size_t remaining = full.size() - sent;
        int chunk = remaining > 65536 ? 65536 : static_cast<int>(remaining);
        int n = send(client, full.data() + sent, chunk, 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

void handleApiConvert(SocketHandle client, const std::map<std::string, std::string> &q) {
    auto itVal = q.find("value");
    auto itFrom = q.find("from");
    if (itVal == q.end() || itFrom == q.end()) {
        sendResponse(client, 400, "Bad Request", "application/json",
                     R"({"ok":false,"error":"Missing 'value' or 'from' parameter."})");
        return;
    }
    int fromBase;
    try {
        fromBase = std::stoi(itFrom->second);
    } catch (...) {
        sendResponse(client, 400, "Bad Request", "application/json",
                     R"({"ok":false,"error":"'from' must be an integer base."})");
        return;
    }

    std::vector<int> targets = {2, 8, 10, 16};
    auto itTo = q.find("to");
    if (itTo != q.end()) {
        try {
            int customBase = std::stoi(itTo->second);
            if (std::find(targets.begin(), targets.end(), customBase) == targets.end()) {
                targets.push_back(customBase);
            }
        } catch (...) {
            sendResponse(client, 400, "Bad Request", "application/json",
                         R"({"ok":false,"error":"'to' must be an integer base."})");
            return;
        }
    }

    ConvertResult r = convert(itVal->second, fromBase, targets);
    std::ostringstream body;
    body << "{";
    if (!r.ok) {
        body << "\"ok\":false,\"error\":\"" << jsonEscape(r.error) << "\"";
    } else {
        body << "\"ok\":true,\"decimal\":\"" << jsonEscape(r.decimal) << "\",\"outputs\":{";
        bool first = true;
        for (auto &[base, digits] : r.outputs) {
            if (!first) body << ",";
            first = false;
            body << "\"" << base << "\":\"" << jsonEscape(digits) << "\"";
        }
        body << "}";
    }
    body << "}";
    sendResponse(client, 200, "OK", "application/json", body.str());
}

void handleClient(SocketHandle client, const std::string &publicDir) {
    char buf[8192];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { closeSocket(client); return; }
    buf[n] = '\0';

    std::istringstream stream(buf);
    std::string requestLine;
    std::getline(stream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    HttpRequest req;
    if (!parseRequestLine(requestLine, req)) {
        sendResponse(client, 400, "Bad Request", "text/plain", "Bad request");
        closeSocket(client);
        return;
    }

    if (req.path == "/api/convert") {
        handleApiConvert(client, req.query);
        closeSocket(client);
        return;
    }

    std::string relPath = (req.path == "/") ? "/index.html" : req.path;
    // Basic guard against path traversal.
    if (relPath.find("..") != std::string::npos) {
        sendResponse(client, 403, "Forbidden", "text/plain", "Forbidden");
        closeSocket(client);
        return;
    }
    std::string fullPath = publicDir + relPath;
    std::string contents;
    if (readFile(fullPath, contents)) {
        sendResponse(client, 200, "OK", contentTypeFor(fullPath), contents);
    } else {
        sendResponse(client, 404, "Not Found", "text/plain", "404 Not Found");
    }
    closeSocket(client);
}

int main(int argc, char **argv) {
    int port = 8080;
    if (argc > 1) {
        try { port = std::stoi(argv[1]); } catch (...) {}
    }
    std::string publicDir = "public";

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    SocketHandle server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == kInvalidSocket) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port << "\n";
        return 1;
    }
    if (listen(server_fd, 16) < 0) {
        std::cerr << "Failed to listen\n";
        return 1;
    }

    std::cout << "Base Converter server running at http://localhost:" << port << "\n";
    std::cout << "Serving static files from ./" << publicDir << "\n";

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        SocketHandle client = accept(server_fd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (client == kInvalidSocket) continue;
        handleClient(client, publicDir);
    }

    closeSocket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
