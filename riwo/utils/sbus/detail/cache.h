// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_DETAIL_CACHE_H
#define RIWO_UTILS_UTILS_SBUS_DETAIL_CACHE_H

#include <riwo/core/async_expected.h>
#include <riwo/core/shared_mutex.h>
#include <riwo/utils/process.h>
#include <atomic>

namespace riwo::utils
{

template <concepts::streamer_type_p Tag>
struct arg_converter<std::vector<std::byte>,Tag>
{
	static constexpr bool valid = true;
	using payload_t = std::vector<std::byte>;

	[[nodiscard]] static auto convert(const payload_t &value) requires valid
	{
		if constexpr( concepts::optional_p<Tag> )
		{
			using value_t = std::remove_cvref_t<Tag>::value_t;
			using target_t = optional<value_t>;

			target_t opt;
			if( not value.empty() )
			{
				if constexpr( std::is_same_v<value_t,std::vector<std::byte>> )
					opt = value;
				else
					opt = *streamer<value_t>::decode(value);
			}
			return opt;
		}
		else
		{
			using target_t = std::remove_cvref_t<Tag>;
			if( value.empty() )
				return target_t();

			if constexpr( std::is_same_v<target_t,std::vector<std::byte>> )
				return value;
			else
				return *streamer<target_t>::decode(value);
		}
	}
};

namespace sbus
{

template <concepts::subscriber Subscriber>
class RIWO_UTILS_TAPI cache<Subscriber>::impl : public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	template <typename...Args>
	using signal_ptr = std::shared_ptr<signal_t<Args...>>;

	struct cache_t
	{
		RIWO_META_FIELDS (
			( uint64_t , time, 0 ),
			( payload_t, data    )
		);
	};
	struct cache_event
	{
		RIWO_UTILS_SBUS_TYPE_IMPL (
			" ___35947__RIWO_UTILS_SBUS__PRIVATE__CACHE_CACHE_EVENT__27136___ "
		)
		RIWO_META_FIELDS (
			( std::string, topic    ),
			( uint64_t   , pid  , 0 ),
			( uint64_t   , time , 0 ),
			( payload_t  , data     )
		);
	};
	struct transparent_string_hash
	{
		using is_transparent = void;

		[[nodiscard]] size_t operator()(std::string_view value) const noexcept {
			return std::hash<std::string_view>{}(value);
		}
		[[nodiscard]] size_t operator()(const std::string &value) const noexcept {
			return operator()(std::string_view(value));
		}
	};

public:
	explicit impl(executor_t exec) :
		m_subscriber(std::move(exec))
	{
		m_subscriber.subscribe (
		[this](std::string_view topic, const void *data, size_t size) -> awaitable<void>
		{
			std::span view {
				static_cast<const std::byte*>(data), size
			};
			payload_t payload = { view.begin(), view.end() };
			auto time = std::numeric_limits<uint64_t>::max();

			std::string _topic(topic);
			if( _topic == cache_event::riwo_sbus_topic_v )
			{
				auto cache = *streamer<cache_event>::decode(payload);
				if( auto pid = process::self_pid(); not pid or cache.pid == *pid )
					co_return ;

				_topic = std::move(cache.topic);
				payload = std::move(cache.data);
				time = cache.time;
			}
			std::unique_lock locker(m_caches_mutex);
			auto &curr = m_caches[_topic];

			if( time < curr.time )
				co_return ;

			curr.time = time;
			if( curr.data.size() == size )
			{
				if( memcmp(curr.data.data(), data, size) == 0 )
					co_return ;
			}
			auto _prev = std::move(curr.data);
			auto _curr = curr.data = std::move(payload);
			locker.unlock();

			signal_ptr<payload_t,payload_t> signal {};
			{
				std::lock_guard lock(m_signals_mutex);
				auto &obj = m_signals[_topic];

				if( not obj )
					obj = std::make_shared<signal_t<payload_t,payload_t>>();
				signal = obj;
			}
			co_await signal->emit(_curr, _prev);

			co_await m_signal.emit (
				std::move(_topic), std::move(_curr), std::move(_prev)
			);
			co_return ;
		});
	}
	~impl() {
		m_subscriber.cancel();
	}

public:
	template <typename T>
	static void decode_payload(T &target, payload_t payload) noexcept
	{
		using type = std::remove_cvref_t<T>;
		if constexpr( std::is_same_v<type,payload_t> )
			target = std::move(payload);

		else if constexpr( riwo::concepts::optional<type> )
		{
			using value_t = type::value_t;
			if( payload.empty() )
				return ;

			if constexpr( std::is_same_v<value_t,payload_t> )
				target = std::move(payload);

			else if constexpr( riwo::concepts::streamer_type<value_t> )
				target = *streamer<value_t>::decode(payload);

			else if( payload.size() >= sizeof(value_t) )
				target = *reinterpret_cast<const value_t*>(payload.data());
		}
		else if constexpr( riwo::concepts::streamer_type<type> )
		{
			if( not payload.empty() )
				target = *streamer<type>::decode(payload);
		}
		else
		{
			if( payload.size() >= sizeof(type) )
				target = *reinterpret_cast<const type*>(payload.data());
		}
	}

