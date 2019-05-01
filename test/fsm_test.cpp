#include "gtest/gtest.h"
#include "../include/protocol.hpp"
#include "../include/raft_actor.hpp"
#include "../include/fsm.hpp"
#include "../include/concurrent.hpp"

#include <vector>
#include <utility>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <functional>

using namespace std;
using namespace raft::concurrent;
using namespace raft::protocol;
using namespace raft::protocol::request;
using namespace raft::protocol::response;
using namespace raft::actor;
using namespace raft::fsm;

class IdleActor : public RaftActor {
public:
    IdleActor(RaftActorOptions options) : options(options) {}
    ~IdleActor() {}

    void start(std::function<void(RaftEvent&)>) override {}
    
    void stop() override {}

    void send(RaftEvent&, RaftActorAddress&) override {}

    RaftActorOptions& get_options() override { return options; }

private:
    RaftActorOptions options;
};

class IdleAddress : public RaftActorAddress {
public:
    IdleAddress() {}
    ~IdleAddress() {}
    string to_string() const { return "idle"; }
};

pair<shared_ptr<RaftActor>, shared_ptr<RaftFSM>> create_fsm() {
    shared_ptr<RaftActorAddress> address = make_shared<IdleAddress>();
    RaftActorOptions::PeerMap peers {
        { 1, address }, { 2, address }, { 3, address }, { 4, address }, { 5, address }
    };
    RaftActorOptions options { peers, 1 };
    shared_ptr<RaftActor> actor_ptr = make_shared<IdleActor>(options);

    RaftFSMOptions fsm_options {
        { 50, 100 }, 10
    };
    auto fsm_ptr =  make_shared<RaftFSM>(actor_ptr, fsm_options);
    fsm_ptr->start();
    return { actor_ptr, fsm_ptr };
}

void wait_for_election_timeout(int terms = 3) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100 * terms));
}

TEST(FSMTest, FollowerToCandidate) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;
    wait_for_election_timeout();
    ASSERT_TRUE(fsm->get_term() > 0);
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());
}

TEST(FSMTest, CandidateToFollowerOnNewTerm) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;
    wait_for_election_timeout();
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());

    RequestVoteReq r(2, 1000);
    fsm->handle(r);
    ASSERT_EQ(RaftRole::follower, fsm->get_role());
}

TEST(FSMTest, CandidateToFollowerOnNewLeader) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;
    wait_for_election_timeout();
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());

    AppendEntriesReq r(2, fsm->get_term());
    fsm->handle(r);
    ASSERT_EQ(RaftRole::follower, fsm->get_role());
}

TEST(FSMTest, StayCandidateOnStaleLeader) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;
    wait_for_election_timeout();
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());

    AppendEntriesReq r(2, fsm->get_term() - 1);
    fsm->handle(r);
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());
}

TEST(FSMTest, CandidateToLeader) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;
    wait_for_election_timeout();
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());

    RequestVoteRes r1 { 2, fsm->get_term(), true };
    RequestVoteRes r2 { 3, fsm->get_term(), true };
    RequestVoteRes r3 { 4, fsm->get_term(), false };
    RequestVoteRes r4 { 5, fsm->get_term(), false };
    fsm->handle(r1);
    fsm->handle(r2);
    fsm->handle(r3);
    fsm->handle(r4);
    ASSERT_EQ(RaftRole::leader, fsm->get_role());
}

TEST(FSMTest, LeaderToFollower) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;
    wait_for_election_timeout();
    ASSERT_EQ(RaftRole::candidate, fsm->get_role());

    RequestVoteRes r1 { 2, fsm->get_term(), true };
    RequestVoteRes r2 { 3, fsm->get_term(), true };
    RequestVoteRes r3 { 4, fsm->get_term(), false };
    RequestVoteRes r4 { 5, fsm->get_term(), false };
    fsm->handle(r1);
    fsm->handle(r2);
    fsm->handle(r3);
    fsm->handle(r4);
    ASSERT_EQ(RaftRole::leader, fsm->get_role());

    AppendEntriesReq r { 3, 1000 };
    fsm->handle(r);
    ASSERT_EQ(RaftRole::follower, fsm->get_role());
}

TEST(FSMTest, HeartBeatSuppressesElectionTimeout) {
    auto ptrs = create_fsm();
    auto fsm = ptrs.second;

    Scheduler s;
    s.schedule([] { return 10; }, 
        [fsm] { 
            AppendEntriesReq req { 2, 1 };
            fsm->handle(req);
         });
    wait_for_election_timeout();

    // no election timeout happens yet.
    ASSERT_EQ(RaftRole::follower, fsm->get_role());
    ASSERT_EQ(1, fsm->get_term());
}