#include "Connection.h"

#include <iostream>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start(){
	   std::unique_lock<std::mutex> lock(_mutex);
    _event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    alive = true;
}

// See Connection.h
void Connection::OnError(){
	std::unique_lock<std::mutex> lock(_mutex);
    alive = false;
    _logger->warn("error on {} scoket", _socket);
}

// See Connection.h
void Connection::OnClose(){
	std::unique_lock<std::mutex> lock(_mutex);
    alive = false;
    _logger->debug("closing socket {}", _socket);
}

// See Connection.h
void Connection::DoRead(){
	std::unique_lock<std::mutex> lock(_mutex);
    _logger->debug("Read on {} ",_socket);
    try {
        int readed_bytes = -1;
        while ((readed_bytes = read(_socket, client_buffer + total_offset, sizeof(client_buffer) - total_offset)) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);
            total_offset += readed_bytes;

            while (total_offset > 0) {
                _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, total_offset, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, total_offset - parsed);
                        total_offset -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(total_offset));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, total_offset - to_read);
                    arg_remains -= to_read;
                    total_offset -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");


                    std::string result;
                    if (argument_for_command.size()) {
                        argument_for_command.resize(argument_for_command.size() - 2);
                    }
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Send response
                    result += "\r\n";

                    if (response_queue.empty()) {
                        _event.events |=EPOLLOUT;  // ready to wait for sending a result
                    }
                    response_queue.push_back(result);

                    if (response_queue.size() >= queue_size) {
                        _event.events &= ~EPOLLIN;
                    }

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if (readed_bytes == 0) {
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch(std::runtime_error &error) {
        _logger -> error(error.what());

    }
}

// See Connection.h
void Connection::DoWrite(){
    std::unique_lock<std::mutex> lock(_mutex);
    _logger->debug("Write on {} ",_socket);
    int ret;
    auto it = response_queue.begin();
    do {
        std::string &qhead = *it;
        ret = write(_socket, &qhead[0] + head_offset, qhead.size() - head_offset);

        if (ret > 0) {
            head_offset += ret;
            if (head_offset >= it->size()) {
                it++;
                head_offset = 0;
            }
        }
    } while (ret > 0 && it != response_queue.end());

    response_queue.erase(response_queue.begin(), it);

    if (ret == -1) {
        alive = false;
    }
    if (response_queue.size() < queue_size) {
        _event.events |= EPOLLIN;
    }
    if (response_queue.empty()) {
        _event.events &= ~EPOLLOUT;
    }
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
