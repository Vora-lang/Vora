/**
 * JSON-RPC 2.0 message parsing and serialization implementation.
 */

#include "json_rpc.h"

#include <atomic>
#include <stdexcept>
#include <string>

namespace {

// Recursively sanitize invalid UTF-8 byte sequences in all strings
// within a JSON value, replacing them with U+FFFD (the Unicode
// replacement character). This prevents nlohmann::json::dump()
// from throwing type_error.316 when serializing.
void sanitizeUtf8(std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            i += 1;
        } else if (c < 0xC0) {
            // Unexpected continuation byte — replace.
            s[i] = '\xEF'; s.insert(i + 1, "\xBF\xBD");
            i += 3;
        } else if (c < 0xE0) {
            if (i + 1 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80) {
                s[i] = '\xEF'; s.insert(i + 1, "\xBF\xBD"); i += 3;
            } else { i += 2; }
        } else if (c < 0xF0) {
            if (i + 2 >= s.size() ||
                (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80) {
                s[i] = '\xEF'; s.insert(i + 1, "\xBF\xBD"); i += 3;
            } else { i += 3; }
        } else if (c < 0xF8) {
            if (i + 3 >= s.size() ||
                (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 3]) & 0xC0) != 0x80) {
                s[i] = '\xEF'; s.insert(i + 1, "\xBF\xBD"); i += 3;
            } else { i += 4; }
        } else {
            s[i] = '\xEF'; s.insert(i + 1, "\xBF\xBD"); i += 3;
        }
    }
}

void sanitizeJsonStrings(nlohmann::json& j) {
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        sanitizeUtf8(s);
        j = s;
    } else if (j.is_array()) {
        for (auto& elem : j) {
            sanitizeJsonStrings(elem);
        }
    } else if (j.is_object()) {
        for (auto& [key, val] : j.items()) {
            sanitizeJsonStrings(val);
        }
    }
}

} // anonymous namespace

namespace vora::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// JSON parsing
// ═══════════════════════════════════════════════════════════════════════════

std::optional<nlohmann::json> parseJson(const std::string& json) {
    try {
        return nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

std::optional<JsonRpcMessage> parseJsonRpcMessage(const std::string& json) {
    auto j = parseJson(json);
    if (!j || !j->is_object()) {
        return std::nullopt;
    }

    JsonRpcMessage msg;

    // Validate "jsonrpc": "2.0" (required for all message types)
    auto jrpc = j->find("jsonrpc");
    if (jrpc == j->end() || !jrpc->is_string() || jrpc->get<std::string>() != "2.0") {
        return std::nullopt;
    }
    msg.jsonrpc = "2.0";

    // Extract optional method (requests and notifications)
    auto method = j->find("method");
    if (method != j->end() && method->is_string()) {
        msg.method = method->get<std::string>();
    }

    // Extract optional id (requests and responses)
    auto id = j->find("id");
    if (id != j->end()) {
        // id can be string, number, or null per JSON-RPC 2.0
        msg.id = *id;
    }

    // Extract optional params (requests and notifications)
    auto params = j->find("params");
    if (params != j->end()) {
        msg.params = *params;
    }

    // Extract result (success response)
    auto result = j->find("result");
    if (result != j->end()) {
        msg.result = *result;
    }

    // Extract error (error response)
    auto error = j->find("error");
    if (error != j->end()) {
        msg.error = *error;
    }

    // Structural validation
    if (msg.method.has_value()) {
        // Request or notification — must not have result or error
        if (msg.result.has_value() || msg.error.has_value()) {
            return std::nullopt;
        }
    } else {
        // Response — must have id and exactly one of result/error
        if (!msg.id.has_value()) {
            return std::nullopt;  // response without id
        }
        if (msg.result.has_value() == msg.error.has_value()) {
            return std::nullopt;  // both or neither present
        }
    }

    return msg;
}

// ═══════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════

std::string serializeMessage(const JsonRpcMessage& msg) {
    nlohmann::json j;
    j["jsonrpc"] = "2.0";

    if (msg.method.has_value()) {
        j["method"] = *msg.method;
    }

    if (msg.id.has_value()) {
        j["id"] = *msg.id;
    }

    if (msg.params.has_value()) {
        j["params"] = *msg.params;
    }

    if (msg.result.has_value()) {
        j["result"] = *msg.result;
    }

    if (msg.error.has_value()) {
        j["error"] = *msg.error;
    }

    return j.dump();
}

std::string buildResponse(const nlohmann::json& id, const nlohmann::json& result) {
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["id"] = id;
    j["result"] = result;
    try {
        return j.dump();
    } catch (const nlohmann::json::type_error&) {
        // Result may contain invalid UTF-8 — sanitize and retry.
        sanitizeJsonStrings(j);
        return j.dump();
    }
}

std::string buildErrorResponse(const nlohmann::json& id,
                                int errorCode,
                                const std::string& errorMessage,
                                const nlohmann::json& errorData) {
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["id"] = id;

    nlohmann::json err;
    err["code"] = errorCode;
    err["message"] = errorMessage;
    if (!errorData.is_null()) {
        err["data"] = errorData;
    }

    j["error"] = err;
    try {
        return j.dump();
    } catch (const nlohmann::json::type_error&) {
        sanitizeJsonStrings(j);
        return j.dump();
    }
}

std::string buildNotification(const std::string& method,
                               const nlohmann::json& params) {
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["method"] = method;
    if (!params.is_null()) {
        j["params"] = params;
    }
    try {
        return j.dump();
    } catch (const nlohmann::json::type_error&) {
        sanitizeJsonStrings(j);
        return j.dump();
    }
}

// Thread-safe request ID counter (atomic increment, starts at 1).
static std::atomic<int64_t> g_requestId{1};

std::string buildRequest(const std::string& method,
                          const nlohmann::json& params) {
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["method"] = method;
    j["id"] = g_requestId.fetch_add(1, std::memory_order_relaxed);
    if (!params.is_null()) {
        j["params"] = params;
    }
    return j.dump();
}

}  // namespace vora::lsp
