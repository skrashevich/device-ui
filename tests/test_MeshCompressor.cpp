#include <doctest/doctest.h>

#include "comms/compress/MeshCompressor.h"
#include "generated/mesh_model_data.h"

using namespace mesh;

// ═══════════════════════════════════════════════════════════
// Base91 Tests
// ═══════════════════════════════════════════════════════════

TEST_CASE("Base91 roundtrip - empty")
{
    auto encoded = base91::encode(nullptr, 0);
    CHECK(encoded.empty());
    auto decoded = base91::decode(encoded);
    CHECK(decoded.empty());
}

TEST_CASE("Base91 roundtrip - simple bytes")
{
    uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
    auto encoded = base91::encode(data, sizeof(data));
    CHECK(!encoded.empty());
    auto decoded = base91::decode(encoded);
    REQUIRE(decoded.size() == sizeof(data));
    for (size_t i = 0; i < sizeof(data); i++) {
        CHECK(decoded[i] == data[i]);
    }
}

TEST_CASE("Base91 roundtrip - all byte values")
{
    std::vector<uint8_t> data(256);
    for (int i = 0; i < 256; i++)
        data[i] = static_cast<uint8_t>(i);

    auto encoded = base91::encode(data);
    auto decoded = base91::decode(encoded);
    REQUIRE(decoded.size() == data.size());
    for (size_t i = 0; i < data.size(); i++) {
        CHECK(decoded[i] == data[i]);
    }
}

TEST_CASE("Base91 roundtrip - zeros")
{
    std::vector<uint8_t> data(100, 0);
    auto encoded = base91::encode(data);
    auto decoded = base91::decode(encoded);
    REQUIRE(decoded.size() == data.size());
    for (size_t i = 0; i < data.size(); i++) {
        CHECK(decoded[i] == 0);
    }
}

// ═══════════════════════════════════════════════════════════
// Model Loading Tests
// ═══════════════════════════════════════════════════════════

TEST_CASE("NGramModel loads from binary")
{
    NGramModel model;
    bool ok = model.loadFromBinary(mesh_model_data, mesh_model_data_size);
    CHECK(ok);
    CHECK(model.order() == 9);
    CHECK(model.vocabSize() == 713);
}

TEST_CASE("NGramModel invalid data")
{
    NGramModel model;
    uint8_t bad[] = {0, 1, 2, 3};
    CHECK_FALSE(model.loadFromBinary(bad, sizeof(bad)));
    CHECK_FALSE(model.loadFromBinary(nullptr, 0));
}

// ═══════════════════════════════════════════════════════════
// MeshCompressor Tests
// ═══════════════════════════════════════════════════════════

static MeshCompressor &getCompressor()
{
    static MeshCompressor compressor;
    static bool inited = false;
    if (!inited) {
        compressor.init(mesh_model_data, mesh_model_data_size);
        inited = true;
    }
    return compressor;
}

TEST_CASE("MeshCompressor init")
{
    auto &c = getCompressor();
    CHECK(c.isReady());
}

TEST_CASE("MeshCompressor compress empty string")
{
    auto &c = getCompressor();
    auto result = c.compress("");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == 0x00);
    CHECK(result[1] == 0x00);
}

TEST_CASE("MeshCompressor roundtrip - English")
{
    auto &c = getCompressor();
    std::string text = "Hello world";
    auto compressed = c.compress(text);
    CHECK(!compressed.empty());
    CHECK(compressed.size() < text.size()); // should compress
    auto decompressed = c.decompress(compressed);
    CHECK(decompressed == text);
}

TEST_CASE("MeshCompressor roundtrip - Russian")
{
    auto &c = getCompressor();
    std::string text = "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xb8\xd1\x80"; // "Привет мир"
    auto compressed = c.compress(text);
    CHECK(!compressed.empty());
    auto decompressed = c.decompress(compressed);
    CHECK(decompressed == text);
}

TEST_CASE("MeshCompressor roundtrip - mixed")
{
    auto &c = getCompressor();
    std::string text = "Meshtastic node 42 online";
    auto compressed = c.compress(text);
    CHECK(!compressed.empty());
    auto decompressed = c.decompress(compressed);
    CHECK(decompressed == text);
}

TEST_CASE("MeshCompressor roundtrip - digits")
{
    auto &c = getCompressor();
    std::string text = "123456789";
    auto compressed = c.compress(text);
    CHECK(!compressed.empty());
    auto decompressed = c.decompress(compressed);
    CHECK(decompressed == text);
}

TEST_CASE("MeshCompressor roundtrip - longer message")
{
    auto &c = getCompressor();
    std::string text = "The quick brown fox jumps over the lazy dog";
    auto compressed = c.compress(text);
    CHECK(!compressed.empty());
    CHECK(compressed.size() < text.size());
    auto decompressed = c.decompress(compressed);
    CHECK(decompressed == text);
}

TEST_CASE("MeshCompressor roundtrip - Cyrillic sentence")
{
    auto &c = getCompressor();
    // "Быстрая коричневая лиса"
    std::string text =
        "\xd0\x91\xd1\x8b\xd1\x81\xd1\x82\xd1\x80\xd0\xb0\xd1\x8f "
        "\xd0\xba\xd0\xbe\xd1\x80\xd0\xb8\xd1\x87\xd0\xbd\xd0\xb5\xd0\xb2\xd0\xb0\xd1\x8f "
        "\xd0\xbb\xd0\xb8\xd1\x81\xd0\xb0";
    auto compressed = c.compress(text);
    CHECK(!compressed.empty());
    auto decompressed = c.decompress(compressed);
    CHECK(decompressed == text);
}

// ═══════════════════════════════════════════════════════════
// Base91 Transport Tests
// ═══════════════════════════════════════════════════════════

TEST_CASE("MeshCompressor Base91 roundtrip")
{
    auto &c = getCompressor();
    std::string text = "Hello Meshtastic!";
    auto encoded = c.compressToBase91(text);
    CHECK(!encoded.empty());
    CHECK(encoded[0] == '~');
    auto decoded = c.decompressFromBase91(encoded);
    CHECK(decoded == text);
}

TEST_CASE("MeshCompressor Base91 roundtrip - Russian")
{
    auto &c = getCompressor();
    std::string text = "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82!"; // "Привет!"
    auto encoded = c.compressToBase91(text);
    CHECK(!encoded.empty());
    CHECK(encoded[0] == '~');
    auto decoded = c.decompressFromBase91(encoded);
    CHECK(decoded == text);
}