	template <typename T>
	[[nodiscard]] static changed_result<T> decode_changed(payload_t curr, payload_t prev) noexcept
	{
		changed_result<T> result {};
		decode_payload(result.current, std::move(curr));
		decode_payload(result.previous, std::move(prev));
		return result;
	}

public:
	void set(std::string_view topic, const void *data, size_t size)
	{
		std::unique_lock locker(m_caches_mutex);
		auto &curr = m_caches[std::string(topic)];

		if( curr.data.size() == size )
		{
			if( memcmp(curr.data.data(), data, size) == 0 )
				return ;
		}
		auto _prev = std::move(curr.data);
		std::span view {
			static_cast<const std::byte*>(data), size
		};
		auto _curr = curr.data = { view.begin(), view.end() };
		locker.unlock();

		dispatch(m_subscriber.get_executor(), [this, delivery_topic = std::string(topic),
			previous_payload = std::move(_prev), current_payload = _curr]() mutable -> awaitable<void>
		{
			signal_ptr<payload_t,payload_t> signal {};
			{
				std::lock_guard lock(m_signals_mutex);
				auto &obj = m_signals[delivery_topic];

				if( not obj )
					obj = std::make_shared<signal_t<payload_t,payload_t>>();
				signal = obj;
			}
			co_await signal->emit(current_payload, previous_payload);

			co_await m_signal.emit (
				std::move(delivery_topic),
				std::move(current_payload), std::move(previous_payload)
			);
			co_return ;
		});
		auto pid = process::self_pid();
		if( not pid )
			return ;

		cache_event event {
			.topic = std::string(topic),
			.pid   = *pid              ,
			.data  = std::move(_curr)
		};
		event.time = static_cast<uint64_t>(
			std::chrono::system_clock::now().time_since_epoch().count()
		);
		publish<interface_t>(std::move(event));
	}

	template <riwo::concepts::any_string_p Str>
	void set(std::string_view topic, Str &&str)
	{
		auto view = strtls::to_view(std::forward<Str>(str));
		set(topic, view.data(), view.size());
	}

	template <concepts::unregistered_type_p T>
	void set(std::string_view topic, const T &data)
	{
		using type = std::remove_cvref_t<T>;
		if constexpr( riwo::concepts::streamer_type<type> )
		{
			auto payload = streamer<type>::encode(data);
			set(topic, payload.data(), payload.size());
		}
		else if constexpr( std::is_same_v<type, const_buffer> or
			std::is_same_v<type, asio::const_buffer> )
			set(topic, data.data(), data.size());
		else
			set(topic, &data, sizeof(data));
	}

