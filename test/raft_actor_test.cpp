#include "gtest/gtest.h"
#include "../include/protocol.hpp"
#include "../include/raft_actor.hpp"
#include "../include/concurrent.hpp"

#include <utility>
#include <iostream>
#include <unordered_map>
#include <memory>

using namespace std;
using namespace raft::concurrent;
using namespace raft::protocol;
using namespace raft::protocol::request;
using namespace raft::protocol::response;
using namespace raft::actor;


TEST(ActorTest, RemoteActorTest) {
    CountDownLatch latch (2);

    EndPoint ep1 ("localhost", 9999);
    EndPoint ep2 ("localhost", 9998);
    unordered_map<uint64_t, shared_ptr<RaftActorAddress>> peers {
        { 1, std::make_shared<RemoteRaftActorAddress>(ep1) }, 
        { 2, std::make_shared<RemoteRaftActorAddress>(ep2) }
    };
    RemoteRaftActorOptions op1 (peers, 1);
    RemoteRaftActorOptions op2 (peers, 2);
    RemoteRaftActor actor1 (op1);
    RemoteRaftActor actor2 (op2);

    auto event_handler1 = [&latch, &actor1] (RaftEvent& event) {
        RequestVoteReq& req = dynamic_cast<RequestVoteReq&>(event);
        ASSERT_EQ(2, req.get_server_id());
        ASSERT_EQ(200, req.get_term());

        int term = 201;
        auto sender = actor1.get_peer(req.get_server_id());
        RequestVoteRes res (actor1.self(), term, true);
        actor1.send(res, *sender);

        latch.count_down();
    };

    auto event_handler2 = [&latch](RaftEvent& event) {
        RequestVoteRes& res = dynamic_cast<RequestVoteRes&>(event);
        ASSERT_EQ(201, res.get_term());
        latch.count_down();
    };

    thread t1 { [&] { actor1.start(event_handler1); }};
    thread t2 { [&] { actor2.start(event_handler2); }};
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    RequestVoteReq req (2, 200);
    RemoteRaftActorAddress remote_address(ep1);
    actor2.send(req, remote_address);

    latch.await();
    actor1.stop();
    actor2.stop();

    t1.join();
    t2.join();
}