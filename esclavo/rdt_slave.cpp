#include "rdt_slave.hpp"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>

namespace rdt {

SlaveRdt::SlaveRdt() : socket_fd(-1), master_known(false) {
    std::memset(&master_addr, 0, sizeof(master_addr));
}

SlaveRdt::~SlaveRdt() {
    if (socket_fd != -1) close(socket_fd);
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

        if (!master_known) {
            master_addr = from_addr;
            master_known = true;
        }

        // Extracción y validación del Internet Checksum (RFC 1071)
        uint16_t received_checksum = pkt.checksum;
        RdtPacket temp_pkt = pkt;
        temp_pkt.checksum = 0;
        if (compute_internet_checksum(temp_pkt) != received_checksum) {
            continue; // Descarte silencioso ante corrupción
        }

        // Lógica del Receptor Secuencial
        if (pkt.seq_num == expected_seq) {
            if (pkt.data_len > 0) {
                full_data.append(pkt.payload, pkt.data_len);
            }

            RdtPacket ack_pkt;
            std::memset(&ack_pkt, 0, sizeof(RdtPacket));
            ack_pkt.flags = 4; // ACK
            ack_pkt.seq_num = expected_seq;
            ack_pkt.checksum = 0;
            ack_pkt.checksum = compute_internet_checksum(ack_pkt);
            sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&master_addr, sizeof(master_addr));

            expected_seq++;
            if (pkt.flags == 3) { // Captura de bandera END
                // Barrera de Sincronización del Receptor: Tiempo de guarda pasivo de 500 ms
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                break; 
            }
        } else {
            // Regla de Excepción por Underflow
            if (expected_seq > 0) {
                RdtPacket ack_pkt;
                std::memset(&ack_pkt, 0, sizeof(RdtPacket));
                ack_pkt.flags = 4;
                ack_pkt.seq_num = expected_seq - 1;
                ack_pkt.checksum = 0;
                ack_pkt.checksum = compute_internet_checksum(ack_pkt);
                sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&master_addr, sizeof(master_addr));
            }
        }
    }
    return full_data;
}

bool SlaveRdt::wait_for_ack_timeout(uint32_t expected_seq) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000; // 200 ms configurados mediante estructura timeval

    int select_status = select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_status > 0 && FD_ISSET(socket_fd, &read_fds)) {
        RdtPacket ack_pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t rec_bytes = recvfrom(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&from_addr, &from_len);

        if (rec_bytes == sizeof(RdtPacket) && ack_pkt.flags == 4) {
            uint16_t received_checksum = ack_pkt.checksum;
            RdtPacket temp_pkt = ack_pkt;
            temp_pkt.checksum = 0;
            if (compute_internet_checksum(temp_pkt) == received_checksum) {
                if (ack_pkt.seq_num >= expected_seq) {
                    return true; // Retorna confirmación válida
                }
            }
        }
    }
    return false;
}

void SlaveRdt::send_data_to_master(const std::string& data) {
    if (!master_known) return;

    size_t total_bytes = data.size();
    size_t offset = 0;
    uint32_t current_seq = 0;
    std::vector<RdtPacket> pipeline_packets;

    // Fragmentación del Stream JSON
    while (offset < total_bytes || (total_bytes == 0 && offset == 0)) {
        RdtPacket pkt;
        std::memset(&pkt, 0, sizeof(RdtPacket));

        if (offset == 0) pkt.flags = 1; // START
        else if (offset + 512 >= total_bytes) pkt.flags = 3; // END
        else pkt.flags = 2; // DATA

        pkt.seq_num = current_seq;
        size_t chunk = std::min((size_t)512, total_bytes - offset);
        pkt.data_len = chunk;

        if (chunk > 0) {
            std::memcpy(pkt.payload, data.c_str() + offset, chunk);
        }
        pkt.checksum = 0;
        pkt.checksum = compute_internet_checksum(pkt);

        pipeline_packets.push_back(pkt);
        offset += chunk;
        current_seq++;
        if (total_bytes == 0) break;
    }

    // Pipeline Transmisor Go-Back-N (N = 8)
    const size_t window_size = 8;
    size_t base = 0;
    size_t next_seq_num = 0;
    int consecutive_timeouts = 0;

    while (base < pipeline_packets.size()) {
        while (next_seq_num < base + window_size && next_seq_num < pipeline_packets.size()) {
            RdtPacket send_pkt = pipeline_packets[next_seq_num];
            sendto(socket_fd, &send_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&master_addr, sizeof(master_addr));
            next_seq_num++;
        }

        if (wait_for_ack_timeout(base)) {
            base++; // Desplazamiento progresivo de la ventana
            consecutive_timeouts = 0;
        } else {
            consecutive_timeouts++;
            if (consecutive_timeouts >= 15) { // Umbral de aborto
                throw std::runtime_error("Umbral de aborto excedido: Conexión degradada con el Maestro.");
            }
            next_seq_num = base; // Forzar rebobinado GBN
        }
    }

    // Barrera de Sincronización del Emisor al finalizar el flujo métrico
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

} // namespace rdt