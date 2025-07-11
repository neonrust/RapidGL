#pragma once
#include <atomic>

// references:
//   https://chromium.googlesource.com/chromium/chromium/+/refs/heads/main/base/stack_container.h
//   https://github.com/JamesBoer/Jinx/blob/master/Include/Jinx.hpp

// let's not get overboard
static constexpr size_t max_stack_allocation = 65536;

template<typename T, size_t capacity> requires (sizeof(T[capacity]) <= max_stack_allocation)
class stack_allocator : public std::allocator<T>
{
public:
	using value_type   = typename std::allocator<T>::value_type;
	using size_type    = typename std::allocator<T>::size_type;
	using alloc_traits = std::allocator_traits<std::allocator<T>>;

	template<typename U>
	struct rebind
	{
		using other = stack_allocator<U, capacity>;
	};

	stack_allocator() : std::allocator<T>()
	{
	}
	~stack_allocator()
	{
		if(_in_use)
			deallocate(_buffer, capacity);
	}

	constexpr value_type *allocate(size_type n, const void *hint=nullptr)
	{
		// don't keep track of allocations; it's allocated, or not
		if(n <= capacity and not _in_use)
		{
			_in_use = true;
			return _buffer;
		}
		// too large, use the base allocator
		return alloc_traits::allocate(*this, n);
	}

	constexpr void deallocate(value_type *p, size_type n)
	{
		if(p == _buffer)
			_in_use = false;
		else
			std::allocator<T>::deallocate(p, n);
	}

private:
	alignas(sizeof(void *)) T _buffer[capacity];
	bool _in_use { false };
};


#include <vector>

template<typename T, size_t capacity=16>
class stack_vector : public std::vector<T, stack_allocator<T, capacity>>
{
public:
	stack_vector() : std::vector<T, stack_allocator<T, capacity>>()
	{
		// no harm in reserving up front; the instance is already using the stack space anyway
		this->reserve(capacity);
	}
};