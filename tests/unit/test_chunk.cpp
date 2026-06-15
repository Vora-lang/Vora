#include "gc/gc_heap.h"
// tests/unit/test_chunk.cpp — Chunk bytecode storage unit tests
//
// Tests: write, writeAt, RLE line/column encoding, addConstant dedup
//        for all variant types, writeConstant.

#include "doctest.h"
#include "vm/chunk.h"
#include "vm/opcode.h"

using namespace vora;

TEST_CASE("chunk_empty") {
    Chunk c;
    CHECK(c.code.empty());
    CHECK(c.constants.empty());
    CHECK(c.lines.empty());
    CHECK(c.columns.empty());
}

TEST_CASE("chunk_write_single_byte") {
    Chunk c;
    c.write(0xAB, 1, 5);
    REQUIRE(c.code.size() == 1);
    CHECK(c.code[0] == 0xAB);
    // RLE: one run of length 1 at line 1, column 5
    REQUIRE(c.lines.size() == 2);
    CHECK(c.lines[0] == 1);
    CHECK(c.lines[1] == 1);
    REQUIRE(c.columns.size() == 2);
    CHECK(c.columns[0] == 1);
    CHECK(c.columns[1] == 5);
}

TEST_CASE("chunk_write_two_bytes") {
    Chunk c;
    c.write(0x01, 0x02, 3, 10);
    REQUIRE(c.code.size() == 2);
    CHECK(c.code[0] == 0x01);
    CHECK(c.code[1] == 0x02);
    // Both bytes share the same line/column — RLE run length should be 2
    // In Vora's RLE: run starts at [1]=3, run length [0]=2 (or similar pattern)
    // Actual format: [runLen, value, runLen, value, ...]
}

TEST_CASE("chunk_write_rle_same_line_column") {
    Chunk c;
    c.write(0x01, 1, 1);
    c.write(0x02, 1, 1);
    c.write(0x03, 1, 1);
    REQUIRE(c.code.size() == 3);
    // All 3 bytes at same (line, column) → single RLE run of 3
    REQUIRE(c.lines.size() == 2);
    CHECK(c.lines[0] == 3);
    CHECK(c.lines[1] == 1);
}

TEST_CASE("chunk_write_rle_different_lines") {
    Chunk c;
    c.write(0x01, 1, 1);
    c.write(0x02, 2, 5);
    REQUIRE(c.code.size() == 2);
    // Two runs: [1 at line 1], [1 at line 2]
    REQUIRE(c.lines.size() == 4);
    CHECK(c.lines[0] == 1);
    CHECK(c.lines[1] == 1);
    CHECK(c.lines[2] == 1);
    CHECK(c.lines[3] == 2);
}

TEST_CASE("chunk_writeAt_modifies_existing_byte") {
    Chunk c;
    for (int i = 0; i < 5; i++) c.write(0x00, 1, 1);
    c.writeAt(2, 0xFF);
    CHECK(c.code[2] == 0xFF);
    // Other bytes unchanged
    CHECK(c.code[0] == 0x00);
    CHECK(c.code[4] == 0x00);
}

TEST_CASE("chunk_writeAt_beyond_size_noop") {
    Chunk c;
    c.write(0x01, 1, 1);
    c.writeAt(999, 0xCC);
    // No crash, code unchanged
    REQUIRE(c.code.size() == 1);
    CHECK(c.code[0] == 0x01);
}

TEST_CASE("chunk_addConstant_dedup_nullptr") {
    Chunk c;
    size_t i0 = c.addConstant(nullptr);
    size_t i1 = c.addConstant(nullptr);
    CHECK(i0 == i1);
    CHECK(c.constants.size() == 1);
}

TEST_CASE("chunk_addConstant_dedup_bool") {
    Chunk c;
    size_t i0 = c.addConstant(true);
    size_t i1 = c.addConstant(true);
    CHECK(i0 == i1);  // same value dedup
    size_t i2 = c.addConstant(false);
    CHECK(i2 != i0);  // different value
    CHECK(c.constants.size() == 2);
}

TEST_CASE("chunk_addConstant_dedup_int64") {
    Chunk c;
    size_t i0 = c.addConstant(static_cast<int64_t>(42));
    size_t i1 = c.addConstant(static_cast<int64_t>(42));
    CHECK(i0 == i1);
    size_t i2 = c.addConstant(static_cast<int64_t>(43));
    CHECK(i2 != i0);
    CHECK(c.constants.size() == 2);
}

TEST_CASE("chunk_addConstant_dedup_double") {
    Chunk c;
    size_t i0 = c.addConstant(3.14);
    size_t i1 = c.addConstant(3.14);
    CHECK(i0 == i1);
    size_t i2 = c.addConstant(2.71);
    CHECK(i2 != i0);
    CHECK(c.constants.size() == 2);
}

TEST_CASE("chunk_addConstant_dedup_string") {
    Chunk c;
    size_t i0 = c.addConstant(std::string("hello"));
    size_t i1 = c.addConstant(std::string("hello"));
    CHECK(i0 == i1);
    size_t i2 = c.addConstant(std::string("world"));
    CHECK(i2 != i0);
    CHECK(c.constants.size() == 2);
}

TEST_CASE("chunk_addConstant_cross_type_distinct") {
    Chunk c;
    size_t i0 = c.addConstant(nullptr);            // null
    size_t i1 = c.addConstant(static_cast<int64_t>(0));  // int 0
    // nullptr and int64_t(0) are different types — should NOT dedup
    CHECK(i0 != i1);
    CHECK(c.constants.size() >= 2);
}

TEST_CASE("chunk_addConstant_shared_ptr_not_deduped") {
    Chunk c;
    auto a1 = GcHeap::instance().alloc<Array>();
    auto a2 = GcHeap::instance().alloc<Array>();  // different pointer, same content
    size_t i0 = c.addConstant(a1);
    size_t i1 = c.addConstant(a2);
    // Currently shared_ptr types are NOT value-deduped (by design)
    // Two different shared_ptr objects produce two constants
    CHECK(c.constants.size() >= 2);
    // But the same shared_ptr does dedup (pointer identity via index() match
    // isn't sufficient — the loop only compares nullptr/bool/int/double/string)
}

TEST_CASE("chunk_writeConstant_emits_opcode_and_index") {
    Chunk c;
    c.writeConstant(static_cast<int64_t>(42), 1, 1);
    REQUIRE(c.code.size() == 2);
    CHECK(c.code[0] == static_cast<uint8_t>(OpCode::OP_CONSTANT));
    CHECK(c.code[1] == 0);  // first constant gets index 0
    REQUIRE(c.constants.size() == 1);
    CHECK(std::holds_alternative<int64_t>(c.constants[0]));
    CHECK(std::get<int64_t>(c.constants[0]) == 42);
}
