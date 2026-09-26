// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_LOCK_FREE_QUEUE_H
#define RIWO_CORE_DETAIL_LOCK_FREE_QUEUE_H

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4324)
#endif //_MSC_VER

namespace riwo
{

template <concepts::copy_or_move_constructible T, typename Derived>
void lock_free_queue_base<T,Derived>::force_enqueue(element_t &&data)
{
	force_emplace(std::move(data));
}

template <concepts::copy_or_move_constructible T, typename Derived>
void lock_free_queue_base<T,Derived>::force_enqueue(const element_t &data)
	requires concepts::copy_constructible<element_t>
{
	force_emplace(data);
}

template <concepts::copy_or_move_constructible T, typename Derived>
template <typename...Args>
size_t lock_free_queue_base<T,Derived>::force_emplace(Args&&...args) requires
	concepts::constructible<element_t,Args...>
{
	size_t sum = 0;
	auto self = static_cast<Derived*>(this);
	for(;;)
	{
		if( self->full() )
		{
			// Another thread may empty a full queue between full() and
			// dequeue().  Count only values this call actually evicted.
			if( self->dequeue() )
				sum++;
		}
		if( self->emplace(std::forward<Args>(args)...) )
			break;
	}
	return sum;
}

namespace detail
{

template <concepts::copy_or_move_constructible, queue_type, size_t>
class lock_free_queue_impl;

template <concepts::copy_or_move_constructible T, size_t N> requires (N > 0)
class RIWO_CORE_TAPI lock_free_queue_impl<T, queue_type::linked, N>
{
RIWO_DISABLE_COPY_MOVE(lock_free_queue_impl)
public: lock_free_queue_impl() = default;
};

template <concepts::copy_or_move_constructible T>
class RIWO_CORE_TAPI lock_free_queue_impl<T, queue_type::linked, 0>
{
	RIWO_DISABLE_COPY_MOVE(lock_free_queue_impl)

public:
	explicit lock_free_queue_impl
	(size_t capacity = std::numeric_limits<size_t>::max()) {
		m_capacity = capacity > 0 ? capacity : std::numeric_limits<size_t>::max();
	}
	std::atomic_size_t m_capacity {};
};

} //namespace detail

template <concepts::copy_or_move_constructible T, size_t N>
class RIWO_CORE_TAPI lock_free_queue<T,queue_type::linked,N>::impl :
	public detail::lock_free_queue_impl<T,queue_type::linked,N>
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	struct node
	{
		node() = default;

		template <typename...Args>
		explicit node(std::in_place_t, Args&&...args) {
			data.emplace(std::forward<Args>(args)...);
		}
		optional<element_t> data {};
		std::atomic<node*> next {nullptr};
		node *retired_next = nullptr;
	};

	struct hazard_record
	{
		std::atomic_bool active {false};
		std::atomic<node*> pointers[2] {};
		hazard_record *next = nullptr;

		node *retired = nullptr;
		node *available = nullptr;
		size_t retired_count = 0;
	};

	class hazard_guard
	{
		RIWO_DISABLE_COPY_MOVE(hazard_guard)

	public:
		explicit hazard_guard(impl *owner, std::atomic<hazard_record*> &hint) :
			m_owner(owner), m_record(owner->acquire_hazard_record(hint)) {}

		~hazard_guard()
		{
			clear(0);
			clear(1);
			m_record->active.store(false, std::memory_order_release);
		}

		[[nodiscard]] node *protect(size_t index, const std::atomic<node*> &source)
		{
			node *protected_node = nullptr;
			do {
				protected_node = source.load(std::memory_order_acquire);
				m_record->pointers[index].store(protected_node, std::memory_order_release);
			}
			while( protected_node != source.load(std::memory_order_acquire) );
			return protected_node;
		}

		void clear(size_t index) noexcept {
			m_record->pointers[index].store(nullptr, std::memory_order_release);
		}

		void retire(node *retired_node) {
			m_owner->retire(m_record, retired_node);
		}

		template <typename...Args>
		[[nodiscard]] node *make_node(Args&&...args) {
			return m_owner->make_node(m_record, std::forward<Args>(args)...);
		}

	private:
		impl *m_owner;
		hazard_record *m_record;
	};

public:
	explicit impl(size_t capacity = std::numeric_limits<size_t>::max())
		requires (capacity_v == 0) :
		detail::lock_free_queue_impl<T,queue_type::linked,N>(capacity)
	{
		auto dummy = new node();
		m_head.store(dummy, std::memory_order_release);
		m_tail.store(dummy, std::memory_order_release);
	}

	impl() requires (capacity_v > 0)
	{
		auto dummy = new node();
		m_head.store(dummy, std::memory_order_release);
		m_tail.store(dummy, std::memory_order_release);
	}

