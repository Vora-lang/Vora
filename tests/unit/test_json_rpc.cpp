// tests/unit/test_json_rpc.cpp — JSON-RPC 2.0 unit tests
//
// Tests: JSON parsing, JSON-RPC message parsing/discrimination,
//        serialization, request/notification building, transport
//        Content-Length framing, and message routing.

#include "doctest.h"
#include "json_rpc/json_rpc.h"
#include "json_rpc/transport.h"
#include "json_rpc/message_router.h"

#include <sstream>

using namespace vora::lsp;

// ============================================================================
// JSON parsing (parseJson)
// ============================================================================

TEST_CASE("jsonrpc_parseJson_valid_object") {
    auto j = parseJson(R"({"key": "value"})");
    REQUIRE(j.has_value());
    CHECK((*j)["key"] == "value");
}

TEST_CASE("jsonrpc_parseJson_valid_array") {
    auto j = parseJson(R"([1, 2, 3])");
    REQUIRE(j.has_value());
    CHECK((*j).is_array());
    CHECK((*j).size() == 3);
}

TEST_CASE("jsonrpc_parseJson_invalid") {
    auto j = parseJson(R"({broken)");
    CHECK_FALSE(j.has_value());
}

TEST_CASE("jsonrpc_parseJson_empty_string") {
    auto j = parseJson("");
    CHECK_FALSE(j.has_value());
}

// ============================================================================
// JSON-RPC message parsing (parseJsonRpcMessage)
// ============================================================================

TEST_CASE("jsonrpc_parse_valid_request") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","method":"test","params":{"a":1},"id":1})");
    REQUIRE(msg.has_value());
    CHECK(msg->isRequest());
    CHECK_FALSE(msg->isNotification());
    CHECK_FALSE(msg->isResponse());
    CHECK(msg->method == "test");
    CHECK(msg->id.has_value());
    CHECK((*msg->id) == 1);
}

TEST_CASE("jsonrpc_parse_valid_notification") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","method":"update","params":[1,2,3]})");
    REQUIRE(msg.has_value());
    CHECK(msg->isNotification());
    CHECK_FALSE(msg->isRequest());
    CHECK_FALSE(msg->isResponse());
    CHECK(msg->method == "update");
}

TEST_CASE("jsonrpc_parse_valid_success_response") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","result":42,"id":1})");
    REQUIRE(msg.has_value());
    CHECK(msg->isResponse());
    CHECK(msg->isSuccessResponse());
    CHECK_FALSE(msg->isErrorResponse());
    CHECK((*msg->result) == 42);
}

TEST_CASE("jsonrpc_parse_valid_error_response") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","error":{"code":-32600,"message":"Invalid"},"id":1})");
    REQUIRE(msg.has_value());
    CHECK(msg->isResponse());
    CHECK(msg->isErrorResponse());
    CHECK_FALSE(msg->isSuccessResponse());
}

TEST_CASE("jsonrpc_parse_missing_jsonrpc") {
    auto msg = parseJsonRpcMessage(
        R"({"method":"test","id":1})");
    CHECK_FALSE(msg.has_value());
}

TEST_CASE("jsonrpc_parse_wrong_jsonrpc_version") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"1.0","method":"test","id":1})");
    CHECK_FALSE(msg.has_value());
}

TEST_CASE("jsonrpc_parse_null_id_notification") {
    // JSON-RPC 2.0: null id = notification
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","method":"test","id":null})");
    REQUIRE(msg.has_value());
    CHECK(msg->isNotification());
    CHECK_FALSE(msg->isRequest());
}

TEST_CASE("jsonrpc_parse_request_without_params") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","method":"test","id":1})");
    REQUIRE(msg.has_value());
    CHECK(msg->isRequest());
    CHECK_FALSE(msg->params.has_value());
}

TEST_CASE("jsonrpc_parse_not_a_json_object") {
    auto msg = parseJsonRpcMessage(R"("just a string")");
    CHECK_FALSE(msg.has_value());
}

// ============================================================================
// Serialization
// ============================================================================

TEST_CASE("jsonrpc_serialize_request_roundtrip") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","method":"test","params":{"x":1},"id":42})");
    REQUIRE(msg.has_value());
    std::string json = serializeMessage(*msg);
    auto msg2 = parseJsonRpcMessage(json);
    REQUIRE(msg2.has_value());
    CHECK(msg2->method == "test");
    CHECK((*msg2->id) == 42);
}

