#pragma once
#include <string>
#include <netinet/in.h>

namespace rdt {

struct __attribute__((packed)) RdtPacket {
    uint8_t flags;       // 1: Inicio, 2: Datos, 3: Fin, 4: ACK
    uint32_t seq_num;
    uint32_t data_len;
    char payload[512];
    uint8_t checksum;
};

class SlaveRdt {
private:
    int socket_fd;
    struct sockaddr_in local_addr;
    struct sockaddr_in master_addr;
    bool master_known;

    uint8_t compute_checksum(const RdtPacket& pkt);
    bool wait_for_ack(uint32_t expected_seq);

public:
    SlaveRdt();
    ~SlaveRdt();

    void init_slave(const std::string& local_ip, int local_port);
    std::string receive_data_from_master();
    void send_data_to_master(const std::string& data);
};

} // namespace rdt