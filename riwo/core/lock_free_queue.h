// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_LOCK_FREE_QUEUE_H
#define RIWO_CORE_LOCK_FREE_QUEUE_H

#include <riwo/core/global.h>

namespace riwo
{

template <concepts::copy_or_move_constructible T, typename Derived>
class RIWO_CORE_TAPI lock_free_queue_base
{
	using element_t = T;
	using derived_t = crtp_derived_t<Derived,lock_free_queue_base>;

public:
	void force_enqueue(element_t &&data);
	void force_enqueue(const element_t &data) requires
		concepts::copy_constructible<element_t>;

	// Return number of nodes that were forced to be removed from the queue.
	template <typename...Args>
	size_t force_emplace(Args&&...args) requires
		concepts::constructible<element_t,Args...>;
};

enum class queue_type {
	linked, circular
};
template <concepts::copy_or_move_constructible T,
		  queue_type DS = queue_type::circular,
		  size_t N = 0>
class RIWO_CORE_TAPI lock_free_queue;

// linked list queue
template <concepts::copy_or_move_constructible T, size_t N>
class RIWO_CORE_TAPI lock_free_queue<T, queue_type::linked, N> :
	public lock_free_queue_base<T,lock_free_queue<T, queue_type::linked, N>>
{
	RIWO_DISABLE_COPY(lock_free_queue)

public:
	using element_t = T;
	static constexpr size_t capacity_v = N;

	[[nodiscard]] static consteval size_t capacity()
		noexcept requires (capacity_v > 0);

public:
	explicit lock_free_queue(size_t capacity)
		requires (capacity_v == 0);

	lock_free_queue();
	~lock_free_queue(); // unsafe

	lock_free_queue(lock_free_queue &&other) noexcept; // unsafe
	lock_free_queue &operator=(lock_free_queue &&other) noexcept; // unsafe

public: // safe
	// Construction failures leave the queue usable and do not change size().
	bool enqueue(element_t &&data);
	bool enqueue(const element_t &data) requires
		concepts::copy_constructible<element_t>;

	template <typename...Args>
	bool emplace(Args&&...args) requires
		concepts::constructible<element_t,Args...>;

	// If moving the returned element throws, that element is removed and the
	// queue remains usable (basic exception guarantee).
	optional<element_t> dequeue();
	bool dequeue(element_t &data);

public:
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] bool full() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

public:
	[[nodiscard]] size_t capacity() const noexcept
		requires (capacity_v == 0);

	void set_capacity(size_t size)
		requires (capacity_v == 0);

private:
	class impl;
	impl *m_impl;
};

// circular queue
template <concepts::copy_or_move_constructible T, size_t N>
class RIWO_CORE_TAPI lock_free_queue<T, queue_type::circular, N> :
	public lock_free_queue_base<T,lock_free_queue<T, queue_type::circular, N>>
{
	RIWO_DISABLE_COPY(lock_free_queue)

public:
	using element_t = T;
	static constexpr size_t capacity_v = N;

	[[nodiscard]] static consteval size_t capacity()
		noexcept requires (capacity_v > 0);

public:
	explicit lock_free_queue(size_t capacity)
		requires (capacity_v == 0);

	lock_free_queue();
	~lock_free_queue(); // unsafe

	lock_free_queue(lock_free_queue &&other) noexcept; // unsafe
	lock_free_queue &operator=(lock_free_queue &&other) noexcept; // unsafe

public: // safe
	// Construction failures leave the queue usable and do not change size().
	bool enqueue(element_t &&data);
	bool enqueue(const element_t &data) requires
		concepts::copy_constructible<element_t>;

	template <typename...Args>
	bool emplace(Args&&...args) requires
		concepts::constructible<element_t,Args...>;

	// If moving the returned element throws, that element is removed and the
	// queue remains usable (basic exception guarantee).
	optional<element_t> dequeue();
	bool dequeue(element_t &data);

public:
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] bool full() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

public:
	[[nodiscard]] size_t capacity() const noexcept
		requires (capacity_v == 0);

	// Shrinking changes the logical limit only. Existing elements and the
	// high-water-mark ring storage are retained.
	void set_capacity(size_t size) requires (capacity_v == 0);

	// Reclaim drained blocks and, when the logical target is at most half of
	// the current block and no earlier generation is pending, route future
	// writes to a smaller block. Safe during queue use and never waits for an
	// active operation; protected storage is reclaimed by a later call.
	// Returns true when storage was replaced, retired, or reclaimed.
	bool compact() requires (capacity_v == 0);

private:
	class impl;
	impl *m_impl;
};

template <concepts::copy_or_move_constructible T, size_t N = 0>
using linked_lock_free_queue = lock_free_queue<T, queue_type::linked, N>;

template <concepts::copy_or_move_constructible T, size_t N = 0>
using circular_lock_free_queue = lock_free_queue<T, queue_type::circular, N>;

} //namespace riwo
#include <riwo/core/detail/lock_free_queue.h>


#endif //RIWO_CORE_LOCK_FREE_QUEUE_H
