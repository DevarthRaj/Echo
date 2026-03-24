// sender.cpp – Node A (UDP, optimised packet)
// Build:
//   g++ src/sender.cpp -o sender -I include -lportaudio -lws2_32 -lwinmm -std=c++14
// Run:
//   ./sender 127.0.0.1 9002

/*#include <winsock2.h>
#include <ws2tcpip.h>
#include <portaudio.h>
#include "packet.hpp"

#include <iostream>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>
#include <cstring>

static SOCKET                            g_sock = INVALID_SOCKET;
static sockaddr_in                       g_dest{};
static std::atomic<bool>                 g_running{true};
static uint16_t                          g_seq = 0;   // now uint16_t

static std::mutex                        g_qMtx;
static std::queue<std::vector<uint8_t>>  g_sendQueue;

static int audioCallback(const void* inputBuffer, void*,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo*,
                          PaStreamCallbackFlags, void*)
{
    if (!inputBuffer) return paContinue;
    const int16_t* in = static_cast<const int16_t*>(inputBuffer);
    auto pkt = makePacket(g_seq++, nowMs(), in, (uint16_t)framesPerBuffer);
    std::lock_guard<std::mutex> lk(g_qMtx);
    g_sendQueue.push(std::move(pkt));
    return paContinue;
}

static void sendLoop() {
    while (g_running) {
        std::vector<uint8_t> pkt;
        {
            std::lock_guard<std::mutex> lk(g_qMtx);
            if (!g_sendQueue.empty()) {
                pkt = std::move(g_sendQueue.front());
                g_sendQueue.pop();
            }
        }
        if (!pkt.empty()) {
            sendto(g_sock,
                   reinterpret_cast<const char*>(pkt.data()), (int)pkt.size(),
                   0, (sockaddr*)&g_dest, sizeof(g_dest));
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}

int main(int argc, char* argv[]) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : 9002;

    initSessionClock();  // start relative timestamp clock

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        std::cerr << "[Sender] socket() failed\n"; return 1;
    }

    memset(&g_dest, 0, sizeof(g_dest));
    g_dest.sin_family = AF_INET;
    g_dest.sin_port   = htons((u_short)port);
    inet_pton(AF_INET, host, &g_dest.sin_addr);

    std::cout << "[Sender] Sending UDP to " << host << ":" << port << "\n";
    std::cout << "[Sender] Frame: " << PACKET_FRAME_SIZE
              << " samples = " << (PACKET_FRAME_SIZE * 1000 / PACKET_SAMPLE_RATE)
              << " ms | Packet size: " << (HEADER_SIZE + PACKET_FRAME_SIZE * 2) << " bytes\n";

    Pa_Initialize();

    PaStreamParameters inputParams{};
    inputParams.device = Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        std::cerr << "[Sender] No mic found\n"; return 1;
    }
    inputParams.channelCount     = 1;
    inputParams.sampleFormat     = paInt16;
    inputParams.suggestedLatency =
        Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &inputParams, nullptr,
                                 PACKET_SAMPLE_RATE, PACKET_FRAME_SIZE,
                                 paClipOff, audioCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "[Sender] PortAudio: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    Pa_StartStream(stream);
    std::cout << "[Sender] Mic streaming. Press ENTER to stop.\n";

    std::thread sender(sendLoop);
    std::cin.get();

    g_running = false;
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    sender.join();
    closesocket(g_sock);
    WSACleanup();
    std::cout << "[Sender] Stopped.\n";
    return 0;
}
    */
// sender.cpp – Node A (UDP, pure network latency measurement)
// Build:
//   g++ src/sender.cpp -o sender -I include -lportaudio -lws2_32 -lwinmm -std=c++14
// Run:
//   ./sender 127.0.0.1 9002

#include <winsock2.h>
#include <ws2tcpip.h>
#include <portaudio.h>
#include "packet.hpp"

#include <iostream>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>
#include <cstring>

static SOCKET                            g_sock = INVALID_SOCKET;
static sockaddr_in                       g_dest{};
static std::atomic<bool>                 g_running{true};
static uint16_t                          g_seq = 0;

static std::mutex                        g_qMtx;
static std::queue<std::vector<uint8_t>>  g_sendQueue;

// Audio callback — just capture audio and enqueue.
// Timestamp is NOT set here — it's set in sendLoop() right before sendto()
// so it reflects pure wire departure time, not mic capture time.
static int audioCallback(const void* inputBuffer, void*,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo*,
                          PaStreamCallbackFlags, void*)
{
    if (!inputBuffer) return paContinue;
    const int16_t* in = static_cast<const int16_t*>(inputBuffer);
    // Pass 0 as timestamp — sendLoop will overwrite it before sending
    auto pkt = makePacket(g_seq++, 0, in, (uint16_t)framesPerBuffer);
    std::lock_guard<std::mutex> lk(g_qMtx);
    g_sendQueue.push(std::move(pkt));
    return paContinue;
}

static void sendLoop() {
    while (g_running) {
        std::vector<uint8_t> pkt;
        {
            std::lock_guard<std::mutex> lk(g_qMtx);
            if (!g_sendQueue.empty()) {
                pkt = std::move(g_sendQueue.front());
                g_sendQueue.pop();
            }
        }
        if (!pkt.empty()) {
            // Stamp the packet HERE — as late as possible before hitting the wire.
            // This excludes mic capture latency, queue wait, and everything PA-related.
            // What remains is purely: UDP send → UDP recv time on the receiver.
            stampPacket(pkt, nowMs());

            sendto(g_sock,
                   reinterpret_cast<const char*>(pkt.data()), (int)pkt.size(),
                   0, (sockaddr*)&g_dest, sizeof(g_dest));
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}

int main(int argc, char* argv[]) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : 9002;

    initSessionClock(); // no-op, kept for compatibility

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        std::cerr << "[Sender] socket() failed\n"; return 1;
    }

    memset(&g_dest, 0, sizeof(g_dest));
    g_dest.sin_family = AF_INET;
    g_dest.sin_port   = htons((u_short)port);
    inet_pton(AF_INET, host, &g_dest.sin_addr);

    std::cout << "[Sender] Sending UDP to " << host << ":" << port << "\n";
    std::cout << "[Sender] Frame: " << PACKET_FRAME_SIZE
              << " samples = " << (PACKET_FRAME_SIZE * 1000 / PACKET_SAMPLE_RATE)
              << " ms | Packet size: " << (HEADER_SIZE + PACKET_FRAME_SIZE * 2) << " bytes\n";
    std::cout << "[Sender] Timestamp stamped at sendto() — measuring pure network latency.\n";

    Pa_Initialize();

    PaStreamParameters inputParams{};
    inputParams.device = Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        std::cerr << "[Sender] No mic found\n"; return 1;
    }
    inputParams.channelCount     = 1;
    inputParams.sampleFormat     = paInt16;
    inputParams.suggestedLatency =
        Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &inputParams, nullptr,
                                 PACKET_SAMPLE_RATE, PACKET_FRAME_SIZE,
                                 paClipOff, audioCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "[Sender] PortAudio: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    Pa_StartStream(stream);
    std::cout << "[Sender] Mic streaming. Press ENTER to stop.\n";

    std::thread sender(sendLoop);
    std::cin.get();

    g_running = false;
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    sender.join();
    closesocket(g_sock);
    WSACleanup();
    std::cout << "[Sender] Stopped.\n";
    return 0;
}