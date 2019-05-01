#ifndef fsm_hpp
#define fsm_hpp

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "protocol.hpp"
#include "node.hpp"
#include "raft_actor.hpp"
#include "scheduler.hpp"

namespace raft {
    namespace fsm {
        using namespace raft::protocol;
        using namespace raft::protocol::request;
        using namespace raft::protocol::response;
        using namespace raft::actor;
        using namespace raft::scheduler;

        // a special raft event representing a follower/candidate timeout.
        class ElectionTimeoutEvent : public RaftEvent {
        public:
            ~ElectionTimeoutEvent() {}
            std::uint16_t get_type() const override {
                return Type::election_timeout;
            }

            std::vector<char> get_data() override { return {}; }
            std::uint64_t get_server_id() const override { return 0; }
            std::uint64_t get_term() const override { return 0; }
            std::vector<char> encode() override { return {}; }
        };

        enum class RaftRole {
            follower, candidate, leader
        };

        class Voter {
        public:
            Voter() : last_voted(0), last_voted_term(0) {}

            // return whether the vote request is accepted by this node.
            bool vote_for_leader(const RequestVoteReq& event) {
                if (event.get_term() < last_voted_term) { // reject stale vote request
                    return false; 
                } else if (event.get_term() == last_voted_term) {
                    if (event.get_server_id() == last_voted) {
                        return true;
                    } else {
                        return false;
                    }
                } else {
                    last_voted_term = event.get_term();
                    last_voted = event.get_server_id();
                    return true;
                }
            }
        
        private:
            uint64_t last_voted;  // most recent server id that was voted as leader by this node.
            uint64_t last_voted_term;
        };

        class RaftFSM;
        using RaftFSMCallback = std::function<void(RaftFSM&)>;

        struct RaftFSMOptions {
            std::pair<int, int> election_timeout { 150, 300 } ;
            int heart_beat_rate = 50; // should be much smaller than election timeout.

            // callbacks
            RaftFSMCallback on_become_leader;
            RaftFSMCallback on_sign_off_leader;
        };

        // RaftFSM is used to build a RaftEventHandler when constructing RaftActor.
        class RaftFSM {
        public:
            RaftFSM(std::shared_ptr<RaftActor>, RaftFSMOptions);
            ~RaftFSM();
            RaftFSM(const RaftFSM&) = delete;
            RaftFSM& operator=(const RaftFSM&) = delete;
            RaftFSM(const RaftFSM&&) = delete;
            RaftFSM& operator=(const RaftFSM&&) = delete;

            // start the event loop.
            void start();

            // stop the event loop.
            void stop();

            // block the fsm. it's equivalent to block all incoming and outgoing messages, and 
            // revert to and stay as follower.
            // this method gives other peers a chance of becoming leader.
            void block();

            // unblock the fsm. this makes the fsm re-joins the group.
            void unblock();

            // convert to follower and stay as follower.
            void stay_follower();

            // put the event into internal event queue and return immediately.
            // events in the queue are processed in FIFO order as long as there's cpu cycle.
            void handle(RaftEvent&);

            // return the actor that manages this RaftFSM. A RafActor uniquely manages a single
            // RaftFSM.
            std::shared_ptr<RaftActor> get_actor() { return actor_; }

            RaftRole get_role() const { return role_; }
            uint64_t get_term() const { return term_; }
            uint64_t get_leader() const { return leader_; }
            std::unordered_set<uint64_t> get_votes() const { return voted_peers_; }
            bool is_leader() const { return get_role() == RaftRole::leader; }
            
        protected:
            bool is_stale(RaftEvent&);
            std::shared_ptr<RaftActorAddress> peer_address(RaftEvent&); 
            void convert_to_follower();
            void convert_to_candidate();
            void convert_to_leader();
            void init_follower_state();
            void clear_follower_state();
            void init_candidate_state();
            void clear_candidate_state();
            void init_leader_state();
            void clear_leader_state();
            void start_heartbeat();
            void stop_heartbeat();
            void start_election_timeout();
            void stop_election_timeout();
            void inc_term();
            bool vote_self();
            void request_vote_for_self();
            void start_new_election();
            void clear_prev_state();
            void handle_request_vote(RequestVoteReq&);
            void handle_request_vote_response(RequestVoteRes&);
            void handle_append_entries(AppendEntriesReq&);
            void handle_append_entries_response(AppendEntriesRes&);
            void handle_election_timeout(ElectionTimeoutEvent&);
            void cancel_scheduled_tasks();
            void pause_scheduled_tasks();

            std::shared_ptr<RaftActor> actor_;
            Scheduler scheduler_;
            std::mutex handler_mtx_;
            std::atomic<bool> blocked_ { false };

