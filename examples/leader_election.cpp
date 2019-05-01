#include "../include/raft_factory.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;
using namespace raft::api;
using namespace raft::node;

// g++ --std=c++14 leader_election.cpp -o leader_election && ./leader_election 
// sample output : localhost:9990 was elected as leader
vector<shared_ptr<RaftFSM>> create_cluster() {
    vector<shared_ptr<RaftFSM>> fsms;

    for (uint64_t server_id = 1; server_id <= 5; server_id++) {
        RaftOptions options;
        options.peers = {
            { 1, RaftFactory::create_remote_address(EndPoint("localhost", 9990)) },
            { 2, RaftFactory::create_remote_address(EndPoint("localhost", 9991)) },
            { 3, RaftFactory::create_remote_address(EndPoint("localhost", 9992)) },
            { 4, RaftFactory::create_remote_address(EndPoint("localhost", 9993)) },
            { 5, RaftFactory::create_remote_address(EndPoint("localhost", 9994)) },
        };
        options.self = server_id;
        options.on_become_leader = [](RaftFSM& fsm) { 
            cout << fsm.get_actor()->self_address()->to_string() << " was elected as leader" << endl; 
        };

        fsms.push_back(RaftFactory::create_raft_fsm(options));
    }

    return fsms;
}

int main(int argc, char* argv[]) {
    auto cluster = create_cluster();
    vector<shared_ptr<thread>> threads;
    for (auto &s : cluster) {
        threads.push_back(make_shared<thread>([s] { s->start(); }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    for (auto &s : cluster) {
        s->stop();
    }
    for (auto &t : threads) {
        t->join();
    }

    return 0;
}