	void set(concepts::topic_type auto &&data)
	{
		using T = decltype(data);
		using type = std::remove_cvref_t<T>;

		if constexpr( riwo::concepts::streamer_type<type> )
		{
			auto payload = streamer<type>::encode(data);
			set(type::riwo_sbus_topic_v, payload.data(), payload.size());
		}
		else
			set(type::riwo_sbus_topic_v, &data, sizeof(data));
	}

public:
	signal_t<payload_t,payload_t> &changed(std::string_view topic) noexcept
	{
		std::unique_lock locker(m_signals_mutex); RIWO_UNUSED(locker);
		auto &signal = m_signals[std::string(topic)];

		if( not signal )
			signal = std::make_shared<signal_t<payload_t,payload_t>>();
		return *signal;
	}

private:
	class wait_operation
	{
	public:
		virtual ~wait_operation() = default;
		virtual void cancel() noexcept = 0;
	};

	void register_wait(const std::shared_ptr<wait_operation> &operation)
	{
		std::lock_guard lock(m_wait_operations_mutex);
		m_wait_operations[operation.get()] = operation;
	}

	void unregister_wait(const wait_operation *operation) noexcept
	{
		std::lock_guard lock(m_wait_operations_mutex);
		m_wait_operations.erase(operation);
	}

private:
	template <typename T, typename Handler>
	class wait_state final : public wait_operation,
		public std::enable_shared_from_this<wait_state<T,Handler>>
	{
		using self_t = wait_state;
		using completion_exec_t = asio::associated_executor_t<Handler,executor_t>;

	public:
		wait_state(std::shared_ptr<impl> owner, std::string topic, Handler handler) :
			m_owner(std::move(owner)),
			m_topic(std::move(topic)),
			m_exec(asio::get_associated_executor (
				handler, m_owner->m_subscriber.get_executor()
			)),
			m_cancel_slot(asio::get_associated_cancellation_slot(handler)),
			m_handler(std::move(handler)) {}

		void start()
		{
			auto self = this->shared_from_this();
			m_owner->register_wait(self);

			m_owner->changed(m_topic).connect(self, m_exec,
			[self](payload_t curr, payload_t prev) mutable noexcept
			{
				self->complete(error_code{},
					impl::template decode_changed<T>(
						std::move(curr), std::move(prev)
					)
				);
			});
			if( m_cancel_slot.is_connected() )
			{
				m_cancel_slot.assign([weak = std::weak_ptr<self_t>(self)]
				(asio::cancellation_type_t type) mutable noexcept
				{
					if( type == asio::cancellation_type::none )
						return ;

					if( auto active = weak.lock() )
						active->cancel();
				});
				if( m_completed.load(std::memory_order_acquire) )
					m_cancel_slot.clear();
			}
		}

		void cancel() noexcept override
		{
			if( m_completed.exchange(true, std::memory_order_acq_rel) )
				return ;

			auto self = this->shared_from_this();
			asio::post(m_exec, [self = std::move(self)]() mutable noexcept
			{
				self->finish (
					asio::error::make_error_code(asio::error::operation_aborted),
					changed_result<T>{}
				);
			});
		}

	private:
		void complete(const error_code &error, changed_result<T> result) noexcept
		{
			if( m_completed.exchange(true, std::memory_order_acq_rel) )
				return ;
			finish(error, std::move(result));
		}

		void finish(error_code error, changed_result<T> result) noexcept
		{
			m_cancel_slot.clear();
			m_owner->unregister_wait(this);

			auto self = this->shared_from_this();
			m_owner->changed(m_topic).disconnect(self);

			auto completion = std::move(*m_handler);
			m_handler.reset();
			std::move(completion)(error, std::move(result));
		}

	private:
		std::shared_ptr<impl> m_owner {};
		std::string m_topic {};

		completion_exec_t m_exec {};
		asio::cancellation_slot m_cancel_slot {};

		optional<Handler> m_handler {};
		std::atomic_bool m_completed {false};
	};

public:
	void cancel() noexcept
	{
		std::vector<std::shared_ptr<wait_operation>> operations;
		{
			std::lock_guard lock(m_wait_operations_mutex);
			operations.reserve(m_wait_operations.size());

			for(auto it=m_wait_operations.begin(); it!=m_wait_operations.end(); )
			{
				if( auto operation = it->second.lock() )
				{
					operations.emplace_back(std::move(operation));
					++it;
				}
				else
					it = m_wait_operations.erase(it);
			}
		}
		for(auto &operation : operations)
			operation->cancel();
	}

