#include "gtest/gtest.h"
#include "../include/connection_pool.hpp"

#include <utility>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>

using namespace std;
using namespace raft::conn_pool;
using asio::ip::tcp;

void start_server() {
    asio::io_context io_context;
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 9999));
    tcp::socket socket(io_context);
    acceptor.accept(socket);
}

shared_ptr<Message> make_msg() {
    string s = "hello";
    return std::make_shared<Message>(s);
}

TEST(ConnectionPoolTest, NormalTermination) {
    thread t { [] { start_server(); } };
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ConnectionPool pool { TerminationCondition() } ;
    EndPoint ep ("localhost", 9999);
    auto conn = pool.get(ep);
    conn->write(make_msg());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT_FALSE(conn->closed());
    t.join();
}