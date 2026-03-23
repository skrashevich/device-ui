/**
 * MeshCompressor — N-gram + Arithmetic Coding text compressor for Meshtastic.
 * Port of https://github.com/dimapanov/mesh-compressor
 */

#include "comms/compress/MeshCompressor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mesh {

// ═══════════════════════════════════════════════════════════
// UTF-8 Helpers
// ═══════════════════════════════════════════════════════════

/** Extract individual UTF-8 characters from a string. */
static std::vector<std::string> utf8Chars(const std::string &s)
{
    std::vector<std::string> result;
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        uint8_t c = static_cast<uint8_t>(s[i]);
        if ((c & 0x80) == 0)
            len = 1;
        else if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        if (i + len > s.size())
            len = s.size() - i;
        result.push_back(s.substr(i, len));
        i += len;
    }
    return result;
}

/** Count UTF-8 characters in a string. */
static size_t utf8Length(const std::string &s)
{
    size_t count = 0;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        if ((c & 0x80) == 0)
            i += 1;
        else if ((c & 0xE0) == 0xC0)
            i += 2;
        else if ((c & 0xF0) == 0xE0)
            i += 3;
        else if ((c & 0xF8) == 0xF0)
            i += 4;
        else
            i += 1;
        count++;
    }
    return count;
}

// ═══════════════════════════════════════════════════════════
// Binary format read helpers (little-endian)
// ═══════════════════════════════════════════════════════════

