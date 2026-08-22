#pragma once

#include <cstdint>
#include <chrono>
#include <type_traits>

namespace RGL
{

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::nanoseconds;

using seconds_f = std::chrono::duration<float, std::ratio<1>>;

template <typename T>
struct is_duration : std::false_type {};

template <typename Rep, typename Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <typename T>
concept DurationT = is_duration<std::remove_cvref_t<T>>::value;

struct Time
{
	inline Time(TimePoint now) : start_time(now) { ; }

	void update(Duration delta_time);

	inline bool is_paused() const { return _paused; }
	template<DurationT T=Duration>
	T elapsed() const;

private:
	TimePoint _game_time;
	Duration  _frame_time { std::chrono::microseconds(0) };

	uint64_t _frame_count;

	float    _fps;
	float    _fps_min;
	float    _fps_max;

	template<float avg_time>
	struct AvgFps
	{
		void update([[maybe_unused]] TimePoint now) {

			++num_frames;
		};
		float value;
		TimePoint window_start;
		uint32_t num_frames;
	};
	float    _fps_avg_1; // fps averaged over 1 second
	float    _fps_avg_1_start;
	float    _fps_avg_1_next;
	uint32_t _fps_avg_1_frames;

	float    _fps_avg_10; // fps averaged over 10 seconds
	float    _fps_avg_10_start;
	float    _fps_avg_10_next;
	uint32_t _fps_avg_10_frames;

	bool _paused;
	TimePoint _pause_time;
	Duration  _pause_accumulated_time;

public:
	const TimePoint start_time;

};

template<DurationT T>
inline T Time::elapsed() const
{
	return std::chrono::duration_cast<T>(_game_time - start_time);
}

} // RGL