/* BASELINE SENDER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload)
 *   send 47001  -> relay uplink toward the receiver (YOUR wire format)
 *   bind 47004  <- feedback from your receiver, via the relay (optional)
 *
 * This baseline forwards each frame once, unchanged, and ignores feedback.
 * No redundancy, no retransmission. It cannot pass. That is the point.
 *
 * Env vars available if you want them: T0 (epoch seconds, float),
 * DURATION_S, DELAY_MS. The harness kills this process when the run ends,
 * so a forever-loop is fine.
 *
 * build: make        run: python3 run.py --delay_ms 60
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
//     in_addr.sin_port = htons(47010);
//     in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
//     if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
//         perror("bind 47010");
//         return 1;
//     }

//     int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
//     struct sockaddr_in relay = {0};
//     relay.sin_family = AF_INET;
//     relay.sin_port = htons(47001);
//     relay.sin_addr.s_addr = inet_addr("127.0.0.1");

//     unsigned char buf[2048];
//     for (;;) {
//         ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
//         if (n <= 0) continue;
//         /* your protocol design goes here; baseline = send once, as-is */
//         sendto(out_fd, buf, (size_t)n, 0, (struct sockaddr *)&relay,
//                sizeof relay);
//     }
//     return 0;
// }


/* 
 * C++ SENDER - Piggyback FEC Strategy
 *
 * Architecture:
 * - Reads 164-byte frames from the harness.
 * - Stores the previous frame in memory.
 * - Concatenates the current frame and the previous frame into a 328-byte packet.
 * - Sends to the relay. This uses exactly 2.0x bandwidth but provides instant recovery 
 *   for any single-packet drop.
 */

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <cstring>

int main(void) {
    // 1. Setup inbound socket (47010 <- harness source)
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("bind 47010");
        return 1;
    }

    // 2. Setup outbound socket (47001 -> relay uplink)
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 3. State to hold the previous frame for redundancy
    std::vector<uint8_t> prev_frame;
    bool has_prev = false;
    unsigned char buf[2048];

    // 4. Main Loop
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 164) continue; // Harness always sends 164 bytes (4 seq + 160 payload)

        // Create a dynamic buffer for our outgoing compound packet
        std::vector<uint8_t> compound_packet;

        // A. Insert the CURRENT frame first (164 bytes)
        compound_packet.insert(compound_packet.end(), buf, buf + n);

        // B. Insert the PREVIOUS frame second (164 bytes), if we have one
        if (has_prev) {
            compound_packet.insert(compound_packet.end(), prev_frame.begin(), prev_frame.end());
        }

        // C. Send the compound packet to the relay
        // Size will be 164 bytes for seq 0, and 328 bytes for all subsequent seqs.
        sendto(out_fd, compound_packet.data(), compound_packet.size(), 0, 
               (struct sockaddr *)&relay, sizeof(relay));

        // D. Save the current frame to be the redundant data for the next tick
        prev_frame.assign(buf, buf + n);
        has_prev = true;
    }

    return 0;
}
