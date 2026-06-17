/**
 * JSON-RPC 2.0 message types and utilities.
 *
 * Implements the JSON-RPC 2.0 specification (https://www.jsonrpc.org/specification)
 * on top of nlohmann/json.  Provides message parsing, serialization, and
 * discrimination (request vs notification vs response).
 *
 * This file is part of the Vora LSP (Language Server Protocol) infrastructure
 * (roadmap #3 — JSON-RPC library introduction).
 */

#ifndef VORA_LSP_JSON_RPC_H
#define VORA_LSP_JSON_RPC_H

#include "nlohmann/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace vora::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// JSON-RPC 2.0 error codes
// ═══════════════════════════════════════════════════════════════════════════

/// Standard JSON-RPC 2.0 error codes (pre-defined by the protocol).
enum class JsonRpcErrorCode : int {
    ParseError     = -32700,  ///< Invalid JSON was received.
    InvalidRequest = -32600,  ///< The JSON sent is not a valid Request object.
    MethodNotFound = -32601,  ///< The method does not exist / is not available.
    InvalidParams  = -32602,  ///< Invalid method parameter(s).
    InternalError  = -32603,  ///< Internal JSON-RPC error.
    /// -32000 to -32099 — reserved for server-defined errors.
    ServerNotInitialized = -32002,  ///< Server has not been initialized yet.
    UnknownError         = -32001,  ///< Catch-all server error.
};

/// LSP-specific error codes (JSON-RPC reserves -32800 to -32803 for LSP).
enum class LspErrorCode : int {
    RequestFailed    = -32803,  ///< Request failed but no diagnostic available.
    ServerCancelled  = -32802,  ///< Server cancelled the request.
    ContentModified  = -32801,  ///< Content was modified during request.
    RequestCancelled = -32800,  ///< Client cancelled the request.
};

// ═══════════════════════════════════════════════════════════════════════════
// JSON-RPC message structures
// ═══════════════════════════════════════════════════════════════════════════

/// A parsed JSON-RPC 2.0 message.
///
/// Uses std::optional fields for discrimination:
///   - request:      method + id (non-null)    → isRequest()
///   - notification: method + id (absent)      → isNotification()
///   - response:     result XOR error + id     → isResponse()
///
/// The "jsonrpc" field must be "2.0" — the parser rejects anything else.
struct JsonRpcMessage {
    /// Always "2.0" for valid messages.
    std::string jsonrpc = "2.0";

    /// Present for requests and notifications; absent for responses.
    std::optional<std::string> method;

    /// Request id — must be a number, string, or null (JSON null = notification).
    /// Absent in notifications; present in requests and responses.
    std::optional<nlohmann::json> id;

    /// Request/notification parameters (object or array). Optional per spec.
    std::optional<nlohmann::json> params;

    /// Success response payload.  Mutually exclusive with error.
    std::optional<nlohmann::json> result;

    /// Error response payload.  Mutually exclusive with result.
    std::optional<nlohmann::json> error;

    // ── Discrimination helpers ──────────────────────────────────────────

    /// True if this message is a request (method + non-null id).
    bool isRequest() const noexcept {
        return method.has_value() && id.has_value() && !id->is_null();
    }

    /// True if this message is a notification (method, no id or null id).
    bool isNotification() const noexcept {
        return method.has_value() && (!id.has_value() || id->is_null());
    }

    /// True if this message is a response (no method, has id).
    bool isResponse() const noexcept {
        return !method.has_value() && id.has_value();
    }

    /// True if this response indicates success (has result, no error).
    bool isSuccessResponse() const noexcept {
        return isResponse() && result.has_value() && !error.has_value();
    }

    /// True if this response indicates an error (has error, no result).
    bool isErrorResponse() const noexcept {
        return isResponse() && error.has_value() && !result.has_value();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Parsing
// ═══════════════════════════════════════════════════════════════════════════

/// Parse a JSON string into a JsonRpcMessage.
///
/// Returns std::nullopt if:
///   - The JSON is not a valid object
///   - The "jsonrpc" field is missing or not "2.0"
///   - The message structure is invalid (e.g. both result and error present)
std::optional<JsonRpcMessage> parseJsonRpcMessage(const std::string& json);

/// Parse JSON text into nlohmann::json with basic validation.
/// Returns std::nullopt on parse failure.
std::optional<nlohmann::json> parseJson(const std::string& json);

// ═══════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════

/// Serialize a JsonRpcMessage back to a JSON string (compact, no newlines).
std::string serializeMessage(const JsonRpcMessage& msg);

/// Build a success response JSON string.
std::string buildResponse(const nlohmann::json& id, const nlohmann::json& result);

/// Build an error response JSON string.
std::string buildErrorResponse(const nlohmann::json& id,
                               int errorCode,
                               const std::string& errorMessage,
                               const nlohmann::json& errorData = nlohmann::json());

/// Build a notification JSON string (no id field).
std::string buildNotification(const std::string& method,
                              const nlohmann::json& params = nlohmann::json());

/// Build a request JSON string with an auto-incrementing integer id.
std::string buildRequest(const std::string& method,
                         const nlohmann::json& params = nlohmann::json());

}  // namespace vora::lsp

#endif  // VORA_LSP_JSON_RPC_H
