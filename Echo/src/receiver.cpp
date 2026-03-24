// receiver.cpp – Node B (UDP, optimised packet)
// Build:
//   g++ src/receiver.cpp -o receiver -I include -lportaudio -lws2_32 -lwinmm -std=c++14
// Run:
//   ./receiver 9002

/*#include <winsock2.h>
#include <ws2tcpip.h>
#include <portaudio.h>
#include "packet.hpp"

#include <iostream>
#include <fstream>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

static std::atomic<bool>    g_running{true};

static std::mutex           g_bufMtx;
static std::deque<int16_t>  g_pcmBuf;

static std::mutex           g_wavMtx;
static std::vector<int16_t> g_wavData;

static std::mutex           g_statMtx;
static double               g_totalDelayMs = 0.0;
static uint64_t             g_packetCount  = 0;
static double               g_maxDelayMs   = 0.0;
static uint16_t             g_lastSeq      = 0;
static uint64_t             g_lostPackets  = 0;

// Priming: wait 2 frames before starting playback
static constexpr size_t     PRIME_THRESHOLD = PACKET_FRAME_SIZE * 2;
static std::atomic<bool>    g_primed{false};

static void writeWav(const std::string& path,
                     const std::vector<int16_t>& samples, uint32_t sr)
{
    std::ofstream f(path, std::ios::binary);
    uint32_t dataSize = (uint32_t)(samples.size() * 2);
    uint32_t chunkSize = 36 + dataSize;
    uint16_t fmt=1, ch=1, ba=2, bps=16;
    uint32_t br = sr * 2;
    f.write("RIFF",4); f.write((char*)&chunkSize,4);
    f.write("WAVE",4); f.write("fmt ",4);
    uint32_t fs=16;
    f.write((char*)&fs,4);  f.write((char*)&fmt,2); f.write((char*)&ch,2);
    f.write((char*)&sr,4);  f.write((char*)&br,4);
    f.write((char*)&ba,2);  f.write((char*)&bps,2);
    f.write("data",4); f.write((char*)&dataSize,4);
    f.write((char*)samples.data(), dataSize);
    std::cout << "[Receiver] Saved: " << path
              << " (" << samples.size()/sr << "s)\n";
}

static int playCallback(const void*, void* out,
                         unsigned long frames,
                         const PaStreamCallbackTimeInfo*,
                         PaStreamCallbackFlags, void*)
{
    int16_t* o = static_cast<int16_t*>(out);
    std::lock_guard<std::mutex> lk(g_bufMtx);

    if (!g_primed && g_pcmBuf.size() < PRIME_THRESHOLD) {
        memset(o, 0, frames * sizeof(int16_t));
        return paContinue;
    }
    g_primed = true;

    for (unsigned long i = 0; i < frames; ++i) {
        if (!g_pcmBuf.empty()) {
            o[i] = g_pcmBuf.front();
            g_pcmBuf.pop_front();
        } else {
            o[i] = 0;
        }
    }
    return paContinue;
}

static void recvLoop(SOCKET sock) {
    static uint8_t buf[4096];
    bool firstPacket = true;

    while (g_running) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, (char*)buf, sizeof(buf), 0,
                          (sockaddr*)&from, &fromLen);
        if (n <= 0) continue;

        uint32_t recvMs = nowMs();

        AudioPacketHeader hdr;
        std::vector<int16_t> samples;
        if (!parsePacket(buf, (size_t)n, hdr, samples)) continue;

        // Delay (relative ms — both clocks started at session start)
        double delayMs = (double)(recvMs - hdr.timestamp_ms);

        // Packet loss detection
        if (!firstPacket) {
            uint16_t expected = (uint16_t)(g_lastSeq + 1);
            if (hdr.seq_num != expected) {
                uint16_t lost = (uint16_t)(hdr.seq_num - expected);
                std::lock_guard<std::mutex> lk(g_statMtx);
                g_lostPackets += lost;
            }
        }
        firstPacket = false;
        g_lastSeq = hdr.seq_num;

        {
            std::lock_guard<std::mutex> lk(g_statMtx);
            g_totalDelayMs += delayMs;
            ++g_packetCount;
            if (delayMs > g_maxDelayMs) g_maxDelayMs = delayMs;
            if (g_packetCount % 100 == 0) {  // every 100 pkts = 0.5s at 5ms frames
                std::cout << "[Receiver] Pkts=" << g_packetCount
                          << "  Avg=" << g_totalDelayMs/g_packetCount << "ms"
                          << "  Max=" << g_maxDelayMs << "ms"
                          << "  Lost=" << g_lostPackets
                          << "  Seq=" << hdr.seq_num << "\n";
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_bufMtx);
            if (g_pcmBuf.size() < (size_t)PACKET_SAMPLE_RATE)
                for (auto s : samples) g_pcmBuf.push_back(s);
        }

        {
            std::lock_guard<std::mutex> lk(g_wavMtx);
            g_wavData.insert(g_wavData.end(), samples.begin(), samples.end());
        }
    }
}

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : 9002;

    initSessionClock();  // start relative timestamp clock

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[Receiver] socket() failed\n"; return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[Receiver] bind() failed on port " << port << "\n"; return 1;
    }

    DWORD timeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    std::cout << "[Receiver] Listening on UDP port " << port << "\n";
    std::cout << "[Receiver] Frame: " << PACKET_FRAME_SIZE
              << " samples = " << (PACKET_FRAME_SIZE * 1000 / PACKET_SAMPLE_RATE)
              << " ms | Header: " << HEADER_SIZE << " bytes\n";

    Pa_Initialize();
    PaStreamParameters outParams{};
    outParams.device = Pa_GetDefaultOutputDevice();
    if (outParams.device == paNoDevice) {
        std::cerr << "[Receiver] No output device\n"; return 1;
    }
    outParams.channelCount     = 1;
    outParams.sampleFormat     = paInt16;
    outParams.suggestedLatency =
        Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;  // Low latency now

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, nullptr, &outParams,
                                 PACKET_SAMPLE_RATE, PACKET_FRAME_SIZE,
                                 paClipOff, playCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "[Receiver] PortAudio: " << Pa_GetErrorText(err) << "\n"; return 1;
    }
    Pa_StartStream(stream);

    std::cout << "[Receiver] Playing audio in real time. Press ENTER to stop.\n";

    std::thread recvThread(recvLoop, sock);
    std::cin.get();

    g_running = false;
    recvThread.join();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    {
        std::lock_guard<std::mutex> lk(g_wavMtx);
        if (!g_wavData.empty())
            writeWav("output.wav", g_wavData, PACKET_SAMPLE_RATE);
    }

    {
        std::lock_guard<std::mutex> lk(g_statMtx);
        if (g_packetCount > 0) {
            double lossRate = 100.0 * g_lostPackets / (g_packetCount + g_lostPackets);
            std::cout << "\n── Final Summary ───────────────────\n"
                      << "  Packets received : " << g_packetCount       << "\n"
                      << "  Packets lost     : " << g_lostPackets       << " (" << lossRate << "%)\n"
                      << "  Avg delay        : " << g_totalDelayMs/g_packetCount << " ms\n"
                      << "  Max delay        : " << g_maxDelayMs        << " ms\n"
                      << "────────────────────────────────────\n";
        }
    }

    closesocket(sock);
    WSACleanup();
    std::cout << "[Receiver] Done.\n";
    return 0;
}*/
// receiver.cpp – Node B (UDP, optimised packet)
// Build:
//   g++ src/receiver.cpp -o receiver -I include -lportaudio -lws2_32 -lwinmm -std=c++14
// Run:
//   ./receiver 9002

