#pragma once
#include <string>
#include <netinet/in.h>
#include "rdt_common.hpp"

namespace rdt {

class SlaveRdt {
private:
    int socket_fd;
    struct sockaddr_in local_addr;
    struct sockaddr_in master_addr;
    bool master_known;

    bool wait_for_ack_timeout(uint32_t expected_seq);

public:
    SlaveRdt();
    ~SlaveRdt();

    void init_slave(const std::string& local_ip, int local_port);
    std::string receive_data_from_master();
    void send_data_to_master(const std::string& data);
};

} // namespace rdt