/**
 * @file message_router.h
 * @brief JSON-RPC 2.0 message router — handler registry and dispatch.
 *
 * Provides a lightweight routing framework for JSON-RPC methods:
 *   - registerRequest(method, handler)  — handle requests that expect a response
 *   - registerNotification(method, handler) — handle fire-and-forget notifications
 *   - handleMessage(json) — parse, dispatch, and return serialized response
 *     (or nullopt for notifications / invalid messages)
 *
 * Error handling is built-in per the JSON-RPC 2.0 specification:
 *   - Invalid JSON       → ParseError response (-32700)
 *   - Invalid structure   → InvalidRequest response (-32600)
 *   - Unknown method      → MethodNotFound response (-32601)
 *   - Handler throws      → InternalError response (-32603)
 *
 * This is the core infrastructure for LSP protocol implementation
 * (roadmap #3 — JSON-RPC library introduction).
 */

#ifndef VORA_LSP_MESSAGE_ROUTER_H
#define VORA_LSP_MESSAGE_ROUTER_H

#include "json_rpc.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace vora::lsp {

/// @brief Signature for a JSON-RPC request handler.
/// @details Receives the request "params" field (or `null` if absent) and must
///          return a JSON-serializable result value.
/// @param params  The "params" member of the JSON-RPC request object.
/// @return The result value to be sent back in the response.
/// @throws Any exception thrown by the handler is caught by MessageRouter and
///         automatically converted to an InternalError response (-32603).
using RequestHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

/// @brief Signature for a JSON-RPC notification handler.
/// @details Receives the notification "params" field (or `null` if absent).
///          Notifications are fire-and-forget — no response is sent.
/// @param params  The "params" member of the JSON-RPC notification object.
/// @throws Any exception thrown by the handler is caught and silently logged
///         to stderr, since notifications cannot send error responses per the
///         JSON-RPC 2.0 specification.
using NotificationHandler = std::function<void(const nlohmann::json& params)>;

/// @brief Routes incoming JSON-RPC 2.0 messages to registered handlers.
///
/// Maintains separate registries for requests (methods that return a result)
/// and notifications (fire-and-forget methods). Parses raw JSON text, validates
/// the JSON-RPC structure, dispatches to the appropriate handler, and returns
/// the serialized response.
///
/// Error handling follows the JSON-RPC 2.0 specification:
///   - Parse errors, invalid requests, and unknown methods generate error
///     responses with the standard error codes.
///   - Handler exceptions are caught: requests get an InternalError response,
///     notifications are logged and dropped.
///
/// @note Instances are movable but not copyable, allowing composition
///       of routers (e.g. merging method sets from separate modules).
///
/// Usage:
/// @code
///   MessageRouter router;
///   router.registerRequest("initialize", [](auto& p) { return initResult; });
///   router.registerNotification("initialized", [](auto& p) { /* ... */ });
///   auto response = router.handleMessage(jsonText);
///   if (response) { transport.sendMessage(*response); }
/// @endcode
class MessageRouter {
public:
    MessageRouter() = default;
    ~MessageRouter() = default;

    // Movable (for convenience of composing routers).
    MessageRouter(MessageRouter&&) = default;
    MessageRouter& operator=(MessageRouter&&) = default;

    /// @brief Register a handler for a named request method.
    ///
    /// Requests expect a response containing the handler's return value.
    ///
    /// @param method   The JSON-RPC method name to match against (e.g.
    ///                 "textDocument/hover", "initialize").
    /// @param handler  The callback invoked when a matching request arrives.
    ///                 Receives the "params" field (or `null` if absent) and
    ///                 must return a JSON-serializable result value. If the
    ///                 handler throws, an InternalError response (-32603) is
    ///                 automatically generated.
    ///
    /// @note Overwriting a previously registered method is allowed; the last
    ///       registration wins.
    void registerRequest(const std::string& method, RequestHandler handler);

    /// @brief Register a handler for a named notification method.
    ///
    /// Notifications are fire-and-forget — no response is sent to the caller.
    ///
    /// @param method   The JSON-RPC method name to match against (e.g.
    ///                 "textDocument/didOpen", "initialized").
    /// @param handler  The callback invoked when a matching notification
    ///                 arrives. Receives the "params" field (or `null` if
    ///                 absent). Notifications cannot return errors — exceptions
    ///                 are caught and silently dropped (logged to stderr).
    ///
    /// @note Overwriting a previously registered method is allowed; the last
    ///       registration wins.
    void registerNotification(const std::string& method, NotificationHandler handler);

    /// @brief Handle a raw JSON-RPC 2.0 message string.
    ///
    /// Parses the message, validates the JSON-RPC structure, dispatches to the
    /// appropriate registered handler, and returns the serialized response.
    /// This is the single entry point for all incoming messages.
    ///
    /// @param json  Raw JSON text as received from the transport layer
    ///              (e.g. after Content-Length header processing in stdio mode).
    ///
    /// @return The serialized JSON-RPC response string for requests that have
    ///         an "id" field.
    /// @retval std::nullopt  For notifications (no "id"), batch messages,
    ///         parse errors lacking an id, or invalid messages without an id —
    ///         i.e., whenever the JSON-RPC spec says no response should be sent.
    ///
    /// @note This method is not thread-safe. All calls should be serialized
    ///       from a single consumer thread (the LSP message loop).
    std::optional<std::string> handleMessage(const std::string& json);

private:
    /// @brief Registered request handlers, keyed by method name.
    ///        Requests are methods that expect a response containing a result.
    std::unordered_map<std::string, RequestHandler> requestHandlers_;

    /// @brief Registered notification handlers, keyed by method name.
    ///        Notifications are fire-and-forget — no response is sent.
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
};

}  // namespace vora::lsp

#endif  // VORA_LSP_MESSAGE_ROUTER_H
