#ifndef node_hpp
#define node_hpp

#include <array>
#include <string>
#include <chrono>
#include <iostream>
#include <functional>
#include <memory>
#include <utility>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <future>
#include <thread>
#include "protocol.hpp"
#include "connection_pool.hpp"
#include "node_common.hpp"
#include "external/asio.hpp"

// A thin network layer on top of asio.
namespace raft {
    namespace node {
        class ServerConnection : public std::enable_shared_from_this<ServerConnection> {
        public:
            ServerConnection(tcp::socket, Options&);
            ~ServerConnection() {}
            ServerConnection(const ServerConnection&) = delete;
            ServerConnection& operator=(const ServerConnection&) = delete;

            void start() { read_header(); }

        private:
            void read_header();
            void read_body();

            tcp::socket socket_;
            Options op_;
            std::shared_ptr<EndPoint> sender_;
            std::array<char, Message::header_length> header_;
            std::shared_ptr<std::vector<char>> body_;
        };

        inline ServerConnection::ServerConnection(tcp::socket soc, Options& op) : socket_(std::move(soc)), op_(op) {
            auto ep = socket_.remote_endpoint();
            sender_ = std::make_shared<EndPoint>(ep.address().to_string(), ep.port());
            body_ = std::make_shared<std::vector<char>>(op.in_buf_size);
        }

        inline void ServerConnection::read_header() {
            auto self(shared_from_this());

            asio::async_read(socket_, asio::buffer(header_),
                [this, self] (std::error_code ec, std::size_t) {
                    if (ec) {
                        if (ec == asio::error::eof) {
                            // connection closed by peer, do nothing
                        } else {
                            std::cerr << "failed to read message header :" << ec.message() << std::endl;
                        }
                        socket_.close();
                    } else {
                        read_body();
                    }
                }
            );
        }

        inline void ServerConnection::read_body() {
            std::size_t body_length = 
                static_cast<std::size_t>(raft::protocol::NetworkUtil::atoh<Message::HeaderType>(header_));

            if (body_length > body_->size()) {
                std::cerr << "message size exceeds buffer size, please increase in_buf_size in Options." << std::endl;
                socket_.close();
                return;
            }

            auto self(shared_from_this());
            asio::async_read(socket_, asio::buffer(*body_, body_length), 
                [this, self, body_length] (std::error_code ec, std::size_t) {
                    if (ec) {
                        std::cerr << "failed to read message body :" << ec << std::endl;
                        socket_.close();
                    } else {
                        std::vector<char> data (body_length);
                        std::move(body_->begin(), body_->begin() + body_length, data.begin());

                        Message message (data);
                        op_.on_message(message, *sender_);

                        read_header();
                    }
                }
            );
        }

        class Server {
        public:
            Server(Options&);
            ~Server();

            Server(const Server&) = delete;
            Server& operator=(const Server&) = delete;

            // start the server, block until stop() is called.
            void start();
            void stop();
        private:
            void accept();

            Options op_;
            asio::io_context io_context_;
            std::shared_ptr<tcp::acceptor> acceptor_;
            std::once_flag start_flag_;
            std::once_flag stop_flag_;
        };

        inline Server::Server(Options& op) : op_(op) {
            acceptor_ = std::make_shared<tcp::acceptor>(io_context_, tcp::endpoint(tcp::v4(), op.port));                
        }

        inline Server::~Server() {
            stop();
        }

        inline void Server::start() {
            std::call_once(start_flag_, [this] {
                    accept();
                    io_context_.run();
            });
        }
        
        inline void Server::stop() {
            std::call_once(stop_flag_, [this] {
                std::thread t {  [this] { io_context_.stop(); } };
                t.join();
                acceptor_->close();
            });
        }

        inline void Server::accept() {
            acceptor_->async_accept(
                    [this] (std::error_code ec, tcp::socket soc) {
                        if (!ec) {
                            (std::make_shared<ServerConnection>(std::move(soc), op_))->start();
                        } else {
                            std::cerr << "connection error : " << ec.message() << std::endl;
                        }
                        accept();
                    }
            );
        }

        class Client {
        public:
            Client() {}
            ~Client() {}
            Client(const Client&) = delete;
            Client& operator=(const Client&) = delete;

            // copy the message to outgoing buffer and return immediately.
            // messages copied in out-buffer will be sent in a new tcp-connection asynchronously
            // there is no guarantee on delivery since messages won't be persisted or re-tried.
            // also no guarateen on order-of-delivery since messages will be sent in different 
            // tcp connections.
            // in raft's case, this is an acceptable behavior because raft protocol guarantees 
            // all messages will be delivered eventually. 
            void send(EndPoint&, Message&);

        private:           
            raft::conn_pool::ConnectionPool pool_ { raft::conn_pool::TerminationCondition() };
            asio::io_context io_context_;
        };

        inline void Client::send(EndPoint& endpoint, Message& message) {
            auto data = std::move(message.data);
            auto msg = std::make_shared<Message>(data);
            auto conn = pool_.get(endpoint);
            if (!conn) {
                std::cerr << "failed to acquire connection" << std::endl;
            } else {
                conn->write(msg);
            }
        }
    }
}

#endif