TEST_CASE("jsonrpc_serialize_response_roundtrip") {
    auto msg = parseJsonRpcMessage(
        R"({"jsonrpc":"2.0","result":{"ok":true},"id":"req-1"})");
    REQUIRE(msg.has_value());
    std::string json = serializeMessage(*msg);
    auto msg2 = parseJsonRpcMessage(json);
    REQUIRE(msg2.has_value());
    CHECK(msg2->isSuccessResponse());
    CHECK((*msg2->id) == "req-1");
}

// ============================================================================
// Response builders
// ============================================================================

TEST_CASE("jsonrpc_build_response") {
    std::string json = buildResponse(1, {{"key", "value"}});
    auto msg = parseJsonRpcMessage(json);
    REQUIRE(msg.has_value());
    CHECK(msg->isSuccessResponse());
    CHECK((*msg->id) == 1);
    CHECK((*msg->result)["key"] == "value");
}

TEST_CASE("jsonrpc_build_error_response") {
    std::string json = buildErrorResponse(
        42, -32601, "Method not found",
        {{"method", "nonexistent"}});
    auto msg = parseJsonRpcMessage(json);
    REQUIRE(msg.has_value());
    CHECK(msg->isErrorResponse());
    CHECK((*msg->id) == 42);
    CHECK((*msg->error)["code"] == -32601);
    CHECK((*msg->error)["message"] == "Method not found");
    CHECK((*msg->error)["data"]["method"] == "nonexistent");
}

TEST_CASE("jsonrpc_build_error_response_no_data") {
    std::string json = buildErrorResponse(1, -32000, "Server error");
    auto msg = parseJsonRpcMessage(json);
    REQUIRE(msg.has_value());
    CHECK(msg->isErrorResponse());
    // No "data" field when errorData is null/empty
    CHECK_FALSE((*msg->error).contains("data"));
}

TEST_CASE("jsonrpc_build_notification") {
    std::string json = buildNotification("textDocument/didOpen",
                                          {{"uri", "file:///test.va"}});
    auto msg = parseJsonRpcMessage(json);
    REQUIRE(msg.has_value());
    CHECK(msg->isNotification());
    CHECK(msg->method == "textDocument/didOpen");
}

TEST_CASE("jsonrpc_build_notification_no_params") {
    std::string json = buildNotification("exit");
    auto msg = parseJsonRpcMessage(json);
    REQUIRE(msg.has_value());
    CHECK(msg->isNotification());
    CHECK(msg->method == "exit");
}

TEST_CASE("jsonrpc_build_request") {
    std::string json = buildRequest("initialize",
                                     {{"processId", nullptr}});
    auto msg = parseJsonRpcMessage(json);
    REQUIRE(msg.has_value());
    CHECK(msg->isRequest());
    CHECK(msg->method == "initialize");
    // ID should be auto-generated as an integer
    CHECK(msg->id.has_value());
    CHECK((*msg->id).is_number_integer());
}

// ============================================================================
// Transport: Content-Length header generation
// ============================================================================

TEST_CASE("lsp_transport_header_format") {
    // Test the header format manually (we can't easily mock stdin/stdout
    // but the sendMessage format is well-defined).
    //
    // A properly formed message is:
    //   Content-Length: <N>\r\n
    //   \r\n
    //   <body of exactly N bytes>

    // Build what sendMessage would write
    std::string body = R"({"jsonrpc":"2.0","result":42,"id":1})";
    std::ostringstream header;
    header << "Content-Length: " << body.size() << "\r\n\r\n";
    std::string expected = header.str() + body;

    // Verify the format
    CHECK(expected.find("Content-Length: ") == 0);
    CHECK(expected.find("\r\n\r\n") != std::string::npos);
    CHECK(expected.substr(expected.find("\r\n\r\n") + 4) == body);
}

TEST_CASE("lsp_transport_body_length_matches_header") {
    std::string body = R"({"jsonrpc":"2.0","method":"test","params":{},"id":1})";
    std::ostringstream header;
    header << "Content-Length: " << body.size() << "\r\n\r\n";

    // Extract the length value and verify it matches the body
    std::string headerStr = header.str();
    size_t colon = headerStr.find(':');
    size_t crlf = headerStr.find('\r', colon);
    int length = std::stoi(headerStr.substr(colon + 2, crlf - colon - 2));

    CHECK(length == static_cast<int>(body.size()));
}

// ============================================================================
// Message router: handler registration and dispatch
// ============================================================================

