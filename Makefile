CXX = g++
CXXFLAGS = -std=c++14

GTEST_DIR = ./third_party/googletest/googletest

HEADERS = ./include

USER_DIR = ./test

CPPFLAGS += -isystem $(GTEST_DIR)/include

CXXFLAGS += -g -Wall -Wextra -pthread

GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
                $(GTEST_DIR)/include/gtest/internal/*.h

GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

test : all_tests

clean :
	rm -f all_tests gtest.a gtest_main.a *.o

gtest-all.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest-all.cc

gtest.a : gtest-all.o
	$(AR) $(ARFLAGS) $@ $^

all_tests.o : $(USER_DIR)/all_tests.cpp $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/all_tests.cpp

all_tests : all_tests.o gtest-all.o protocol_test.o node_test.o raft_actor_test.o fsm_test.o connection_pool_test.o leader_election_test.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -lpthread $^ -o $@

protocol_test.o : $(USER_DIR)/protocol_test.cpp $(GTEST_HEADERS) $(HEADERS)/protocol.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/protocol_test.cpp

node_test.o : $(USER_DIR)/node_test.cpp $(GTEST_HEADERS) $(HEADERS)/node.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/node_test.cpp

raft_actor_test.o : $(USER_DIR)/raft_actor_test.cpp $(GTEST_HEADERS) $(HEADERS)/raft_actor.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/raft_actor_test.cpp

fsm_test.o : $(USER_DIR)/fsm_test.cpp $(GTEST_HEADERS) $(HEADERS)/fsm.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/fsm_test.cpp

connection_pool_test.o : $(USER_DIR)/connection_pool_test.cpp $(GTEST_HEADERS) $(HEADERS)/connection_pool.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/connection_pool_test.cpp

leader_election_test.o : $(USER_DIR)/leader_election_test.cpp $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/leader_election_test.cpp