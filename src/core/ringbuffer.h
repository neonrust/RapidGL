#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cassert>

#if !defined(NDEBUG)
#include <array>
#include <print>
#endif

// capacity  : how many elements can fit in the buffer
// _size     : how many elements are currently in the buffer
// _head     : raw index where next element will be written
// _tail     : raw index where the oldest existing element is (if _size > 0, else n/a)

// TODO: having both '_tail' and '_size' is redundant; at a pinch one of them can be removed.

// terminology:
//    position : [ 0, _size )       (virtual position seen from the outside)
//    index    : [ 0, _capacity )   (actual subscript index into the buffer)
//    index & position do not point to the same element (they can, but likely aren't).

// example:  capacity = 3
//
//    o   _tail  _head
//    o
//    o
//    -   _size = 0
//
//  > add('A'):
//
//    A   _tail
//    o   _head
//    o
//    -   _size = 1
//
//  > add('B')
//
//    A   _tail
//    B
//    o   _head
//    -   _size = 2
//
//  > add('C'):
//
//    A   _tail  _head
//    B
//    C
//    -   _size = 3


inline size_t stamp()
{
	static auto _stamp = 0u;
	return ++_stamp;
}

template<typename T>
std::string elem_fmt(const T &v) requires std::integral<T> or std::floating_point<T>
{
	return std::to_string(v);
}

template<typename iT, size_t capacity> requires (capacity > 1)
class RingBuffer_std_iterator;

template<typename T, size_t capacity> requires (capacity > 1)
class RingBuffer
{
	// please, don't store const types  (STL containers also forbid it)
	static_assert(not std::is_const_v<T>);

public:
	enum reclaim_mode
	{
		reclaim_tail = 0,
		reclaim_head = 1,
	};

#if !defined(NDEBUG)
	bool trace { false };
#endif

public:
	RingBuffer();

	template<typename iT, size_t icapacity> requires (icapacity > 1)
	friend class RingBuffer_std_iterator;

	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = value_type &;
	using const_reference = value_type const&;

	using iterator = RingBuffer_std_iterator<value_type, capacity>;
	using const_iterator = RingBuffer_std_iterator<const value_type, capacity>;

	//	void set_capacity(size_t capacity);
	// inline size_t capacity() const { return capacity; }

	// add an element at the head
	T &push(const T &elem);
	void push(const std::vector<T> &elems);

	[[nodiscard]] inline size_t size() const { return _size; }
	[[nodiscard]] inline bool empty() const { return _size == 0; }
	[[nodiscard]] inline bool full() const  { return _size == capacity; }

	[[nodiscard]] inline const T &operator [] (size_t position) const throw()
	{
		if(position >= _size)
			throw std::out_of_range("position out of range");

		return _buffer[_position_index(position)];
	}

	[[nodiscard]] inline const T &head() const throw() { assert(_size > 0); return operator [] (_size - 1); }
	[[nodiscard]] inline const T &tail() const throw() { return operator [] (0); }

	inline void clear()
	{
		_tail = 0;
		_head = 0;
		_size = 0;
	}

	// inline mutating_iterator mutating_iterator() { return mutating_iterator(_tail, 0u, *this); }
	[[nodiscard]] inline       iterator  begin()       noexcept { return       iterator(this,    0u); }
	[[nodiscard]] inline       iterator  end()         noexcept { return       iterator(this, _size); }
	[[nodiscard]] inline const_iterator  begin() const noexcept { return const_iterator(this,    0u); }
	[[nodiscard]] inline const_iterator  end()   const noexcept { return const_iterator(this, _size); }
	[[nodiscard]] inline const_iterator cbegin() const noexcept { return begin(); }
	[[nodiscard]] inline const_iterator cend()   const noexcept { return end(); }

#if !defined(NDEBUG)
	void debug_dump(std::string_view title) const;
#endif

	void remove(size_t position, reclaim_mode mode=reclaim_tail) throw();

private:
	inline size_t _position_index(size_t position) const { return (_tail + position) % capacity; }
	inline size_t _next_index(size_t index) const { return (index + 1) % capacity; }
	inline size_t _prev_index(size_t index) const { return (index + capacity - 1) % capacity; }

	// removing an element. if in the middle, copies an element from the head/front
	void _remove_reclaim_head(size_t position);
	// removing an element. if in the middle, copies an element from the tail/back
	void _remove_reclaim_tail(size_t position);

private:
	T _buffer[capacity];

	size_t _tail;
	size_t _head;
	size_t _size;
};

template<typename T, size_t capacity> requires (capacity > 1)
RingBuffer<T, capacity>::RingBuffer() :
	  _tail(0),
	  _head(0),
	  _size(0)
{
}

