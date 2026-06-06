#pragma once
#include <string>
#include <vector>
#include <netinet/in.h>

namespace rdt {

struct __attribute__((packed)) RdtPacket {
    uint8_t flags;       // 1: Inicio, 2: Datos, 3: Fin, 4: ACK
    uint32_t seq_num;
    uint32_t data_len;
    char payload[512];
    uint8_t checksum;
};

class MasterRdt {
private:
    int socket_fd;
    std::vector<struct sockaddr_in> slave_addrs;
    
    uint8_t compute_checksum(const RdtPacket& pkt);
    bool wait_for_ack(uint32_t expected_seq, struct sockaddr_in& target_slave);

public:
    MasterRdt();
    ~MasterRdt();

    void init_master(const std::string& local_ip, int local_port);
    void add_slave(const std::string& slave_ip, int slave_port);
    void send_data_to_slave(int slave_idx, const std::string& data);
    std::string receive_data_from_slave(int slave_idx);
};

} // namespace rdt