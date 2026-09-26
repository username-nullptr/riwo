// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_DETAIL_PROCESS_H
#define RIWO_UTILS_DETAIL_PROCESS_H

#include <riwo/utils/detail/process_io.h>

namespace riwo::utils { namespace detail
{

class RIWO_UTILS_API process
{
	RIWO_DISABLE_COPY_MOVE(process)

public:
	using executor_t = asio::any_io_executor;
	explicit process(const executor_t &exec);
	~process();

public:
	using path_t = std::filesystem::path;
	void set(const path_t &cmd, const std::vector<path_t> &args) const;
	void add_arg(const path_t &arg) const;

public:
	[[nodiscard]] sys_expected<> start() const;
	void terminate() const noexcept;
	void kill() const noexcept;

	[[nodiscard]] sys_expected<> detach() const noexcept;
	void cancel(bool release) const noexcept;

	[[nodiscard]] bool joinable() const noexcept;

public:
	[[nodiscard]] sys_expected<int> join (
		const std::chrono::nanoseconds &timeout = {}
	) const noexcept;

	[[nodiscard]] awaitable<sys_expected<int>> co_join (
		asio::cancellation_slot cancel_slot, std::chrono::nanoseconds timeout = {}
	) const noexcept;

	[[nodiscard]] awaitable<sys_expected<int>> co_join(std::error_code &error,
		asio::cancellation_slot cancel_slot, std::chrono::nanoseconds timeout = {}
	) const noexcept;

	template <typename Clock, typename Duration>
	[[nodiscard]] sys_expected<int> join
	(const std::chrono::time_point<Clock,Duration> &timeout) const noexcept
	{
		using namespace std::chrono_literals;
		auto no_wait = [this]{
			return join(-1ns);
		};
		auto wait_time = [](const auto &remaining)
		{
			auto value = std::chrono::duration_cast
				<std::chrono::nanoseconds>(remaining);

			return value > std::chrono::nanoseconds::zero() ?
				value : 1ns;
		};
		if constexpr( Clock::is_steady )
		{
			auto now = Clock::now();
			if( now < timeout )
				return join(wait_time(timeout - now));
			return no_wait();
		}
		else
		{
			auto now = Clock::now();
			if( now < timeout )
			{
				do {
					auto expected = join(wait_time(timeout - now));
					if( expected or expected.error() != std::errc::timed_out )
						return expected;
					now = Clock::now();
				}
				while( now < timeout );
				return no_wait();
			}
			return no_wait();
		}
	}

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;

	[[nodiscard]] io_expected write(const const_buffer &buf) const noexcept;
	void async_write(const const_buffer &buf, io_handler_t handler) const;

	enum class read_channel {
		std_output, std_error
	};
	[[nodiscard]] io_expected read(read_channel channel, const mutable_buffer &buf) const noexcept;
	void async_read(read_channel channel, const mutable_buffer &buf, io_handler_t handler) const;

	void normalize_read_error(error_code &error) const noexcept;
	void protect_io_error(const error_code &error) const noexcept;

public:
	void set_work_path(path_t path) noexcept;
	void setenv(std::string_view key, riwo::value value) noexcept;
	void unsetenv(std::string_view key) noexcept;

public:
	[[nodiscard]] process_state state() const noexcept;
	[[nodiscard]] int exit_code() const noexcept;
	[[nodiscard]] pid_t pid() const noexcept;

public:
	[[nodiscard]] static sys_expected<pid_t> self_pid() noexcept;
	static sys_expected<> terminate(pid_t pid) noexcept;
	static sys_expected<> kill(pid_t pid) noexcept;

	[[nodiscard]] static sys_expected<pid_t> set_single (
		const path_t &path, std::string_view key
	);
	[[nodiscard]] static sys_expected<pid_t> set_single (
		std::string_view key
	);

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

} //namespace detail

template <concepts::character CharT, concepts::exec Exec>
class RIWO_UTILS_TAPI basic_process<CharT,Exec>::impl : public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	using path_t = std::filesystem::path;

	explicit impl(const executor_t &exec) :
		m_exec(exec), m_detail(m_exec) {}

	impl() : m_exec(riwo::get_executor()),
		m_detail(m_exec) {}

public:
	void set(string_t cmd, args_t args)
	{
		std::vector<path_t> paths;
		for(auto &arg : args)
			paths.emplace_back(std::move(*arg));
		m_detail.set(std::move(cmd), paths);
	}

