/**
 * LSP stdio transport implementation — Content-Length framed messages.
 */

#include "transport.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace vora::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════

// Ensure stdin/stdout are in binary mode on Windows (avoids CR/LF translation
// corrupting the byte count in Content-Length headers).
static void ensureBinaryMode() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

static bool binaryModeSet = (ensureBinaryMode(), false);

bool StdioTransport::readExact(char* buf, size_t count) {
    size_t remaining = count;
    while (remaining > 0) {
        size_t n = std::fread(buf + (count - remaining), 1, remaining, stdin);
        if (n == 0) {
            // EOF or read error
            return false;
        }
        remaining -= n;
    }
    return true;
}

std::optional<std::string> StdioTransport::readLine() {
    std::string line;
    for (;;) {
        int c = std::fgetc(stdin);
        if (c == EOF) {
            if (line.empty()) {
                return std::nullopt;  // clean EOF at message boundary
            }
            return line;  // partial line at EOF (unlikely but be tolerant)
        }
        if (c == '\r') {
            // Peek ahead for \n (CRLF)
            int next = std::fgetc(stdin);
            if (next == '\n') {
                return line;
            }
            if (next != EOF) {
                // Stray \r without \n — push back the next char
                std::ungetc(next, stdin);
            }
            return line;
        }
        if (c == '\n') {
            return line;
        }
        line += static_cast<char>(c);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

std::optional<std::string> StdioTransport::readMessage() {
    // Parse headers until the empty line (CRLF or LF).
    std::optional<int64_t> contentLength;
    std::string contentType;

    for (;;) {
        auto line = readLine();
        if (!line.has_value()) {
            return std::nullopt;  // EOF before message
        }

        // Empty line → end of headers
        if (line->empty()) {
            break;
        }

        // Parse "Content-Length: <N>"
        if (line->size() > 16 &&
            (line->substr(0, 16) == "Content-Length: " ||
             line->substr(0, 16) == "content-length: ")) {
            try {
                contentLength = std::stoll(line->substr(16));
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            } catch (const std::out_of_range&) {
                return std::nullopt;
            }
        }

        // Parse "Content-Type: <type>" (optional, but LSP uses it)
        if (line->size() > 14 &&
            (line->substr(0, 14) == "Content-Type: " ||
             line->substr(0, 14) == "content-type: ")) {
            contentType = line->substr(14);
        }

        // Ignore other headers
    }

    // Content-Length is mandatory
    if (!contentLength.has_value() || *contentLength < 0) {
        return std::nullopt;
    }

    // Read the JSON body
    size_t length = static_cast<size_t>(*contentLength);
    if (length == 0) {
        return std::string();
    }

    std::string body(length, '\0');
    if (!readExact(body.data(), length)) {
        return std::nullopt;
    }

    return body;
}

bool StdioTransport::sendMessage(const std::string& json) {
    // Format: "Content-Length: <N>\r\n\r\n<json>"
    std::ostringstream header;
    header << "Content-Length: " << json.size() << "\r\n\r\n";

    std::string headerStr = header.str();

    // Write header + body in a single call when possible
    // (two writes are fine — stdout is typically buffered line-by-line)
    size_t written = std::fwrite(headerStr.data(), 1, headerStr.size(), stdout);
    if (written != headerStr.size()) {
        return false;
    }

    written = std::fwrite(json.data(), 1, json.size(), stdout);
    if (written != json.size()) {
        return false;
    }

    // Flush to ensure the message is sent immediately.
    std::fflush(stdout);
    return true;
}

void StdioTransport::log(const std::string& message) {
    // Write directly to stderr — never goes through stdout, so it can't
    // corrupt the JSON-RPC transport stream.
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fwrite("\n", 1, 1, stderr);
    std::fflush(stderr);
}

}  // namespace vora::lsp