	~impl()
	{
		auto current = m_head.load(std::memory_order_relaxed);
		while( current )
		{
			auto next = current->next.load(std::memory_order_relaxed);
			delete current;
			current = next;
		}
		auto record = m_hazard_records.load(std::memory_order_relaxed);
		while( record )
		{
			current = record->retired;
			while( current )
			{
				auto next = current->retired_next;
				delete current;
				current = next;
			}
			current = record->available;
			while( current )
			{
				auto next = current->retired_next;
				delete current;
				current = next;
			}
			auto next = record->next;
			delete record;
			record = next;
		}
		current = m_recycled.exchange(nullptr, std::memory_order_relaxed);
		while( current )
		{
			auto next = current->retired_next;
			delete current;
			current = next;
		}
	}

private:
	[[nodiscard]] hazard_record *acquire_hazard_record(std::atomic<hazard_record*> &hint)
	{
		if( auto record = hint.load(std::memory_order_acquire) )
		{
			if( bool expected = false;
				record->active.compare_exchange_strong(expected, true,
					std::memory_order_acq_rel, std::memory_order_relaxed) )
				return record;
		}
		for(auto record=m_hazard_records.load(std::memory_order_acquire);
			record; record=record->next)
		{
			if( bool expected = false;
				record->active.compare_exchange_strong(expected, true,
					std::memory_order_acq_rel, std::memory_order_relaxed) )
			{
				hint.store(record, std::memory_order_release);
				return record;
			}
		}
		auto new_record = std::make_unique<hazard_record>();
		new_record->active.store(true, std::memory_order_relaxed);

		auto head = m_hazard_records.load(std::memory_order_relaxed);
		do {
			new_record->next = head;
		}
		while( not m_hazard_records.compare_exchange_weak(head, new_record.get(),
			   std::memory_order_release, std::memory_order_relaxed) );

		auto result = new_record.release();
		hint.store(result, std::memory_order_release);
		return result;
	}

	[[nodiscard]] bool is_hazard(const node *target_node) const noexcept
	{
		for(auto record=m_hazard_records.load(std::memory_order_acquire);
			record; record=record->next)
		{
			for(auto &pointer : record->pointers)
			{
				if( pointer.load(std::memory_order_acquire) == target_node )
					return true;
			}
		}
		return false;
	}

	void recycle(node *first, node *last, size_t count) noexcept
	{
		if( count > recycled_node_capacity )
		{
			while( first )
			{
				auto next = first->retired_next;
				delete first;
				first = next;
			}
			return ;
		}
		auto pooled = m_recycled_count.fetch_add(count, std::memory_order_relaxed);
		if( pooled > recycled_node_capacity - count )
		{
			m_recycled_count.fetch_sub(count, std::memory_order_relaxed);
			while( first )
			{
				auto next = first->retired_next;
				delete first;
				first = next;
			}
			return ;
		}
		auto head = m_recycled.load(std::memory_order_relaxed);
		do {
			last->retired_next = head;
		}
		while( not m_recycled.compare_exchange_weak(head, first,
			std::memory_order_release, std::memory_order_relaxed) );
	}

	template <typename...Args>
	[[nodiscard]] node *make_node(hazard_record *record, Args&&...args)
	{
		auto result = record->available;
		if( result )
			record->available = result->retired_next;
		else
		{
			result = m_recycled.exchange(nullptr, std::memory_order_acq_rel);
			if( not result )
				return new node(std::in_place, std::forward<Args>(args)...);

			size_t recycled_count = 0;
			for(auto current=result; current; current=current->retired_next)
				recycled_count++;

			m_recycled_count.fetch_sub(recycled_count, std::memory_order_relaxed);
			record->available = result->retired_next;
		}
		result->retired_next = nullptr;
		result->next.store(nullptr, std::memory_order_relaxed);
		try {
			result->data.emplace(std::forward<Args>(args)...);
		}
		catch(...)
		{
			result->retired_next = record->available;
			record->available = result;
			throw;
		}
		return result;
	}

	void retire(hazard_record *record, node *retired_node)
	{
		retired_node->retired_next = record->retired;
		record->retired = retired_node;

		if( ++record->retired_count < retire_scan_threshold )
			return ;

		node *hazards[hazard_snapshot_capacity] {};
		size_t hazard_count = 0;
		bool hazard_overflow = false;

		for(auto current_record=m_hazard_records.load(std::memory_order_acquire);
			current_record; current_record=current_record->next)
		{
			for(auto &pointer : current_record->pointers)
			{
				if( auto pointer_value = pointer.load(std::memory_order_acquire) )
				{
					if( hazard_count == hazard_snapshot_capacity )
						hazard_overflow = true;
					else
						hazards[hazard_count++] = pointer_value;
				}
			}
		}
		node *reclaimed = nullptr;
		node *reclaimed_tail = nullptr;

		size_t reclaimed_count = 0;
		auto link = &record->retired;

		while( *link )
		{
			auto current = *link;
			const auto hazardous = hazard_overflow ? is_hazard(current) :
				std::find(hazards, hazards + hazard_count, current) != hazards + hazard_count;

			if( hazardous )
				link = &current->retired_next;
			else
			{
				*link = current->retired_next;
				--record->retired_count;

				current->next.store(nullptr, std::memory_order_relaxed);
				current->retired_next = reclaimed;

				reclaimed = current;
				reclaimed_count++;

				if( not reclaimed_tail )
					reclaimed_tail = current;
			}
		}
		if( reclaimed )
			recycle(reclaimed, reclaimed_tail, reclaimed_count);
	}

public:
	static constexpr size_t retire_scan_threshold = 64;
	static constexpr size_t hazard_snapshot_capacity = 32;
	static constexpr size_t recycled_node_capacity = 1024;

