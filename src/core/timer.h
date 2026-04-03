#pragma once
#include <chrono>

namespace RGL
{
    class Timer final
    {
    public:
        /**
         * @brief Returns current time in seconds.
         * @return Time in seconds.
         */
        static double getTime()
        {
			auto now = std::chrono::steady_clock::now();
			static const auto start_time = now;

			return double(std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time).count()) / double(SECOND);
        }

    private:
		static const long long SECOND = 1000000000LL;
    };
}

