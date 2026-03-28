#pragma once

#include <functional>
#include <chrono>

namespace RGL
{

template<typename T>
class ScopedExec
{
public:
	using Func = std::function<void(T &obj)>;
public:
	ScopedExec(Func enter, Func exit);
	~ScopedExec();

private:
	T _obj;
};


class ScopedTimer //: public ScopedExec<Timer>
{
public:
	using ClosureFunc = std::function<void(std::chrono::nanoseconds)>;
public:
	ScopedTimer(ClosureFunc closure={});
	~ScopedTimer();

private:
	ClosureFunc _closure;
	std::chrono::steady_clock::time_point _T0;
};

inline ScopedTimer::ScopedTimer(ClosureFunc closure) :
	_closure(closure)
{
	_T0 = std::chrono::steady_clock::now();
}

inline ScopedTimer::~ScopedTimer()
{
	const auto T1 = std::chrono::steady_clock::now();
	if(_closure)
		_closure(T1 - _T0);
}

} // RGL