            // shared states
            RaftFSMOptions options_;
            RaftRole role_;
            uint64_t term_;
            uint64_t leader_;  // -1 for no leader yet.
            Voter voter_;
            std::shared_ptr<ScheduledTask> election_timeout_task_;
        
            // candidate states
            // server ids which voted this node as leader.
            std::unordered_set<uint64_t> voted_peers_;

            // leader states
            std::shared_ptr<ScheduledTask> heartbeat_task_;
        };

        inline RaftFSM::RaftFSM(std::shared_ptr<RaftActor> actor, RaftFSMOptions options = RaftFSMOptions()) :
            options_(options), 
            actor_(actor), 
            role_(RaftRole::follower), term_(0), leader_(-1) {}

        inline RaftFSM::~RaftFSM() {
            cancel_scheduled_tasks();
            stop();
        }

        inline void RaftFSM::start() {
            init_follower_state();
            get_actor()->start([this](RaftEvent& event) { handle(event); });
        }

        inline void RaftFSM::stop() {
            get_actor()->stop();
        }

        inline void RaftFSM::block() {
            stay_follower();
            blocked_.store(true);
        }

        inline void RaftFSM::unblock() {
            blocked_.store(false);
        }

        inline void RaftFSM::stay_follower() {
            convert_to_follower();
            pause_scheduled_tasks();
        }

        inline void RaftFSM::cancel_scheduled_tasks() {
            if (election_timeout_task_) {
                election_timeout_task_->cancel();
            }
            if (heartbeat_task_) {
                heartbeat_task_->cancel();
            }
        }

        inline void RaftFSM::pause_scheduled_tasks() {
            if (election_timeout_task_) {
                election_timeout_task_->pause();
            }
            if (heartbeat_task_) {
                heartbeat_task_->pause();
            }
        }

        // process next event in the event queue.
        inline void RaftFSM::handle(RaftEvent& event) {
            if (blocked_.load()) {
                return;
            }

            std::unique_lock<std::mutex> lock(handler_mtx_);

            // perform common operations described in Fig.2 of the raft paper, the "Rules for Servers"section.
            if (event.get_term() > term_) {
                term_ = event.get_term();
                convert_to_follower();
            }

            // perform event-specific operations.
            switch (event.get_type()) {
                case Type::request_vote_req:
                    handle_request_vote(dynamic_cast<RequestVoteReq&>(event));
                    break;
                case Type::request_vote_res:
                    handle_request_vote_response(dynamic_cast<RequestVoteRes&>(event));
                    break;
                case Type::append_entries_req:
                    handle_append_entries(dynamic_cast<AppendEntriesReq&>(event));
                    break;
                case Type::append_entries_res:
                    handle_append_entries_response(dynamic_cast<AppendEntriesRes&>(event));
                    break;
                case Type::election_timeout:
                    handle_election_timeout(dynamic_cast<ElectionTimeoutEvent&>(event));
                    break;
                default:
                    throw std::invalid_argument("unsupported event type");
            }
        }

        inline bool RaftFSM::is_stale(RaftEvent& event) {
            return event.get_term() < term_;
        }

        inline std::shared_ptr<RaftActorAddress> RaftFSM::peer_address(RaftEvent& event) {
            return get_actor()->get_peer(event.get_server_id());
        }

        inline void RaftFSM::inc_term() {
            term_++;
        }

        inline void RaftFSM::convert_to_follower() {
            bool was_leader = is_leader();
            if (role_ != RaftRole::follower) {
                std::cout << "server " << get_actor()->self() << " <= follower at term " << term_ << std::endl; 
                clear_prev_state();
                // note that it doesn't increment term here. 
                role_ = RaftRole::follower;
                init_follower_state();

                if (was_leader) {
                    if (auto fn = options_.on_sign_off_leader) {
                        fn(*this);
                    }
                }
            }
        }

        inline void RaftFSM::convert_to_candidate() {
            bool was_leader = is_leader();

            std::cout << "server " << get_actor()->self() << " <= candidate at term " << term_ << std::endl; 
            clear_prev_state();
            inc_term();
            role_ = RaftRole::candidate;
            init_candidate_state();

            if (was_leader) {
                if (auto fn = options_.on_sign_off_leader) {
                    fn(*this);
                }
            }
        }

        inline void RaftFSM::convert_to_leader() {
            std::cout << "server " << get_actor()->self() << " <= leader at term " << term_ << std::endl;
            clear_prev_state();
            inc_term();
            role_ = RaftRole::leader;
            init_leader_state();

            if (auto fn = options_.on_become_leader) {
                fn(*this);
            }
        }

        inline void RaftFSM::init_follower_state() {
            start_election_timeout();
        }

