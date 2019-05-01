// we use hand-written ser-de functions instead of third-party library like proto-buffer,
// since the event structures for leader election is very simple.
#ifndef protocol_hpp
#define protocol_hpp

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <algorithm>
#include <initializer_list>
#include "utils.hpp"

namespace raft {
    namespace protocol {
        enum Type {
            request_vote_req = 1,
            request_vote_res,
            append_entries_req,
            append_entries_res,

            election_timeout = 100
        };

        class NetworkUtil {
        public:
            // convert i (which uses machine-specific byte order)
            // to a 4-byte array with network byte order (big-endian).
            template <typename T>
            static std::array<char, sizeof(T)> htoa(T h) {
                return ntoa<T>(hton<T>(h));
            }

            template <typename T> 
            static std::vector<char> htov(T h) {
                auto a = htoa(h);
                std::vector<char> v (a.size());
                return std::move(a.begin(), a.end(), v.begin());
            }
            
            // convert a 4-byte array (in network byte order) back to
            // T with machine-specific byte order.
            template <typename T>
            static T atoh(std::array<char, sizeof(T)>& a) {
                return ntoh<T>(aton<T>(a));
            }

            template <typename T>
            static T vtoh(std::vector<char>& v) {
                assert (v.size() == sizeof(T));

                std::array<char, sizeof(T)> a;
                std::move(v.begin(), v.end(), a.begin());
                return atoh<T>(a);
            }
            
            private:
            // convert i in machine-specific byte order to another T in network order.
            template <typename T>
            static T hton(T h) {
                if (is_big_endian()) {
                    return h;
                } else {
                    T n = 0;
                    const size_t bits = sizeof(T) * 8;
                    for (size_t shift = 0; shift <= bits - 8; shift += 8) {
                        n |= ((h >> shift) & 0xff) << (bits - 8 - shift);
                    }
                    return n;
                }
            }
            
            template <typename T>
            static T ntoh(T n) {
                return hton<T>(n);
            }
            
            // convert i in network byte order to a byte array in the same byte order.
            template <typename T>
            static std::array<char, sizeof(T)> ntoa(T n) {
                std::array<char, sizeof(T)> a;
                for (int i = static_cast<int>(a.size() - 1), shift = 0; i >= 0; shift += 8, i--) {
                    a[i] = static_cast<char>((n >> shift) & 0xff);
                }
                return a;
            }
            
            template <typename T>
            static T aton(std::array<char, sizeof(T)>& a) {
                T n = 0;
                const size_t bits = sizeof(T) * 8;
                for (size_t i = 0, shift = bits - 8; i < a.size(); shift -= 8, i++) {
                    n |= static_cast<T>(a[i]) << shift;
                }
                return n;
            }
            
            static bool is_big_endian() {
                // the function is borrowed from <unix network programming, 3ed>
                union {
                    short s;
                    char c[sizeof(short)];
                } un;
                
                un.s = 0x0102;
                return un.c[0] == 1 && un.c[1] == 2;
            }
        };

        class RaftEvent {
        public:
            virtual ~RaftEvent() {}
            virtual std::uint16_t get_type() const = 0;
            virtual std::vector<char> get_data() = 0;

            virtual std::uint64_t get_server_id() const = 0;
            virtual std::uint64_t get_term() const = 0;

            virtual std::vector<char> encode() = 0;
        };

        class RaftBaseEvent : public RaftEvent {
        public:
            RaftBaseEvent(std::uint64_t server_id, std::uint64_t term) : server_id(server_id), term(term) {}

            // deserialize from vector of char.
            RaftBaseEvent(std::vector<char>& data) {
                auto s = sizeof(std::uint64_t);
                decode_member(data.begin(), data.begin() + s, server_id);
                decode_member(data.begin() + s, data.begin() + 2 * s, term);
            }

            // encode the event's type and data into a new vector.
            // return the encoded char array.
            // format : type <2 bytes>, data <length bytes>
            std::vector<char> encode() override {                
                auto type = NetworkUtil::htoa<std::uint16_t>(get_type());
                auto data = get_data();
                
                std::vector<char> v (type.size() + data.size());
                std::move(type.begin(), type.end(), v.begin());
                std::move(data.begin(), data.end(), v.begin() + type.size());

                return v;
            }

