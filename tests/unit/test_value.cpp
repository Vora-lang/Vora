#include "gc/gc_heap.h"
// tests/unit/test_value.cpp — Value system unit tests
//
// Tests: variant holds checks, isNumeric, toDouble, promoteToFloat,
//        valueToString for all 10 variant alternatives.

#include "doctest.h"
#include "runtime/value.h"

using namespace vora;

TEST_CASE("value_nullptr_holds") {
    Value v(nullptr);
    CHECK(std::holds_alternative<std::nullptr_t>(v));
    CHECK_FALSE(isNumeric(v));
    CHECK_FALSE(std::holds_alternative<double>(v));
    CHECK_FALSE(std::holds_alternative<int64_t>(v));
}

TEST_CASE("value_int64_holds") {
    Value v(static_cast<int64_t>(42));
    CHECK(std::holds_alternative<int64_t>(v));
    CHECK(isNumeric(v));
    CHECK_FALSE(std::holds_alternative<double>(v));
}

TEST_CASE("value_double_holds") {
    Value v(3.14);
    CHECK(std::holds_alternative<double>(v));
    CHECK(isNumeric(v));
    CHECK_FALSE(std::holds_alternative<int64_t>(v));
}

TEST_CASE("value_bool_holds") {
    Value v(true);
    CHECK(std::holds_alternative<bool>(v));
    CHECK_FALSE(isNumeric(v));
    Value w(false);
    CHECK(std::holds_alternative<bool>(w));
}

TEST_CASE("value_string_holds") {
    Value v(GcHeap::instance().alloc<GcString>("hello"));
    CHECK(std::holds_alternative<GcPtr<GcString>>(v));
    CHECK_FALSE(isNumeric(v));
}

TEST_CASE("value_array_holds") {
    auto arr = GcHeap::instance().alloc<Array>();
    Value v(arr);
    CHECK(std::holds_alternative<GcPtr<Array>>(v));
    CHECK_FALSE(isNumeric(v));
}

TEST_CASE("value_toDouble_int64") {
    CHECK(toDouble(Value(static_cast<int64_t>(42))) == 42.0);
    CHECK(toDouble(Value(static_cast<int64_t>(-7))) == -7.0);
    CHECK(toDouble(Value(static_cast<int64_t>(0))) == 0.0);
    // Large int64 should still convert (lossy near INT64_MAX)
    CHECK(toDouble(Value(static_cast<int64_t>(1000000))) == 1e6);
}

TEST_CASE("value_toDouble_double") {
    CHECK(toDouble(Value(3.14)) == 3.14);
    CHECK(toDouble(Value(0.0)) == 0.0);
    CHECK(toDouble(Value(-0.0)) == -0.0);
}

TEST_CASE("value_promoteToFloat_int") {
    Value result = promoteToFloat(Value(static_cast<int64_t>(5)));
    CHECK(std::holds_alternative<double>(result));
    CHECK(std::get<double>(result) == 5.0);
}

TEST_CASE("value_promoteToFloat_double_identity") {
    Value v(3.14);
    Value result = promoteToFloat(v);
    CHECK(std::holds_alternative<double>(result));
    CHECK(std::get<double>(result) == 3.14);
}

TEST_CASE("value_promoteToFloat_non_numeric_identity") {
    Value v(true);
    Value result = promoteToFloat(v);
    CHECK(std::holds_alternative<bool>(result));
}

TEST_CASE("value_isNumeric_coverage") {
    CHECK(isNumeric(Value(static_cast<int64_t>(1))));
    CHECK(isNumeric(Value(3.14)));
    CHECK_FALSE(isNumeric(Value(nullptr)));
    CHECK_FALSE(isNumeric(Value(true)));
    CHECK_FALSE(isNumeric(Value(GcHeap::instance().alloc<GcString>("hi"))));
    CHECK_FALSE(isNumeric(Value(GcHeap::instance().alloc<Array>())));
}

TEST_CASE("valueToString_null") {
    CHECK(valueToString(Value(nullptr)) == "null");
}

TEST_CASE("valueToString_bool") {
    CHECK(valueToString(Value(true)) == "true");
    CHECK(valueToString(Value(false)) == "false");
}

TEST_CASE("valueToString_int64") {
    CHECK(valueToString(Value(static_cast<int64_t>(42))) == "42");
    CHECK(valueToString(Value(static_cast<int64_t>(-7))) == "-7");
    CHECK(valueToString(Value(static_cast<int64_t>(0))) == "0");
}

TEST_CASE("valueToString_double") {
    // Exact format may vary, check it contains the number
    std::string s = valueToString(Value(3.14));
    CHECK(s.find("3.14") != std::string::npos);
    // Zero should render cleanly
    std::string z = valueToString(Value(0.0));
    CHECK((z == "0" || z.find("0") != std::string::npos));
}

TEST_CASE("valueToString_string") {
    CHECK(valueToString(Value(GcHeap::instance().alloc<GcString>("hello"))) == "hello");
    CHECK(valueToString(Value(GcHeap::instance().alloc<GcString>(""))) == "");
}

TEST_CASE("valueToString_array") {
    auto arr = GcHeap::instance().alloc<Array>();
    // Empty array
    CHECK(valueToString(Value(arr)) == "[]");
    // Array with elements
    auto arr2 = GcHeap::instance().alloc<Array>();
    arr2->elements.push_back(Value(static_cast<int64_t>(1)));
    arr2->elements.push_back(Value(static_cast<int64_t>(2)));
    arr2->elements.push_back(Value(static_cast<int64_t>(3)));
    CHECK(valueToString(Value(arr2)) == "[1, 2, 3]");
    // Nested array
    auto nested = GcHeap::instance().alloc<Array>();
    nested->elements.push_back(Value(arr2));
    CHECK(valueToString(Value(nested)) == "[[1, 2, 3]]");
}