	template <typename...Args>
	void set(string_t cmd, Args&&...args)
	{
		m_detail.set(std::move(cmd), {});
		(
			m_detail.add_arg(std::format (
				l_str(char_t,"{}"), std::forward<Args>(args)
			)), ...
		);
	}

public:
	template <typename...Args>
	[[nodiscard]] sys_expected<> start(const string_t &cmd, Args&&...args) noexcept
	{
		if( joinable() )
		{
			return sys_unexpected(make_error_code (
				std::errc::device_or_resource_busy
			));
		}
		try
		{
			set(cmd, std::forward<Args>(args)...);
			auto expected = m_detail.start();
			if( not expected )
			{
				return sys_unexpected (
					riwo::detail::canonical_error(expected.error())
				);
			}
			return expected;
		}
		catch(...) {}
		return sys_unexpected (
			exception_error(std::current_exception())
		);
	}

	[[nodiscard]] sys_expected<> start(const string_t &cmd, const args_t &args) noexcept
	{
		if( joinable() )
		{
			return sys_unexpected(make_error_code (
				std::errc::device_or_resource_busy
			));
		}
		try
		{
			if( not cmd.empty() )
				set(cmd, args);

			auto expected = m_detail.start();
			if( not expected )
			{
				return sys_unexpected (
					riwo::detail::canonical_error(expected.error())
				);
			}
			return expected;
		}
		catch(...) {}
		return sys_unexpected (
			exception_error(std::current_exception())
		);
	}

	void terminate() noexcept {
		m_detail.terminate();
	}
	void kill() noexcept {
		m_detail.kill();
	}

	void detach()
	{
		if( auto expected = m_detail.detach(); not expected )
		{
			system_error::loc_throw (
				riwo::detail::canonical_error(expected.error())
			);
		}
	}

	void cancel(cancel_option option) noexcept
	{
		switch( option )
		{
		case cancel_option::terminate:
			m_detail.terminate();
			break;

		case cancel_option::kill:
			m_detail.kill();
			break;

		case cancel_option::none:
		case cancel_option::detach:
			break;
		}
		m_detail.cancel(option != cancel_option::none);
	}

	[[nodiscard]] bool joinable() const noexcept {
		return m_detail.joinable();
	}

public:
	template <typename Token>
	[[nodiscard]] auto join(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_error_code_token_v<Token> )
		{
			using namespace std::chrono_literals;
			return expected_value_or_error(join_expected(0ns), token);
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			using namespace std::chrono_literals;
			return expected_value_or_throw(join_expected(0ns));
		}
		else if constexpr( is_time_v<token_t> )
			return expected_value_or_throw(join_expected(token));
		else
		{
			return initiate_expected<int>(m_exec,
			[self = this->shared_from_this()]() mutable -> awaitable<sys_expected<int>> {
				co_return co_await self->co_join_expected();
			}, std::forward<Token>(token));
		}
	}

	template <concepts::time_p Time>
	[[nodiscard]] sys_expected<int> join_expected(const Time &timeout) noexcept
	{
		auto expected = [&]() -> sys_expected<int>
		{
			if constexpr( concepts::duration_p<Time> )
			{
				return m_detail.join(std::chrono::duration_cast
					<std::chrono::nanoseconds>(timeout));
			}
			else
				return m_detail.join(timeout);
		}();
		riwo::detail::canonicalize_expected(expected);
		return expected;
	}

	[[nodiscard]] awaitable<sys_expected<int>> co_join_expected()
	{
		auto cancellation = co_await asio::this_coro::cancellation_state;
		co_return co_await m_detail.co_join (
			cancellation.slot(), std::chrono::nanoseconds::zero()
		);
	}

	void cleanup_exec() noexcept
	{
		if( not joinable() )
			return ;
		kill();
		RIWO_UNUSED(join_expected(std::chrono::nanoseconds::zero()));
	}

