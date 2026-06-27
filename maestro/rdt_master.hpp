#pragma once
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <map>
#include <netinet/in.h>
#include "rdt_common.hpp"

namespace rdt {

struct SlaveNode {
    int id;
    struct sockaddr_in addr;
    std::queue<RdtPacket> packet_queue;
    std::mutex queue_mutex;
    uint32_t expected_seq;
};

class MasterRdt {
private:
    int socket_fd;
    std::vector<SlaveNode*> slaves;
    
    // Hilo de recepción asíncrono y control de ciclo de vida
    std::thread rx_thread;
    bool keep_running;
    
    void background_receiver();
    bool transmit_gbn_pipeline(SlaveNode* slave, const std::vector<RdtPacket>& pipeline_packets);

public:
    MasterRdt();
    ~MasterRdt();

    void init_master(const std::string& local_ip, int local_port);
    void add_slave(const std::string& slave_ip, int slave_port);
    void send_data_to_slave(int slave_idx, const std::string& data);
    std::string receive_data_from_slave(int slave_idx);
};

} // namespace rdt