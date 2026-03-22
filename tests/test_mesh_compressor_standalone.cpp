/**
 * Standalone test for MeshCompressor — compiles without project dependencies.
 * Build: g++ -std=c++14 -I include -o test_mc tests/test_mesh_compressor_standalone.cpp source/comms/compress/MeshCompressor.cpp && ./test_mc
 */

#include "comms/compress/MeshCompressor.h"
#include "generated/mesh_model_data.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  %-50s", name); \
    fflush(stdout);

#define PASS() \
    printf("OK\n"); \
    tests_passed++;

#define FAIL(msg) \
    printf("FAIL: %s\n", msg); \
    tests_failed++;

using namespace mesh;

// ═══ Base91 ═══

void test_base91_empty()
{
    TEST("Base91: empty");
    auto enc = base91::encode(nullptr, 0);
    assert(enc.empty());
    auto dec = base91::decode(enc);
    assert(dec.empty());
    PASS();
}

void test_base91_hello()
{
    TEST("Base91: hello roundtrip");
    uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    auto enc = base91::encode(data, sizeof(data));
    auto dec = base91::decode(enc);
    assert(dec.size() == sizeof(data));
    assert(memcmp(dec.data(), data, sizeof(data)) == 0);
    PASS();
}

void test_base91_all_bytes()
{
    TEST("Base91: all 256 byte values roundtrip");
    std::vector<uint8_t> data(256);
    for (int i = 0; i < 256; i++)
        data[i] = static_cast<uint8_t>(i);
    auto enc = base91::encode(data);
    auto dec = base91::decode(enc);
    assert(dec.size() == data.size());
    for (size_t i = 0; i < data.size(); i++)
        assert(dec[i] == data[i]);
    PASS();
}

// ═══ Model Loading ═══

void test_model_load()
{
    TEST("NGramModel: load from binary");
    NGramModel model;
    bool ok = model.loadFromBinary(mesh_model_data, mesh_model_data_size);
    assert(ok);
    assert(model.order() == 9);
    assert(model.vocabSize() == 713);
    PASS();
}

void test_model_invalid()
{
    TEST("NGramModel: reject invalid data");
    NGramModel model;
    uint8_t bad[] = {0, 1, 2, 3};
    assert(!model.loadFromBinary(bad, sizeof(bad)));
    assert(!model.loadFromBinary(nullptr, 0));
    PASS();
}

// ═══ Compress/Decompress ═══

static MeshCompressor &compressor()
{
    static MeshCompressor c;
    static bool inited = false;
    if (!inited) {
        c.init(mesh_model_data, mesh_model_data_size);
        inited = true;
    }
    return c;
}

void test_compress_empty()
{
    TEST("Compress: empty string");
    auto &c = compressor();
    auto result = c.compress("");
    assert(result.size() == 2);
    assert(result[0] == 0x00);
    assert(result[1] == 0x00);
    PASS();
}

void test_roundtrip(const char *name, const char *text)
{
    char label[80];
    snprintf(label, sizeof(label), "Roundtrip: %s", name);
    TEST(label);

    auto &c = compressor();
    std::string input(text);
    auto compressed = c.compress(input);
    assert(!compressed.empty());

    auto decompressed = c.decompress(compressed);
    if (decompressed != input) {
        printf("\n    Expected: \"%s\"\n    Got:      \"%s\"\n", text, decompressed.c_str());
        FAIL("roundtrip mismatch");
        return;
    }

    double ratio = 100.0 * (1.0 - static_cast<double>(compressed.size()) / static_cast<double>(input.size()));
    printf("OK (%zu -> %zu bytes, %.0f%% compression)\n", input.size(), compressed.size(), ratio);
    tests_passed++;
}

void test_base91_transport()
{
    TEST("Base91 transport: roundtrip");
    auto &c = compressor();
    std::string text = "Hello Meshtastic!";
    auto encoded = c.compressToBase91(text);
    assert(!encoded.empty());
    assert(encoded[0] == '~');
    auto decoded = c.decompressFromBase91(encoded);
    assert(decoded == text);
    PASS();
}

int main()
{
    printf("=== MeshCompressor Tests ===\n\n");

    printf("--- Base91 ---\n");
    test_base91_empty();
    test_base91_hello();
    test_base91_all_bytes();

    printf("\n--- Model Loading ---\n");
    test_model_load();
    test_model_invalid();

    printf("\n--- Compress/Decompress ---\n");
    test_compress_empty();
    test_roundtrip("English short", "Hello world");
    test_roundtrip("English sentence", "The quick brown fox jumps over the lazy dog");
    test_roundtrip("Russian", "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xb8\xd1\x80");
    test_roundtrip("Russian sentence",
                   "\xd0\x91\xd1\x8b\xd1\x81\xd1\x82\xd1\x80\xd0\xb0\xd1\x8f "
                   "\xd0\xba\xd0\xbe\xd1\x80\xd0\xb8\xd1\x87\xd0\xbd\xd0\xb5\xd0\xb2\xd0\xb0\xd1\x8f "
                   "\xd0\xbb\xd0\xb8\xd1\x81\xd0\xb0");
    test_roundtrip("Mixed", "Meshtastic node 42 online");
    test_roundtrip("Digits", "123456789");
    test_roundtrip("Meshtastic msg", "Node !abc1234 heard 5 min ago");

    printf("\n--- Base91 Transport ---\n");
    test_base91_transport();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