// receiver.cpp – Node B (UDP, pure network latency measurement)
// Build:
//   g++ src/receiver.cpp -o receiver -I include -lportaudio -lws2_32 -lwinmm -std=c++14
// Run:
//   ./receiver 9002

// receiver.cpp – Node B (UDP, pure network latency measurement)
// Build:
//   g++ src/receiver.cpp -o receiver -I include -lportaudio -lws2_32 -lwinmm -std=c++14
// Run:
//   ./receiver 9002

#include <winsock2.h>
#include <ws2tcpip.h>
#include <portaudio.h>
#include "packet.hpp"

#include <iostream>
#include <fstream>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

static std::atomic<bool>    g_running{true};

static std::mutex           g_bufMtx;
static std::deque<int16_t>  g_pcmBuf;

static std::mutex           g_wavMtx;
static std::vector<int16_t> g_wavData;

// ── Network latency stats (pure wire: sendto → recvfrom) ──────
static std::mutex  g_statMtx;
static double      g_totalDelayMs = 0.0;
static double      g_maxDelayMs   = 0.0;
static uint64_t    g_packetCount  = 0;
static uint16_t    g_lastSeq      = 0;
static uint64_t    g_lostPackets  = 0;

// ── Jitter (inter-arrival deviation from expected 5ms gap) ────
static double      g_totalJitterMs = 0.0;
static double      g_maxJitterMs   = 0.0;
static uint32_t    g_lastArrivalMs = 0;
static bool        g_firstPacket   = true;