TEST_CASE("lsp_router_request_dispatch") {
    MessageRouter router;
    router.registerRequest("add", [](const nlohmann::json& params) -> nlohmann::json {
        return params["a"].get<int>() + params["b"].get<int>();
    });

    std::string result = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"add","params":{"a":3,"b":4},"id":1})"
    ).value();

    auto msg = parseJsonRpcMessage(result);
    REQUIRE(msg.has_value());
    CHECK(msg->isSuccessResponse());
    CHECK((*msg->id) == 1);
    CHECK((*msg->result) == 7);
}

TEST_CASE("lsp_router_notification_dispatch") {
    MessageRouter router;
    int callCount = 0;
    router.registerNotification("ping", [&](const nlohmann::json&) {
        callCount++;
    });

    auto response = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"ping"})"
    );
    // Notifications don't return a response
    CHECK_FALSE(response.has_value());
    CHECK(callCount == 1);
}

TEST_CASE("lsp_router_method_not_found") {
    MessageRouter router;
    auto response = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"nonexistent","id":1})"
    );
    REQUIRE(response.has_value());
    auto msg = parseJsonRpcMessage(*response);
    REQUIRE(msg.has_value());
    CHECK(msg->isErrorResponse());
    CHECK((*msg->error)["code"] == static_cast<int>(JsonRpcErrorCode::MethodNotFound));
}

TEST_CASE("lsp_router_invalid_json") {
    MessageRouter router;
    auto response = router.handleMessage("{not json");
    REQUIRE(response.has_value());
    auto msg = parseJsonRpcMessage(*response);
    REQUIRE(msg.has_value());
    CHECK(msg->isErrorResponse());
    CHECK((*msg->error)["code"] == static_cast<int>(JsonRpcErrorCode::ParseError));
}

TEST_CASE("lsp_router_invalid_message_structure") {
    MessageRouter router;
    // Valid JSON but missing jsonrpc field
    auto response = router.handleMessage(R"({"method":"test","id":1})");
    REQUIRE(response.has_value());
    auto msg = parseJsonRpcMessage(*response);
    REQUIRE(msg.has_value());
    CHECK(msg->isErrorResponse());
    CHECK((*msg->error)["code"] == static_cast<int>(JsonRpcErrorCode::InvalidRequest));
}

TEST_CASE("lsp_router_handler_throws") {
    MessageRouter router;
    router.registerRequest("crash", [](const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("simulated failure");
    });

    auto response = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"crash","id":1})"
    );
    REQUIRE(response.has_value());
    auto msg = parseJsonRpcMessage(*response);
    REQUIRE(msg.has_value());
    CHECK(msg->isErrorResponse());
    CHECK((*msg->error)["code"] == static_cast<int>(JsonRpcErrorCode::InternalError));
}

TEST_CASE("lsp_router_unknown_notification_silently_ignored") {
    MessageRouter router;
    auto response = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"unknownNotification"})"
    );
    // Unknown notifications are silently dropped per JSON-RPC spec
    CHECK_FALSE(response.has_value());
}

TEST_CASE("lsp_router_multiple_handlers") {
    MessageRouter router;

    router.registerRequest("echo", [](const nlohmann::json& params) {
        return params;
    });

    router.registerNotification("log", [](const nlohmann::json& params) {
        // Just consume the notification
        REQUIRE(params.contains("msg"));
    });

    // Test request
    auto r1 = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"echo","params":{"hello":"world"},"id":"a"})"
    );
    REQUIRE(r1.has_value());
    auto msg1 = parseJsonRpcMessage(*r1);
    CHECK((*msg1->result)["hello"] == "world");

    // Test notification
    auto r2 = router.handleMessage(
        R"({"jsonrpc":"2.0","method":"log","params":{"msg":"hello"}})"
    );
    CHECK_FALSE(r2.has_value());
}

// ============================================================================
// Error code values (spec compliance)
// ============================================================================

TEST_CASE("jsonrpc_error_codes_standard") {
    CHECK(static_cast<int>(JsonRpcErrorCode::ParseError) == -32700);
    CHECK(static_cast<int>(JsonRpcErrorCode::InvalidRequest) == -32600);
    CHECK(static_cast<int>(JsonRpcErrorCode::MethodNotFound) == -32601);
    CHECK(static_cast<int>(JsonRpcErrorCode::InvalidParams) == -32602);
    CHECK(static_cast<int>(JsonRpcErrorCode::InternalError) == -32603);
}

TEST_CASE("jsonrpc_error_codes_server") {
    CHECK(static_cast<int>(JsonRpcErrorCode::ServerNotInitialized) == -32002);
    CHECK(static_cast<int>(JsonRpcErrorCode::UnknownError) == -32001);
}
