#pragma once

#include "container_types.h"

#include <algorithm>
#include <cmath>  // std::floor()
#include <print>

// Cubic Bézier curve
//
// TODO: simplify equation
//   B(t) = p0*(1 - t)^3
//          + p1 * 3 * t * (1 - t)^2
//          + p2 * 3 * t^2 * (1 - t)
//          + p3 * t^3
//
// 1st derivative:  (simplified)
//   B'(t) = 3 * (1 - t)^2 * (p1 - p0)
//           + 6 * t * (1 - t) * (p2 - p1)
//           + 3 * t * t * (p3 - p2)

// ParamT is the parameter(izing) type.
//   Needs to be "arithmetic"
// T is the value type.
//   Can be anything, but needs to support: T*ParamT, T*T and T + T

// https://pomax.github.io/bezierinfo

namespace RGL
{

template<typename T, typename ParamT=float>
class BézierSpline
{
public:
	static constexpr auto epsilon { std::numeric_limits<ParamT>::epsilon() * 5 };  // TODO: higher precision if ParamT is (long) double

	struct control_point
	{
		T point;
		T incoming;
		T outgoing;
	};

	using StorageT = small_vec<control_point, 16>;

public:
	using const_iterator = typename StorageT::const_iterator;
	using iterator = typename StorageT::iterator;
	using size_t = typename StorageT::size_type;

public:
	void reserve(size_t num_points);
	inline void add(T point) { add(point, point, point); }
	void add(T point, T incoming, T outgoing);

	inline size_t size() const { return _points.size(); }
	inline size_t num_curves() const { return empty()? 0: size() - 1; }
	inline bool empty() const { return _points.empty(); }
	inline void clear() { _points.clear(); }

	T value(ParamT t) const;
	T derivative(ParamT t) const;   // "speed"
	T derivative2nd(ParamT t) const; // "acceleration"

	inline control_point front() const { return _points.front(); }
	inline control_point back() const { return _points.back(); }

