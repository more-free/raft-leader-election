#ifndef raft_actor_hpp
#define raft_actor_hpp

#include <vector>
#include <iostream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>

#include "protocol.hpp"
#include "node.hpp"

namespace raft {
    namespace actor {
        using raft::protocol::RaftEvent;
        using raft::protocol::RaftEventFactory;
        using raft::node::Message;
        using raft::node::Server;
        using raft::node::Client;
        using raft::node::Options;
        using raft::node::EndPoint;
  
        class RaftActorAddress {
        public:
            virtual ~RaftActorAddress() {}
            virtual std::string to_string() const = 0;
        };

        class RaftActorOptions {
        public:
            using PeerMap = std::unordered_map<std::uint64_t, std::shared_ptr<RaftActorAddress>>;

            RaftActorOptions(PeerMap& peers, uint64_t server_id) : peers(peers), server_id(server_id) {}

            PeerMap peers;
            uint64_t server_id;
        };

        class RaftActor {
        public:
            virtual ~RaftActor() {}

            // start the event-handling loop
            virtual void start(std::function<void(RaftEvent&)>) = 0;
            
            // stop the event-handling loop
            virtual void stop() = 0;

            virtual void send(RaftEvent&, RaftActorAddress&) = 0;

            virtual RaftActorOptions& get_options() = 0;

            // return peer's address given peer's server id, or nullptr if not exists.
            std::shared_ptr<RaftActorAddress> get_peer(std::uint64_t server_id) {
                auto peers = get_options().peers;
                auto iter = peers.find(server_id);
                return (iter == peers.end()) ? nullptr : iter->second;
            }

            // return all peers available.
            std::vector<std::shared_ptr<RaftActorAddress>> get_peers() {
                std::vector<std::shared_ptr<RaftActorAddress>> peers;
                for (auto &i : get_options().peers) {
                    if (i.first != self()) {
                        peers.push_back(i.second);
                    }
                }
                return peers;
            }

            // return server id represented by this actor
            std::uint64_t self() {
                return get_options().server_id;
            }

            // return this node's address
            std::shared_ptr<RaftActorAddress> self_address() {
                return get_peer(self());
            }

            // return number of nodes that form a quorum
            size_t majority() {
                size_t s = get_options().peers.size();
                return s / 2 + 1;
            }
        };

        /* remote actors */

        class RemoteRaftActorAddress : public RaftActorAddress {
        public:
            RemoteRaftActorAddress(const EndPoint& endpoint) : endpoint_(endpoint) {}
            EndPoint& get_endpoint() { return endpoint_; }

            std::string to_string() const override {
                return endpoint_.to_string();
            }

        private:
            EndPoint endpoint_;
        };

        class RemoteRaftActorOptions : public RaftActorOptions {
        public:
            RemoteRaftActorOptions(
                RaftActorOptions::PeerMap peers,
                uint64_t server_id, 
                size_t max_msg_size = 1024) : 
                    RaftActorOptions(peers, server_id), max_msg_size(max_msg_size) {}

            // max size for a single message exchanged between two remote actors.
            size_t max_msg_size;
        };

        class RemoteRaftActor : public RaftActor {
        public:
            RemoteRaftActor(RemoteRaftActorOptions&);
            ~RemoteRaftActor() {}
            RemoteRaftActor(const RemoteRaftActor&) = delete;
            RemoteRaftActor& operator=(const RemoteRaftActor&) =delete;

            RaftActorOptions& get_options() override;
            void start(std::function<void(RaftEvent&)>) override;
            void stop() override;
            void send(RaftEvent&, RaftActorAddress&) override;

        private:
            int get_port();

            RemoteRaftActorOptions op_;
            std::unique_ptr<Server> server_;
            std::unique_ptr<Client> client_;

            std::once_flag start_server_flag_;
        };

        inline RemoteRaftActor::RemoteRaftActor(RemoteRaftActorOptions& op) : op_(op) {}

        inline RaftActorOptions& RemoteRaftActor::get_options() { return op_; }

        inline int RemoteRaftActor::get_port() {
            auto self = std::static_pointer_cast<RemoteRaftActorAddress>(self_address()); 
            return self->get_endpoint().port;
        }

        inline void RemoteRaftActor::start(std::function<void(RaftEvent&)> event_handler) {
            std::call_once(start_server_flag_, [this, event_handler]() mutable {
                auto on_message = [event_handler] (Message& message, EndPoint&) {
                    std::shared_ptr<RaftEvent> event;
                    try {
                        event = RaftEventFactory::decode(message.data);
                    } catch (std::invalid_argument& e) {
                        std::cerr << e.what() << std::endl;
                    }

                    if (event) {              
                        event_handler(*event);
                    }
                };

                Options server_op (this->get_port(), this->op_.max_msg_size, on_message);
                server_ = std::make_unique<Server>(server_op);
                client_ = std::make_unique<Client>();
            });

            server_->start();
        }

        inline void RemoteRaftActor::stop() {
            server_->stop();
        }

        inline void RemoteRaftActor::send(RaftEvent& event, RaftActorAddress& address) {
            if (!client_) {
                std::cerr << "client has not been initialized" << std::endl;
            }

            // for now it doesn't support sending messages to local actor.
            assert (typeid(address) == typeid(RemoteRaftActorAddress));

            RemoteRaftActorAddress& remote_address = dynamic_cast<RemoteRaftActorAddress&>(address);
            Message message (event.encode());
            EndPoint endpoint = remote_address.get_endpoint();
            client_->send(endpoint, message);
        }
    }
}

#endif