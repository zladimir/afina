#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <mutex>
#include <spdlog/logger.h>
#include <memory>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>
#include <afina/logging/Service.h>
#include "spdlog/logger.h"
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Storage> &pstorage, std::shared_ptr<spdlog::logger> &pl) : _socket(s), pStorage(pstorage) , _logger(pl) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    inline bool isAlive(){
        std::unique_lock<std::mutex> lock(_mutex);
        return alive;
    }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    char client_buffer[4096];
    bool alive;
    std::shared_ptr<Afina::Storage> pStorage;
    std::vector<std::string> response_queue;
    std::size_t queue_size = 100;
    std::size_t total_offset;
    std::size_t head_offset;
    std::mutex _mutex;
    std::shared_ptr<spdlog::logger> _logger;
    std::unique_ptr<Execute::Command> command_to_execute;
    Protocol::Parser parser;
    std::size_t arg_remains;
    std::string argument_for_command;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