	inline const_iterator begin() const { return _points.cbegin(); }
	inline const_iterator end() const { return _points.cend(); }

private:
	ParamT curve_params(ParamT t, size_t &cp0_idx) const;

private:
	StorageT _points;
};

// ----------------------------------------------------------------------------

template<typename T, typename ParamT>
inline void BézierSpline<T, ParamT>::reserve(size_t num_points)
{
	_points.reserve(num_points);
}

// ----------------------------------------------------------------------------

template<typename T, typename ParamT>
void BézierSpline<T, ParamT>::add(T point, T incoming, T outgoing)
{
	if(_points.capacity() == 0)
		_points.reserve(8);

	_points.push_back({ point, incoming, outgoing });
}

template<typename T, typename ParamT>
T BézierSpline<T, ParamT>::value(ParamT t) const
{
	if(empty())
		return T{};
	else if(size() == 1 or t <= epsilon)
		return _points.front().point;
	else if(t + epsilon >= 1)
		return _points.back().point;

#if !defined(NDEBUG)
	if(t < 0 or t > 1)
		std::println(stderr, "BézierSpline::value() t OoR: {}", t);
#endif

	t = std::max(0.f, std::min(1.f, t));

	size_t cp0i;
	t = curve_params(t, cp0i);

	const auto cp1i = cp0i + 1;

	const auto &p0 = _points[cp0i].point;
	const auto &p1 = _points[cp0i].outgoing;
	const auto &p2 = _points[cp1i].incoming;
	const auto &p3 = _points[cp1i].point;

	//  p = p0*(1 - t)^3
	//      + p1*3*t*(1 - t)^2
	//      + p2*3*t*t*(1-t)
	//      + p3*t*t*t
	const auto t2 = t*t;
	const auto t3 = t2*t;
	const auto tinv = 1 - t;
	const auto tinv2 = tinv*tinv;
	const auto tinv3 = tinv2*tinv;

	const auto A = p0*tinv3;
	const auto B = p1*3*t*tinv2;
	const auto C = p2*3*t2*tinv;
	const auto D = p3*t3;

	return A + B + C + D;
}

template<typename T, typename ParamT>
inline T BézierSpline<T, ParamT>::derivative(ParamT t) const
{
	if(empty())
		return T{};
	else if(size() == 1 or t <= epsilon)
		return _points.front().outgoing;
	else if(t + epsilon >= 1)
		return _points.back().incoming;

#if !defined(NDEBUG)
	if(t < 0 or t > 1)
		std::println(stderr, "BézierSpline::value() t OoR: {}", t);
#endif

	t = std::max(0.f, std::min(1.f, t));

	size_t cp0i;
	t = curve_params(t, cp0i);

	const auto cp1i = cp0i + 1;

	const auto &p0 = _points[cp0i].point;
	const auto &p1 = _points[cp0i].outgoing;
	const auto &p2 = _points[cp1i].incoming;
	const auto &p3 = _points[cp1i].point;

	//   B'(t) = 3 * (1 - t)^2 * (p1 - p0)
	//           + 6 * t * (1 - t) * (p2 - p1)
	//           + 3 * t * t * (p3 - p2)
	// https://www.symbolab.com/solver
	// 3 * (1 - t)^2 * (B - A) + 6 * t * (1 - t) * (C - B) + 3 * t^2 * (D - C)

	const auto t2 = t*t;
	const auto tinv = 1 - t;
	const auto tinv2 = tinv*tinv;

	const auto A = (p1 - p0) * 3 * tinv2;
	const auto B = (p2 - p1) * 6 * t * tinv;
	const auto C = (p3 - p2) * 3 * t2;

	return A + B + C;
}

// ----------------------------------------------------------------------------

template<typename T, typename ParamT>
inline T BézierSpline<T, ParamT>::derivative2nd(ParamT t) const
{
	if(empty())
		return T{};
	else if(size() == 1 or t <= epsilon)
		return _points.front().outgoing;
	else if(t + epsilon >= 1)
		return _points.back().incoming;

#if !defined(NDEBUG)
	if(t < 0 or t > 1)
		std::println(stderr, "BézierSpline::value() t OoR: {}", t);
#endif

	t = std::max(0.f, std::min(1.f, t));

	size_t cp0i;
	t = curve_params(t, cp0i);

	const auto cp1i = cp0i + 1;

	const auto &p0 = _points[cp0i].point;
	const auto &p1 = _points[cp0i].outgoing;
	const auto &p2 = _points[cp1i].incoming;
	const auto &p3 = _points[cp1i].point;

	// B''(t) = 6(1 - t)(P2 - 2P1 + P0) + 6t(P3 - 2P2 + P1)

	const auto A = (p0 - p1*2 + p2) * 6 * (1 - t);
	const auto B = (p1 - p2*2 + p3) * 6 * t;

	return A + B;
}

// ----------------------------------------------------------------------------

// ============================================================================
// Name:    curve_params
// ----------------------------------------------------------------------------
// Description:  Compute index corresponding to the start of a curve
//               based on 't'.
// ----------------------------------------------------------------------------
// IN parameters:   t        - spline parametrization
//                             0 = start of spline, 1 = end of spline
// ----------------------------------------------------------------------------
// OUT parameters:  cp0_idx  - container index to control point 0
//   implied OUT:   container index to cp1     (cp0_idx + 1)
// ----------------------------------------------------------------------------
// Returns:   The corresponding fraction of 't' that lies between cp0 and cp1,
//            0 corresponding to cp0 and 1 to cp1.
// ============================================================================
template<typename T, typename ParamT>
ParamT BézierSpline<T, ParamT>::curve_params(ParamT t, size_t &cp0_idx) const
{
	// this is only called when there are atleast 2 points (i.e. one curve)

	const auto num_points = size();

	const auto index_f = t * ParamT(num_points - 1);
	const auto preceeding_cp = std::floor(index_f);
	cp0_idx = size_t(preceeding_cp);

	return index_f - preceeding_cp;
}

} // RGL