	template <typename Token>
	[[nodiscard]] auto run(string_t cmd, args_t args, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		using unbound_t = token_unbound_t<token_t>;

		if constexpr( is_detached_v<unbound_t> )
		{
			auto started = start(cmd, args);
			if( not started )
				system_error::loc_throw(started.error());
			detach();
		}
		else if constexpr( is_error_code_token_v<Token> )
		{
			auto started = start(cmd, args);
			if( not started )
			{
				token = started.error();
				return int {};
			}
			return join(std::forward<Token>(token));
		}
		else if constexpr( is_sync_opt_token_v<Token> or is_time_v<token_t> )
		{
			auto started = start(cmd, args);
			if( not started )
				system_error::loc_throw(started.error());
			return join(std::forward<Token>(token));
		}
		else
		{
			return initiate_expected<int>(m_exec, [self = this->shared_from_this(),
				command = std::move(cmd), arguments = std::move(args)
			]() mutable -> awaitable<sys_expected<int>>
			{
				auto started = self->start(command, arguments);
				if( not started )
					co_return sys_unexpected(started.error());
				co_return co_await self->co_join_expected();
			},
			std::forward<Token>(token));
		}
	}

public:
	template <typename Token>
	[[nodiscard]] auto write(const const_buffer &buf, Token &&token)
	{
		if constexpr( is_error_code_token_v<Token> )
		{
			auto result = m_detail.write(buf);
			if( not result )
				m_detail.protect_io_error(result.error());
			return expected_value_or_error(std::move(result), token);
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			auto result = m_detail.write(buf);
			if( not result )
				m_detail.protect_io_error(result.error());
			return expected_value_or_throw(result);
		}
		else
		{
			using token_t = std::remove_cvref_t<Token>;
			if constexpr( is_detached_v<token_unbound_t<token_t>> )
			{
				auto owner = detail::copy_process_buffer(buf);
				return initiate_io<size_t>(m_exec,
				[self = this->shared_from_this(), owner]<typename Handle>(Handle &&completion_token) mutable
				{
					return self->async_write(buffer(*owner), asio::consign (
						std::forward<Handle>(completion_token), owner
					));
				},
				std::forward<Token>(token));
			}
			else
			{
				return initiate_io<size_t>(m_exec,
				[self = this->shared_from_this(), buf]<typename Handle>(Handle &&completion_token) mutable
				{
					return self->async_write(buf,
						std::forward<Handle>(completion_token)
					);
				},
				std::forward<Token>(token));
			}
		}
	}

	template <typename Token>
	[[nodiscard]] auto async_write(const const_buffer &buf, Token &&token)
	{
		return detail::initiate_process_io (
		[self = this->shared_from_this(), buf](auto completion_handler) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_handler);
			auto exec = asio::get_associated_executor(completion_handler, self->m_exec);

			auto immediate_exec = asio::get_associated_immediate_executor (
				completion_handler, self->m_exec
			);
			auto alloc = asio::get_associated_allocator(completion_handler);

			auto owned_handler = [self, handler = std::move(completion_handler)]
			(error_code error, std::size_t size) mutable
			{
				self->m_detail.protect_io_error(error);
				std::move(handler)(error, size);
			};
			auto bound_handler = asio::bind_immediate_executor(immediate_exec,
				asio::bind_allocator(alloc, asio::bind_executor(exec,
					asio::bind_cancellation_slot(slot, std::move(owned_handler))
				))
			);
			self->m_detail.async_write(buf,
				detail::process::io_handler_t(std::move(bound_handler))
			);
		},
		std::forward<Token>(token));
	}