static uint16_t readU16LE(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readU32LE(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t readU16BE(const uint8_t *p)
{
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

// ═══════════════════════════════════════════════════════════
// Base91
// ═══════════════════════════════════════════════════════════

static const char B91_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    "!#$%&()*+,./:;<=>?@[]^_`{|}~\"";

static int b91DecodeTable[256];
static bool b91TableInit = false;

static void initB91Table()
{
    if (b91TableInit)
        return;
    std::memset(b91DecodeTable, -1, sizeof(b91DecodeTable));
    for (int i = 0; i < 91; i++) {
        b91DecodeTable[static_cast<uint8_t>(B91_ALPHABET[i])] = i;
    }
    b91TableInit = true;
}

namespace base91 {

std::string encode(const uint8_t *data, size_t len)
{
    initB91Table();
    if (len == 0)
        return "";

    std::string result;
    result.reserve(len * 123 / 100); // ~23% overhead
    uint32_t n = 0;
    int nbits = 0;

    for (size_t i = 0; i < len; i++) {
        n |= static_cast<uint32_t>(data[i]) << nbits;
        nbits += 8;
        if (nbits > 13) {
            uint32_t val = n & 8191u; // lower 13 bits
            if (val > 88) {
                n >>= 13;
                nbits -= 13;
            } else {
                val = n & 16383u; // lower 14 bits
                n >>= 14;
                nbits -= 14;
            }
            result += B91_ALPHABET[val % 91];
            result += B91_ALPHABET[val / 91];
        }
    }

    if (nbits > 0) {
        result += B91_ALPHABET[n % 91];
        if (n >= 91 || nbits > 7) {
            result += B91_ALPHABET[n / 91];
        }
    }

    return result;
}

std::string encode(const std::vector<uint8_t> &data)
{
    return encode(data.data(), data.size());
}

std::vector<uint8_t> decode(const char *text, size_t len)
{
    initB91Table();
    if (len == 0)
        return {};

    std::vector<uint8_t> result;
    result.reserve(len * 100 / 123);
    uint32_t n = 0;
    int nbits = 0;
    int v = -1;

    for (size_t i = 0; i < len; i++) {
        int c = b91DecodeTable[static_cast<uint8_t>(text[i])];
        if (c < 0)
            continue; // skip invalid chars

        if (v == -1) {
            v = c;
        } else {
            v += c * 91;
            int b = (v & 8191) > 88 ? 13 : 14;
            n |= static_cast<uint32_t>(v) << nbits;
            nbits += b;
            v = -1;
            while (nbits >= 8) {
                result.push_back(static_cast<uint8_t>(n & 0xFF));
                n >>= 8;
                nbits -= 8;
            }
        }
    }

    if (v != -1) {
        n |= static_cast<uint32_t>(v) << nbits;
        nbits += 7;
        while (nbits >= 8) {
            result.push_back(static_cast<uint8_t>(n & 0xFF));
            n >>= 8;
            nbits -= 8;
        }
    }

    return result;
}

std::vector<uint8_t> decode(const std::string &text)
{
    return decode(text.c_str(), text.size());
}

} // namespace base91

// ═══════════════════════════════════════════════════════════
// NGramModel
// ═══════════════════════════════════════════════════════════

NGramModel::NGramModel() : order_(0) {}

bool NGramModel::loadFromBinary(const uint8_t *data, size_t len)
{
    if (len < 7)
        return false;

    // Check magic
    if (data[0] != 'M' || data[1] != 'C' || data[2] != '0' || data[3] != '1')
        return false;

    size_t pos = 4;
    order_ = data[pos++];
    uint16_t vocab_size = readU16LE(data + pos);
    pos += 2;

    // Read vocabulary (UTF-8 encoded characters)
    vocab_.clear();
    vocab_.reserve(vocab_size);
    vocab_idx_.clear();

    for (uint16_t i = 0; i < vocab_size; i++) {
        if (pos + 2 > len)
            return false;
        uint16_t ch_len = readU16LE(data + pos);
        pos += 2;
        if (pos + ch_len > len)
            return false;
        std::string ch(reinterpret_cast<const char *>(data + pos), ch_len);
        pos += ch_len;
        vocab_idx_[ch] = static_cast<int>(vocab_.size());
        vocab_.push_back(ch);
    }

    // Read level data
    levels_.clear();
    levels_.resize(order_ + 1);

    for (int n = 0; n <= order_; n++) {
        if (pos + 4 > len)
            return false;
        uint32_t num_contexts = readU32LE(data + pos);
        pos += 4;

        auto &level = levels_[n];
        level.reserve(num_contexts);

        for (uint32_t c = 0; c < num_contexts; c++) {
            if (pos + 2 > len)
                return false;
            uint16_t ctx_len = readU16LE(data + pos);
            pos += 2;
            if (pos + ctx_len > len)
                return false;
            std::string ctx(reinterpret_cast<const char *>(data + pos), ctx_len);
            pos += ctx_len;

            if (pos + 2 > len)
                return false;
            uint16_t num_pairs = readU16LE(data + pos);
            pos += 2;

            ContextData cd;
            cd.total = 0;
            cd.char_counts.reserve(num_pairs);
            for (uint16_t p = 0; p < num_pairs; p++) {
                if (pos + 6 > len)
                    return false;
                uint16_t char_idx = readU16LE(data + pos);
                pos += 2;
                uint32_t count = readU32LE(data + pos);
                pos += 4;
                cd.char_counts.push_back({char_idx, count});
                cd.total += count;
            }

            level[ctx] = std::move(cd);
        }
    }

    cdf_cache_.clear();
    return true;
}

int NGramModel::vocabIndex(const std::string &ch_utf8) const
{
    auto it = vocab_idx_.find(ch_utf8);
    return (it != vocab_idx_.end()) ? it->second : -1;
}

bool NGramModel::hasChar(const std::string &ch_utf8) const
{
    return vocab_idx_.find(ch_utf8) != vocab_idx_.end();
}

void NGramModel::ensureChar(const std::string &ch_utf8)
{
    if (hasChar(ch_utf8))
        return;
    if (vocab_.size() >= 65535)
        return; // uint16_t limit for char_counts keys

    // Append to end — no index shifting, no map rebuilding needed.
    // Both encoder and decoder call ensureChar in the same order
    // (extra chars are transmitted sorted in the wire header),
    // so vocab indices stay consistent across peers.
    int idx = static_cast<int>(vocab_.size());
    vocab_.push_back(ch_utf8);
    vocab_idx_[ch_utf8] = idx;

    cdf_cache_.clear();
}

void NGramModel::clearCache()
{
    cdf_cache_.clear();
}

PsramVector<CdfEntry> NGramModel::getCdf(const std::string &context)
{
    auto it = cdf_cache_.find(context);
    if (it != cdf_cache_.end())
        return it->second;

    auto cdf = computeCdf(context);

    if (cdf_cache_.size() < CDF_CACHE_MAX) {
        cdf_cache_.emplace(context, cdf);
    }

    return cdf;
}

PsramVector<CdfEntry> NGramModel::computeCdf(const std::string &context)
{
    size_t n_vocab = vocab_.size();

    // Step 1: find active orders and their weights
    struct ActiveOrder {
        int n;
        std::string ctx;
        uint32_t total;
        double weight;
    };
    std::vector<ActiveOrder> active;
    double total_w = 0.0;

    // Pre-compute UTF-8 char boundaries once (avoids repeated utf8Chars allocs)
    size_t ctx_byte_len = context.size();
    size_t ctx_char_offsets[64]; // byte offset of each UTF-8 char (max order ~9)
    size_t ctx_n_chars = 0;
    {
        size_t i = 0;
        while (i < ctx_byte_len && ctx_n_chars < 64) {
            ctx_char_offsets[ctx_n_chars++] = i;
            uint8_t c = static_cast<uint8_t>(context[i]);
            if ((c & 0x80) == 0) i += 1;
            else if ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else i += 4;
            if (i > ctx_byte_len) i = ctx_byte_len;
        }
    }

    for (int n = order_; n >= 0; n--) {
        std::string ctx;
        if (n > 0) {
            // Take last n UTF-8 characters from context — zero allocations
            size_t start_char = ctx_n_chars > static_cast<size_t>(n) ? ctx_n_chars - n : 0;
            size_t start_byte = ctx_char_offsets[start_char];
            ctx = context.substr(start_byte);
        }

        if (n < static_cast<int>(levels_.size())) {
            auto level_it = levels_[n].find(ctx);
            if (level_it != levels_[n].end() && level_it->second.total > 0) {
                double w = static_cast<double>((n + 1) * (n + 1) * (n + 1)) *
                           std::log1p(static_cast<double>(level_it->second.total));
                active.push_back({n, ctx, level_it->second.total, w});
                total_w += w;
            }
        }
    }

    // Step 2: compute raw frequency for each vocab symbol
    // Start with uniform epsilon (1 per symbol)
    // Use PsramVector to avoid exhausting internal SRAM on ESP32
    PsramVector<int32_t> freqs(n_vocab, 1);
    int32_t epsilon_total = static_cast<int32_t>(n_vocab);

    if (total_w > 0.0) {
        int32_t scale = static_cast<int32_t>(CDF_SCALE) - epsilon_total;

        for (const auto &a : active) {
            const auto &level_data = levels_[a.n].at(a.ctx);
            double factor = (a.weight / total_w) * static_cast<double>(scale) / static_cast<double>(a.total);

            for (const auto &cc : level_data.char_counts) {
                if (cc.idx < n_vocab) {
                    freqs[cc.idx] += static_cast<int32_t>(static_cast<double>(cc.count) * factor);
                }
            }
        }
    }

    // Normalize to CDF_SCALE exactly
    int32_t total = 0;
    for (size_t i = 0; i < n_vocab; i++)
        total += freqs[i];

    if (total != static_cast<int32_t>(CDF_SCALE)) {
        int32_t diff = static_cast<int32_t>(CDF_SCALE) - total;
        if (diff > 0) {
            // Add diff to the most frequent symbol
            size_t max_idx = 0;
            for (size_t i = 1; i < n_vocab; i++) {
                if (freqs[i] > freqs[max_idx])
                    max_idx = i;
            }
            freqs[max_idx] += diff;
        } else {
            // Remove -diff from the most frequent symbols
            PsramVector<size_t> indices(n_vocab);
            for (size_t i = 0; i < n_vocab; i++)
                indices[i] = i;
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return freqs[a] > freqs[b]; });

            int32_t remaining = -diff;
            for (size_t idx : indices) {
                if (remaining <= 0)
                    break;
                int32_t can_remove = freqs[idx] - 1;
                int32_t remove = std::min(can_remove, remaining);
                freqs[idx] -= remove;
                remaining -= remove;
            }
        }
    }

    // Build CDF
    PsramVector<CdfEntry> cdf;
    cdf.reserve(n_vocab);
    uint32_t cum = 0;
    for (size_t i = 0; i < n_vocab; i++) {
        uint32_t f = static_cast<uint32_t>(freqs[i]);
        cdf.push_back({static_cast<uint32_t>(i), cum, cum + f});
        cum += f;
    }

    return cdf;
}

// ═══════════════════════════════════════════════════════════
// ArithmeticEncoder
// ═══════════════════════════════════════════════════════════

ArithmeticEncoder::ArithmeticEncoder() : low_(0), high_(MASK), pending_(0) {}

void ArithmeticEncoder::emitBit(int bit)
{
    bits_.push_back(static_cast<uint8_t>(bit));
    uint8_t opp = static_cast<uint8_t>(1 - bit);
    for (int i = 0; i < pending_; i++)
        bits_.push_back(opp);
    pending_ = 0;
}

void ArithmeticEncoder::encodeSymbol(uint32_t cum_low, uint32_t cum_high, uint32_t total)
{
    uint64_t rng = static_cast<uint64_t>(high_) - static_cast<uint64_t>(low_) + 1;
    high_ = static_cast<uint32_t>(low_ + (rng * cum_high) / total - 1);
    low_ = static_cast<uint32_t>(low_ + (rng * cum_low) / total);

    for (;;) {
        if (high_ < HALF) {
            emitBit(0);
        } else if (low_ >= HALF) {
            emitBit(1);
            low_ -= HALF;
            high_ -= HALF;
        } else if (low_ >= QUARTER && high_ < THREE_QUARTER) {
            pending_++;
            low_ -= QUARTER;
            high_ -= QUARTER;
        } else {
            break;
        }
        low_ = (low_ << 1) & MASK;
        high_ = ((high_ << 1) | 1) & MASK;
    }
}

std::vector<uint8_t> ArithmeticEncoder::finish()
{
    pending_++;
    if (low_ < QUARTER) {
        emitBit(0);
    } else {
        emitBit(1);
    }

    // Pad to byte boundary
    while (bits_.size() % 8 != 0)
        bits_.push_back(0);

    // Pack bits into bytes
    std::vector<uint8_t> result;
    result.reserve(bits_.size() / 8);
    for (size_t i = 0; i < bits_.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = static_cast<uint8_t>((byte << 1) | bits_[i + j]);
        }
        result.push_back(byte);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════
// ArithmeticDecoder
// ═══════════════════════════════════════════════════════════

ArithmeticDecoder::ArithmeticDecoder(const uint8_t *data, size_t len)
    : data_(data), data_len_(len), low_(0), high_(MASK), value_(0), bit_pos_(0), total_bits_(len * 8)
{
    for (int i = 0; i < PRECISION; i++) {
        value_ = ((value_ << 1) | static_cast<uint32_t>(readBit())) & MASK;
    }
}

int ArithmeticDecoder::readBit()
{
    if (bit_pos_ >= total_bits_)
        return 0;
    size_t byte_idx = bit_pos_ >> 3;
    int bit_idx = 7 - (bit_pos_ & 7);
    bit_pos_++;
    return (data_[byte_idx] >> bit_idx) & 1;
}

uint32_t ArithmeticDecoder::decodeSymbol(const PsramVector<CdfEntry> &cdf)
{
    if (cdf.empty())
        return UINT32_MAX;

    uint64_t rng = static_cast<uint64_t>(high_) - static_cast<uint64_t>(low_) + 1;
    uint32_t scaled =
        static_cast<uint32_t>(((static_cast<uint64_t>(value_) - static_cast<uint64_t>(low_) + 1) *
                                    static_cast<uint64_t>(CDF_SCALE) -
                                1) /
                               rng);

    // Binary search for symbol where cum_low <= scaled < cum_high
    size_t lo = 0, hi = cdf.size() - 1;
    while (lo < hi) {
        size_t mid = (lo + hi) >> 1;
        if (cdf[mid].cum_high <= scaled)
            lo = mid + 1;
        else
            hi = mid;
    }

    uint32_t sym = cdf[lo].char_index;
    uint32_t cum_low = cdf[lo].cum_low;
    uint32_t cum_high = cdf[lo].cum_high;

    high_ = static_cast<uint32_t>(low_ + (rng * cum_high) / CDF_SCALE - 1);
    low_ = static_cast<uint32_t>(low_ + (rng * cum_low) / CDF_SCALE);

    for (;;) {
        if (high_ < HALF) {
            // nothing
        } else if (low_ >= HALF) {
            low_ -= HALF;
            high_ -= HALF;
            value_ -= HALF;
        } else if (low_ >= QUARTER && high_ < THREE_QUARTER) {
            low_ -= QUARTER;
            high_ -= QUARTER;
            value_ -= QUARTER;
        } else {
            break;
        }
        low_ = (low_ << 1) & MASK;
        high_ = ((high_ << 1) | 1) & MASK;
        value_ = ((value_ << 1) | static_cast<uint32_t>(readBit())) & MASK;
    }

    return sym;
}

// ═══════════════════════════════════════════════════════════
// MeshCompressor
// ═══════════════════════════════════════════════════════════

MeshCompressor::MeshCompressor() : ready_(false) {}

bool MeshCompressor::init(const uint8_t *model_data, size_t model_len)
{
    ready_ = model_.loadFromBinary(model_data, model_len);
    return ready_;
}

std::vector<uint8_t> MeshCompressor::compress(const std::string &text)
{
    if (!ready_)
        return {};

    if (text.empty())
        return {0x00, 0x00};

    auto chars = utf8Chars(text);
    if (chars.size() > 65535)
        return {}; // too long for wire format (uint16 text_len)
    if (chars.size() > 255) {
        // Meshtastic payload limit is ~233 bytes; compressed output
        // will almost certainly exceed that for very long messages
    }

    // Find extra characters not in model vocabulary
    std::vector<std::string> extra_chars;
    {
        std::vector<std::string> seen;
        for (const auto &ch : chars) {
            if (!model_.hasChar(ch)) {
                if (std::find(seen.begin(), seen.end(), ch) == seen.end()) {
                    seen.push_back(ch);
                }
            }
        }
        extra_chars = seen;
        std::sort(extra_chars.begin(), extra_chars.end());
    }

    // Add extra chars to model vocab
    for (const auto &ch : extra_chars) {
        model_.ensureChar(ch);
    }

    ArithmeticEncoder encoder;
    std::string bos_str(model_.order(), BOS);
    std::string context = bos_str;

    std::string eof_str(1, EOFS);

    for (const auto &ch : chars) {
        auto cdf = model_.getCdf(context);
        int idx = model_.vocabIndex(ch);
        if (idx < 0 || static_cast<size_t>(idx) >= cdf.size())
            return {}; // should not happen after ensureChar

        // CDF is built in vocab order, so cdf[idx].char_index == idx
        encoder.encodeSymbol(cdf[idx].cum_low, cdf[idx].cum_high, CDF_SCALE);

        context += ch;
        // Keep last order_ characters
        auto ctx_chars = utf8Chars(context);
        if (ctx_chars.size() > static_cast<size_t>(model_.order())) {
            context.clear();
            for (size_t i = ctx_chars.size() - model_.order(); i < ctx_chars.size(); i++)
                context += ctx_chars[i];
        }
    }

    // Encode EOF
    {
        auto cdf = model_.getCdf(context);
        int eof_idx = model_.vocabIndex(eof_str);
        if (eof_idx >= 0 && static_cast<size_t>(eof_idx) < cdf.size()) {
            encoder.encodeSymbol(cdf[eof_idx].cum_low, cdf[eof_idx].cum_high, CDF_SCALE);
        }
    }

    auto ac_bytes = encoder.finish();

    // Build header: [uint16 BE text_len] [uint8 n_extra] [extra chars as utf-8]
    std::vector<uint8_t> result;
    uint16_t text_len = static_cast<uint16_t>(chars.size());
    result.push_back(static_cast<uint8_t>(text_len >> 8));
    result.push_back(static_cast<uint8_t>(text_len & 0xFF));
    if (extra_chars.size() > 255)
        return {}; // too many extra chars for wire format (uint8)
    result.push_back(static_cast<uint8_t>(extra_chars.size()));

    for (const auto &ch : extra_chars) {
        result.push_back(static_cast<uint8_t>(ch.size()));
        for (uint8_t b : ch)
            result.push_back(b);
    }

    result.insert(result.end(), ac_bytes.begin(), ac_bytes.end());
    return result;
}

std::string MeshCompressor::decompress(const uint8_t *data, size_t len)
{
    if (!ready_ || len < 3)
        return "";

    uint16_t text_len = readU16BE(data);
    if (text_len == 0)
        return "";
    // Guard against DoS: Meshtastic max payload is ~233 bytes,
    // so decompressed text realistically won't exceed ~1000 chars
    static constexpr uint16_t MAX_DECOMPRESS_LEN = 4096;
    if (text_len > MAX_DECOMPRESS_LEN)
        return "";

    uint8_t n_extra = data[2];
    size_t offset = 3;

    // Read extra chars and add to model vocab
    for (uint8_t i = 0; i < n_extra; i++) {
        if (offset >= len)
            return "";
        uint8_t ch_len = data[offset++];
        if (offset + ch_len > len)
            return "";
        std::string ch(reinterpret_cast<const char *>(data + offset), ch_len);
        offset += ch_len;
        model_.ensureChar(ch);
    }

    if (offset >= len)
        return "";

    const uint8_t *ac_data = data + offset;
    size_t ac_len = len - offset;

    ArithmeticDecoder decoder(ac_data, ac_len);
    std::string bos_str(model_.order(), BOS);
    std::string context = bos_str;
    std::string eof_str(1, EOFS);

    std::string result;
    for (uint32_t i = 0; i < static_cast<uint32_t>(text_len) + 1; i++) { // +1 for EOF
        auto cdf = model_.getCdf(context);
        uint32_t sym_idx = decoder.decodeSymbol(cdf);

        if (sym_idx >= model_.vocabSize())
            break;

        const std::string &ch = model_.vocabAt(sym_idx);
        if (ch == eof_str)
            break;

        result += ch;
        context += ch;

        // Keep last order_ characters
        auto ctx_chars = utf8Chars(context);
        if (ctx_chars.size() > static_cast<size_t>(model_.order())) {
            context.clear();
            for (size_t j = ctx_chars.size() - model_.order(); j < ctx_chars.size(); j++)
                context += ctx_chars[j];
        }
    }

    return result;
}

std::string MeshCompressor::decompress(const std::vector<uint8_t> &data)
{
    return decompress(data.data(), data.size());
}

std::string MeshCompressor::compressToBase91(const std::string &text)
{
    auto compressed = compress(text);
    if (compressed.empty())
        return "";
    return "~" + base91::encode(compressed);
}

std::string MeshCompressor::decompressFromBase91(const std::string &encoded)
{
    if (encoded.empty() || encoded[0] != '~')
        return "";
    auto decoded = base91::decode(encoded.c_str() + 1, encoded.size() - 1);
    if (decoded.empty())
        return "";
    return decompress(decoded);
}

} // namespace mesh
