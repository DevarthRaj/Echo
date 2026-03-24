# Real-Time Audio Streaming — UDP
## Node A (Sender) ──UDP──> Node B (Receiver)

No external libraries beyond PortAudio. Just raw UDP sockets.

---

## Prerequisites — install once in MSYS2 UCRT64 shell

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-portaudio
```

---

## Project layout

```
project/
├── include/
│   └── packet.hpp
└── src/
    ├── sender.cpp
    └── receiver.cpp
```

---

## Build (from project root in MSYS2 UCRT64 shell)

```bash
# Receiver (Node B)
g++ src/receiver.cpp -o receiver -I include -lportaudio -lws2_32 -lwinmm -std=c++14

# Sender (Node A)
g++ src/sender.cpp -o sender -I include -lportaudio -lws2_32 -lwinmm -std=c++14
```

---

## Run (two terminals)

**Terminal 1 — start receiver FIRST:**
```bash
./receiver 9002
```

**Terminal 2 — start sender:**
```bash
./sender 127.0.0.1 9002
```

Speak into mic → hear audio live on receiver.
Press ENTER in receiver terminal to stop and save `output.wav`.

---

## Why UDP instead of WebSocket?

| | WebSocket | UDP |
|---|---|---|
| Latency | Higher (framing + handshake) | Lower (fire and forget) |
| Lost packets | Retransmitted (bad for audio) | Dropped (fine for audio) |
| External libs | websocketpp needed | None — just OS sockets |
| Complexity | High | Low |

---

## Packet format (packet.hpp)

```
[4B seq_num][8B timestamp_us][2B num_samples][2B sample_rate][N×2B PCM int16]
```

## Expected delay

| Component | Estimate |
|---|---|
| Frame duration | 10 ms (160 samples @ 16 kHz) |
| PortAudio input latency | ~5–10 ms |
| UDP loopback | < 1 ms |
| PortAudio output latency | ~5–10 ms |
| **Total** | **~20–30 ms** |