// Priming: wait 2 frames before starting playback
static constexpr size_t  PRIME_THRESHOLD = PACKET_FRAME_SIZE * 2;
static std::atomic<bool> g_primed{false};

// ── WAV writer ────────────────────────────────────────────────
static void writeWav(const std::string& path,
                     const std::vector<int16_t>& samples, uint32_t sr)
{
    std::ofstream f(path, std::ios::binary);
    uint32_t dataSize  = (uint32_t)(samples.size() * 2);
    uint32_t chunkSize = 36 + dataSize;
    uint16_t fmt=1, ch=1, ba=2, bps=16;
    uint32_t br = sr * 2;
    f.write("RIFF",4); f.write((char*)&chunkSize,4);
    f.write("WAVE",4); f.write("fmt ",4);
    uint32_t fs=16;
    f.write((char*)&fs,4);  f.write((char*)&fmt,2); f.write((char*)&ch,2);
    f.write((char*)&sr,4);  f.write((char*)&br,4);
    f.write((char*)&ba,2);  f.write((char*)&bps,2);
    f.write("data",4); f.write((char*)&dataSize,4);
    f.write((char*)samples.data(), dataSize);
    std::cout << "[Receiver] Saved: " << path
              << " (" << samples.size()/sr << "s)\n";
}

// ── PortAudio playback callback ───────────────────────────────
static int playCallback(const void*, void* out,
                         unsigned long frames,
                         const PaStreamCallbackTimeInfo*,
                         PaStreamCallbackFlags, void*)
{
    int16_t* o = static_cast<int16_t*>(out);
    std::lock_guard<std::mutex> lk(g_bufMtx);

    if (!g_primed && g_pcmBuf.size() < PRIME_THRESHOLD) {
        memset(o, 0, frames * sizeof(int16_t));
        return paContinue;
    }
    g_primed = true;

    for (unsigned long i = 0; i < frames; ++i) {
        if (!g_pcmBuf.empty()) {
            o[i] = g_pcmBuf.front();
            g_pcmBuf.pop_front();
        } else {
            o[i] = 0;
        }
    }
    return paContinue;
}

// ── Receive loop ──────────────────────────────────────────────
static void recvLoop(SOCKET sock) {
    static uint8_t buf[4096];

    while (g_running) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, (char*)buf, sizeof(buf), 0,
                          (sockaddr*)&from, &fromLen);
        if (n <= 0) continue;

        // Grab receive timestamp immediately after recvfrom — as early as possible
        uint32_t arrivalMs = nowMs();

        AudioPacketHeader hdr;
        std::vector<int16_t> samples;
        if (!parsePacket(buf, (size_t)n, hdr, samples)) continue;

        // ── Pure network delay: sendto() → recvfrom() ────────
        // Both timestamps use system_clock with the same epoch.
        // PA latency, queue wait, mic capture — none of it is included.
        double netDelayMs = (double)(arrivalMs - hdr.timestamp_ms);

        // ── Jitter ────────────────────────────────────────────
        double jitterMs = 0.0;
        {
            std::lock_guard<std::mutex> lk(g_statMtx);
            if (!g_firstPacket) {
                constexpr double EXPECTED_GAP_MS =
                    (PACKET_FRAME_SIZE * 1000.0) / PACKET_SAMPLE_RATE; // 5ms
                double gap = (double)(arrivalMs - g_lastArrivalMs);
                jitterMs = gap - EXPECTED_GAP_MS;
                if (jitterMs < 0) jitterMs = -jitterMs;
                g_totalJitterMs += jitterMs;
                if (jitterMs > g_maxJitterMs) g_maxJitterMs = jitterMs;
            }
            g_lastArrivalMs = arrivalMs;
        }

        // ── Packet loss ───────────────────────────────────────
        {
            std::lock_guard<std::mutex> lk(g_statMtx);
            if (!g_firstPacket) {
                uint16_t expected = (uint16_t)(g_lastSeq + 1);
                if (hdr.seq_num != expected) {
                    uint16_t lost = (uint16_t)(hdr.seq_num - expected);
                    g_lostPackets += lost;
                }
            }
            g_firstPacket = false;
            g_lastSeq = hdr.seq_num;
        }

        // ── Accumulate & print live stats ─────────────────────
        {
            std::lock_guard<std::mutex> lk(g_statMtx);
            g_totalDelayMs += netDelayMs;
            ++g_packetCount;
            if (netDelayMs > g_maxDelayMs) g_maxDelayMs = netDelayMs;

            if (g_packetCount % 100 == 0) { // every ~0.5s
                double avgNet    = g_totalDelayMs / g_packetCount;
                double avgJitter = g_packetCount > 1
                                   ? g_totalJitterMs / (g_packetCount - 1) : 0.0;
                std::cout << "[Receiver]"
                          << "  Pkts="   << g_packetCount
                          << "  Net="    << avgNet    << "ms"
                          << "  Jitter=" << avgJitter << "ms"
                          << "  Lost="   << g_lostPackets
                          << "  Seq="    << hdr.seq_num
                          << "\n";
            }
        }

        // ── Push into jitter buffer ───────────────────────────
        {
            std::lock_guard<std::mutex> lk(g_bufMtx);
            if (g_pcmBuf.size() < (size_t)PACKET_SAMPLE_RATE)
                for (auto s : samples) g_pcmBuf.push_back(s);
        }

        // ── Record to WAV ─────────────────────────────────────
        {
            std::lock_guard<std::mutex> lk(g_wavMtx);
            g_wavData.insert(g_wavData.end(), samples.begin(), samples.end());
        }
    }
}

