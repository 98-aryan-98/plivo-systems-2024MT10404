/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
// #include <arpa/inet.h>
// #include <stdio.h>
// #include <string.h>
// #include <sys/socket.h>
// #include <unistd.h>

// int main(void) {
//     int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in in_addr = {0};
//     in_addr.sin_family = AF_INET;
//     in_addr.sin_port = htons(47002);
//     in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
//     if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
//         perror("bind 47002");
//         return 1;
//     }

//     int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in player = {0};
//     player.sin_family = AF_INET;
//     player.sin_port = htons(47020);
//     player.sin_addr.s_addr = inet_addr("127.0.0.1");

//     unsigned char buf[2048];
//     for (;;) {
//         ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
//         if (n <= 0) continue;
//         /* jitter buffer / reorder / recovery logic goes here */
//         sendto(out_fd, buf, (size_t)n, 0, (struct sockaddr *)&player,
//                sizeof player);
//     }
//     return 0;
// }

/*
 * C++ RECEIVER
 *
 * Upgraded with a decoupled Jitter Buffer.
 * - Main thread: Receives packets from the network, strips headers, and stores them in a map.
 * - Playout thread: Wakes up exactly on the 20ms clock to dispatch the correct sequence.
 */

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <cstring>

using namespace std::chrono;

// The Jitter Buffer: Sequence Number -> 160-byte Payload
std::map<uint32_t, std::vector<uint8_t>> jitter_buffer;
std::mutex jb_mutex;

void playout_loop(int out_fd, struct sockaddr_in player_addr, double t0, int delay_ms)
{
    uint32_t next_seq = 0;

    while (true)
    {
        // Calculate the absolute deadline for the current sequence: t0 + delay + (seq * 20ms)
        double target_time_s = t0 + (delay_ms / 1000.0) + (next_seq * 0.020);

        // Target 1ms before the deadline to ensure it arrives "BEFORE" the harness checks it
        auto target_dur = duration<double>(target_time_s - 0.001);
        auto target_tp = system_clock::time_point(duration_cast<system_clock::duration>(target_dur));

        auto now = system_clock::now();
        if (now < target_tp)
        {
            std::this_thread::sleep_until(target_tp);
        }

        std::vector<uint8_t> payload;

        // Safely extract the packet from the jitter buffer
        {
            std::lock_guard<std::mutex> lock(jb_mutex);
            auto it = jitter_buffer.find(next_seq);
            if (it != jitter_buffer.end())
            {
                payload = it->second;
                // Erase this packet and any stale packets before it
                jitter_buffer.erase(jitter_buffer.begin(), std::next(it));
            }
        }

        // If we have the payload, format and send it to the harness player
        if (!payload.empty())
        {
            std::vector<uint8_t> packet(164);
            uint32_t net_seq = htonl(next_seq);
            std::memcpy(packet.data(), &net_seq, 4);
            std::memcpy(packet.data() + 4, payload.data(), 160);

            sendto(out_fd, packet.data(), packet.size(), 0,
                   (struct sockaddr *)&player_addr, sizeof(player_addr));
        }

        // Advance the 20ms clock
        next_seq++;
    }
}

int main(void)
{
    // 1. Read environment variables provided by the harness
    const char *t0_str = std::getenv("T0");
    const char *delay_str = std::getenv("DELAY_MS");

    if (!t0_str || !delay_str)
    {
        std::cerr << "Missing required environment variables (T0, DELAY_MS)." << std::endl;
        return 1;
    }

    double t0 = std::stod(t0_str);
    int delay_ms = std::stoi(delay_str);

    // 2. Setup inbound socket (47002 <- relay)
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0)
    {
        perror("bind 47002");
        return 1;
    }

    // 3. Setup outbound socket (47020 -> harness player)
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 4. Launch the precise playout thread
    std::thread player_thread(playout_loop, out_fd, player, t0, delay_ms);

    // 5. Main loop: Receive, parse, and buffer
    unsigned char buf[2048];
    for (;;)
    {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 164)
            continue; // Ignore malformed or truncated packets

        std::lock_guard<std::mutex> lock(jb_mutex);

        // 1. Always extract and buffer the primary frame (bytes 0 to 163)
        uint32_t seq1;
        std::memcpy(&seq1, buf, 4);
        seq1 = ntohl(seq1);
        std::vector<uint8_t> payload1(buf + 4, buf + 164);
        jitter_buffer[seq1] = payload1;

        // 2. If the packet contains the redundant frame, extract and buffer it too (bytes 164 to 327)
        if (n >= 328)
        {
            uint32_t seq2;
            std::memcpy(&seq2, buf + 164, 4);
            seq2 = ntohl(seq2);
            std::vector<uint8_t> payload2(buf + 168, buf + 328);
            jitter_buffer[seq2] = payload2;
        }
    }

    player_thread.join();
    return 0;
}