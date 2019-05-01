#ifndef node_common_hpp
#define node_common_hpp

#include <vector>
#include <string>
#include <functional>
#include "protocol.hpp"
#include "external/asio.hpp"

namespace raft {
    namespace node {
        struct Message {
            static const std::size_t header_length = 4;
            using HeaderType = std::uint32_t;

            Message(std::vector<char>& data) : data(std::move(data)) {}
            Message(std::vector<char>&& data) : data(data) {}
            
            Message(std::string& s) : data(s.size()) {
                std::move(s.begin(), s.end(), data.begin());
            }
                        
            // move the message's data to an encoded vector and return.
            // that means after calling this function, data no longer maintains valid message data.
            std::vector<char> encode() {
                auto header = raft::protocol::NetworkUtil::htoa<HeaderType>(static_cast<HeaderType>(data.size()));
                std::vector<char> e (header.size() + data.size());
                std::move(header.begin(), header.end(), e.begin());
                std::move(data.begin(), data.end(), e.begin() + header.size());

                return e;
            }

            std::vector<char> data;
        };

        struct EndPoint {
            static size_t hasher(const EndPoint& ep) {
                return std::hash<std::string>()(ep.host) ^ std::hash<int>()(ep.port);
            }

            EndPoint(std::string host, int port) : host(host), port(port) {}
            std::string host;
            int port;

            std::string to_string() const {
                return host + ":" + std::to_string(port);
            }

            bool operator==(const EndPoint& other) const {
                return host == other.host && port == other.port;
            }
        };

        using asio::ip::tcp;
        using OnMessage = std::function<void(Message&, EndPoint&)>;

        struct Options {
            Options(int port, size_t in_buf_size, OnMessage on_message) : 
                port(port), in_buf_size(in_buf_size), on_message(on_message) {}

            // the port the server listens on
            int port;

            // buffer size (in bytes) for incoming messages.
            size_t in_buf_size;

            // invoked when a new message is deliverded. must be non-blocking.
            OnMessage on_message;
        };
    }
}

#endif