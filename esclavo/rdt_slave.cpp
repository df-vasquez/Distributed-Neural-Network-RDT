#include "rdt_slave.hpp"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdexcept>

namespace rdt {

SlaveRdt::SlaveRdt() : socket_fd(-1), master_known(false) {
    std::memset(&master_addr, 0, sizeof(master_addr));
}

SlaveRdt::~SlaveRdt() {
    if (socket_fd != -1) close(socket_fd);
}

uint8_t SlaveRdt::compute_checksum(const RdtPacket& pkt) {
    uint32_t sum = 0;
    sum += pkt.flags;
    sum += pkt.seq_num;
    sum += pkt.data_len;
    for (int i = 0; i < 512; ++i) {
        sum += static_cast<uint8_t>(pkt.payload[i]);
    }
    return static_cast<uint8_t>(sum % 256);
}

void SlaveRdt::init_slave(const std::string& local_ip, int local_port) {
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) throw std::runtime_error("No se pudo crear el socket del Esclavo");

    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(local_port);
    inet_pton(AF_INET, local_ip.c_str(), &local_addr.sin_addr);

    if (bind(socket_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        throw std::runtime_error("Fallo el bind del puerto en el Esclavo");
    }
}

std::string SlaveRdt::receive_data_from_master() {
    std::string full_data = "";
    uint32_t expected_seq = 0;

    while (true) {
        RdtPacket pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t rec_bytes = recvfrom(socket_fd, &pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&from_addr, &from_len);
        if (rec_bytes != sizeof(RdtPacket)) continue;

        // Registrar la dirección del maestro la primera vez que nos hable
        if (!master_known) {
            master_addr = from_addr;
            master_known = true;
        }

        // Validar integridad clásica RDT 3.0 (Checksum y Secuencia correcta)
        if (compute_checksum(pkt) == pkt.checksum && pkt.seq_num == expected_seq) {
            if (pkt.data_len > 0) {
                full_data.append(pkt.payload, pkt.data_len);
            }

            // Enviar ACK de confirmación inmediata
            RdtPacket ack_pkt;
            std::memset(&ack_pkt, 0, sizeof(RdtPacket));
            ack_pkt.flags = 4; // ACK
            ack_pkt.seq_num = expected_seq;
            ack_pkt.checksum = compute_checksum(ack_pkt);
            sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&master_addr, sizeof(master_addr));

            expected_seq++;
            if (pkt.flags == 3) break; // Fin de la transferencia del segmento
        } else {
            // Re-enviar ACK del último paquete bien recibido si hay duplicación o desfase
            RdtPacket ack_pkt;
            std::memset(&ack_pkt, 0, sizeof(RdtPacket));
            ack_pkt.flags = 4;
            ack_pkt.seq_num = expected_seq - 1;
            ack_pkt.checksum = compute_checksum(ack_pkt);
            sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&master_addr, sizeof(master_addr));
        }
    }
    return full_data;
}

bool SlaveRdt::wait_for_ack(uint32_t expected_seq) {
    fd_set read_fds;
    struct timeval timeout;
    RdtPacket ack_pkt;

    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 200000; // 200ms Timeout

    int select_status = select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_status > 0 && FD_ISSET(socket_fd, &read_fds)) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t rec_bytes = recvfrom(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&from_addr, &from_len);

        if (rec_bytes == sizeof(RdtPacket)) {
            if (ack_pkt.flags == 4 && 
                ack_pkt.seq_num == expected_seq &&
                compute_checksum(ack_pkt) == ack_pkt.checksum) {
                return true;
            }
        }
    }
    return false;
}

void SlaveRdt::send_data_to_master(const std::string& data) {
    if (!master_known) return; // No se puede enviar datos si no conocemos al maestro

    size_t total_bytes = data.size();
    size_t offset = 0;
    uint32_t current_seq = 0;

    while (offset < total_bytes || (total_bytes == 0 && offset == 0)) {
        RdtPacket pkt;
        std::memset(&pkt, 0, sizeof(RdtPacket));

        if (offset == 0) pkt.flags = 1;
        else if (offset + 512 >= total_bytes) pkt.flags = 3;
        else pkt.flags = 2;

        pkt.seq_num = current_seq;
        size_t chunk = std::min((size_t)512, total_bytes - offset);
        pkt.data_len = chunk;

        if (chunk > 0) {
            std::memcpy(pkt.payload, data.c_str() + offset, chunk);
        }
        pkt.checksum = compute_checksum(pkt);

        bool ack_received = false;
        int retries = 0;
        while (!ack_received && retries < 15) {
            sendto(socket_fd, &pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&master_addr, sizeof(master_addr));
            if (wait_for_ack(current_seq)) {
                ack_received = true;
            } else {
                retries++;
            }
        }

        if (!ack_received) throw std::runtime_error("Conexión perdida con el maestro en el envío");

        offset += chunk;
        current_seq++;
        if (total_bytes == 0) break;
    }
}

} // namespace rdt