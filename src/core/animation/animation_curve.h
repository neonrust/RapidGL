#pragma once

#include "game_time.h"
#include "bezier_spline.h"
#include <memory>


namespace RGL::anim
{


template<typename ValueT=float, typename TimeT=seconds_f, typename Precision=float>
struct timeline_point
{
	TimeT time;
	ValueT value;

	[[nodiscard]] inline timeline_point operator * (Precision f) const {
		return { TimeT(Precision(time.count())*f), ValueT(value*f) };
	}
	[[nodiscard]] inline timeline_point operator * (const timeline_point &that) const {
		return { TimeT(time.count()*that.time.count()), value*that.value };
	}
	[[nodiscard]] inline timeline_point operator + (const timeline_point &that) const {
		return { time + that.time, value + that.value };
	}
	[[nodiscard]] inline timeline_point operator - (const timeline_point &that) const {
		return { time - that.time, value - that.value};
	}
};

template<typename ValueT=float, typename TimeT=seconds_f, typename Precision=float>
class animation_curve
{
	using point_t = timeline_point<ValueT, TimeT, Precision>;
	using curve_t = BézierSpline<point_t, Precision>;

public:
	static constexpr TimeT epsilon_time { std::numeric_limits<Precision>::epsilon() * 3 };

public:
	void add(const point_t &point, const point_t &incoming, const point_t &outgoing);

	[[nodiscard]] uint_fast32_t count() const;
	void clear();
	[[nodiscard]] inline bool empty() const { return _curve.empty(); }

	[[nodiscard]] inline const curve_t &curve() const { return _curve; }

