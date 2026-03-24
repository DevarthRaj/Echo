#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>

// ──────────────────────────────────────────────────────────────
//  Optimised Audio Packet Layout
//  [2B seq_num][4B timestamp_ms][N*2B PCM samples]
//
//  Changes from original:
//    seq_num      : 4B → 2B  (wraps at 65535, ~109 min at 100pkt/s)
//    timestamp    : 8B → 4B  (relative ms from session start, not epoch)
//    num_samples  : DROPPED  (derived from UDP packet length)
//    sample_rate  : DROPPED  (fixed constant, not needed per-packet)
//
//  Header: 16B → 6B
//  Total packet: 336B → 326B per frame
// ──────────────────────────────────────────────────────────────

/*#pragma pack(push, 1)
struct AudioPacketHeader {
    uint16_t seq_num;       // Sequence number — detect loss / reorder
    uint32_t timestamp_ms;  // Ms since session start — for delay measurement
};
#pragma pack(pop)

static constexpr uint16_t PACKET_SAMPLE_RATE = 16000; // Hz
static constexpr uint16_t PACKET_FRAME_SIZE  = 80;    // samples (5 ms @ 16 kHz, halved for lower latency)
static constexpr size_t   HEADER_SIZE        = sizeof(AudioPacketHeader); // 6 bytes

// Session start time — set once at program startup by calling initSessionClock()
static uint32_t g_session_start_ms = 0;

inline void initSessionClock() {
    using namespace std::chrono;
    g_session_start_ms = (uint32_t)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

inline uint32_t nowMs() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count() - g_session_start_ms;
}

// Serialize
inline std::vector<uint8_t> makePacket(uint16_t seq,
                                        uint32_t ts_ms,
                                        const int16_t* samples,
                                        uint16_t n)
{
    AudioPacketHeader hdr;
    hdr.seq_num      = seq;
    hdr.timestamp_ms = ts_ms;

    std::vector<uint8_t> buf(HEADER_SIZE + n * sizeof(int16_t));
    memcpy(buf.data(), &hdr, HEADER_SIZE);
    memcpy(buf.data() + HEADER_SIZE, samples, n * sizeof(int16_t));
    return buf;
}

// Deserialize — num_samples derived from packet length, no field needed
inline bool parsePacket(const uint8_t* data, size_t len,
                         AudioPacketHeader& hdrOut,
                         std::vector<int16_t>& samplesOut)
{
    if (len < HEADER_SIZE) return false;
    memcpy(&hdrOut, data, HEADER_SIZE);

    size_t payloadBytes = len - HEADER_SIZE;
    if (payloadBytes % sizeof(int16_t) != 0) return false; // malformed

    uint16_t numSamples = (uint16_t)(payloadBytes / sizeof(int16_t));
    samplesOut.resize(numSamples);
    memcpy(samplesOut.data(), data + HEADER_SIZE, payloadBytes);
    return true;
} */
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>

// ──────────────────────────────────────────────────────────────
//  Audio Packet Layout
//  [2B seq_num][4B timestamp_ms][N*2B PCM samples]
//
//  timestamp_ms: wall-clock ms since 2024-01-01 UTC.
//                Both sender and receiver use system_clock with the
//                same fixed epoch, so the difference is pure network delay.
//
//  Header: 6 bytes | Total: 6 + 160 = 166 bytes per 5ms frame
// ──────────────────────────────────────────────────────────────

#pragma pack(push, 1)
struct AudioPacketHeader {
    uint16_t seq_num;      // Sequence number — detect loss / reorder
    uint32_t timestamp_ms; // Ms since 2024-01-01 UTC (shared epoch, both nodes)
};
#pragma pack(pop)

static constexpr uint16_t PACKET_SAMPLE_RATE = 16000; // Hz
static constexpr uint16_t PACKET_FRAME_SIZE  = 80;    // samples (5 ms @ 16 kHz)
static constexpr size_t   HEADER_SIZE        = sizeof(AudioPacketHeader); // 6 bytes

// Fixed epoch: 2024-01-01 00:00:00 UTC in ms
// Subtracting this keeps the value in uint32_t range (~49 days of headroom)
static constexpr uint64_t EPOCH_OFFSET_MS = 1704067200000ULL;

// No-op — kept so existing main() calls don't break
inline void initSessionClock() {}

// Wall-clock ms relative to EPOCH_OFFSET_MS.
// Uses system_clock so both sender and receiver share the same reference.
inline uint32_t nowMs() {
    using namespace std::chrono;
    uint64_t ms = (uint64_t)duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    return (uint32_t)(ms - EPOCH_OFFSET_MS);
}

// Serialize — timestamp_ms is overwritten by sendLoop() just before sendto()
inline std::vector<uint8_t> makePacket(uint16_t seq,
                                        uint32_t ts_ms,
                                        const int16_t* samples,
                                        uint16_t n)
{
    AudioPacketHeader hdr;
    hdr.seq_num      = seq;
    hdr.timestamp_ms = ts_ms;

    std::vector<uint8_t> buf(HEADER_SIZE + n * sizeof(int16_t));
    memcpy(buf.data(), &hdr, HEADER_SIZE);
    memcpy(buf.data() + HEADER_SIZE, samples, n * sizeof(int16_t));
    return buf;
}

// Overwrite the timestamp in an already-serialised packet.
// Called right before sendto() so timestamp = wire departure time only.
inline void stampPacket(std::vector<uint8_t>& pkt, uint32_t ts_ms) {
    memcpy(pkt.data() + sizeof(uint16_t), &ts_ms, sizeof(ts_ms));
}

// Deserialize — num_samples derived from UDP payload length
inline bool parsePacket(const uint8_t* data, size_t len,
                         AudioPacketHeader& hdrOut,
                         std::vector<int16_t>& samplesOut)
{
    if (len < HEADER_SIZE) return false;
    memcpy(&hdrOut, data, HEADER_SIZE);

    size_t payloadBytes = len - HEADER_SIZE;
    if (payloadBytes % sizeof(int16_t) != 0) return false;

    uint16_t numSamples = (uint16_t)(payloadBytes / sizeof(int16_t));
    samplesOut.resize(numSamples);
    memcpy(samplesOut.data(), data + HEADER_SIZE, payloadBytes);
    return true;
}