#include "rdt_master.hpp"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdexcept>

namespace rdt {

MasterRdt::MasterRdt() : socket_fd(-1) {}

MasterRdt::~MasterRdt() {
    if (socket_fd != -1) close(socket_fd);
}

uint8_t MasterRdt::compute_checksum(const RdtPacket& pkt) {
    uint32_t sum = 0;
    sum += pkt.flags;
    sum += pkt.seq_num;
    sum += pkt.data_len;
    for (int i = 0; i < 512; ++i) {
        sum += static_cast<uint8_t>(pkt.payload[i]);
    }
    return static_cast<uint8_t>(sum % 256);
}

void MasterRdt::init_master(const std::string& local_ip, int local_port) {
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) throw std::runtime_error("No se pudo crear el socket del Maestro");

    struct sockaddr_in local_addr;
    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(local_port);
    inet_pton(AF_INET, local_ip.c_str(), &local_addr.sin_addr);

    if (bind(socket_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        throw std::runtime_error("Fallo el bind del puerto en el Maestro");
    }
}

void MasterRdt::add_slave(const std::string& slave_ip, int slave_port) {
    struct sockaddr_in slave_addr;
    std::memset(&slave_addr, 0, sizeof(slave_addr));
    slave_addr.sin_family = AF_INET;
    slave_addr.sin_port = htons(slave_port);
    inet_pton(AF_INET, slave_ip.c_str(), &slave_addr.sin_addr);
    slave_addrs.push_back(slave_addr);
}

bool MasterRdt::wait_for_ack(uint32_t expected_seq, struct sockaddr_in& target_slave) {
    fd_set read_fds;
    struct timeval timeout;
    RdtPacket ack_pkt;

    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 200000; // 200ms Timeout Kurose-Ross RDT 3.0

    int select_status = select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_status > 0 && FD_ISSET(socket_fd, &read_fds)) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t rec_bytes = recvfrom(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&from_addr, &from_len);

        if (rec_bytes == sizeof(RdtPacket)) {
            if (from_addr.sin_port == target_slave.sin_port &&
                ack_pkt.flags == 4 && 
                ack_pkt.seq_num == expected_seq &&
                compute_checksum(ack_pkt) == ack_pkt.checksum) {
                return true; // ACK Válido recibido exitosamente
            }
        }
    }
    return false; // Timeout o paquete corrupto
}

void MasterRdt::send_data_to_slave(int slave_idx, const std::string& data) {
    if (slave_idx < 0 || slave_idx >= (int)slave_addrs.size()) return;
    struct sockaddr_in target = slave_addrs[slave_idx];

    size_t total_bytes = data.size();
    size_t offset = 0;
    uint32_t current_seq = 0;

    while (offset < total_bytes || (total_bytes == 0 && offset == 0)) {
        RdtPacket pkt;
        std::memset(&pkt, 0, sizeof(RdtPacket));

        if (offset == 0) pkt.flags = 1; // Inicio
        else if (offset + 512 >= total_bytes) pkt.flags = 3; // Fin
        else pkt.flags = 2; // Datos continuos

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
            sendto(socket_fd, &pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&target, sizeof(target));
            if (wait_for_ack(current_seq, target)) {
                ack_received = true;
            } else {
                retries++;
            }
        }

        if (!ack_received) throw std::runtime_error("Conexión perdida con el esclavo (Max Retries)");
        
        offset += chunk;
        current_seq++;
        if (total_bytes == 0) break;
    }
}

std::string MasterRdt::receive_data_from_slave(int slave_idx) {
    if (slave_idx < 0 || slave_idx >= (int)slave_addrs.size()) return "";
    struct sockaddr_in target = slave_addrs[slave_idx];

    std::string full_data = "";
    uint32_t expected_seq = 0;

    while (true) {
        RdtPacket pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t rec_bytes = recvfrom(socket_fd, &pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&from_addr, &from_len);
        if (rec_bytes != sizeof(RdtPacket) || from_addr.sin_port != target.sin_port) continue;

        // Validar checksum e integridad
        if (compute_checksum(pkt) == pkt.checksum && pkt.seq_num == expected_seq) {
            if (pkt.data_len > 0) {
                full_data.append(pkt.payload, pkt.data_len);
            }

            // Responder ACK instantáneo
            RdtPacket ack_pkt;
            std::memset(&ack_pkt, 0, sizeof(RdtPacket));
            ack_pkt.flags = 4;
            ack_pkt.seq_num = expected_seq;
            ack_pkt.checksum = compute_checksum(ack_pkt);
            sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&target, sizeof(target));

            expected_seq++;
            if (pkt.flags == 3) break; // Fin de la transferencia
        } else {
            // Re-enviar ACK previo si el paquete está duplicado o desordenado
            RdtPacket ack_pkt;
            std::memset(&ack_pkt, 0, sizeof(RdtPacket));
            ack_pkt.flags = 4;
            ack_pkt.seq_num = expected_seq - 1;
            ack_pkt.checksum = compute_checksum(ack_pkt);
            sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&target, sizeof(target));
        }
    }
    return full_data;
}

} // namespace rdt