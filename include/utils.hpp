#ifndef utils_hpp
#define utils_hpp

#include <vector>
#include <chrono>

namespace raft {
    namespace utils {
        class DataUtils {
        public:
            static std::vector<char> concat(std::vector<char> a, std::vector<char> b) {
                std::vector<char> c (a.size() + b.size());
                std::move(a.begin(), a.end(), c.begin());
                std::move(b.begin(), b.end(), c.begin() + a.size());
                return c;
            }
        };

        class TimeUtils {
        public:
            static uint64_t now_ms() {
                auto now = std::chrono::system_clock::now();
                auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
                return static_cast<uint64_t>(now_ms.time_since_epoch().count());
            }
        };
    }
}

#endif
