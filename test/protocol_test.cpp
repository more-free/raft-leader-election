#include "gtest/gtest.h"
#include "../include/protocol.hpp"

#include <utility>
#include <chrono>

using namespace std;
using namespace raft::protocol;
using namespace raft::protocol::request;
using namespace raft::protocol::response;

TEST(NetworkUtilTest, NetworkByteOrder) {
    uint16_t h1 = 13;
    auto a1 = NetworkUtil::htoa<uint16_t>(h1);
    ASSERT_EQ(h1, NetworkUtil::atoh<uint16_t>(a1));

    uint32_t h2 = 1234;
    auto a2 = NetworkUtil::htoa<uint32_t>(h2);
    ASSERT_EQ(h2, NetworkUtil::atoh<uint32_t>(a2));

    uint64_t h3 = 1231235555;
    auto a3 = NetworkUtil::htoa<uint64_t>(h3);
    ASSERT_EQ(h3, NetworkUtil::atoh<uint64_t>(a3));
}

TEST(RaftEventTest, RequestVoteReqSerDe) {
    shared_ptr<RaftEvent> req = make_shared<RequestVoteReq>(1, 100);
    vector<char> data = req->encode();
    shared_ptr<RequestVoteReq> req2 = dynamic_pointer_cast<RequestVoteReq>(RaftEventFactory::decode(data));

    ASSERT_EQ(1, req2->get_server_id());
    ASSERT_EQ(100, req2->get_term());
}

TEST(RaftEventTest, AppendEntriesReqSerDe) {
    shared_ptr<RaftEvent> req = make_shared<AppendEntriesReq>(1, 100);
    vector<char> data = req->encode();
    shared_ptr<AppendEntriesReq> req2 = dynamic_pointer_cast<AppendEntriesReq>(RaftEventFactory::decode(data));

    ASSERT_EQ(1, req2->get_server_id());
    ASSERT_EQ(100, req2->get_term());
}

TEST(RaftEventTest, RequestVoteResSerDe) {
    shared_ptr<RaftEvent> res = make_shared<RequestVoteRes>(1, 100, true);
    vector<char> data = res->encode();
    shared_ptr<RequestVoteRes> res2 = dynamic_pointer_cast<RequestVoteRes>(RaftEventFactory::decode(data));

    ASSERT_EQ(1, res2->get_server_id());
    ASSERT_EQ(100, res2->get_term());
    ASSERT_TRUE(res2->is_vote_granted());
}

TEST(RaftEventTest, AppendEntriesResSerDe) {
    shared_ptr<RaftEvent> res = make_shared<AppendEntriesRes>(1, 100);
    vector<char> data = res->encode();
    shared_ptr<AppendEntriesRes> res2 = dynamic_pointer_cast<AppendEntriesRes>(RaftEventFactory::decode(data));

    ASSERT_EQ(1, res2->get_server_id());
    ASSERT_EQ(100, res2->get_term());
}