	template <typename T = payload_t, typename Handler>
	void async_wait_changed(std::string topic, Handler &&handler)
	{
		using handler_t = std::remove_cvref_t<Handler>;
		using state_t = wait_state<T,handler_t>;

		auto allocator = asio::get_associated_allocator(handler);
		auto operation = std::allocate_shared<state_t>(allocator,
			this->shared_from_this(), std::move(topic),
			handler_t(std::forward<Handler>(handler))
		);
		operation->start();
	}

public:
	subscriber_t m_subscriber {};

	std::unordered_map <
		std::string, cache_t, transparent_string_hash, std::equal_to<>
	> m_caches {};

	// Payload copies and map growth are unbounded; readers can still proceed in
	// parallel through the blocking shared lock.
	shared_mutex m_caches_mutex {};

	signal_t<std::string_view,payload_t,payload_t> m_signal {};

	std::unordered_map <
		std::string, signal_ptr<payload_t,payload_t>
	> m_signals {};

	// This map is only ever accessed exclusively and may allocate a signal.
	std::mutex m_signals_mutex {};

	std::unordered_map <
		const wait_operation*, std::weak_ptr<wait_operation>
	> m_wait_operations {};

	std::mutex m_wait_operations_mutex {};
};

template <concepts::subscriber Subscriber>
template <typename Exec0>
cache<Subscriber>::cache(Exec0 &&exec)
	requires riwo::concepts::match_sched<Exec0,executor_t> :
	m_impl(std::make_shared<impl>(executor_t(get_executor_helper(std::forward<Exec0>(exec)))))
{

}

template <concepts::subscriber Subscriber>
cache<Subscriber>::~cache() = default;

template <concepts::subscriber Subscriber>
cache<Subscriber> &cache<Subscriber>::set(std::string_view topic, const void *data, size_t size)
{
	m_impl->set(topic, data, size);
	return *this;
}

template <concepts::subscriber Subscriber>
template <riwo::concepts::any_string_p...Args>
cache<Subscriber> &cache<Subscriber>::set(std::string_view topic, Args&&...args)
	requires (sizeof...(Args) > 0)
{
	(void) std::initializer_list<int> {
		(m_impl->set(topic, std::forward<Args>(args)), 0) ...
	};
	return *this;
}

template <concepts::subscriber Subscriber>
template <concepts::unregistered_type_p...Args>
cache<Subscriber> &cache<Subscriber>::set(std::string_view topic, Args&&...args)
	requires (sizeof...(Args) > 0)
{
	(void) std::initializer_list<int> {
		(m_impl->set(topic, std::forward<Args>(args)), 0) ...
	};
	return *this;
}

template <concepts::subscriber Subscriber>
template <concepts::topic_type...Args>
cache<Subscriber> &cache<Subscriber>::set(Args&&...args)
	requires (sizeof...(Args) > 0)
{
	(void) std::initializer_list<int> {
		(m_impl->set(std::forward<Args>(args)), 0) ...
	};
	return *this;
}

template <concepts::subscriber Subscriber>
template <concepts::topic_type T>
optional<T> cache<Subscriber>::get() const
{
	using type = std::remove_cvref_t<T>;
	auto payload = get(type::riwo_sbus_topic_v);

	if( payload.empty() )
		return {};

	if constexpr( riwo::concepts::streamer_type<type> )
		return *streamer<type>::decode(payload);
	else
		return *reinterpret_cast<const type*>(payload.data());
}

template <concepts::subscriber Subscriber>
template <typename T>
optional<T> cache<Subscriber>::get(std::string_view topic) const
{
	using type = std::remove_cvref_t<T>;
	if constexpr( concepts::topic_type<type> )
	{
		if( topic != type::riwo_sbus_topic_v )
			invalid_argument::loc_throw("Topic does not match.");
	}
	std::shared_lock locker(m_impl->m_caches_mutex);
	auto pos = m_impl->m_caches.find(topic);

	if( pos == m_impl->m_caches.end() or pos->second.data.empty() )
		return {};

	payload_t payload;
	payload.reserve(pos->second.data.size());

	payload.insert (payload.end(),
		pos->second.data.begin(), pos->second.data.end()
	);
	locker.unlock();

	if constexpr( riwo::concepts::streamer_type<type> )
		return *streamer<type>::decode(payload);
	else
		return *reinterpret_cast<const type*>(payload.data());
}

template <concepts::subscriber Subscriber>
auto cache<Subscriber>::get(std::string_view topic) const -> payload_t
{
	std::shared_lock locker(m_impl->m_caches_mutex); RIWO_UNUSED(locker);
	auto pos = m_impl->m_caches.find(topic);
	return pos == m_impl->m_caches.end() ? payload_t{} : pos->second.data;
}

template <concepts::subscriber Subscriber>
auto cache<Subscriber>::get() const noexcept -> std::map<std::string,payload_t>
{
	std::map<std::string,payload_t> map;
	std::shared_lock locker(m_impl->m_caches_mutex); RIWO_UNUSED(locker);

	for(auto &[topic, cached_entry] : m_impl->m_caches)
		map.emplace(topic, cached_entry.data);
	return map;
}

template <concepts::subscriber Subscriber>
auto cache<Subscriber>::changed(std::string_view topic) noexcept -> signal_t<payload_t,payload_t>&
{
	return m_impl->changed(topic);
}

template <concepts::subscriber Subscriber>
auto cache<Subscriber>::changed() noexcept -> signal_t<std::string_view,payload_t,payload_t>&
{
	return m_impl->m_signal;
}

template <concepts::subscriber Subscriber>
template <concepts::topic_type T>
auto cache<Subscriber>::changed() noexcept -> signal_t<payload_t,payload_t>&
{
	using type = std::remove_cvref_t<T>;
	return changed(type::riwo_sbus_topic_v);
}

template <concepts::subscriber Subscriber>
template <concepts::topic_type T, typename Token>
auto cache<Subscriber>::wait_changed(Token &&token)
	requires is_token_v<Token,T>
{
	using type = std::remove_cvref_t<T>;
	return wait_changed<T>(type::riwo_sbus_topic_v, std::forward<Token>(token));
}

template <concepts::subscriber Subscriber>
template <typename Token>
auto cache<Subscriber>::wait_changed(std::string_view topic, Token &&token)
	requires is_token_v<Token>
{
	return wait_changed<payload_t>(topic, std::forward<Token>(token));
}

template <concepts::subscriber Subscriber>
template <typename T, typename Token>
auto cache<Subscriber>::wait_changed(std::string_view topic, Token &&token)
	requires is_token_v<Token,T>
{
	using type = std::remove_cvref_t<T>;
	if constexpr( concepts::topic_type<type> )
	{
		if( topic != type::riwo_sbus_topic_v )
			invalid_argument::loc_throw("Topic does not match.");
	}
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		auto future = wait_changed<T>(topic,
			asio::redirect_error(use_future, adapted_error.get())
		);
		return future.get();
	}
	else if constexpr( is_sync_opt_token_v<Token> )
		return wait_changed<T>(topic, use_future).get();
	else
	{
		return initiate_io<changed_result<T>>(get_executor(),
		[impl = m_impl->shared_from_this(), wait_topic = std::string(topic)]
		<typename Handler>(Handler &&handler) mutable
		{
			impl->template async_wait_changed<T>(
				std::move(wait_topic), std::forward<Handler>(handler)
			);
		},
		std::forward<Token>(token));
	}
}

template <concepts::subscriber Subscriber>
cache<Subscriber> &cache<Subscriber>::cancel() noexcept
{
	m_impl->cancel();
	return *this;
}

template <concepts::subscriber Subscriber>
auto cache<Subscriber>::subscriber() noexcept -> subscriber_t
{
	return m_impl->m_subscriber;
}

template <concepts::subscriber Subscriber>
auto cache<Subscriber>::get_executor() noexcept -> executor_t
{
	return subscriber().get_executor();
}

}} //namespace riwo::utils::sbus


#endif //RIWO_UTILS_UTILS_SBUS_DETAIL_CACHE_H
