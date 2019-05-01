#include "gtest/gtest.h"
#include "../include/protocol.hpp"
#include "../include/raft_actor.hpp"
#include "../include/fsm.hpp"
#include "../include/concurrent.hpp"
#include "../include/raft_factory.hpp"

#include <vector>
#include <utility>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

using namespace std;
using namespace raft::concurrent;
using namespace raft::protocol;
using namespace raft::protocol::request;
using namespace raft::protocol::response;
using namespace raft::actor;
using namespace raft::fsm;
using namespace raft::api;

class BlockableRaftFSM : public RaftFSM {
public:
    BlockableRaftFSM(std::shared_ptr<RaftActor> actor, RaftFSMOptions options = RaftFSMOptions()) : 
        RaftFSM(actor, options) {}
    
    void handle(RaftEvent& event) {
        if (!is_blocked(event)) {
            RaftFSM::handle(event);
        }
    }

    void block_event(uint16_t event_type) {
        blocked_event_types_.insert(event_type);
    }

    void unblock_event(uint16_t event_type) {
        blocked_event_types_.erase(event_type);
    }

private:
    bool is_blocked(RaftEvent& event) {
        return blocked_event_types_.find(event.get_type()) != blocked_event_types_.end();
    }

    unordered_set<uint16_t> blocked_event_types_;
};

class LocalRaftCluster {
public:
    static vector<EndPoint> endpoints;
    static RaftFSMOptions fsm_options;

    LocalRaftCluster(RaftFSMOptions options = fsm_options) {        
        RaftActorOptions::PeerMap peers;
        for (auto i = 0; i < endpoints.size(); i++) {
            uint64_t server_id = i + 1;
            peers[server_id] = make_shared<RemoteRaftActorAddress>(endpoints[i]);
        }

        vector<RemoteRaftActorOptions> actor_options;
        for (auto i = 0; i < endpoints.size(); i++) {
            uint64_t server_id = i + 1;
            RemoteRaftActorOptions options { peers, server_id };
            actor_options.push_back(options);
        }

        for (auto i = 0; i < actor_options.size(); i++) {
            uint64_t server_id = i + 1;
            auto actor = make_shared<RemoteRaftActor>(actor_options[i]);
            auto fsm = make_shared<BlockableRaftFSM>(actor, options);
            fsms_[server_id] = fsm;
        }
    }

    ~LocalRaftCluster() {
        stop_all();
    }

    void start_all() {
        unique_lock<mutex> lock(mtx_);
        for (auto &s : fsms_) {
            auto t = make_shared<thread>([&] { s.second->start(); });
            threads_.push_back(t);
        }
        running_.store(true);
    }

    void stop_all() {
        unique_lock<mutex> lock(mtx_);
        if (running_.load()) {
            for (auto &s : fsms_) {
                s.second->stop();
            }
            for (auto &t : threads_) {
                t->join();
            }
            running_.store(false);
        }
    }

    void block_event(uint64_t server_id, uint16_t event_type) {
        auto iter = fsms_.find(server_id);
        if (iter != fsms_.end()) {
            iter->second->block_event(event_type);
        }
    }

    void unblock_event(uint64_t server_id, uint16_t event_type) {
        auto iter = fsms_.find(server_id);
        if (iter != fsms_.end()) {
            iter->second->unblock_event(event_type);
        }
    }

    void block_server(uint64_t server_id) {
        auto iter = fsms_.find(server_id);
        if (iter != fsms_.end()) {
            iter->second->block();
        }
    }

    void unblock_server(uint64_t server_id) {
        auto iter = fsms_.find(server_id);
        if (iter != fsms_.end()) {
            iter->second->unblock();
        }
    }

    uint64_t current_leader() {
        uint64_t leader_id = 0;
        for (auto &s : fsms_) {
            if (s.second->is_leader()) {
                if (leader_id == 0) {
                    leader_id = s.first;
                } else {
                    throw runtime_error("more than one leader detected");
                }
            }
        }
        return leader_id;
    }

    size_t size() {
        return fsms_.size();
    }

private:
    unordered_map<uint64_t, shared_ptr<BlockableRaftFSM>> fsms_;
    vector<shared_ptr<thread>> threads_;
    mutex mtx_;
    atomic<bool> running_ { false };
};

vector<EndPoint> LocalRaftCluster::endpoints = {
    { "localhost", 9990 }, { "localhost",  9991 }, { "localhost", 9992 }, { "localhost", 9993 }, { "localhost", 9994 }
};

RaftFSMOptions LocalRaftCluster::fsm_options = { { 150, 200 }, 20 };

void sleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

TEST(FSMTest, TestBasicCommunication) {
    const vector<EndPoint> endpoints {{ "localhost", 9990 }, { "localhost",  9991 }, { "localhost", 9992 }};
    RaftActorOptions::PeerMap peers;
    for (auto i = 0; i < endpoints.size(); i++) {
        uint64_t server_id = i + 1;
        peers[server_id] = make_shared<RemoteRaftActorAddress>(endpoints[i]);
    }

    vector<shared_ptr<RaftFSM>> fsms;
    for (auto i = 0; i < endpoints.size(); i++) {
        RaftOptions options;
        options.self = i + 1;
        options.peers = peers;
        options.heartbeat_rate_ms = LocalRaftCluster::fsm_options.heart_beat_rate;
        options.election_timeout_ms = LocalRaftCluster::fsm_options.election_timeout;
        fsms.push_back(RaftFactory::create_raft_fsm(options));
    }

    vector<shared_ptr<thread>> threads;
    for (auto s : fsms) {
        threads.push_back(make_shared<thread>([s] { s->start(); } ));
    }
    sleep(2000);
    for (auto s : fsms) {
        s->stop();
    }
    for (auto &t : threads) {
        t->join();
    }
}

TEST(FSMTest, ElectLeaderSuccessfully) {
    LocalRaftCluster cluster;
    cluster.start_all();
    sleep(2000);
    ASSERT_TRUE(cluster.current_leader() > 0);
}

TEST(FSMTest, LeaderTimeoutAndRejoin) {
    LocalRaftCluster cluster;
    cluster.start_all();
    sleep(2000);

    auto leader = cluster.current_leader();
    ASSERT_TRUE(leader > 0);
    cluster.block_server(leader);
    sleep(3000);
    cluster.unblock_server(leader);
    sleep(1000);

    auto new_leader = cluster.current_leader();
    ASSERT_TRUE(new_leader > 0);
    ASSERT_TRUE(leader != new_leader);
}

TEST(FSMTest, FollowerTimeout) {
    LocalRaftCluster cluster;
    cluster.start_all();
    sleep(2000);
    auto leader = cluster.current_leader();
    ASSERT_TRUE(leader > 0);

    auto follower = leader == LocalRaftCluster::endpoints.size() ? leader - 1 : leader + 1;
    cluster.block_server(follower);
    sleep(1000);
    auto new_leader = cluster.current_leader();
    ASSERT_TRUE(leader == new_leader);
}

TEST(FSMTest, DistributedLock) {
    unordered_set<uint64_t> leaders;
    RaftFSMOptions options {
        { 150, 300 }, 
        20,
        [&leaders](RaftFSM& fsm) { 
            leaders.insert(fsm.get_actor()->self());
            fsm.stay_follower();
        },
        [](RaftFSM& fsm) {}
    };
    LocalRaftCluster cluster { options };
    cluster.start_all();
    sleep(5000);
    
    ASSERT_EQ(unordered_set<uint64_t>({1, 2, 3, 4, 5}), leaders);
}