template<typename T, size_t capacity> requires (capacity > 1)
T &RingBuffer<T, capacity>::push(const T &elem)
{
	const auto at = _head;

	_buffer[at] = elem;

	_head = _next_index(_head);

	if(_size != capacity)   // ring isn't full
		++_size;
	else
	{
		// ring is full; advance tail as well (we just overwrote the oldest element)
		_tail = _next_index(_tail);
	}

#if !defined(NDEBUG)
	//	debug_dump("added");
#endif
	return _buffer[at];
}

template<typename T, size_t capacity> requires (capacity > 1)
void RingBuffer<T, capacity>::push(const std::vector<T> &elems)
{
	auto iter = elems.begin();

	// if we'll overwrite the whole buffer, just copy the last ones that fit
	if(elems.size() > capacity)
		std::advance(iter, elems.size() - capacity);

	// TODO: this could probably be done more efficiently
	//   e.g. computing what the '_head' & '_tail' should be
	//        and just do the necessary memcpy() (max 3 calls)

	for(; iter != elems.end(); ++iter)
		 add(*iter);
}

template<typename T, size_t capacity> requires (capacity > 1)
inline void RingBuffer<T, capacity>::remove(size_t position, reclaim_mode mode) throw()
{
	if(position >= _size)
		throw std::out_of_range("position out of range");

	if(position == 0)                // removing first element; need only move tail forward
		_tail = _next_index(_tail);
	else if(position == _size - 1)   // removing last element; need only move head backwards
		_head = _prev_index(_head);
	else if(mode == reclaim_tail)
		_remove_reclaim_tail(position);
	else
		_remove_reclaim_head(position);

	// in any case, size got smaller
	--_size;
}

template<typename T, size_t capacity> requires (capacity > 1)
void RingBuffer<T, capacity>::_remove_reclaim_head(size_t position)
{
	// removing in the middle; copy the front-most element here (i.e. "frontloaded"), and move head
	_head = _prev_index(_head);

	const auto index = _position_index(position);

	_buffer[index] = _buffer[_head];
}

template<typename T, size_t capacity> requires (capacity > 1)
void RingBuffer<T, capacity>::_remove_reclaim_tail(size_t position)
{
	// removing in the middle; copy the back-most element here (i.e. "backloaded"), and move tail
	const auto index = _position_index(position);

	_buffer[index] = _buffer[_tail];
	_tail = _next_index(_tail);
}

#if !defined(NDEBUG)
template<typename T, size_t capacity> requires (capacity > 1)
void RingBuffer<T, capacity>::debug_dump(std::string_view title) const
{
	std::print("{} \x1b[33;1mrb.dump: \x1b[0;32;1;3m{}\x1b[0;33;1m size: \x1b[m{}\x1b[0;33;1m head: \x1b[0;34;1m[{}]\x1b[33;1m tail: \x1b[0;34;1m[{}]\n", stamp(), title, _size, _head, _tail);
	std::print("\x1b[33;1m  items: \x1b[m{{");
	for(size_t idx = 0; idx < _size; idx++)
	{
		const auto raw_idx = ((idx + _tail) % capacity);
		std::print(" {}\x1b[0;34;1m[{}]\x1b[0;32;1m:\x1b[m{}", idx, raw_idx, elem_fmt(_buffer[raw_idx]));
	}
	std::print("}}\n");
}
#endif

// ------------------------------------------------------------------

template<typename T, size_t capacity> requires (capacity > 1)
class RingBuffer_std_iterator
{
	using rbuf_t = std::conditional_t<std::is_const_v<T>,
									  const RingBuffer<std::remove_const_t<T>, capacity>,
									  RingBuffer<T, capacity>>;

public:
	using value_type        = T;
	using iterator_category = std::bidirectional_iterator_tag;
	using iterator_concept  = iterator_category;  // for ranges
	using difference_type   = std::ptrdiff_t;
	using pointer           = value_type *;
	using reference         = value_type &;

	RingBuffer_std_iterator() = default;
	inline RingBuffer_std_iterator(rbuf_t *rb, size_t position) :
		_position { position },
		_index { (rb->_head + position) % capacity },
		_buffer { rb }
	{
	}
	inline RingBuffer_std_iterator(const RingBuffer_std_iterator &that) :
		_position { that._position },
		_index { that._index },
		_buffer { that._buffer }
	{
	}

	inline RingBuffer_std_iterator &operator = (const RingBuffer_std_iterator &that)
	{
		_position = that._position;
		_index = that._index;
		_buffer = that._buffer;
	}

	RingBuffer_std_iterator &operator ++ ();
	RingBuffer_std_iterator &operator -- ();

	inline RingBuffer_std_iterator operator ++ (int)  // suffix
	{
		auto copy = *this;
		operator ++ ();
		return copy;
	}

	inline RingBuffer_std_iterator operator -- (int)  // suffix
	{
		auto copy = *this;
		operator -- ();
		return copy;
	}

