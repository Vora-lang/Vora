/**
 * LSP stdio transport layer — Content-Length framed messages.
 *
 * The Language Server Protocol (LSP) communicates over stdin/stdout using
 * HTTP-style header framing:
 *
 *   Content-Length: <byte-count>\r\n
 *   \r\n
 *   <JSON payload of exactly <byte-count> bytes>
 *
 * This transport handles reading and writing these framed messages.
 * All methods are synchronous (blocking I/O on stdin).
 */

#ifndef VORA_LSP_TRANSPORT_H
#define VORA_LSP_TRANSPORT_H

#include <cstdio>
#include <optional>
#include <string>

namespace vora::lsp {

/// Read/write JSON-RPC messages over stdin/stdout with Content-Length framing.
///
/// Usage:
///   StdioTransport transport;
///   auto msg = transport.readMessage();  // blocks until a full message arrives
///   if (msg) { ... }
///   transport.sendMessage(jsonString);
class StdioTransport {
public:
    StdioTransport() = default;
    ~StdioTransport() = default;

    // Non-copyable, non-movable (owns stdin/stdout handles).
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;
    StdioTransport(StdioTransport&&) = delete;
    StdioTransport& operator=(StdioTransport&&) = delete;

    /// Read one complete JSON-RPC message from stdin.
    ///
    /// Parses the Content-Length header, then reads exactly the specified
    /// number of bytes.  Returns std::nullopt on EOF or unrecoverable read
    /// error.  Blocks until a complete message is available.
    std::optional<std::string> readMessage();

    /// Write a JSON-RPC message to stdout with Content-Length framing.
    ///
    /// Prepends the "Content-Length: <N>\r\n\r\n" header, then writes the
    /// message body.  Flushes stdout to ensure the message is sent immediately.
    /// Returns true on success, false on write error.
    bool sendMessage(const std::string& json);

    /// Write a raw string to stderr (for logging without corrupting the
    /// stdout transport channel).
    void log(const std::string& message);

private:
    /// Read exactly `count` bytes from stdin into buf.
    /// Returns false on EOF or read error before count bytes are read.
    bool readExact(char* buf, size_t count);

    /// Read a single line (\r\n or \n terminated) from stdin.
    /// Returns the line without the trailing \r\n.
    std::optional<std::string> readLine();
};

}  // namespace vora::lsp

#endif  // VORA_LSP_TRANSPORT_H
