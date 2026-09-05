/**
 * @file transport.h
 * @brief LSP stdio transport layer — Content-Length framed messages.
 *
 * @details
 * The Language Server Protocol (LSP) communicates over stdin/stdout using
 * HTTP-style header framing:
 *
 * @code
 *   Content-Length: <byte-count>\r\n
 *   \r\n
 *   <JSON payload of exactly <byte-count> bytes>
 * @endcode
 *
 * This transport handles reading and writing these framed messages.
 * All methods are synchronous (blocking I/O on stdin).
 *
 * On Windows the transport forces stdin/stdout into binary mode to prevent
 * CR/LF translation from corrupting the byte count in Content-Length headers.
 *
 * @note The transport channel (stdout) and the log channel (stderr) are
 *       strictly separated — log output never interleaves with JSON-RPC
 *       messages.
 */

#ifndef VORA_LSP_TRANSPORT_H
#define VORA_LSP_TRANSPORT_H

#include <cstdio>
#include <optional>
#include <string>

/**
 * @namespace vora::lsp
 * @brief JSON-RPC infrastructure for the Vora LSP/DAP servers.
 */
namespace vora::lsp {

/**
 * @class StdioTransport
 * @brief Read/write JSON-RPC messages over stdin/stdout with Content-Length
 *        framing.
 *
 * @details
 * Implements the LSP base protocol transport as specified in the LSP
 * specification.  Every message (request, response, or notification) is
 * prefixed with a `Content-Length` header followed by a blank line and the
 * JSON body.
 *
 * The class is non-copyable and non-movable — it is designed to be
 * instantiated once and held for the lifetime of the server process, as it
 * exclusively owns the stdin/stdout handles.
 *
 * Usage:
 * @code
 *   StdioTransport transport;
 *   auto msg = transport.readMessage();  // blocks until a full message arrives
 *   if (msg) { ... }
 *   transport.sendMessage(jsonString);
 * @endcode
 */
class StdioTransport {
public:
    StdioTransport() = default;
    ~StdioTransport() = default;

    /// @brief Non-copyable, non-movable — this object owns stdin/stdout.
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;
    StdioTransport(StdioTransport&&) = delete;
    StdioTransport& operator=(StdioTransport&&) = delete;

    /**
     * @brief Read one complete JSON-RPC message from stdin.
     *
     * @details
     * Parses HTTP-style headers (Content-Length is mandatory;
     * Content-Type is parsed but optional).  Once the header block
     * ends at a blank line, reads exactly the specified number of
     * body bytes.  Unknown headers are silently ignored.
     *
     * @return The raw JSON message body on success.
     * @retval std::nullopt On EOF before a complete message, or on
     *         unrecoverable read error, or on a malformed/missing
     *         Content-Length header.
     *
     * @note This method **blocks** until a complete message is available.
     */
    std::optional<std::string> readMessage();

    /**
     * @brief Write a JSON-RPC message to stdout with Content-Length framing.
     *
     * @details
     * Prepends the `Content-Length: <N>\r\n\r\n` header, writes the
     * message body, and flushes stdout so the peer receives the
     * message immediately.
     *
     * @param json The raw JSON-RPC message body (without header).
     * @return `true` on success.
     * @retval `false` If either the header or the body write fails.
     */
    bool sendMessage(const std::string& json);

    /**
     * @brief Write a raw string to stderr for diagnostic logging.
     *
     * @details
     * Writes directly to stderr, never to stdout.  This keeps log
     * output strictly separated from the JSON-RPC transport stream so
     * the LSP client does not receive corrupted protocol data.
     *
     * @param message The log line to emit (a trailing newline is
     *        appended automatically).
     */
    void log(const std::string& message);

private:
    /**
     * @brief Read exactly @p count bytes from stdin into @p buf.
     *
     * @details
     * Loops calling `std::fread` until @p count bytes have been read
     * or a read returns zero bytes (EOF or error).
     *
     * @param buf Destination buffer (must be at least @p count bytes).
     * @param count Number of bytes to read.
     * @return `true` if exactly @p count bytes were read.
     * @retval `false` On EOF or read error before @p count bytes
     *         could be read.
     */
    bool readExact(char* buf, size_t count);

    /**
     * @brief Read a single line from stdin.
     *
     * @details
     * Reads characters one at a time until a line terminator is
     * encountered or EOF is reached.  Accepts both `\r\n` (CRLF,
     * as required by the LSP specification) and bare `\n` (LF) for
     * tolerance with non-conforming clients.  A stray `\r` without
     * a following `\n` also terminates the line (the next character
     * is pushed back onto the stream).
     *
     * @return The line content without the trailing terminator.
     * @retval std::nullopt If EOF is reached before any characters
     *         were read (clean EOF at a message boundary).
     * @retval std::string A partial line if EOF occurs mid-line
     *         (unlikely; the implementation returns what it has
     *         for tolerance).
     */
    std::optional<std::string> readLine();
};

}  // namespace vora::lsp

#endif  // VORA_LSP_TRANSPORT_H
