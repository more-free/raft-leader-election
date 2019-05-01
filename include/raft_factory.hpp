#ifndef actor_factory_hpp
#define actor_factory_hpp

#include "fsm.hpp"
#include "raft_actor.hpp"

// Provides a wrapper-API layer for actor creation and manipulation.
namespace raft { 
    namespace api {
        using raft::actor::RaftActor;
        using raft::actor::RemoteRaftActor;
        using raft::actor::RaftActorOptions;
        using raft::actor::RemoteRaftActorOptions;
        using raft::actor::RemoteRaftActorAddress;
        using raft::fsm::RaftFSM;
        using raft::fsm::RaftFSMOptions;
        using raft::fsm::RaftFSMCallback;

        class RaftOptions {
        public:
            // Map from unique server_id to host:port pair.
            RaftActorOptions::PeerMap peers;

            // id of this server
            uint64_t self;

            // (min, max) value of election timeout in ms.
            std::pair<int, int> election_timeout_ms = { 300, 500 };

            // heartbeat rate in ms.
            int heartbeat_rate_ms = 100;

            // call when the current server becomes leader, must be non-blocking.
            RaftFSMCallback on_become_leader;

            // call when the current server signs off from leader, must be non-blocking.
            RaftFSMCallback on_sign_off_leader;
        };

        class RaftFactory {
        public:
            static std::shared_ptr<RemoteRaftActorAddress> create_remote_address(const actor::EndPoint& endpoint) {
                return std::make_shared<RemoteRaftActorAddress>(endpoint);
            }

            static std::shared_ptr<RaftFSM> create_raft_fsm(RaftOptions options) {
                RemoteRaftActorOptions actor_options { options.peers, options.self };
                auto actor = std::make_shared<RemoteRaftActor>(actor_options);
                RaftFSMOptions fsm_options { 
                    options.election_timeout_ms, options.heartbeat_rate_ms, options.on_become_leader, options.on_sign_off_leader };
                return std::make_shared<RaftFSM>(actor, fsm_options);
            }
        };
    }
}

#endif