#pragma once

// #include <hive>
#include <functional>
#include <cstddef>
#include <deque>
#include <vector>

namespace RGL
{

template<typename EventT>
class EventQueue
{
public:
	EventQueue(size_t reserve);

	void push(const EventT event, bool replace_equivalent=false);

	size_t process(std::function<bool(const EventT &)> handler);

	// void remove(EventID id);
	size_t remove(std::function<bool(const EventT &)> predicate);

private:
	std::deque<EventT> _queue;
};

template<typename EventT>
inline EventQueue<EventT>::EventQueue(size_t reserve)
{
	_queue.reserve(std::max(reserve, 32lu));
}

template<typename EventT>
inline void EventQueue<EventT>::push(const EventT event, bool replace_equivalent)
{
	// TODO: `replace_equivalent`
	_queue.push_back(event);
}

template<typename EventT>
inline size_t EventQueue<EventT>::process(std::function<bool (const EventT &)> handler)
{
	if(_queue.empty())
		return 0;

	const auto start = _queue.begin();
	auto iter = start;
	while(iter != _queue.end())
	{
		const auto &event = *iter;
		++iter;

		if(not handler(event))
			break;
	}

	if(iter != start)
	{
		if(iter == _queue.end())
			_queue.clear();
		else
		{
			// remove all processed events; 'iter' points to the last processed event
			_queue.erase(start, ++iter);
		}
	}
}

template<typename EventT>
inline size_t EventQueue<EventT>::remove(std::function<bool (const EventT &)> predicate)
{
	static std::vector<typename decltype(_queue)::const_iterator> to_remove;
	to_remove.reserve(8);
	to_remove.clear();

	for(auto iter = _queue.begin(); iter != _queue.end(); ++iter)
	{
		if(predicate(*iter))
			to_remove(iter);
	}

	for(auto riter = to_remove.rbegin(); riter != to_remove.rend(); ++riter)
		_queue.erase(riter);

	return to_remove.size();
}

} // RGL