// ── Main ──────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : 9002;

    initSessionClock(); // no-op, kept for compatibility

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[Receiver] socket() failed\n"; return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[Receiver] bind() failed on port " << port << "\n"; return 1;
    }

    DWORD timeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    Pa_Initialize();
    PaStreamParameters outParams{};
    outParams.device = Pa_GetDefaultOutputDevice();
    if (outParams.device == paNoDevice) {
        std::cerr << "[Receiver] No output device\n"; return 1;
    }
    outParams.channelCount     = 1;
    outParams.sampleFormat     = paInt16;
    outParams.suggestedLatency =
        Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, nullptr, &outParams,
                                 PACKET_SAMPLE_RATE, PACKET_FRAME_SIZE,
                                 paClipOff, playCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "[Receiver] PortAudio: " << Pa_GetErrorText(err) << "\n"; return 1;
    }
    Pa_StartStream(stream);

    std::cout << "[Receiver] Listening on UDP port " << port << "\n";
    std::cout << "[Receiver] Frame: " << PACKET_FRAME_SIZE
              << " samples = " << (PACKET_FRAME_SIZE * 1000 / PACKET_SAMPLE_RATE)
              << " ms | Header: " << HEADER_SIZE << " bytes\n";
    std::cout << "[Receiver] Measuring pure network latency (sendto → recvfrom).\n";
    std::cout << "[Receiver] Playing audio. Press ENTER to stop.\n\n";

    std::thread recvThread(recvLoop, sock);
    std::cin.get();

    g_running = false;
    recvThread.join();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    {
        std::lock_guard<std::mutex> lk(g_wavMtx);
        if (!g_wavData.empty())
            writeWav("output.wav", g_wavData, PACKET_SAMPLE_RATE);
    }

    // ── Final summary ─────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(g_statMtx);
        if (g_packetCount > 0) {
            double avgNet    = g_totalDelayMs / g_packetCount;
            double avgJitter = g_packetCount > 1
                               ? g_totalJitterMs / (g_packetCount - 1) : 0.0;
            double lossRate  = 100.0 * g_lostPackets /
                               (g_packetCount + g_lostPackets);

            std::cout << "\n========================================\n"
                      << "  PURE NETWORK LATENCY SUMMARY\n"
                      << "  (sendto on sender -> recvfrom on receiver)\n"
                      << "  PA, mic, speaker latency excluded.\n"
                      << "========================================\n"
                      << "  Avg network delay : " << avgNet        << " ms\n"
                      << "  Max network delay : " << g_maxDelayMs  << " ms\n"
                      << "  Avg jitter        : " << avgJitter     << " ms\n"
                      << "  Max jitter        : " << g_maxJitterMs << " ms\n"
                      << "----------------------------------------\n"
                      << "  Packets received  : " << g_packetCount << "\n"
                      << "  Packets lost      : " << g_lostPackets
                      << " (" << lossRate << "%)\n"
                      << "========================================\n";
        }
    }

    closesocket(sock);
    WSACleanup();
    std::cout << "[Receiver] Done.\n";
    return 0;
}