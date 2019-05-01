#include "../include/raft_factory.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;
using namespace raft::api;
using namespace raft::node;

// g++ --std=c++14 distributed_lock.cpp -o distributed_lock && ./distributed_lock
// sample output : 
//  localhost:9990 acquired lock
//  localhost:9990 released lock
//  localhost:9991 acquired lock
//  localhost:9991 released lock
//  localhost:9993 acquired lock
//  localhost:9993 released lock
//  localhost:9992 acquired lock
//  localhost:9992 released lock
//  localhost:9994 acquired lock
//  localhost:9994 released lock
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

        // called when the current server is elected as leader. it's semantically equivalent to a "lock" call.
        options.on_become_leader = [](RaftFSM& fsm) { 
            cout << fsm.get_actor()->self_address()->to_string() << " acquired lock" << endl;

            // sign-off the leadership. it's semantically equivalent to a "unlock" call. 
            fsm.stay_follower(); 
        };

        // optionally callback. called after the above "unlock" call finishes.
        options.on_sign_off_leader = [](RaftFSM& fsm) {
            cout << fsm.get_actor()->self_address()->to_string() << " released lock" << endl;
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