#include "rdt_master.hpp"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdexcept>
#include <algorithm>

namespace rdt {

// quitar static para que todos los nodos simulen
// poner static para que solo el primer nodo simule 
static int simulation_drops = 0, simulation_does = 0;
static int simulation_corruptions = 0, corruption_does = 0;

static bool drop_packet() {
    if (simulation_drops >= 5) return false;
    if (++simulation_does % 5 == 0) {
        ++simulation_drops;
        return true;
    }
    return false;
}

static bool corrupt_packet(RdtPacket& pkt) {
    if (simulation_corruptions >= 3) return false;
    if (++corruption_does % 7 == 0) {
        ++simulation_corruptions;
        pkt.payload[0] = 'k';
        return true;
    }
    return false;
}

MasterRdt::MasterRdt() : socket_fd(-1), keep_running(false) {}

MasterRdt::~MasterRdt() {
    keep_running = false;
    if (rx_thread.joinable()) {
        rx_thread.join();
    }
    if (socket_fd != -1) {
        close(socket_fd);
    }
    for (auto slave : slaves) {
        delete slave;
    }
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

    keep_running = true;
    rx_thread = std::thread(&MasterRdt::background_receiver, this);
    std::cout << "SISTEMA: Hilo receptor asincrono y multiplexador activados" << std::endl;
}

void MasterRdt::add_slave(const std::string& slave_ip, int slave_port) {
    SlaveNode* slave = new SlaveNode();
    slave->id = slaves.size();
    std::memset(&slave->addr, 0, sizeof(slave->addr));
    slave->addr.sin_family = AF_INET;
    slave->addr.sin_port = htons(slave_port);
    inet_pton(AF_INET, slave_ip.c_str(), &slave->addr.sin_addr);
    slave->expected_seq = 0;
    slaves.push_back(slave);
    std::cout << "SISTEMA: Registro dinamico completado para Nodo " << slave->id << " en puerto " << slave_port << std::endl;
}

void MasterRdt::background_receiver() {
    while (keep_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;

        int activity = select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (activity <= 0 || !FD_ISSET(socket_fd, &read_fds)) continue;

        RdtPacket pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t rec_bytes = recvfrom(socket_fd, &pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&from_addr, &from_len);
        if (rec_bytes != sizeof(RdtPacket)) continue;

        uint16_t received_checksum = pkt.checksum;
        RdtPacket temp_pkt = pkt;
        temp_pkt.checksum = 0; 
        if (compute_internet_checksum(temp_pkt) != received_checksum) {
            std::cout << "ALERTA: Integridad violada, descarte por error de checksum Internet" << std::endl;
            continue; 
        }

        SlaveNode* target_slave = nullptr;
        for (auto slave : slaves) {
            if (slave->addr.sin_port == from_addr.sin_port) {
                target_slave = slave;
                break;
            }
        }

        if (target_slave) {
            std::lock_guard<std::mutex> lock(target_slave->queue_mutex);
            target_slave->packet_queue.push(pkt);
        }
    }
}

bool MasterRdt::transmit_gbn_pipeline(SlaveNode* slave, const std::vector<RdtPacket>& pipeline_packets) {
    const size_t window_size = 8;
    size_t base = 0;
    size_t next_seq_num = 0;
    int consecutive_timeouts = 0;

    std::cout << "SISTEMA: Iniciando transmision GBN hacia Nodo " << slave->id  << " | Total paquetes: " << pipeline_packets.size() << std::endl;

    while (base < pipeline_packets.size()) {
        while (next_seq_num < base + window_size && next_seq_num < pipeline_packets.size()) {
            RdtPacket send_pkt = pipeline_packets[next_seq_num];

            // Simular perdida de paquete
            if (drop_packet()) {
                std::cout << "SIMULACION: SEQ " << send_pkt.seq_num << " descartado (pérdida #" << simulation_drops << ")" << std::endl;
            } else {
                if (corrupt_packet(send_pkt)) {
                    std::cout << "SIMULACION: SEQ " << send_pkt.seq_num << " enviado con corrupcion intencional (#" << simulation_corruptions << ")" << std::endl;
                }
                sendto(socket_fd, &send_pkt, sizeof(RdtPacket), 0,(struct sockaddr*)&slave->addr, sizeof(slave->addr));
                std::cout << "  TX: Enviando SEQ " << send_pkt.seq_num 
                      << " | Flag " << (int)send_pkt.flags 
                      << " | Ventana base: " << base << std::endl;
            }
            
            next_seq_num++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        bool ack_processed = false;

        std::lock_guard<std::mutex> lock(slave->queue_mutex);
        while (!slave->packet_queue.empty()) {
            RdtPacket rx_pkt = slave->packet_queue.front();
            slave->packet_queue.pop();

            if (rx_pkt.flags == 4) {
                if (rx_pkt.seq_num >= base) {
                    std::cout << "  RX: Confirmado ACK SEQ " << rx_pkt.seq_num << " | Deslizando base a " << (rx_pkt.seq_num + 1) << std::endl;
                    base = rx_pkt.seq_num + 1;
                    consecutive_timeouts = 0;
                    ack_processed = true;
                }
            }
        }

        if (!ack_processed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            consecutive_timeouts++;
            if (consecutive_timeouts >= 15) {
                std::cout << "CRITICO: Limite de retransmisiones alcanzado. Abortando transmision." << std::endl;
                return false;
            }
            std::cout << "  TIMEOUT: Expiracion en secuencia base " << base << " | Forzando retroceso Go-Back-N" << std::endl;
            next_seq_num = base; 
        }
    }
    std::cout << "SISTEMA: Pipeline de datos completado correctamente para Nodo " << slave->id << std::endl;
    return true;
}

void MasterRdt::send_data_to_slave(int slave_idx, const std::string& data) {
    if (slave_idx < 0 || slave_idx >= (int)slaves.size()) return;
    SlaveNode* slave = slaves[slave_idx];

    size_t total_bytes = data.size();
    size_t offset = 0;
    uint32_t current_seq = 0;
    std::vector<RdtPacket> pipeline_packets;

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
        pkt.checksum = 0;
        pkt.checksum = compute_internet_checksum(pkt);

        pipeline_packets.push_back(pkt);
        offset += chunk;
        current_seq++;
        if (total_bytes == 0) break;
    }

    if (!transmit_gbn_pipeline(slave, pipeline_packets)) {
        throw std::runtime_error("Desconexión forzada: Límite de aborto alcanzado con Esclavo.");
    }
}

std::string MasterRdt::receive_data_from_slave(int slave_idx) {
    if (slave_idx < 0 || slave_idx >= (int)slaves.size()) return "";
    SlaveNode* slave = slaves[slave_idx];
    slave->expected_seq = 0; 

    std::string full_data = "";
    std::cout << "SISTEMA: Esperando flujo metrico desde Nodo " << slave->id << "..." << std::endl;

    while (true) {
        RdtPacket pkt;
        bool packet_ready = false;

        {
            std::lock_guard<std::mutex> lock(slave->queue_mutex);
            if (!slave->packet_queue.empty()) {
                pkt = slave->packet_queue.front();
                slave->packet_queue.pop();
                packet_ready = true;
            }
        }

        if (!packet_ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (pkt.seq_num == slave->expected_seq) {
            std::cout << "  RX: Recibido SEQ " << pkt.seq_num 
                      << " | Tamaño: " << pkt.data_len << " bytes" << std::endl;
                      
            if (pkt.data_len > 0) {
                full_data.append(pkt.payload, pkt.data_len);
            }

            RdtPacket ack_pkt;
            std::memset(&ack_pkt, 0, sizeof(RdtPacket));
            ack_pkt.flags = 4;
            ack_pkt.seq_num = slave->expected_seq;
            ack_pkt.checksum = 0;
            ack_pkt.checksum = compute_internet_checksum(ack_pkt);
            sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&slave->addr, sizeof(slave->addr));

            slave->expected_seq++;
            if (pkt.flags == 3) {
                std::cout << "SISTEMA: Fin de transmision detectado | Flujo unificado" << std::endl;
                break; 
            }
        } else {
            if (slave->expected_seq > 0) {
                std::cout << "  DESFASE: Recibido SEQ " << pkt.seq_num << " | Esperado: " << slave->expected_seq << " | Reenviando ACK previo: " << (slave->expected_seq - 1) << std::endl;
                          
                RdtPacket ack_pkt;
                std::memset(&ack_pkt, 0, sizeof(RdtPacket));
                ack_pkt.flags = 4;
                ack_pkt.seq_num = slave->expected_seq - 1;
                ack_pkt.checksum = 0;
                ack_pkt.checksum = compute_internet_checksum(ack_pkt);
                sendto(socket_fd, &ack_pkt, sizeof(RdtPacket), 0, (struct sockaddr*)&slave->addr, sizeof(slave->addr));
            }
        }
    }
    return full_data;
}

} // namespace rdt