	[[nodiscard]] TimeT start_time() const; // really should be at 0
	[[nodiscard]] TimeT end_time() const;
	[[nodiscard]] TimeT duration() const; // end - start

private:
	curve_t _curve;
};

template<typename ValueT=float, typename TimeT=seconds_f, typename Precision=float>
using curve_ref = std::shared_ptr<animation_curve<ValueT, TimeT, Precision>>;
template<typename ValueT=float, typename TimeT=seconds_f, typename Precision=float>
using curve_cref = std::shared_ptr<const animation_curve<ValueT, TimeT, Precision>>;

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision>
void animation_curve<ValueT, TimeT, Precision>::add(const point_t &point, const point_t &incoming, const point_t &outgoing)
{
	// verify some sanity that animation curves require

	if(point.time.count() < 0)
		throw std::out_of_range("'time' may not be negative");

	if(empty() and point.time > TimeT(0))
	{
		// std::println(stderr, "animation_curve: first point is not at T = 0");
		// TODO: insert a fake point at zero?
		// add({ 0, point.value }, { 0, point.value }, { point.time/4, point.value });
	}

	// TODO: this should allow adding points in the middle;
	//   the new point is then inserted at the correct place
	if(not empty() and point.time <= curve().back().point.time)
		throw std::out_of_range("points must have increasing 'time'");

	if(incoming.time > point.time or outgoing.time < point.time)
		throw std::out_of_range("incoming/outgoing 'time' value can't overlap with control point's 'time'");
	if(point.time - incoming.time < epsilon_time and std::abs(incoming.value - point.value) > 1e-4)
		throw std::out_of_range("control point incoming slope is too steep");
	if(outgoing.time - point.time < epsilon_time and std::abs(outgoing.value - point.value) > 1e-4)
		throw std::out_of_range("control point outgoing slope is too steep");

	if(_curve.capacity() == 0)
		_curve.reserve(4);
	_curve.add(point, incoming, outgoing);
}


template<typename ValueT, typename TimeT, typename Precision>
uint_fast32_t animation_curve<ValueT, TimeT, Precision>::count() const
{
	return _curve.size();
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision>
inline void animation_curve<ValueT, TimeT, Precision>::clear()
{
	_curve.clear();
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision>
inline TimeT animation_curve<ValueT, TimeT, Precision>::start_time() const
{
	if(empty())
		return TimeT(0);

	// NOTE: this should always be 0

	return _curve.front().point.time;
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision>
inline TimeT animation_curve<ValueT, TimeT, Precision>::end_time() const
{
	if(empty())
		return TimeT(0);

	return _curve.back().point.time;
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision>
TimeT animation_curve<ValueT, TimeT, Precision>::duration() const
{
	if(empty())
		return TimeT(0);

	return end_time() - start_time();
}

// ----------------------------------------------------------------------------

template<typename ValueT=float, typename TimeT=seconds_f, typename Precision=float, typename CurveT=curve_cref<ValueT, TimeT, Precision>>
class curve_sampler
{
	using point_t = timeline_point<ValueT, TimeT, Precision>;
	using anim_curve = CurveT;

public:
	curve_sampler(const anim_curve &curve);

	[[nodiscard]] inline ValueT value_at(TimeT t) const;
	[[nodiscard]] point_t value_at(TimeT t, uint_fast32_t &hint) const;

	uint_fast32_t tesselate(float density=10.f);
	[[nodiscard]] uint_fast32_t tesselation_samples() const;
	[[nodiscard]] inline const std::vector<point_t> &tesselated() const { return _tesselated; }
	[[nodiscard]] inline bool has_tesselation() const { return not _tesselated.empty(); }

	void reset_hint();

	inline const anim_curve &curve() const { return _curve; }

private:
	anim_curve _curve;
	std::vector<point_t> _tesselated;
	uint32_t _sample_hint { 0 };
};

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision, typename CurveT>
inline curve_sampler<ValueT, TimeT, Precision, CurveT>::curve_sampler(const anim_curve &curve) :
	_curve(curve)
{
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision, typename CurveT>
inline ValueT curve_sampler<ValueT, TimeT, Precision, CurveT>::value_at(TimeT t) const
{
	return value_at(t, _sample_hint).value;
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision, typename CurveT>
curve_sampler<ValueT, TimeT, Precision, CurveT>::point_t curve_sampler<ValueT, TimeT, Precision, CurveT>::value_at(TimeT t, uint_fast32_t &hint) const
{
	assert(not _tesselated.empty());

	// see: https://pomax.github.io/bezierinfo/#yforx
	//      shows how to "find value by t" wihtout tesselation.
	//      it "looks" slower, though ;)

	if(hint > _tesselated.size()) // hint points outside; assume tesselate() was called (with higher density)
		hint = 0;

	auto start = _tesselated.begin() + hint;
	auto end = _tesselated.end();

	// TODO: binary search (lower_bound) is actually not a very efficient choise:
	//   the desired sample will *always* be at the very beginning of the range.
	//   at the very least, 'end' hould not the end of the entire array.
	//   but, at a moderate amount of samples (say < 100), this will still be quite fast.
	//   tesselate() could also generate some simple BSP for the array, to quickly know roughly
	//   where to start the search.

	// binary search for the first value *larger* than 't'
	auto found = std::lower_bound(start, end, t, [](const auto &A, TimeT t) {
		return A.time < t;
	});
	if(found == _tesselated.end())
	{
		hint = 0;
		return _tesselated.back();
	}
	if(found == _tesselated.begin())
		// the code below relies on 'found' not being the same as 'begin'
		return _tesselated.front();

	const auto index = found - _tesselated.begin();

	// next time we can start the search here
	hint = index;

	const auto p0 = *(found - 1);
	const auto p1 = *found;

	// lerp between 'p0' and 'p1' using 't' (which lies between those points)
	const auto p_interval = p1.time - p0.time;
	const auto fraction = Precision((t - p0.time).count())/Precision(p_interval.count());
	return p0 - (p0 - p1)*fraction;
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision, typename CurveT>
inline uint_fast32_t curve_sampler<ValueT, TimeT, Precision, CurveT>::tesselation_samples() const
{
	return _tesselated.size();
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision, typename CurveT>
inline void curve_sampler<ValueT, TimeT, Precision, CurveT>::reset_hint()
{
	_sample_hint = 0;
}

// ----------------------------------------------------------------------------

template<typename ValueT, typename TimeT, typename Precision, typename CurveT>
uint_fast32_t curve_sampler<ValueT, TimeT, Precision, CurveT>::tesselate(float density)
{
	if(_curve->count() < 2)
	{
		if(_curve->count() == 1)
		{
			_tesselated.push_back(_curve->curve().back().point);
			return 1;
		}

		return 0;
	}

	_tesselated.reserve(256); // can't predict how many there'll be
	_tesselated.clear();

	const float step_size { 1.f/(Precision(_curve->count())*density) };

	float t { 0 };
	while(true)
	{
		auto p = _curve->curve().value(t);
		_tesselated.push_back(p);

		// scale point density with length of derivative2nd(t)  (linearly?)
		const auto d2 = _curve->curve().derivative2nd(t);
		const auto d2_time = duration_cast<seconds_f>(d2.time).count();
		auto d2len = 1 + std::sqrt(d2_time*d2_time + d2.value*d2.value);
		const auto step = step_size/d2len;
		t += step;

		if(t + anim_curve::element_type::epsilon_time.count() >= 1)
		{
			_tesselated.push_back(_curve->curve().value(1));
			break;
		}
	}

	return _tesselated.size();
}

} // RGL::anim