// Copyright 2013-2025 Daniel Parker
// Distributed under Boost license

#ifndef MOCK_STATEFUL_ALLOCATOR
#define MOCK_STATEFUL_ALLOCATOR

#include <algorithm>
#include <iostream>
#include <memory>
#include <list>
#include <type_traits>
#include <utility>

template <typename T>
class mock_stateful_allocator
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    std::allocator<T> impl_;
    int id_;

    mock_stateful_allocator() = delete;

    mock_stateful_allocator(int id) noexcept
        : id_(id)
    {
    }
    mock_stateful_allocator(const mock_stateful_allocator& other) noexcept
        : id_(other.id_)
    {
    }
    template< class U >
    mock_stateful_allocator(const mock_stateful_allocator<U>& other) noexcept
        : id_(other.id_)
    {
    }

    mock_stateful_allocator& operator = (const mock_stateful_allocator& other) = delete;

    mock_stateful_allocator& operator = (mock_stateful_allocator&& other) noexcept {
        id_ = other.id_;
        other.id_ = -1;
        return *this;
    }

    T* allocate(size_type n) 
    {
        return impl_.allocate(n);
    }

    void deallocate(T* ptr, size_type n) noexcept 
    {
        impl_.deallocate(ptr, n);
    }

    template <typename U>
    struct rebind
    {
        using other = mock_stateful_allocator<U>;
    };

    friend bool operator==(const mock_stateful_allocator& lhs, const mock_stateful_allocator& rhs) noexcept
    {
        return lhs.id_ == rhs.id_;
    }

    friend bool operator!=(const mock_stateful_allocator& lhs, const mock_stateful_allocator& rhs) noexcept
    {
        return lhs.id_ != rhs.id_;
    }
};

#endif
