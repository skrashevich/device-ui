#pragma once

/**
 * MeshCompressor — N-gram + Arithmetic Coding text compressor for Meshtastic.
 *
 * Port of https://github.com/dimapanov/mesh-compressor
 * Achieves 50-87% compression on short bilingual (RU/EN) messages,
 * far exceeding standard algorithms on short texts.
 *
 * Wire format:
 *   [2 bytes] original text length (uint16 BE)
 *   [1 byte]  number of extra characters not in model vocabulary
 *   [variable] extra chars: [1-byte UTF-8 length][UTF-8 bytes] each
 *   [variable] arithmetic-coded bitstream (byte-aligned)
 *
 * Text transport (Base91):
 *   Prefix '~' + Base91-encoded compressed bytes.
 *   Works as a regular TEXT_MESSAGE — compression-aware receivers decode it.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mesh {

// ─── Constants ───────────────────────────────────────────

static constexpr uint32_t CDF_SCALE = 1u << 20;  // 1048576
static constexpr int PRECISION = 32;
static constexpr uint32_t FULL = 0u;        // 1 << 32 overflows, handled specially
static constexpr uint32_t HALF = 1u << 31;
static constexpr uint32_t QUARTER = 1u << 30;
static constexpr uint32_t THREE_QUARTER = 3u * QUARTER;
static constexpr uint32_t MASK = 0xFFFFFFFFu;

static constexpr char BOS = '\x02';  // Beginning of sequence
static constexpr char EOFS = '\x03'; // End of sequence

static constexpr size_t CDF_CACHE_MAX = 50000;

// ─── CDF Entry ───────────────────────────────────────────

struct CdfEntry {
    uint32_t char_index; // index into vocab
    uint32_t cum_low;
    uint32_t cum_high;
};

// ─── N-Gram Language Model ───────────────────────────────

class NGramModel {
public:
    NGramModel();

    /**
     * Load model from binary blob.
     * Binary format:
     *   "MC01" magic, uint8 order, uint16LE vocab_size,
     *   vocab entries (uint32LE codepoint each),
     *   per-level: uint32LE num_contexts,
     *     per-context: uint16LE ctx_utf8_len, ctx_utf8[], uint16LE num_pairs,
     *       per-pair: uint16LE vocab_idx, uint32LE count
     */
    bool loadFromBinary(const uint8_t *data, size_t len);

    /** Get CDF table for arithmetic coder given a context string (UTF-8). */
    std::vector<CdfEntry> getCdf(const std::string &context);

    /** Add a character to vocabulary if not already present. */
    void ensureChar(const std::string &ch_utf8);

    /** Clear CDF cache (e.g. after vocab changes). */
    void clearCache();

    int order() const { return order_; }
    size_t vocabSize() const { return vocab_.size(); }

    /** Get UTF-8 string for vocab index. */
    const std::string &vocabAt(size_t idx) const { return vocab_[idx]; }

    /** Find vocab index for a UTF-8 character, or -1 if not found. */
    int vocabIndex(const std::string &ch_utf8) const;

    /** Check if a character is in the vocabulary. */
    bool hasChar(const std::string &ch_utf8) const;

private:
    std::vector<CdfEntry> computeCdf(const std::string &context);

    int order_;
    std::vector<std::string> vocab_; // sorted UTF-8 characters
    std::unordered_map<std::string, int> vocab_idx_;

    // counts_[n][context_utf8][vocab_idx] = count
    struct ContextData {
        std::unordered_map<uint16_t, uint32_t> char_counts;
        uint32_t total;
    };
    std::vector<std::unordered_map<std::string, ContextData>> levels_;

    // CDF cache
    std::unordered_map<std::string, std::vector<CdfEntry>> cdf_cache_;
};

// ─── Arithmetic Encoder ──────────────────────────────────

class ArithmeticEncoder {
public:
    ArithmeticEncoder();

    void encodeSymbol(uint32_t cum_low, uint32_t cum_high, uint32_t total);
    std::vector<uint8_t> finish();

private:
    void emitBit(int bit);

    uint32_t low_;
    uint32_t high_;
    int pending_;
    std::vector<uint8_t> bits_;
};

// ─── Arithmetic Decoder ──────────────────────────────────

class ArithmeticDecoder {
public:
    ArithmeticDecoder(const uint8_t *data, size_t len);

    /** Decode one symbol from CDF table. Returns vocab index. */
    uint32_t decodeSymbol(const std::vector<CdfEntry> &cdf);

private:
    int readBit();

    const uint8_t *data_;
    size_t data_len_;
    uint32_t low_;
    uint32_t high_;
    uint32_t value_;
    size_t bit_pos_;
    size_t total_bits_;
};

// ─── Base91 ──────────────────────────────────────────────

namespace base91 {

std::string encode(const uint8_t *data, size_t len);
std::string encode(const std::vector<uint8_t> &data);
std::vector<uint8_t> decode(const char *text, size_t len);
std::vector<uint8_t> decode(const std::string &text);

} // namespace base91

// ─── MeshCompressor (main API) ───────────────────────────

class MeshCompressor {
public:
    MeshCompressor();

    /**
     * Initialize with pre-loaded model binary data.
     * The data pointer must remain valid for the lifetime of this object
     * only during this call (data is copied internally).
     */
    bool init(const uint8_t *model_data, size_t model_len);

    /** Check if model is loaded and ready. */
    bool isReady() const { return ready_; }

    /**
     * Compress a UTF-8 text message.
     * Returns binary compressed data in wire format.
     * Returns empty vector on error.
     */
    std::vector<uint8_t> compress(const std::string &text);

    /**
     * Decompress binary data back to UTF-8 text.
     * Returns empty string on error.
     */
    std::string decompress(const uint8_t *data, size_t len);
    std::string decompress(const std::vector<uint8_t> &data);

    /**
     * Compress and encode as Base91 text with '~' prefix.
     * Can be sent as a regular TEXT_MESSAGE.
     */
    std::string compressToBase91(const std::string &text);

    /**
     * Decode Base91 text (with '~' prefix) and decompress.
     */
    std::string decompressFromBase91(const std::string &encoded);

    /** Access the underlying model. */
    NGramModel &model() { return model_; }

private:
    NGramModel model_;
    bool ready_;
};

} // namespace mesh
