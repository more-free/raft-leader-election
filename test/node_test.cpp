#include "gtest/gtest.h"
#include "../include/concurrent.hpp"
#include "../include/node.hpp"
#include <iostream>
#include <chrono>
#include <string>
#include <set>
#include <vector>
#include <thread>
#include <future>
#include <cstdlib>

using namespace std;
using namespace raft::node;
using namespace raft::concurrent;
using namespace raft::protocol;

string random_word(const size_t length) {
    char w[length];
    for (auto i = 0; i < length; i++) {
        w[i] = static_cast<char>('a' + rand() % 26);
    }
    return string(w);
}

set<string> gen_random(const size_t size, const size_t length) {
    set<string> words;
    for (size_t t = 0; t < size; t++) {
        words.insert(random_word(length));
    }
    return words;
}

TEST(NodeTest, SingleClientAndServer) {
    const int in_buf = 1024;
    const string host = "localhost";
    const int port = 9999;
    
    BlockingQueue<string> queue;
    Options op(port, in_buf, [&queue](Message& msg, EndPoint& ep) {
        string s (msg.data.begin(), msg.data.end());
        queue.put(std::move(s));
    });
    Server server(op);
    thread t_server { [&server]{
        server.start();
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const int message_size = 10000;
    const int message_length = 1000;
    set<string> messages = gen_random(message_size, message_length);
    Client client;
    EndPoint ep (host, port);

    for (auto &s : messages) {
        string ss = s;
        Message msg(ss);
        client.send(ep, msg);
    }

    set<string> received;
    for (int i = 0; i < message_size; i++) {
        string s;
        queue.get(s);
        received.insert(std::move(s));
    }

    server.stop();
    t_server.join();

    ASSERT_EQ(messages, received);
}