	inline reference operator * () const
	{
		return _buffer->_buffer[_index];
	}

	inline pointer operator -> () const
	{
		return &_buffer->_buffer[_index];
	}

	inline bool operator == (const RingBuffer_std_iterator &that) const
	{
		return _position == that._position and _buffer == that._buffer;
	}

private:
	size_t _position { 0 };
	size_t _index { 0 };
	rbuf_t *_buffer { nullptr };
};

template<typename T, size_t capacity> requires (capacity > 1)
RingBuffer_std_iterator<T, capacity> &RingBuffer_std_iterator<T, capacity>::operator ++ ()
{
	if(_position < _buffer->size())
	{
		++_position;
		_index = _buffer->_next_index(_index);
	}
	return *this;
}

template<typename T, size_t capacity> requires (capacity > 1)
inline RingBuffer_std_iterator<T, capacity> &RingBuffer_std_iterator<T, capacity>::operator -- ()
{
	if(_position > 0)
	{
		--_position;
		_index = _buffer->_prev_index(_index);
	}
	return *this;
}

// ------------------------------------------------------------------
/*
template<class T, size_t capacity> requires (capacity > 1)
class RingBuffer<T, capacity>::mutating_iterator
{
public:
	inline mutating_iterator(size_t index, size_t position, RingBuffer &rb) :
		  _index { index },
		  _position { position },
		  _buffer { rb },
		  _current_removed { false }
	{
//		std::print("\x1b[33;1mrb: iter ctor[\x1b[m{}\x1b[33;1m] @ \x1b[m{}\n", index, position);
	}

	inline operator bool() const
	{
		return valid();
	}

	bool next()
	{
		if(_valid())
		{
			if(not _current_removed)
			{
//				if(_buffer.trace)
//					std::print("\x1b[33;1mrb: ++iter\x1b[m: {}", _index);

				++_position;
				_index = _buffer._next_index(_index);

//				if(_buffer.trace)
//					std::print(" -> {}\n", _index);
			}
//			else
//			{
//				if(_buffer.trace)
//					std::print("\x1b[33;1mrb: ++iter\x1b[m just reset 'removed' flag\n");
//			}
			// in any case, the 'removed' flag is cleared now
			_current_removed = false;
			return _valid();
		}
		return false;
	}

	inline T &current() const
	{
		assert(not _current_removed and _valid());

		T &v = _buffer._buffer[_index];
//		if(_buffer.trace)
//			std::print("\x1b[33;1mrb: *iter[\x1b[m{}\x1b[33;1m]=\x1b[m{}\n", _index, elem_fmt(v));
		return v;
	}

	inline mutating_iterator &operator ++ (int)  // suffix
	{
		next();
		return *this;
	}

	inline T &operator * () const { return current(); }

	void remove_current(RingBuffer<T, capacity>::reclaim_mode mode=RingBuffer<T, capacity>::reclaim_tail)
	{
		// TODO: does this work if mode == 'reclaim_head' ?

		if(not _current_removed and _valid())
		{
#if !defined(NDEBUG)
			if(_position > 0 and _position < _buffer._size - 1)
				_buffer.debug_dump("before removing middle");
#endif

			_buffer.remove(_position, mode);

			if(_position == 0)
			{
				_index = _buffer._tail;
#if !defined(NDEBUG)
				if(_buffer.trace)
				{
					_buffer.debug_dump("removed first");
					std::print("{} rb.mut_iter: _index: {} _position: {}\n", stamp(), _index, _position);
				}
#endif
			}
			else if(_position == _buffer._size - 1) // hm, this compares the updated '_size', shouldn't this be == _size w/o -1 ?
			{
				_index = _buffer._prev_index(_buffer._head);
#if !defined(NDEBUG)
				if(_buffer.trace)
					_buffer.debug_dump("removed last");
				//std::print("{} rb.mut_iter: removed last, _index: {}\n", stamp(), _index);
#endif
			}
			else
			{
				_index = _buffer._next_index(_index);
#if !defined(NDEBUG)
				if(_buffer.trace)
					_buffer.debug_dump("removed middle");
#endif
			}

			_current_removed = true;
		}
	}

	inline bool _valid() const
	{
		return _position < _buffer._size;
	}

	inline bool valid() const
	{
		const bool is_valid = _valid();
#if !defined(NDEBUG)
		if(_buffer.trace)
		{
			std::print("\x1b[33;1mrb.mut_iter.valid: \x1b[m{}\x1b[33;1m / \x1b[m{}", _position, _buffer._size);
			if(not is_valid)
				std::print("  \x1b[31;1mINVALID\x1b[m");
			std::print("\n");
		}
#endif
		return is_valid;
	}


private:
	size_t _index;
	size_t _position;
	RingBuffer &_buffer;
	bool _current_removed;
};
*/
