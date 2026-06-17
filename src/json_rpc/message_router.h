/**
 * JSON-RPC 2.0 message router — handler registry + dispatch.
 *
 * Provides a lightweight routing framework for JSON-RPC methods:
 *   - registerRequest(method, handler)  — handle requests that expect a response
 *   - registerNotification(method, handler) — handle fire-and-forget notifications
 *   - handleMessage(json) — parse + dispatch + return serialized response
 *     (or nullopt for notifications / invalid messages)
 *
 * Error handling is built-in:
 *   - Invalid JSON      → ParseError response
 *   - Invalid structure  → InvalidRequest response
 *   - Unknown method     → MethodNotFound response
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

/// Signature for a request handler: takes params, returns result.
/// Throwing from a handler is caught and converted to an InternalError response.
using RequestHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

/// Signature for a notification handler: takes params, returns nothing.
/// Throwing from a handler is caught and logged (notifications can't send errors).
using NotificationHandler = std::function<void(const nlohmann::json& params)>;

/// Routes incoming JSON-RPC messages to registered handlers.
///
/// Usage:
///   MessageRouter router;
///   router.registerRequest("initialize", [](auto& p) { return initResult; });
///   router.registerNotification("initialized", [](auto& p) { ... });
///   auto response = router.handleMessage(jsonText);
///   if (response) { transport.sendMessage(*response); }
class MessageRouter {
public:
    MessageRouter() = default;
    ~MessageRouter() = default;

    // Movable (for convenience of composing routers).
    MessageRouter(MessageRouter&&) = default;
    MessageRouter& operator=(MessageRouter&&) = default;

    /// Register a handler for a named request method.
    ///
    /// The handler receives the "params" field (or null if absent) and
    /// must return a JSON-serializable result value.  If the handler
    /// throws, an InternalError response is automatically generated.
    void registerRequest(const std::string& method, RequestHandler handler);

    /// Register a handler for a named notification method.
    ///
    /// The handler receives the "params" field (or null if absent).
    /// Notifications cannot return errors — exceptions are caught and
    /// silently dropped (logged to stderr).
    void registerNotification(const std::string& method, NotificationHandler handler);

    /// Handle a raw JSON-RPC message string.
    ///
    /// Parses the message, dispatches to the appropriate handler, and
    /// returns the serialized response JSON (for requests) or nullopt
    /// (for notifications, invalid messages, or parse errors without an id).
    ///
    /// This is the single entry point for all incoming messages.
    std::optional<std::string> handleMessage(const std::string& json);

private:
    std::unordered_map<std::string, RequestHandler> requestHandlers_;
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
};

}  // namespace vora::lsp

#endif  // VORA_LSP_MESSAGE_ROUTER_H