public:
	using read_channel = detail::process::read_channel;

	template <read_channel Channel, typename Token>
	[[nodiscard]] auto read(const mutable_buffer &buf, Token &&token)
	{
		if constexpr( is_error_code_token_v<Token> )
		{
			auto result = m_detail.read(Channel, buf);
			if( not result )
			{
				auto read_error = result.error();
				canonicalize_read_error(read_error);
				m_detail.protect_io_error(read_error);
				result = sys_unexpected(read_error);
			}
			return expected_value_or_error(std::move(result), token);
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			auto result = m_detail.read(Channel, buf);
			if( not result )
			{
				auto read_error = result.error();
				canonicalize_read_error(read_error);
				m_detail.protect_io_error(read_error);
				result = sys_unexpected(read_error);
			}
			return expected_value_or_throw(result);
		}
		else
		{
			return initiate_io<size_t>(m_exec,
			[self = this->shared_from_this(), buf]<typename Handle>(Handle &&completion_token) mutable
			{
				return self->template async_read<Channel>(buf,
					std::forward<Handle>(completion_token)
				);
			},
			std::forward<Token>(token));
		}
	}

	template <read_channel Channel, typename Token>
	[[nodiscard]] auto async_read(const mutable_buffer &buf, Token &&token)
	{
		return detail::initiate_process_io (
		[self = this->shared_from_this(), buf](auto completion_handler) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_handler);
			auto exec = asio::get_associated_executor(completion_handler, self->m_exec);

			auto immediate_exec = asio::get_associated_immediate_executor(
				completion_handler, self->m_exec
			);
			auto alloc = asio::get_associated_allocator(completion_handler);

			auto owned_handler = [self, handler = std::move(completion_handler)]
			(error_code error, std::size_t size) mutable
			{
				self->m_detail.normalize_read_error(error);
				canonicalize_read_error(error);

				self->m_detail.protect_io_error(error);
				std::move(handler)(error, size);
			};
			auto bound_handler = asio::bind_immediate_executor(immediate_exec,
				asio::bind_allocator(alloc, asio::bind_executor(exec,
					asio::bind_cancellation_slot(slot, std::move(owned_handler))
				))
			);
			self->m_detail.async_read(Channel, buf,
				detail::process::io_handler_t(std::move(bound_handler))
			);
		},
		std::forward<Token>(token));
	}

	template <read_channel Channel>
	[[nodiscard]] sys_expected<std::vector<std::byte>> read_all()
	{
		std::vector<std::byte> result;
		std::array<std::byte,8192> chunk {};

		auto read_result = m_detail.read(Channel, buffer(chunk));
		while( read_result and *read_result != 0 )
		{
			const auto read_size = *read_result;
			result.insert (
				result.end(), chunk.begin(),
				chunk.begin() + read_size
			);
			read_result = m_detail.read(Channel, buffer(chunk));
		}
		if( not read_result )
		{
			if( const auto read_error = read_result.error(); not is_read_eof(read_error) )
			{
				m_detail.protect_io_error(read_error);
				return sys_unexpected(read_error);
			}
		}
		return result;
	}

	template <read_channel Channel, concepts::buffer Buffer, typename Handler>
	class read_buffer_state final :
		public std::enable_shared_from_this<read_buffer_state<Channel,Buffer,Handler>>
	{
		using state_t = read_buffer_state;

	public:
		read_buffer_state(std::shared_ptr<impl> implementation, Handler handler) :
			m_implementation(std::move(implementation)),
			m_handler(std::move(handler)) {}

		static void launch(std::shared_ptr<impl> implementation, Handler handler)
		{
			auto associated_allocator = asio::get_associated_allocator(handler);
			using allocator_t = std::allocator_traits
				<decltype(associated_allocator)>::template rebind_alloc<state_t>;

			auto operation = std::allocate_shared<state_t>(
				allocator_t(associated_allocator),
				std::move(implementation), std::move(handler)
			);
			operation->read_next();
		}

	private:
		void read_next()
		{
			auto operation_state = this->shared_from_this();
			auto associated_slot = asio::get_associated_cancellation_slot(m_handler);

			auto associated_executor = asio::get_associated_executor (
				m_handler, m_implementation->m_exec
			);
			auto associated_allocator = asio::get_associated_allocator(m_handler);

			auto next_handler = asio::bind_allocator(associated_allocator,
				asio::bind_executor(associated_executor,
					asio::bind_cancellation_slot(associated_slot, [state = std::move(operation_state)]
					(error_code read_error, std::size_t read_size) mutable {
						state->read_complete(read_error, read_size);
					})
				)
			);
			if constexpr( is_array_buffer_v<Buffer> )
			{
				m_implementation->template async_read<Channel>(
					buffer(m_result), std::move(next_handler)
				);
			}
			else
			{
				m_implementation->template async_read<Channel>(
					buffer(m_chunk), std::move(next_handler)
				);
			}
		}

		void read_complete(error_code read_error, std::size_t read_size)
		{
			if constexpr( is_array_buffer_v<Buffer> )
				complete(read_error, std::move(m_result));
			else
			{
				if( read_size > 0 )
				{
					try {
						m_source.insert(m_source.end(), m_chunk.begin(),
							m_chunk.begin() + read_size
						);
					}
					catch(const std::bad_alloc&)
					{
						complete(make_system_error_code(std::errc::not_enough_memory), {});
						return ;
					}
				}
				if( not read_error and read_size > 0 )
				{
					read_next();
					return ;
				}
				if( read_error and not is_read_eof(read_error) )
				{
					complete(read_error, {});
					return ;
				}
				try {
					complete({}, copy_buffer_data<Buffer>(
						std::move(m_source)
					));
				}
				catch(const std::bad_alloc&) {
					complete(make_system_error_code(std::errc::not_enough_memory), {});
				}
			}
		}

		void complete(error_code read_error, Buffer result)
		{
			auto final_handler = std::move(m_handler);
			std::move(final_handler)(read_error, std::move(result));
		}

	private:
		std::shared_ptr<impl> m_implementation {};
		Handler m_handler;
		Buffer m_result {};

		std::vector<std::byte> m_source {};
		std::array<std::byte,8192> m_chunk {};
	};

	template <read_channel Channel, concepts::buffer Buffer, typename Token>
	[[nodiscard]] auto async_read_buffer(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));

		return asio::async_initiate<token_t,void(error_code,Buffer)>(
		[self = this->shared_from_this()]<typename Handle>(Handle completion_handler) mutable
		{
			using handler_t = Handle;
			read_buffer_state<Channel,Buffer,handler_t>::launch (
				std::move(self), std::move(completion_handler)
			);
		},
		completion_token);
	}

	[[nodiscard]] static bool is_read_eof(const error_code &error) noexcept
	{
		const error_code eof_error = asio::error::eof;
		const auto broken_pipe = std::make_error_condition(std::errc::broken_pipe);
		const auto error_condition = error.default_error_condition();

		return equivalent_error(error, eof_error) or
			   equivalent_error(error_condition, broken_pipe);
	}

	template <typename Left, typename Right>
	[[nodiscard]] static bool equivalent_error
	(const Left &left, const Right &right) noexcept
	{
		return left.value() == right.value() and (
			left.category() == right.category() or
			std::string_view(left.category().name()) == right.category().name()
		);
	}

	static void canonicalize_read_error(error_code &error) noexcept
	{
		if( is_read_eof(error) )
			error = make_error_code(asio::error::eof);
	}