        inline void RaftFSM::clear_follower_state() {}

        inline void RaftFSM::init_candidate_state() {
            start_election_timeout();
            vote_self();
            request_vote_for_self();
        }

        inline void RaftFSM::clear_candidate_state() {
            stop_election_timeout();
            voted_peers_.clear();
        }

        inline void RaftFSM::init_leader_state() {
            start_heartbeat();
        }

        inline void RaftFSM::clear_leader_state() {
            stop_heartbeat();
        }

        inline void RaftFSM::clear_prev_state() {
            if (role_ == RaftRole::follower) {
                clear_follower_state();
            } else if (role_ == RaftRole::candidate) {
                clear_candidate_state();
            } else if (role_ == RaftRole::leader) {
                clear_leader_state();
            }
        }

        inline bool RaftFSM::vote_self() {
            RequestVoteReq r(get_actor()->self(), term_);
            bool voted = voter_.vote_for_leader(r);
            if (voted) {
                voted_peers_.insert(get_actor()->self());
            }
            return voted;
        }

        inline void RaftFSM::request_vote_for_self() {
            for (auto &peer : get_actor()->get_peers()) {
                RequestVoteReq r(get_actor()->self(), term_);
                get_actor()->send(r, *peer);
            }
        }

        inline void RaftFSM::start_new_election() {
            if (role_ == RaftRole::candidate) {
                inc_term();
                clear_candidate_state();
                init_candidate_state();
            }
        }

        inline void RaftFSM::start_heartbeat() {
            if (!heartbeat_task_) {
                heartbeat_task_ = scheduler_.schedule([this] { 
                    return options_.heart_beat_rate;
                }, [this] {
                    for(auto &peer : get_actor()->get_peers()) {
                        AppendEntriesReq heartbeat(get_actor()->self(), term_);
                        get_actor()->send(heartbeat, *peer);
                    } 
                });
            } else {
                heartbeat_task_->resume();
            }
        }

        inline void RaftFSM::stop_heartbeat() {
            if (heartbeat_task_) {
                heartbeat_task_->pause();
            }
        }

        inline void RaftFSM::start_election_timeout() {
            if (!election_timeout_task_) {
                election_timeout_task_ = scheduler_.schedule([this] {
                    auto min = options_.election_timeout.first;
                    auto gap = options_.election_timeout.second - min;
                    return rand() % gap + min;
                }, [this] {
                    ElectionTimeoutEvent timeout_event;
                    handle(timeout_event);
                });
            } else {
                election_timeout_task_->resume();
            }
        }

        inline void RaftFSM::stop_election_timeout() {
            if (election_timeout_task_) {
                election_timeout_task_->pause();
            }
        }

        inline void RaftFSM::handle_request_vote(RequestVoteReq& event) {
            if (is_stale(event)) {
                RequestVoteRes r(get_actor()->self(), term_, false);
                get_actor()->send(r, *peer_address(event));
            } else {
                if (role_ == RaftRole::follower) {
                    election_timeout_task_->reset();
                }
                
                RequestVoteRes r(get_actor()->self(), term_, voter_.vote_for_leader(event));
                get_actor()->send(r, *peer_address(event));
            }
        }

        inline void RaftFSM::handle_request_vote_response(RequestVoteRes& event) {
            if (!is_stale(event)) {
                if (role_ == RaftRole::candidate) {
                    if (event.is_vote_granted()) {
                        voted_peers_.insert(event.get_server_id());

                        // voted by majority
                        if (voted_peers_.size() >= get_actor()->majority()) {
                            convert_to_leader();
                        }
                    }
                }
            }
        }

        inline void RaftFSM::handle_append_entries(AppendEntriesReq& event) {
            if (event.is_heartbeat()) {
                if (!is_stale(event)) {
                    if (role_ == RaftRole::candidate || role_ == RaftRole::follower) {
                        election_timeout_task_->reset();
                    }
                    if (role_ == RaftRole::candidate) {
                        convert_to_follower();
                    }
                }
                AppendEntriesRes r(get_actor()->self(), term_);
                get_actor()->send(r, *peer_address(event));
            } else {
                // it's not needed for leader election
                throw std::invalid_argument("unsupported event type for leader election");
            }
        }

        inline void RaftFSM::handle_append_entries_response(AppendEntriesRes& event) {
            // for leader election we do not have to take any action
        }

        inline void RaftFSM::handle_election_timeout(ElectionTimeoutEvent& event) {
            std::cout << "server " << get_actor()->self() << " election timeut " << std::endl;
            if (role_ == RaftRole::follower) {
                convert_to_candidate();
            } else if (role_ == RaftRole::candidate) {
                start_new_election();
            }
        }
    }
}
#endif