            std::vector<char> get_data() override {
                return encode_member({ server_id, term });
            }

            std::uint64_t get_server_id() const override {
                return server_id;
            }

            std::uint64_t get_term() const override {
                return term;
            }

        protected:
            template <typename T>
            void decode_member(std::vector<char>::iterator begin, std::vector<char>::iterator end, T& member) {
                std::array<char, sizeof(T)> data;
                std::move(begin, end, data.begin());
                member = NetworkUtil::atoh<T>(data);
            }

            template <typename T>
            std::vector<char> encode_member(std::initializer_list<T> members) {
                std::vector<char> data (members.size() * sizeof(T));
                auto cur = data.begin();
                for (auto &m : members) {
                    auto a = NetworkUtil::htoa<T>(m);
                    std::move(a.begin(), a.end(), cur);
                    cur += a.size();
                }
                return data;
            }

            std::uint64_t server_id;
            std::uint64_t term;
        };

        namespace request {
            class RequestVoteReq : public RaftBaseEvent {
            public:
                RequestVoteReq(std::uint64_t server_id, std::uint64_t term) : RaftBaseEvent(server_id, term) {}
                RequestVoteReq(std::vector<char>& data) : RaftBaseEvent(data) {}

                ~RequestVoteReq() {}

                std::uint16_t get_type() const override {
                    return Type::request_vote_req;
                }
            };

            // for leader election stage, this structure is the same as RequestVote call.
            // the AppendEntries call is also used as heartbeat.
            class AppendEntriesReq : public RaftBaseEvent {
            public:
                AppendEntriesReq(std::uint64_t server_id, std::uint64_t term) : RaftBaseEvent(server_id, term) {}
                AppendEntriesReq(std::vector<char>& data) : RaftBaseEvent(data) {}

                ~AppendEntriesReq() {}

                std::uint16_t get_type() const override {
                    return Type::append_entries_req;
                }
                
                bool is_heartbeat() { return true; }
            };
        }

        namespace response {
            class RequestVoteRes : public RaftBaseEvent {
            public:
                RequestVoteRes(std::uint64_t server_id, std::uint64_t term, bool vote_granted) : 
                    RaftBaseEvent(server_id, term), vote_granted(vote_granted) {} 
                
                RequestVoteRes(std::vector<char>& data) : RaftBaseEvent(data) {
                    decode_member(data.begin() + 2 * sizeof(std::uint64_t), data.end(), vote_granted);
                }

                ~RequestVoteRes() {}

                std::uint16_t get_type() const override {
                    return Type::request_vote_res;
                }

                std::vector<char> get_data() override {
                    auto base_data = encode_member({ server_id, term });
                    auto granted_data = encode_member({ vote_granted });
                    return raft::utils::DataUtils::concat(base_data, granted_data);
                }

                bool is_vote_granted() { return vote_granted; }
            
            private:
                bool vote_granted;
            };

            class AppendEntriesRes : public RaftBaseEvent {
            public:
                AppendEntriesRes(std::uint64_t server_id, std::uint64_t term) : RaftBaseEvent(server_id, term) {}
                AppendEntriesRes(std::vector<char>& data) : RaftBaseEvent(data) {}

                ~AppendEntriesRes() {}

                std::uint16_t get_type() const override {
                    return Type::append_entries_res;
                }
            };
        }

        class RaftEventFactory {
        public:
            static std::shared_ptr<RaftEvent> decode(std::vector<char>& v) {
                assert (v.size() >= sizeof(std::uint16_t));

                std::vector<char> type_data (v.begin(), v.begin() + sizeof(std::uint16_t));
                std::uint16_t type = NetworkUtil::vtoh<std::uint16_t>(type_data);

                std::vector<char> data (v.size() - type_data.size());
                std::move(v.begin() + type_data.size(), v.end(), data.begin());

                switch (type) {
                    case Type::request_vote_req:
                        return std::make_shared<request::RequestVoteReq>(data);
                    case Type::request_vote_res:
                        return std::make_shared<response::RequestVoteRes>(data);
                    case Type::append_entries_req:
                        return std::make_shared<request::AppendEntriesReq>(data);
                    case Type::append_entries_res:
                        return std::make_shared<response::AppendEntriesRes>(data);
                    default:
                        throw std::invalid_argument("unsupported type");
                }
            }
        };
    }
}

#endif