public:
	void set_work_path(path_t path) noexcept {
		m_detail.set_work_path(std::move(path));
	}
	void setenv(std::string_view key, riwo::value value) noexcept {
		m_detail.setenv(key, std::move(value));
	}
	void unsetenv(std::string_view key) noexcept {
		m_detail.unsetenv(key);
	}

public:
	[[nodiscard]] process_state state() const noexcept {
		return m_detail.state();
	}
	[[nodiscard]] int exit_code() const noexcept {
		return m_detail.exit_code();
	}
	[[nodiscard]] pid_t pid() const noexcept {
		return m_detail.pid();
	}
	[[nodiscard]] executor_t get_executor() const noexcept {
		return m_exec;
	}

public:
	[[nodiscard]] static sys_expected<pid_t> self_pid() noexcept {
		return detail::process::self_pid();
	}
	static sys_expected<> terminate(pid_t pid) noexcept {
		return detail::process::terminate(pid);
	}
	static sys_expected<> kill(pid_t pid) noexcept {
		return detail::process::kill(pid);
	}

	[[nodiscard]] static sys_expected<pid_t>
	set_single(const path_t &path, std::string_view key) {
		return detail::process::set_single(path, key);
	}
	[[nodiscard]] static sys_expected<pid_t>
	set_single(std::string_view key) {
		return detail::process::set_single(key);
	}

private:
	executor_t m_exec {};
	detail::process m_detail;
};

