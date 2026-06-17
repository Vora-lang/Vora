/**
 * JSON-RPC 2.0 message router implementation.
 */

#include "message_router.h"

#include <iostream>
#include <stdexcept>

namespace vora::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// Registration
// ═══════════════════════════════════════════════════════════════════════════

void MessageRouter::registerRequest(const std::string& method,
                                     RequestHandler handler) {
    requestHandlers_[method] = std::move(handler);
}

void MessageRouter::registerNotification(const std::string& method,
                                           NotificationHandler handler) {
    notificationHandlers_[method] = std::move(handler);
}

// ═══════════════════════════════════════════════════════════════════════════
// Dispatch
// ═══════════════════════════════════════════════════════════════════════════

std::optional<std::string> MessageRouter::handleMessage(const std::string& json) {
    // ── Step 1: Parse JSON ───────────────────────────────────────────
    auto parsed = parseJson(json);
    if (!parsed) {
        // Not valid JSON at all — return ParseError (no id available)
        return buildErrorResponse(
            {},  // id = null (can't extract from broken JSON)
            static_cast<int>(JsonRpcErrorCode::ParseError),
            "Parse error: invalid JSON"
        );
    }

    // ── Step 2: Parse JSON-RPC structure ─────────────────────────────
    auto msg = parseJsonRpcMessage(json);
    if (!msg) {
        // Valid JSON but not a valid JSON-RPC message
        auto id = parsed->contains("id") ? (*parsed)["id"] : nlohmann::json();
        return buildErrorResponse(
            id,
            static_cast<int>(JsonRpcErrorCode::InvalidRequest),
            "Invalid Request: not a valid JSON-RPC 2.0 message"
        );
    }

    // ── Step 3: Dispatch by message type ─────────────────────────────
    if (msg->isNotification()) {
        // Notification — fire-and-forget, no response expected.
        if (!msg->method.has_value()) {
            return std::nullopt;  // shouldn't happen (isNotification checks this)
        }

        auto it = notificationHandlers_.find(*msg->method);
        if (it == notificationHandlers_.end()) {
            // Unknown notification — silently drop per JSON-RPC spec.
            // (LSP spec says servers should log but not respond.)
            return std::nullopt;
        }

        nlohmann::json params = msg->params.value_or(nlohmann::json());
        try {
            it->second(params);
        } catch (const std::exception& e) {
            // Notifications can't send errors back to the client.
            // Log to stderr (doesn't corrupt the stdout transport).
            std::cerr << "[Vora LSP] Notification handler '" << *msg->method
                      << "' threw: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Vora LSP] Notification handler '" << *msg->method
                      << "' threw unknown exception" << std::endl;
        }
        return std::nullopt;
    }

    if (msg->isRequest()) {
        // Request — handle and send response.
        if (!msg->method.has_value() || !msg->id.has_value()) {
            return buildErrorResponse(
                msg->id.value_or(nlohmann::json()),
                static_cast<int>(JsonRpcErrorCode::InvalidRequest),
                "Invalid Request: missing method or id"
            );
        }

        auto it = requestHandlers_.find(*msg->method);
        if (it == requestHandlers_.end()) {
            return buildErrorResponse(
                *msg->id,
                static_cast<int>(JsonRpcErrorCode::MethodNotFound),
                "Method not found: '" + *msg->method + "'"
            );
        }

        nlohmann::json params = msg->params.value_or(nlohmann::json());
        try {
            nlohmann::json result = it->second(params);
            return buildResponse(*msg->id, result);
        } catch (const std::exception& e) {
            return buildErrorResponse(
                *msg->id,
                static_cast<int>(JsonRpcErrorCode::InternalError),
                std::string("Internal error: ") + e.what()
            );
        } catch (...) {
            return buildErrorResponse(
                *msg->id,
                static_cast<int>(JsonRpcErrorCode::InternalError),
                "Internal error: unknown exception"
            );
        }
    }

    // Responses — not typically routed (the server is the responder, not
    // the requester).  But for completeness, just pass through.
    return std::nullopt;
}

}  // namespace vora::lsp