	alignas(64) std::atomic<node*> m_head {nullptr};
	alignas(64) std::atomic<node*> m_tail {nullptr};
	alignas(64) std::atomic<size_t> m_size {0};
	alignas(64) std::atomic<hazard_record*> m_hazard_records {nullptr};

	std::atomic<hazard_record*> m_enqueue_hazard_hint {nullptr};
	std::atomic<hazard_record*> m_dequeue_hazard_hint {nullptr};

	alignas(64) std::atomic<node*> m_recycled {nullptr};
	std::atomic_size_t m_recycled_count {0};
};

template <concepts::copy_or_move_constructible T, size_t N>
consteval size_t lock_free_queue<T,queue_type::linked,N>::capacity()
	noexcept requires (capacity_v > 0)
{
	return capacity_v;
}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::linked,N>::lock_free_queue(size_t capacity)
	requires (capacity_v == 0) :
	m_impl(new impl(capacity))
{

}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::linked,N>::lock_free_queue() :
	m_impl(new impl())
{

}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::linked,N>::~lock_free_queue()
{
	delete m_impl;
}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::linked,N>::lock_free_queue(lock_free_queue &&other) noexcept :
	m_impl(other.m_impl)
{
	other.m_impl = new impl();
}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::linked,N>&
lock_free_queue<T,queue_type::linked,N>::operator=(lock_free_queue &&other) noexcept
{
	if( &other == this )
		return *this;
	delete m_impl;
	m_impl = other.m_impl;
	other.m_impl = new impl();
	return *this;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::linked,N>::enqueue(element_t &&data)
{
	return emplace(std::move(data));
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::linked,N>::enqueue(const element_t &data)
	requires concepts::copy_constructible<element_t>
{
	return emplace(data);
}

template <concepts::copy_or_move_constructible T, size_t N>
template <typename...Args>
bool lock_free_queue<T,queue_type::linked,N>::emplace(Args&&...args) requires
	concepts::constructible<element_t,Args...>
{
	typename impl::hazard_guard hazard(m_impl, m_impl->m_enqueue_hazard_hint);

	// Reserve capacity before publishing the node. This keeps concurrent
	// producers from exceeding a bounded queue's configured capacity.
	auto size = m_impl->m_size.load(std::memory_order_relaxed);
	for(;;)
	{
		if( size >= capacity() )
			return false; // full

		if( m_impl->m_size.compare_exchange_weak(size, size + 1,
			std::memory_order_acq_rel, std::memory_order_relaxed) )
			break;
	}
	typename impl::node *new_node = nullptr;
	try {
		new_node = hazard.make_node(std::forward<Args>(args)...);
	}
	catch(...)
	{
		m_impl->m_size.fetch_sub(1, std::memory_order_release);
		throw;
	}
	for(;;)
	{
		auto old_tail = hazard.protect(0, m_impl->m_tail);
		auto next = old_tail->next.load(std::memory_order_acquire);

		if( old_tail != m_impl->m_tail.load(std::memory_order_acquire) )
			continue;
		if( next )
		{
			m_impl->m_tail.compare_exchange_weak(old_tail, next,
				std::memory_order_release, std::memory_order_relaxed);
			continue;
		}
		if( auto expected = static_cast<impl::node*>(nullptr);
			old_tail->next.compare_exchange_weak(expected, new_node,
				std::memory_order_release, std::memory_order_relaxed) )
		{
			m_impl->m_tail.compare_exchange_strong(old_tail, new_node,
				std::memory_order_release, std::memory_order_relaxed);
			return true;
		}
	}
	return false;
}

template <concepts::copy_or_move_constructible T, size_t N>
optional<T> lock_free_queue<T,queue_type::linked,N>::dequeue()
{
	if( m_impl->m_size.load(std::memory_order_acquire) == 0 )
		return nullopt;

	typename impl::hazard_guard hazard(m_impl, m_impl->m_dequeue_hazard_hint);
	for(;;)
	{
		auto old_head = hazard.protect(0, m_impl->m_head);
		auto next = hazard.protect(1, old_head->next);

		if( old_head != m_impl->m_head.load(std::memory_order_acquire) )
			continue;

		if( not next )
			return nullopt;

		auto old_tail = m_impl->m_tail.load(std::memory_order_acquire);
		if( old_head == old_tail )
		{
			m_impl->m_tail.compare_exchange_weak(old_tail, next,
				std::memory_order_release, std::memory_order_relaxed);
			continue;
		}
		if( not m_impl->m_head.compare_exchange_weak(old_head, next,
			std::memory_order_acq_rel, std::memory_order_relaxed) )
			continue;

		m_impl->m_size.fetch_sub(1, std::memory_order_release);
		hazard.clear(0);
		hazard.retire(old_head);

		optional<element_t> elem;
		try {
			elem.emplace(std::move(*next->data));
		}
		catch(...)
		{
			next->data.reset();
			throw;
		}
		next->data.reset();
		return elem;
	}
	return nullopt;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::linked,N>::dequeue(T &data)
{
	auto _data = dequeue();
	if( _data )
	{
		data = std::move(*_data);
		return true;
	}
	return false;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::linked,N>::empty() const noexcept
{
	return size() == 0;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::linked,N>::full() const noexcept
{
	return size() >= capacity();
}

template <concepts::copy_or_move_constructible T, size_t N>
size_t lock_free_queue<T,queue_type::linked,N>::size() const noexcept
{
	return m_impl->m_size.load(std::memory_order_acquire);
}

template <concepts::copy_or_move_constructible T, size_t N>
size_t lock_free_queue<T,queue_type::linked,N>::capacity()
	const noexcept requires (capacity_v == 0)
{
	return m_impl->m_capacity.load(std::memory_order_acquire);
}

template <concepts::copy_or_move_constructible T, size_t N>
void lock_free_queue<T,queue_type::linked,N>::set_capacity(size_t size)
	requires (capacity_v == 0)
{
	m_impl->m_capacity.store (
		size > 0 ? size : std::numeric_limits<size_t>::max(),
		std::memory_order_release
	);
}

namespace detail
{

template <concepts::copy_or_move_constructible T>
class RIWO_CORE_TAPI circular_lock_free_queue_block
{
	RIWO_DISABLE_COPY_MOVE(circular_lock_free_queue_block)

	struct cell
	{
		std::atomic_size_t sequence {0};
		std::atomic_bool occupied {false};
		alignas(T) char storage[sizeof(T)] {};

		[[nodiscard]] T *value() noexcept {
			return std::launder(reinterpret_cast<T*>(storage));
		}
	};

public:
	explicit circular_lock_free_queue_block(size_t capacity) :
		m_capacity(capacity),
		m_index_mask((capacity & (capacity - 1)) == 0 ?
			capacity - 1 : std::numeric_limits<size_t>::max()
		),
		m_cells(new cell[capacity])
	{
		for(size_t i=0; i<m_capacity; i++)
			m_cells[i].sequence.store(i * 2, std::memory_order_relaxed);
	}

	~circular_lock_free_queue_block()
	{
		// Queue destruction is documented as unsafe. With no concurrent users,
		// destroy every value that is still published in the ring.
		auto begin = m_dequeue_pos.load(std::memory_order_relaxed);
		auto end = m_enqueue_pos.load(std::memory_order_relaxed);

		for(auto pos=begin; pos<end; pos++)
		{
			if( auto &entry = m_cells[index(pos)];
				entry.sequence.load(std::memory_order_relaxed) == pos * 2 + 1 and
				entry.occupied.load(std::memory_order_relaxed) )
				entry.value()->~T();
		}
	}

public:
	template <bool TrackSize = true, typename...Args>
	[[nodiscard]] bool emplace(Args&&...args) requires
		concepts::constructible<T,Args...>
	{
		size_t pos = m_enqueue_pos.load(std::memory_order_relaxed);
		cell *entry = nullptr;
		for(;;)
		{
			entry = &m_cells[index(pos)];
			auto sequence = entry->sequence.load(std::memory_order_acquire);

			auto diff = static_cast<std::intptr_t>(sequence) -
				static_cast<std::intptr_t>(pos * 2);

			if( diff == 0 )
			{
				if( m_enqueue_pos.compare_exchange_weak(pos, pos + 1,
					std::memory_order_relaxed, std::memory_order_relaxed) )
					break;
			}
			else if( diff < 0 )
			{
				// A constructor that throws leaves a published tombstone so
				// consumers can preserve FIFO position. Help reclaim a head
				// tombstone before reporting that the physical ring is full.
				if( discard_tombstone() )
				{
					pos = m_enqueue_pos.load(std::memory_order_relaxed);
					continue;
				}
				return false;
			}
			else
				pos = m_enqueue_pos.load(std::memory_order_relaxed);
		}
		try {
			new (entry->storage) T(std::forward<Args>(args)...);
			entry->occupied.store(true, std::memory_order_relaxed);
		}
		catch(...)
		{
			// Publish an empty position. Without this tombstone, dequeue()
			// remains permanently stuck behind the abandoned sequence.
			entry->occupied.store(false, std::memory_order_relaxed);
			entry->sequence.store(pos * 2 + 1, std::memory_order_release);
			(void)discard_tombstone();
			throw;
		}
		if constexpr( TrackSize )
			m_size.fetch_add(1, std::memory_order_release);

		entry->sequence.store(pos * 2 + 1, std::memory_order_release);
		return true;
	}

	template <bool TrackSize = true>
	[[nodiscard]] optional<T> dequeue()
	{
		for(;;)
		{
			size_t pos = m_dequeue_pos.load(std::memory_order_relaxed);
			cell *entry = nullptr;
			for(;;)
			{
				entry = &m_cells[index(pos)];
				auto sequence = entry->sequence.load(std::memory_order_acquire);

				auto diff = static_cast<std::intptr_t>(sequence) -
					static_cast<std::intptr_t>(pos * 2 + 1);

				if( diff == 0 )
				{
					if( m_dequeue_pos.compare_exchange_weak(pos, pos + 1,
						std::memory_order_relaxed, std::memory_order_relaxed) )
						break;
				}
				else if( diff < 0 )
					return nullopt;
				else
					pos = m_dequeue_pos.load(std::memory_order_relaxed);
			}
			if( not entry->occupied.load(std::memory_order_acquire) )
			{
				entry->sequence.store((pos + m_capacity) * 2,
					std::memory_order_release);
				continue;
			}
			optional<T> result;
			try {
				result.emplace(std::move(*entry->value()));
			}
			catch(...)
			{
				entry->value()->~T();
				entry->occupied.store(false, std::memory_order_relaxed);

				if constexpr( TrackSize )
					m_size.fetch_sub(1, std::memory_order_release);

				entry->sequence.store((pos + m_capacity) * 2,
					std::memory_order_release);
				throw;
			}
			entry->value()->~T();
			entry->occupied.store(false, std::memory_order_relaxed);

			if constexpr( TrackSize )
				m_size.fetch_sub(1, std::memory_order_release);

			entry->sequence.store((pos + m_capacity) * 2, std::memory_order_release);
			return result;
		}
	}

	[[nodiscard]] size_t size() const noexcept {
		return m_size.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return m_dequeue_pos.load(std::memory_order_acquire) >=
			m_enqueue_pos.load(std::memory_order_acquire);
	}

public:
	const size_t m_capacity;
	std::atomic_bool m_closed {false};
	std::atomic_size_t m_active_enqueues {0};

	std::atomic<circular_lock_free_queue_block*> m_next {nullptr};
	circular_lock_free_queue_block *m_retired_end = nullptr;
	circular_lock_free_queue_block *m_retired_next = nullptr;

private:
	[[nodiscard]] bool discard_tombstone() noexcept
	{
		auto pos = m_dequeue_pos.load(std::memory_order_relaxed);
		auto &entry = m_cells[index(pos)];
		const auto sequence = entry.sequence.load(std::memory_order_acquire);
		const auto diff = static_cast<std::intptr_t>(sequence) -
			static_cast<std::intptr_t>(pos * 2 + 1);

		if( diff != 0 or entry.occupied.load(std::memory_order_acquire) )
			return false;
		if( not m_dequeue_pos.compare_exchange_weak(pos, pos + 1,
			std::memory_order_relaxed, std::memory_order_relaxed) )
			return true;

		entry.sequence.store((pos + m_capacity) * 2,
			std::memory_order_release);
		return true;
	}

	[[nodiscard]] size_t index(size_t position) const noexcept
	{
		return m_index_mask != std::numeric_limits<size_t>::max() ?
			position & m_index_mask : position % m_capacity;
	}

	const size_t m_index_mask;
	std::unique_ptr<cell[]> m_cells;

	alignas(64) std::atomic_size_t m_enqueue_pos {0};
	alignas(64) std::atomic_size_t m_dequeue_pos {0};
	alignas(64) std::atomic_size_t m_size {0};
};

template <concepts::copy_or_move_constructible T, size_t N> requires (N > 0)
class RIWO_CORE_TAPI lock_free_queue_impl<T, queue_type::circular, N>
{
	RIWO_DISABLE_COPY_MOVE(lock_free_queue_impl)

public:
	lock_free_queue_impl() : m_block(N) {}

	static_assert(std::atomic_size_t::is_always_lock_free,
		"Atomic size must be lock-free"
	);

public:
	template <typename...Args>
	[[nodiscard]] bool emplace(Args&&...args) requires
		concepts::constructible<T,Args...>
	{
		return m_block.emplace(std::forward<Args>(args)...);
	}

	[[nodiscard]] optional<T> dequeue() {
		return m_block.dequeue();
	}

	[[nodiscard]] size_t size() const noexcept {
		return m_block.size();
	}

private:
	circular_lock_free_queue_block<T> m_block;
};

template <concepts::copy_or_move_constructible T>
class RIWO_CORE_TAPI lock_free_queue_impl<T, queue_type::circular, 0>
{
	RIWO_DISABLE_COPY_MOVE(lock_free_queue_impl)

public:
	static_assert(std::atomic_size_t::is_always_lock_free,
		"Atomic size must be lock-free"
	);
	using block = circular_lock_free_queue_block<T>;

	struct hazard_record
	{
		std::atomic_bool active {false};
		std::atomic<block*> pointer {nullptr};
		hazard_record *next = nullptr;
	};

	class hazard_guard
	{
		RIWO_DISABLE_COPY_MOVE(hazard_guard)

	public:
		explicit hazard_guard
		(lock_free_queue_impl *owner, std::atomic<hazard_record*> &hint, std::atomic<hazard_record*> &records) :
			m_record(owner->acquire_hazard_record(hint, records)) {}

		~hazard_guard()
		{
			m_record->pointer.store(nullptr, std::memory_order_seq_cst);
			m_record->active.store(false, std::memory_order_release);
		}

		[[nodiscard]] block *protect(const std::atomic<block*> &source)
		{
			block *result = nullptr;
			do {
				result = source.load(std::memory_order_acquire);
				m_record->pointer.store(result, std::memory_order_seq_cst);
			}
			while( result != source.load(std::memory_order_acquire) );
			return result;
		}

	private:
		hazard_record *m_record;
	};

private:
	static constexpr size_t compact_reclaim_budget = 8;

	[[nodiscard]] static constexpr size_t normalize_capacity(size_t capacity) noexcept {
		return capacity > 0 ? capacity : 64;
	}

	[[nodiscard]] static constexpr size_t storage_capacity(size_t capacity) noexcept
	{
		size_t result = 1;
		constexpr auto maximum = std::numeric_limits<size_t>::max();

		while( result < capacity and result <= maximum / 2 )
			result *= 2;

		return result < capacity ? capacity : result;
	}

	static void delete_blocks(block *current, const block *end = nullptr) noexcept
	{
		while( current and current != end )
		{
			auto next = current->m_next.load(std::memory_order_relaxed);
			delete current;
			current = next;
		}
	}

	[[nodiscard]] hazard_record *acquire_hazard_record
	(std::atomic<hazard_record*> &hint, std::atomic<hazard_record*> &records)
	{
		if( auto record = hint.load(std::memory_order_acquire) )
		{
			if( bool expected = false;
				record->active.compare_exchange_strong(expected, true,
					std::memory_order_acq_rel, std::memory_order_relaxed) )
				return record;
		}
		for(auto record=records.load(std::memory_order_acquire);
			record; record=record->next)
		{
			if( bool expected = false;
				record->active.compare_exchange_strong(expected, true,
					std::memory_order_acq_rel, std::memory_order_relaxed) )
			{
				hint.store(record, std::memory_order_release);
				return record;
			}
		}
		auto new_record = std::make_unique<hazard_record>();
		new_record->active.store(true, std::memory_order_relaxed);

		auto head = records.load(std::memory_order_relaxed);
		do {
			new_record->next = head;
		}
		while( not records.compare_exchange_weak(head, new_record.get(),
			std::memory_order_release, std::memory_order_relaxed) );

		auto result = new_record.release();
		hint.store(result, std::memory_order_release);
		return result;
	}

	[[nodiscard]] static bool is_hazard
	(const block *target, const std::atomic<hazard_record*> &records) noexcept
	{
		for(auto record=records.load(std::memory_order_acquire); record; record=record->next)
		{
			if( record->pointer.load(std::memory_order_seq_cst) == target )
				return true;
		}
		return false;
	}

	[[nodiscard]] bool is_hazard(const block *target) const noexcept
	{
		return is_hazard(target, m_enqueue_hazard_records) or
			   is_hazard(target, m_dequeue_hazard_records);
	}

	void retire_blocks(block *first, block *end) noexcept
	{
		if( first == end )
			return;

		first->m_retired_end = end;
		first->m_retired_next = m_retired_blocks;
		m_retired_blocks = first;
	}

	[[nodiscard]] bool reclaim_retired_blocks(size_t budget = compact_reclaim_budget) noexcept
	{
		bool reclaimed = false;
		auto link = &m_retired_blocks;

		for(size_t inspected=0; *link and inspected<budget; ++inspected)
		{
			auto first = *link;
			const auto end = first->m_retired_end;
			bool hazardous = false;

			for(auto current=first; current != end; current=current->m_next.load(std::memory_order_acquire))
			{
				if( is_hazard(current) )
				{
					hazardous = true;
					break;
				}
			}
			if( hazardous )
			{
				link = &first->m_retired_next;
				continue;
			}
			*link = first->m_retired_next;
			delete_blocks(first, end);
			reclaimed = true;
		}
		return reclaimed;
	}

public:
	explicit lock_free_queue_impl(size_t capacity = 64)
	{
		capacity = normalize_capacity(capacity);
		m_capacity.store(capacity, std::memory_order_relaxed);
		m_first = new block(storage_capacity(capacity));
		m_enqueue_block.store(m_first, std::memory_order_relaxed);
		m_dequeue_block.store(m_first, std::memory_order_relaxed);
	}

	~lock_free_queue_impl()
	{
		while( m_retired_blocks )
		{
			auto first = m_retired_blocks;
			m_retired_blocks = first->m_retired_next;
			delete_blocks(first, first->m_retired_end);
		}
		delete_blocks(m_first);

		for(auto records : {&m_enqueue_hazard_records, &m_dequeue_hazard_records})
		{
			auto record = records->load(std::memory_order_relaxed);
			while( record )
			{
				auto next = record->next;
				delete record;
				record = next;
			}
		}
	}

public:
	template <typename...Args>
	[[nodiscard]] bool emplace(Args&&...args) requires
		concepts::constructible<T,Args...>
	{
		// Reserve against the logical capacity before touching the current
		// physical block.  In particular, old blocks may still contain values
		// after a growth, and a shrink may put the queue above its new limit.
		const auto capacity = m_capacity.load(std::memory_order_acquire);
		if( m_size.load(std::memory_order_relaxed) >= capacity )
			return false;

		if( m_size.fetch_add(1, std::memory_order_acq_rel) >= capacity )
		{
			m_size.fetch_sub(1, std::memory_order_release);
			return false;
		}
		hazard_guard hazard(this, m_enqueue_hazard_hint, m_enqueue_hazard_records);
		auto current = hazard.protect(m_enqueue_block);
		current->m_active_enqueues.fetch_add(1, std::memory_order_acq_rel);

		while( current->m_closed.load(std::memory_order_acquire) )
		{
			current->m_active_enqueues.fetch_sub(1, std::memory_order_release);
			current = hazard.protect(m_enqueue_block);
			current->m_active_enqueues.fetch_add(1, std::memory_order_acq_rel);
		}
		bool result = false;
		try {
			result = current->template emplace<false>(std::forward<Args>(args)...);
		}
		catch(...)
		{
			current->m_active_enqueues.fetch_sub(1, std::memory_order_release);
			m_size.fetch_sub(1, std::memory_order_release);
			throw;
		}
		current->m_active_enqueues.fetch_sub(1, std::memory_order_release);
		if( not result )
			m_size.fetch_sub(1, std::memory_order_release);
		return result;
	}

	[[nodiscard]] optional<T> dequeue()
	{
		hazard_guard hazard(this, m_dequeue_hazard_hint, m_dequeue_hazard_records);
		for(;;)
		{
			auto current = hazard.protect(m_dequeue_block);
			bool logical_size_updated = false;
			try {
				if( auto result = current->template dequeue<false>() )
				{
					m_size.fetch_sub(1, std::memory_order_release);
					logical_size_updated = true;
					return result;
				}
			}
			catch(...)
			{
				// The block has already released a claimed element so the
				// logical count must follow it even when T's move throws.
				if( not logical_size_updated )
					m_size.fetch_sub(1, std::memory_order_release);
				throw;
			}
			if( current == m_enqueue_block.load(std::memory_order_acquire) or
				not current->m_closed.load(std::memory_order_acquire) or
				current->m_active_enqueues.load(std::memory_order_acquire) != 0 or
				not current->empty() or
				is_hazard(current, m_enqueue_hazard_records) )
				return nullopt;

			auto next = current->m_next.load(std::memory_order_acquire);
			if( not next )
				return nullopt;

			m_dequeue_block.compare_exchange_weak(current, next,
				std::memory_order_release, std::memory_order_relaxed
			);
		}
	}

	void set_capacity(size_t capacity)
	{
		std::lock_guard lock(m_resize_mutex);
		capacity = normalize_capacity(capacity);
		auto current = m_enqueue_block.load(std::memory_order_acquire);

		if( capacity <= current->m_capacity )
		{
			m_capacity.store(capacity, std::memory_order_release);
			return;
		}
		// Grow geometrically so a series of small increases does not leave one
		// allocation behind for every set_capacity() call.  Physical storage is
		// deliberately retained on shrink, like vector capacity.
		constexpr auto maximum = std::numeric_limits<size_t>::max();
		const auto doubled = current->m_capacity <= maximum / 2 ?
			current->m_capacity * 2 : maximum;

		auto new_block = std::make_unique<block>(
			storage_capacity(std::max(capacity, doubled))
		);
		for(;;)
		{
			current = m_enqueue_block.load(std::memory_order_acquire);
			if( capacity <= current->m_capacity )
			{
				m_capacity.store(capacity, std::memory_order_release);
				return;
			}
			if( bool expected = false;
				not current->m_closed.compare_exchange_weak(expected, true,
					std::memory_order_acq_rel, std::memory_order_relaxed) )
				continue;

			current->m_next.store(new_block.get(), std::memory_order_release);
			m_enqueue_block.store(new_block.release(), std::memory_order_release);
			m_capacity.store(capacity, std::memory_order_release);
			return;
		}
	}

	[[nodiscard]] bool compact()
	{
		std::lock_guard lock(m_resize_mutex);
		bool changed = reclaim_retired_blocks();

		auto retire_drained_prefix = [&]
		{
			// A final successful dequeue need not revisit the now-empty block to
			// advance the shared cursor. Only advance blocks with no protected
			// operation; otherwise reclamation is deferred without waiting.
			for(;;)
			{
				auto dequeue = m_dequeue_block.load(std::memory_order_acquire);

				if( dequeue == m_enqueue_block.load(std::memory_order_acquire) or
					not dequeue->m_closed.load(std::memory_order_acquire) or
					dequeue->m_active_enqueues.load(std::memory_order_acquire) != 0 or
					not dequeue->empty() or is_hazard(dequeue) )
					break;

				auto next = dequeue->m_next.load(std::memory_order_acquire);
				if( not next )
					break;

				m_dequeue_block.compare_exchange_weak(dequeue, next,
					std::memory_order_release, std::memory_order_relaxed);
			}
			// Blocks before the dequeue cursor have already drained. Detach them
			// immediately, but defer deletion while an operation protects one.
			auto dequeue = m_dequeue_block.load(std::memory_order_acquire);
			if( m_first != dequeue )
			{
				auto old_first = m_first;
				m_first = dequeue;

				retire_blocks(old_first, dequeue);
				changed = true;
			}
		};
		retire_drained_prefix();

		auto current = m_enqueue_block.load(std::memory_order_acquire);
		const auto target = storage_capacity (
			m_capacity.load(std::memory_order_acquire)
		);
		// Keep at most one generation transition outstanding. Repeated compact()
		// calls must not allocate faster than consumers can drain the old chain.
		if( m_dequeue_block.load(std::memory_order_acquire) == current and
			target <= current->m_capacity / 2 )
		{
			// Do not move live values. Future enqueues use the replacement while
			// consumers finish the old chain in FIFO order.
			auto replacement = std::make_unique<block>(target);
			current->m_closed.store(true, std::memory_order_release);
			current->m_next.store(replacement.get(), std::memory_order_release);

			m_enqueue_block.store(replacement.get(), std::memory_order_release);
			replacement.release();

			changed = true;
			retire_drained_prefix();
		}
		return reclaim_retired_blocks() or changed;
	}

	[[nodiscard]] size_t size() const noexcept {
		return m_size.load(std::memory_order_acquire);
	}

	[[nodiscard]] size_t capacity() const noexcept {
		return m_capacity.load(std::memory_order_acquire);
	}

private:
	block *m_first = nullptr;
	block *m_retired_blocks = nullptr;
	// Resizing allocates and may reclaim an entire block chain.  Keep waiters
	// asleep instead of burning a CPU around that comparatively long work.
	std::mutex m_resize_mutex;

	alignas(64) std::atomic_size_t m_capacity {};
	alignas(64) std::atomic_size_t m_size {0};

	alignas(64) std::atomic<block*> m_enqueue_block {nullptr};
	alignas(64) std::atomic<block*> m_dequeue_block {nullptr};

	alignas(64) std::atomic<hazard_record*> m_enqueue_hazard_records {nullptr};
	alignas(64) std::atomic<hazard_record*> m_dequeue_hazard_records {nullptr};

	std::atomic<hazard_record*> m_enqueue_hazard_hint {nullptr};
	std::atomic<hazard_record*> m_dequeue_hazard_hint {nullptr};
};

} //namespace detail

template <concepts::copy_or_move_constructible T, size_t N>
class RIWO_CORE_TAPI lock_free_queue<T,queue_type::circular,N>::impl :
	public detail::lock_free_queue_impl<T,queue_type::circular,N>
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	using detail::lock_free_queue_impl
		<T,queue_type::circular,N>::lock_free_queue_impl;
};

template <concepts::copy_or_move_constructible T, size_t N>
consteval size_t lock_free_queue<T,queue_type::circular,N>::capacity()
	noexcept requires (capacity_v > 0)
{
	return capacity_v;
}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::circular,N>::lock_free_queue(size_t capacity)
	requires (capacity_v == 0) :
	m_impl(new impl(capacity))
{

}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::circular,N>::lock_free_queue() :
	m_impl(new impl())
{

}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::circular,N>::~lock_free_queue()
{
	delete m_impl;
}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::circular,N>::lock_free_queue(lock_free_queue &&other) noexcept :
	m_impl(other.m_impl)
{
	other.m_impl = new impl();
}

template <concepts::copy_or_move_constructible T, size_t N>
lock_free_queue<T,queue_type::circular,N>&
lock_free_queue<T,queue_type::circular,N>::operator=(lock_free_queue &&other) noexcept
{
	if( &other == this )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	other.m_impl = new impl();
	return *this;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::circular,N>::enqueue(element_t &&data)
{
	return emplace(std::move(data));
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::circular,N>::enqueue(const element_t &data)
	requires concepts::copy_constructible<element_t>
{
	return emplace(data);
}

template <concepts::copy_or_move_constructible T, size_t N>
template <typename...Args>
bool lock_free_queue<T,queue_type::circular,N>::emplace(Args&&...args) requires
	concepts::constructible<element_t,Args...>
{
	return m_impl->emplace(std::forward<Args>(args)...);
}

template <concepts::copy_or_move_constructible T, size_t N>
optional<T> lock_free_queue<T,queue_type::circular,N>::dequeue()
{
	return m_impl->dequeue();
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::circular,N>::dequeue(element_t &data)
{
	auto _data = dequeue();
	if( _data )
	{
		data = std::move(*_data);
		return true;
	}
	return false;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::circular,N>::empty() const noexcept
{
	return size() == 0;
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::circular,N>::full() const noexcept
{
	return size() >= capacity();
}

template <concepts::copy_or_move_constructible T, size_t N>
size_t lock_free_queue<T,queue_type::circular,N>::size() const noexcept
{
	return m_impl->size();
}

template <concepts::copy_or_move_constructible T, size_t N>
size_t lock_free_queue<T,queue_type::circular,N>::capacity()
	const noexcept requires (capacity_v == 0)
{
	return m_impl->capacity();
}

template <concepts::copy_or_move_constructible T, size_t N>
void lock_free_queue<T,queue_type::circular,N>::set_capacity(size_t size)
	requires (capacity_v == 0)
{
	m_impl->set_capacity(size);
}

template <concepts::copy_or_move_constructible T, size_t N>
bool lock_free_queue<T,queue_type::circular,N>::compact()
	requires (capacity_v == 0)
{
	return m_impl->compact();
}

} //namespace riwo

#ifdef _MSC_VER
# pragma warning(pop)
#endif //_MSC_VER

#endif //RIWO_CORE_DETAIL_LOCK_FREE_QUEUE_H