template <concepts::character CharT, concepts::exec Exec>
basic_process<CharT,Exec>::basic_process(string_t cmd, args_t args)
	requires concepts::match_sched<io_executor_t,Exec> :
	m_impl(std::make_shared<impl>())
{
	m_impl->set(std::move(cmd), std::move(args));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename...Args>
basic_process<CharT,Exec>::basic_process(string_t cmd, Args&&...args) requires
	concepts::match_sched<io_executor_t,Exec> and concepts::formatter<char_t,Args...> :
	m_impl(std::make_shared<impl>())
{
	m_impl->set(std::move(cmd), std::forward<Args>(args)...);
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Scheduler>
basic_process<CharT,Exec>::basic_process(Scheduler &&exec, string_t cmd, args_t args) requires
	(not std::same_as<std::remove_cvref_t<Scheduler>,basic_process>) and
	concepts::match_sched<Scheduler,Exec> :
	m_impl(std::make_shared<impl>(executor_t(get_executor_helper(std::forward<Scheduler>(exec)))))
{
	m_impl->set(std::move(cmd), std::move(args));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Scheduler, typename...Args>
basic_process<CharT,Exec>::basic_process(Scheduler &&exec, string_t cmd, Args&&...args) requires
	(not std::same_as<std::remove_cvref_t<Scheduler>,basic_process>) and
	concepts::match_sched<Scheduler,Exec> and
	concepts::formatter<char_t,Args...> :
	m_impl(std::make_shared<impl>(executor_t(get_executor_helper(std::forward<Scheduler>(exec)))))
{
	m_impl->set(std::move(cmd), std::forward<Args>(args)...);
}

template <concepts::character CharT, concepts::exec Exec>
basic_process<CharT,Exec>::basic_process(basic_process &&other) noexcept :
	m_impl(std::move(other.m_impl))
{

}

template <concepts::character CharT, concepts::exec Exec>
basic_process<CharT,Exec> &basic_process<CharT,Exec>::operator=(basic_process &&other) noexcept
{
	if( &other == this )
		return *this;

	// Like std::thread, replacing an owned joinable child terminates.
	if( joinable() )
	{
		m_impl->cleanup_exec();
		std::terminate();
	}
	m_impl = std::move(other.m_impl);
	return *this;
}

template <concepts::character CharT, concepts::exec Exec>
basic_process<CharT,Exec>::~basic_process() = default;

template <concepts::character CharT, concepts::exec Exec>
template <typename...Args>
sys_expected<> basic_process<CharT,Exec>::start(const string_t &cmd, Args&&...args) noexcept
	requires concepts::formatter<char_t,Args...>
{
	return m_impl->start(cmd, std::forward<Args>(args)...);
}

template <concepts::character CharT, concepts::exec Exec>
sys_expected<> basic_process<CharT,Exec>::start(const string_t &cmd, const args_t &args) noexcept
{
	return m_impl->start(cmd, args);
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::terminate() noexcept
{
	m_impl->terminate();
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::kill() noexcept
{
	m_impl->kill();
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::detach()
{
	if( not m_impl )
	{
		system_error::loc_throw(make_error_code (
			std::errc::invalid_argument
		));
	}
	m_impl->detach();
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::cancel(cancel_option option) noexcept
{
	m_impl->cancel(option);
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::join(Token &&token)
	requires dis_detach_token_v<Token,int> or concepts::time_p<Token>
{
	using unbound_t = token_unbound_t<Token>;
	if constexpr( not is_deferred_v<unbound_t> )
	{
		if( not m_impl )
		{
			const auto error = make_system_error_code(std::errc::invalid_argument);
			if constexpr( is_error_code_token_v<Token> )
			{
				token = error;
				return int {};
			}
			else if constexpr( is_sync_opt_token_v<Token> or is_time_v<std::remove_cvref_t<Token>> )
				system_error::loc_throw(error);
			else
			{
				return initiate_expected<int>(asio::system_executor{},
				[error]() -> awaitable<sys_expected<int>> {
					co_return sys_unexpected(error);
				}, std::forward<Token>(token));
			}
		}
	}
	return m_impl->join(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <concepts::tf_opt_token<error_code,size_t> Token>
auto basic_process<CharT,Exec>::write(const const_buffer &buf, Token &&token)
{
	return m_impl->write(buf, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::read(const mutable_buffer &buf, Token &&token)
	requires dis_detach_token_v<Token,size_t>
{
	return m_impl->template read<impl::read_channel::std_output>
		(buf, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <concepts::buffer Buffer, typename Token>
auto basic_process<CharT,Exec>::read(Token &&token) requires
	dis_detach_token_v<Token,Buffer>
{
	using channel = impl::read_channel;
	if constexpr( is_array_buffer_v<Buffer> )
	{
		if constexpr( is_error_code_token_v<Token> )
		{
			Buffer result {};
			RIWO_UNUSED(m_impl->template read<channel::std_output>(
				buffer(result), std::forward<Token>(token)
			));
			return result;
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			Buffer result {};
			RIWO_UNUSED(m_impl->template read<channel::std_output>(
				buffer(result), std::forward<Token>(token)
			));
			return result;
		}
		else
		{
			return initiate_io<Buffer>(get_executor(),
			[implementation = m_impl]<typename Handle>(Handle &&completion_token) mutable
			{
				return implementation->template async_read_buffer
					<channel::std_output,Buffer>(std::forward<Handle>(completion_token));
			},
			std::forward<Token>(token));
		}
	}
	else if constexpr( is_error_code_token_v<Token> )
	{
		auto source = expected_value_or_error (
			m_impl->template read_all<channel::std_output>(), token
		);
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		auto source = expected_value_or_throw (
			m_impl->template read_all<channel::std_output>()
		);
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else
	{
		return initiate_io<Buffer>(get_executor(),
		[implementation = m_impl]<typename Handle>(Handle &&completion_token) mutable
		{
			return implementation->template async_read_buffer
				<channel::std_output,Buffer>(std::forward<Handle>(completion_token));
		},
		std::forward<Token>(token));
	}
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::read(Token &&token) requires
	dis_detach_token_v<Token,std::vector<std::byte>>
{
	return read<std::vector<std::byte>>(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::read_stderr(const mutable_buffer &buf, Token &&token)
	requires dis_detach_token_v<Token,size_t>
{
	return m_impl->template read<impl::read_channel::std_error>
		(buf, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <concepts::buffer Buffer, typename Token>
auto basic_process<CharT,Exec>::read_stderr(Token &&token) requires
	dis_detach_token_v<Token,Buffer>
{
	using channel = impl::read_channel;
	if constexpr( is_array_buffer_v<Buffer> )
	{
		if constexpr( is_error_code_token_v<Token> )
		{
			Buffer result {};
			RIWO_UNUSED(m_impl->template read<channel::std_error>(
				buffer(result), std::forward<Token>(token)
			));
			return result;
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			Buffer result {};
			RIWO_UNUSED(m_impl->template read<channel::std_error>(
				buffer(result), std::forward<Token>(token)
			));
			return result;
		}
		else
		{
			return initiate_io<Buffer>(get_executor(),
			[implementation = m_impl]<typename Handle>(Handle &&completion_token) mutable
			{
				return implementation->template async_read_buffer
					<channel::std_error,Buffer>(std::forward<Handle>(completion_token));
			},
			std::forward<Token>(token));
		}
	}
	else if constexpr( is_error_code_token_v<Token> )
	{
		auto source = expected_value_or_error (
			m_impl->template read_all<channel::std_error>(), token
		);
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		auto source = expected_value_or_throw (
			m_impl->template read_all<channel::std_error>()
		);
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else
	{
		return initiate_io<Buffer>(get_executor(),
		[implementation = m_impl]<typename Handle>(Handle &&completion_token) mutable
		{
			return implementation->template async_read_buffer
				<channel::std_error,Buffer>(std::forward<Handle>(completion_token));
		},
		std::forward<Token>(token));
	}
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::read_stderr(Token &&token) requires
	dis_detach_token_v<Token,std::vector<std::byte>>
{
	return read_stderr<std::vector<std::byte>>(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::run(const string_t &cmd, const args_t &args, Token &&token)
	requires task_token_v<Token,int>
{
	return m_impl->run(cmd, args, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::run(const string_t &cmd, Token &&token)
	requires task_token_v<Token,int>
{
	return run(cmd, {}, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::run(Token &&token)
	requires task_token_v<Token,int>
{
	return run({}, {}, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::set_work_path(path_t path) noexcept
{
	m_impl->set_work_path(std::move(path));
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::setenv(std::string_view key, riwo::value value) noexcept
{
	m_impl->setenv(key, std::move(value));
}

template <concepts::character CharT, concepts::exec Exec>
void basic_process<CharT,Exec>::unsetenv(std::string_view key) noexcept
{
	m_impl->unsetenv(key);
}

template <concepts::character CharT, concepts::exec Exec>
auto basic_process<CharT,Exec>::state() const noexcept -> state_t
{
	return m_impl->state();
}

template <concepts::character CharT, concepts::exec Exec>
int basic_process<CharT,Exec>::exit_code() const noexcept
{
	return m_impl->exit_code();
}

template <concepts::character CharT, concepts::exec Exec>
pid_t basic_process<CharT,Exec>::pid() const noexcept
{
	return m_impl ? m_impl->pid() : 0;
}

template <concepts::character CharT, concepts::exec Exec>
bool basic_process<CharT,Exec>::joinable() const noexcept
{
	return m_impl and m_impl->joinable();
}

template <concepts::character CharT, concepts::exec Exec>
basic_process<CharT,Exec>::executor_t
basic_process<CharT,Exec>::get_executor() const noexcept
{
	return m_impl->get_executor();
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::exec(const string_t &cmd, const args_t &args, Token &&token)
	requires exec_token_v<Token>
{
	return exec(riwo::get_executor(), cmd, args, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <typename Token>
auto basic_process<CharT,Exec>::exec(const string_t &cmd, Token &&token)
	requires exec_token_v<Token>
{
	return exec(riwo::get_executor(), cmd, {}, std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
template <concepts::match_sched<Exec> Exec0, typename Token>
auto basic_process<CharT,Exec>::exec(Exec0 &&exec, const string_t &cmd,
	const args_t &args, Token &&token) requires exec_token_v<Token>
{
	using token_t = std::remove_cvref_t<Token>;
	using unbound_t = token_unbound_t<token_t>;
	using process_t = basic_process;

	auto object_expected = [&]() -> sys_expected<std::shared_ptr<process_t>>
	{
		try {
			return std::make_shared<process_t>(
				exec, cmd, args
			);
		}
		catch(const std::system_error &exception) {
			return sys_unexpected(exception.code());
		}
		catch(const std::bad_alloc&) {
			return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
		}
		catch(...) {}
		return sys_unexpected(make_system_error_code(std::errc::io_error));
	}();

	if constexpr( is_error_code_token_v<Token> )
	{
		if( not object_expected )
		{
			token = object_expected.error();
			return sys_expected<int>(sys_unexpected(error_code(token)));
		}
		auto object = std::move(*object_expected);
		auto started = object->start();
		if( not started )
		{
			token = started.error();
			return sys_expected<int>(sys_unexpected(error_code(token)));
		}
		auto result = object->m_impl->join_expected (
			std::chrono::nanoseconds::zero()
		);
		if( not result )
		{
			token = result.error();
			object->m_impl->cleanup_exec();
		}
		else
			token.clear();
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> or is_time_v<token_t> )
	{
		if( not object_expected )
			return sys_expected<int>(sys_unexpected(object_expected.error()));

		auto object = std::move(*object_expected);
		auto started = object->start();

		if( not started )
			return sys_expected<int>(sys_unexpected(started.error()));

		auto result = [&]() -> sys_expected<int>
		{
			if constexpr( is_time_v<token_t> )
				return object->m_impl->join_expected(token);
			else
			{
				return object->m_impl->join_expected (
					std::chrono::nanoseconds::zero()
				);
			}
		}();
		if( not result )
			object->m_impl->cleanup_exec();
		return result;
	}
	else if constexpr( is_detached_v<unbound_t> )
	{
		if( not object_expected )
			return sys_expected<int>(sys_unexpected(object_expected.error()));

		auto object = std::move(*object_expected);
		auto started = object->start();

		if( not started )
			return sys_expected<int>(sys_unexpected(started.error()));
		try {
			object->detach();
		}
		catch(const std::system_error &exception)
		{
			const auto detach_error = exception.code();
			object->m_impl->cleanup_exec();
			return sys_expected<int>(sys_unexpected(detach_error));
		}
		return sys_expected<int>(0);
	}
	else
	{
		const auto operation_error = object_expected ?
			error_code{} : object_expected.error();

		auto process_object = object_expected ?
			std::move(*object_expected) : nullptr;

		auto operation_exec = process_object ?
			process_object->get_executor() : executor_t(get_executor_helper(exec));

		auto operation = [object = std::move(process_object), operation_error]
		()mutable -> awaitable<sys_expected<int>>
		{
			if( operation_error )
				co_return sys_unexpected(operation_error);

			auto started = object->start();
			if( not started )
				co_return sys_unexpected(started.error());

			sys_expected<int> result;
			try {
				result = co_await object->m_impl->co_join_expected();
			}
			catch(...)
			{
				const auto operation_exception_error =
					exception_error(std::current_exception());

				object->m_impl->cleanup_exec();
				co_return sys_unexpected(operation_exception_error);
			}
			if( not result )
			{
				const auto operation_result_error = result.error();
				object->m_impl->cleanup_exec();
				co_return sys_unexpected(operation_result_error);
			}
			co_return result;
		};
		if constexpr( is_use_future_v<unbound_t> or
			is_use_awaitable_v<unbound_t> or is_deferred_v<unbound_t> )
		{
			return initiate_preserved_expected<int>(operation_exec,
				std::move(operation), std::forward<Token>(token)
			);
		}
		else
		{
			return initiate_expected<int>(operation_exec,
				std::move(operation), std::forward<Token>(token)
			);
		}
	}
}

template <concepts::character CharT, concepts::exec Exec>
template <concepts::match_sched<Exec> Exec0, typename Token>
auto basic_process<CharT,Exec>::exec(Exec0 &&exec, const string_t &cmd, Token &&token)
	requires exec_token_v<Token>
{
	return basic_process::exec(std::forward<Exec0>(exec), cmd, {},
		std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec>
sys_expected<pid_t> basic_process<CharT,Exec>::self_pid() noexcept
{
	auto expected = impl::self_pid();
	riwo::detail::canonicalize_expected(expected);
	return expected;
}

template <concepts::character CharT, concepts::exec Exec>
sys_expected<> basic_process<CharT,Exec>::terminate(pid_t pid) noexcept
{
	auto expected = impl::terminate(pid);
	riwo::detail::canonicalize_expected(expected);
	return expected;
}

template <concepts::character CharT, concepts::exec Exec>
sys_expected<> basic_process<CharT,Exec>::kill(pid_t pid) noexcept
{
	auto expected = impl::kill(pid);
	riwo::detail::canonicalize_expected(expected);
	return expected;
}

template <concepts::character CharT, concepts::exec Exec>
sys_expected<pid_t> basic_process<CharT,Exec>::set_single(const path_t &path, std::string_view key)
{
	auto expected = impl::set_single(path, key);
	riwo::detail::canonicalize_expected(expected);
	return expected;
}

template <concepts::character CharT, concepts::exec Exec>
sys_expected<pid_t> basic_process<CharT,Exec>::set_single(std::string_view key)
{
	auto expected = impl::set_single(key);
	riwo::detail::canonicalize_expected(expected);
	return expected;
}

} //namespace riwo::utils


#endif //RIWO_UTILS_DETAIL_PROCESS_H
