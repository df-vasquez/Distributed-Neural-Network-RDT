#pragma once
#include <cstdint>
#include <cstring>

namespace rdt {

struct __attribute__((packed)) RdtPacket {
    uint8_t flags;       // 1: START, 2: DATA, 3: END, 4: ACK
    uint32_t seq_num;
    uint32_t data_len;
    char payload[512];
    uint16_t checksum;   // 2 Bytes exactos - RFC 1071
};

// Algoritmo Internet Checksum Estándar (RFC 1071)
inline uint16_t compute_internet_checksum(const RdtPacket& pkt) {
    uint32_t sum = 0;
    const uint16_t* ptr = reinterpret_cast<const uint16_t*>(&pkt);
    // Sumar los primeros 521 bytes (260 palabras de 16 bits)
    for (int i = 0; i < 260; ++i) {
        sum += ntohs(ptr[i]);
    }
    // Añadir el último byte residual (flags/estructuras impares) tratado sin signo
    const uint8_t* residual_ptr = reinterpret_cast<const uint8_t*>(&pkt);
    sum += residual_ptr[520]; 

    // Plegar la suma de 32 bits a 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

} // namespace rdt