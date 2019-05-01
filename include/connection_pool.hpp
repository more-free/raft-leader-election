#ifndef connection_pool_hpp
#define connection_pool_hpp

#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <functional>
#include "scheduler.hpp"
#include "node_common.hpp"
#include "utils.hpp"
#include "external/asio.hpp"

namespace raft {
    namespace conn_pool {
        using asio::ip::tcp;
        using raft::node::Message;
        using raft::node::EndPoint;
        using raft::scheduler::Scheduler;

        // TODO not used yet.
        struct TerminationCondition {
            TerminationCondition(int connection_timeout = 3000) : connection_timeout(connection_timeout) {}

            // duration before considering a connection as expired. milliseconds.
            int connection_timeout;
        };

        class Connection {
        public:
            Connection(tcp::socket soc, TerminationCondition term_cond) : term_cond_(term_cond), soc_(std::move(soc)) {}
            ~Connection() { soc_.close(); }

            Connection(const Connection&) = delete;
            Connection& operator=(const Connection&) = delete;

            void write(std::shared_ptr<Message>);

            bool closed() { return closed_.load(); }
        private:
            TerminationCondition term_cond_;
            tcp::socket soc_;
            std::atomic<bool> closed_ { false };
        };

        // a simple wrapper on asio sockets for maintaining persistent tcp connections.
        // a connection is created upon request, and cached in the pool,
        // until it is closed by peer (thus causing some error doing client-side I/O).
        class ConnectionPool {
        public:
            ConnectionPool(TerminationCondition term_cond) : term_cond_(term_cond) {
                std::function<size_t(const EndPoint&)> hasher = EndPoint::hasher;
                std::unordered_map<EndPoint, std::shared_ptr<Connection>, decltype(hasher)> map (16, hasher);
                conns_ = map;
            }

            ~ConnectionPool() {}

            // request a connection from the pool.
            // create the new one on-demand, or re-use existing one if avaiable.
            std::shared_ptr<Connection> get(const EndPoint&);

        private:
            std::shared_ptr<Connection> create(const EndPoint&);

            TerminationCondition term_cond_;
            asio::io_context io_context_;
            std::mutex mtx_;
            std::unordered_map<EndPoint, std::shared_ptr<Connection>, std::function<size_t(const EndPoint&)>> conns_;
        };

        inline std::shared_ptr<Connection> ConnectionPool::get(const EndPoint& endpoint) {
            std::unique_lock<std::mutex> lock(mtx_);
            auto iter = conns_.find(endpoint);
            if (iter == conns_.end() || iter->second->closed()) {
                auto conn = create(endpoint);
                if (conn) {
                    // also release resource (via Connection's destructor).
                    conns_[endpoint] = conn;
                } else {
                    return nullptr;
                }
            }
            return conns_[endpoint];
        }

        inline std::shared_ptr<Connection> ConnectionPool::create(const EndPoint& endpoint) {
            tcp::resolver resolver(io_context_);
            auto results = resolver.resolve(endpoint.host, std::to_string(endpoint.port));
            std::error_code ec;
            tcp::socket soc(io_context_);
            asio::connect(soc, results, ec);

            if (ec) {
                std::cerr << "cannot connect to server : " << ec.message() << std::endl;
                return nullptr;
            } else {
                return std::make_shared<Connection>(std::move(soc), term_cond_);
            }
        }

        inline void Connection::write(std::shared_ptr<Message> msg) {
            std::error_code ec;
            asio::write(soc_, asio::buffer(msg->encode()), ec);
            if (ec) {
                std::cerr << "failed to send message, closing connection" << std::endl;
                closed_.store(true);
            }
        }
    }
}

#endif