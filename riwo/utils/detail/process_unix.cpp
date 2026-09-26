// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifdef __unix__

#include <riwo/utils/process.h>
#include <riwo/utils/logger.h>

#include <riwo/coro/utils.h>
#include <utility>

#include <sys/wait.h>
#include <wordexp.h>
#include <csignal>
#include <pwd.h>
#include <map>

#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace riwo::utils::detail
{

namespace fs = std::filesystem;

using namespace std::chrono_literals;
using namespace operators;

using executor_t = process::executor_t;
using descriptor_t = asio::posix::stream_descriptor;

using read_channel_t = process::read_channel;
using args_t = std::vector<std::string>;
using envs_t = std::map<std::string, value>;

[[nodiscard]] static error_code sys_error() {
	return error_code(std::error_code(errno, std::system_category()));
}

[[nodiscard]] static bool is_shell_assignment(std::string_view word) noexcept
{
	const auto equal_pos = word.find('=');
	if( equal_pos == std::string_view::npos or equal_pos == 0 )
		return false;

	auto is_name_head = [](char character) noexcept
	{
		return character == '_' or
			(character >= 'A' and character <= 'Z') or
			(character >= 'a' and character <= 'z');
	};
	auto is_name_body = [is_name_head](char character) noexcept
	{
		return is_name_head(character) or
			(character >= '0' and character <= '9');
	};
	if( not is_name_head(word.front()) )
		return false;

	for(size_t index = 1; index < equal_pos; ++index)
	{
		if( not is_name_body(word[index]) )
			return false;
	}
	return true;
}

[[nodiscard]] static bool exit_on_parent_exit(::pid_t parent_pid) noexcept
{
#ifdef __linux__
	if( prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 )
		return false;
	return getppid() == parent_pid;
#else
	RIWO_UNUSED(parent_pid);
	return true;
#endif
}

namespace
{

class RIWO_DECL_HIDDEN vindicator final :
	public std::enable_shared_from_this<vindicator>
{
	RIWO_DISABLE_COPY_MOVE(vindicator)

public:
	using ptr_t = std::shared_ptr<vindicator>;
	explicit vindicator(const executor_t &exec) :
		m_exec(exec), m_stdin(exec), m_stdout(exec), m_stderr(exec) {}

	~vindicator() noexcept {
		stop_and_reap(false);
	}

public:
	[[nodiscard]] sys_expected<> start(bool is_pipe, std::string_view cmd,
		const args_t &args, std::string_view work_path, const envs_t &envs)
	{
		error_code error; RIWO_UNUSED(error);
		sys_expected<> expected;

		if( m_joinable.load(std::memory_order_acquire) )
		{
			return expected.despair(std::make_error_code (
				std::errc::device_or_resource_busy
			));
		}
		else if( cmd.empty() )
		{
			return expected.despair(std::make_error_code (
				std::errc::no_such_file_or_directory
			));
		}
		join_monitor_thread();
		finish_io(m_generation.load(std::memory_order_acquire), true);

		m_released.store(false, std::memory_order_release);
		auto generation = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;

		// From the perspective of the child process.
		int stdin_pipe [2] {-1,-1};
		int stdout_pipe[2] {-1,-1};
		int stderr_pipe[2] {-1,-1};
		::pid_t parent_pid = -1;

		error = m_stdin.close(error);
		if( pipe2(stdin_pipe, O_NONBLOCK) < 0 )
		{
			expected.despair(sys_error());
			goto stdin_error;
		}
		error = m_stdout.close(error);
		if( pipe2(stdout_pipe, O_NONBLOCK) < 0 )
		{
			expected.despair(sys_error());
			goto stdout_error;
		}
		error = m_stderr.close(error);
		if( pipe2(stderr_pipe, O_NONBLOCK) < 0 )
		{
			expected.despair(sys_error());
			goto stderr_error;
		}
		parent_pid = getpid();
		m_pid = fork();
		if( m_pid < 0 )
		{
			expected.despair(sys_error());
			goto fork_error;
		}
		else if( m_pid == 0 ) //child
		{
			if( not exit_on_parent_exit(parent_pid) )
				_exit(255);

			fcntl(stdin_pipe[0], F_SETFL,
				fcntl(stdin_pipe[0], F_GETFL) & ~O_NONBLOCK
			);
			fcntl(stdout_pipe[1], F_SETFL,
				fcntl(stdout_pipe[1], F_GETFL) & ~O_NONBLOCK
			);
			fcntl(stderr_pipe[1], F_SETFL,
				fcntl(stderr_pipe[1], F_GETFL) & ~O_NONBLOCK
			);
			dup2(stdin_pipe [0], STDIN_FILENO );
			dup2(stdout_pipe[1], STDOUT_FILENO);
			dup2(stderr_pipe[1], STDERR_FILENO);

			close(stdin_pipe [1]);
			close(stdout_pipe[0]);
			close(stderr_pipe[0]);

			int res = chdir(work_path.data());
			RIWO_UNUSED(res);

			for(auto &[key, value] : envs)
				setenv(key.c_str(), value->c_str(), true);

			if( is_pipe )
			{
				default_shell()
				.transform([&](const std::string &shell)
				{
					res = execlp(shell.c_str(),
						shell.c_str(), "-c", cmd.data(), nullptr
					);
					return shell;
				})
				.or_else([&]
				{
					expected.despair(std::make_error_code(
						std::errc::no_such_file_or_directory
					));
					_exit(-1);
				});
			}
			else
			{
				auto _args = new const char*[1 + args.size() + 1] {
					cmd.data(), nullptr
				};
				for(size_t i=0; i<args.size(); i++)
				{
					if( not args[i].empty() )
						_args[1 + i] = args[i].c_str();
				}
				res = execvp(cmd.data(), const_cast<char**>(_args));
				delete[] _args;
			}
			perror("||| *** *** *** Error: execvp *** >>> ");
			_exit(res);
		}
		if( not expected )
			goto exec_error;

		error.clear();
		error = m_stdin.assign(stdin_pipe[1], error);
		if( error )
		{
			expected.despair(error);
			stop_and_reap(true);
			goto exec_error;
		}
		stdin_pipe[1] = -1;
		close(stdin_pipe[0]);
		stdin_pipe[0] = -1;

		error.clear();
		error = m_stdout.assign(stdout_pipe[0], error);
		if( error )
		{
			expected.despair(error);
			stop_and_reap(true);
			goto exec_error;
		}
		stdout_pipe[0] = -1;
		close(stdout_pipe[1]);
		stdout_pipe[1] = -1;

		error.clear();
		error = m_stderr.assign(stderr_pipe[0], error);
		if( error )
		{
			expected.despair(error);
			stop_and_reap(true);
			goto exec_error;
		}
		stderr_pipe[0] = -1;
		close(stderr_pipe[1]);
		stderr_pipe[1] = -1;

		m_state = process_state::running;
		try {
			const auto child_pid = m_pid.load(std::memory_order_acquire);
			m_thread = std::thread([self = shared_from_this(), child_pid, generation]{
				self->monitor_child(child_pid, generation);
			});
			m_joinable.store(true, std::memory_order_release);
		}
		catch(const std::system_error &exception)
		{
			expected.despair(exception.code());
			stop_and_reap(true);
		}
		catch(const std::bad_alloc&)
		{
			expected.despair(make_system_error_code(std::errc::not_enough_memory));
			stop_and_reap(true);
		}
		catch(...)
		{
			expected.despair(make_system_error_code(std::errc::io_error));
			stop_and_reap(true);
		}
		return expected;

	exec_error: {
		m_joinable.store(false, std::memory_order_release);
		m_join_in_progress.store(false, std::memory_order_release);
		m_pid = -1;

		publish_terminal_state(process_state::crashed, 255);
		std::vector<std::shared_ptr<asio::steady_timer>> vector;
		{
			std::lock_guard timer_lock(m_timer_mutex);
			vector = std::move(m_co_join_list);
		}
		for(auto &timer : vector)
			timer->cancel();
	}
	fork_error:
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);
		stderr_pipe[0] = stderr_pipe[1] = -1;

	stderr_error:
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		stdout_pipe[0] = stdout_pipe[1] = -1;

	stdout_error:
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		stdin_pipe[0] = stdin_pipe[1] = -1;

	stdin_error:
		return expected;
	}

private:
	void publish_terminal_state(process_state state, int exit_code) noexcept
	{
		{
			std::lock_guard lock(m_cv_mutex);
			m_exit_code = exit_code;
			m_state = state;
		}
		m_cv.notify_all();
	}

	void monitor_child(int child_pid, std::uint64_t generation) noexcept
	{
		int status = 0;
		bool reaped = false;
		for(;;)
		{
			const int result = waitpid(child_pid, &status, 0);
			if( result == child_pid )
			{
				if( WIFSTOPPED(status) or WIFCONTINUED(status) )
					continue;
				reaped = true;
				break;
			}
			if( result < 0 and errno == EINTR )
				continue;

			const int wait_error = errno;
			riwo_utils_clog_error("Riwo.Utils",
				"process: waitpid failed: '{}' ({})",
				strerror(wait_error), wait_error
			);
			break;
		}
		if( generation == m_generation.load(std::memory_order_acquire) )
		{
			process_state state;
			int exit_code;

			if( reaped and WIFEXITED(status) )
			{
				exit_code = WEXITSTATUS(status);
				state = process_state::exited;
			}
			else
			{
				exit_code = reaped and WIFSIGNALED(status) ?
					WTERMSIG(status) : 255;
				state = process_state::crashed;
			}
			publish_terminal_state(state, exit_code);
		}
		try {
			if( m_released.load(std::memory_order_acquire) )
			{
				// A released control block has no public owner. Keep it alive until
				// descriptor cleanup has run on its executor, otherwise destruction
				// may race execution_context shutdown on the monitor thread.
				riwo::post(m_exec, [self = shared_from_this(), generation]{
					self->finish_io(generation, false);
				});
			}
			else
			{
				// A joined process retains its public owner. Avoid extending that
				// lifetime merely because executor progress has stopped.
				std::weak_ptr weak = shared_from_this();
				riwo::post(m_exec, [weak = std::move(weak), generation]
				{
					if( auto self = weak.lock() )
						self->finish_io(generation, false);
				});
			}
		}
		catch(...) {
			finish_io(generation, false);
		}
	}

	void join_monitor_thread() noexcept
	{
		if( not m_thread.joinable() )
			return ;
		try {
			if( m_thread.get_id() == std::this_thread::get_id() )
				m_thread.detach();
			else
				m_thread.join();
		}
		catch(...) {}
	}

	void finish_io(std::uint64_t generation, bool close_output) noexcept
	{
		if( generation != m_generation.load(std::memory_order_acquire) )
			return ;

		error_code error;
		error = m_stdin.close(error);
		if( close_output )
		{
			error.clear();
			error = m_stdout.close(error);
			error.clear();
			error = m_stderr.close(error);
		}
		m_pid.store(-1, std::memory_order_release);

		std::vector<std::shared_ptr<asio::steady_timer>> timers;
		{
			std::lock_guard timer_lock(m_timer_mutex);
			timers = std::move(m_co_join_list);
		}
		for(auto &timer : timers)
		{
			try {
				timer->cancel();
			}
			catch(...) {}
		}
	}

	void stop_and_reap(bool force) noexcept
	{
		const auto state = m_state.load(std::memory_order_acquire);
		const int child_pid = m_pid.load(std::memory_order_acquire);
		const bool must_stop = force or state == process_state::running;

		if( must_stop and child_pid > 0 )
			::kill(child_pid, SIGKILL);

		if( m_thread.joinable() )
			join_monitor_thread();

		else if( must_stop and child_pid > 0 )
		{
			int status = 0;
			int wait_result = -1;
			do {
				wait_result = waitpid(child_pid, &status, 0);
			}
			while( wait_result < 0 and errno == EINTR );

			const int exit_code = wait_result == child_pid and WIFSIGNALED(status) ?
				WTERMSIG(status) : 255;

			publish_terminal_state(process_state::crashed, exit_code);
		}
		if( m_state.load(std::memory_order_acquire) == process_state::running )
			publish_terminal_state(process_state::crashed, 255);

		finish_io(m_generation.load(std::memory_order_acquire), true);
	}

	[[nodiscard]] bool register_join_timer(const std::shared_ptr<asio::steady_timer> &timer)
	{
		std::lock_guard timer_lock(m_timer_mutex);
		if( m_state.load(std::memory_order_acquire) != process_state::running )
			return false;

		m_co_join_list.emplace_back(timer);
		return true;
	}

	[[nodiscard]] error_code claim_join() noexcept
	{
		if( not m_joinable.load(std::memory_order_acquire) )
			return make_system_error_code(std::errc::invalid_argument);

		if( bool expected = false;
			not m_join_in_progress.compare_exchange_strong(expected, true, std::memory_order_acq_rel) )
			return make_system_error_code(std::errc::device_or_resource_busy);

		if( not m_joinable.load(std::memory_order_acquire) )
		{
			m_join_in_progress.store(false, std::memory_order_release);
			return make_system_error_code(std::errc::invalid_argument);
		}
		return {};
	}

	class join_claim final
	{
	public:
		explicit join_claim(vindicator &owner) noexcept :
			m_owner(owner) {}

		~join_claim()
		{
			if( m_consume )
				m_owner.m_joinable.store(false, std::memory_order_release);
			m_owner.m_join_in_progress.store(false, std::memory_order_release);
		}

		void consume() noexcept {
			m_consume = true;
		}

	private:
		vindicator &m_owner;
		bool m_consume = false;
	};

public:
	[[noreturn]] void _throw() noexcept
	{
		stop_and_reap(true);
		std::terminate();
	}

public:
	void terminate() const noexcept
	{
		if( m_state == process_state::running )
			::kill(m_pid.load(std::memory_order_acquire), SIGTERM);
	}

	void kill() noexcept
	{
		if( m_state == process_state::running )
			::kill(m_pid.load(std::memory_order_acquire), SIGKILL);
	}

	[[nodiscard]] sys_expected<> detach() noexcept
	{
		if( not m_joinable.load(std::memory_order_acquire) )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( m_join_in_progress.load(std::memory_order_acquire) )
		{
			return sys_unexpected(make_error_code (
				std::errc::device_or_resource_busy
			));
		}
		if( bool expected = true;
			not m_joinable.compare_exchange_strong(expected, false, std::memory_order_acq_rel) )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		m_released.store(true, std::memory_order_release);
		return {};
	}

	[[nodiscard]] bool joinable() const noexcept {
		return m_joinable.load(std::memory_order_acquire);
	}

	void cancel(bool release) noexcept
	{
		if( release )
		{
			m_released.store(true, std::memory_order_release);
			m_joinable.store(false, std::memory_order_release);
		}

		try {
			riwo::dispatch(m_exec, [self = shared_from_this()]
			{
				error_code error;
				error = self->m_stdin.cancel(error);

				error.clear();
				error = self->m_stdout.cancel(error);

				error.clear();
				error = self->m_stderr.cancel(error);

				RIWO_UNUSED(error);
				std::vector<std::shared_ptr<asio::steady_timer>> vector;
				{
					std::lock_guard timer_lock(self->m_timer_mutex);
					vector = std::move(self->m_co_join_list);
				}
				for(auto &timer : vector)
				{
					try {
						timer->cancel();
					}
					catch(...) {}
				}
			});
		}
		catch(...) {}
	}

public:
	[[nodiscard]] sys_expected<int> join(std::chrono::nanoseconds timeout) noexcept
	{
		if( auto claim_error = claim_join() )
			return sys_unexpected(claim_error);

		join_claim claim(*this);
		auto state = m_state.load();

		if( state == process_state::idle )
		{
			claim.consume();
			return sys_unexpected (
				make_system_error_code(std::errc::no_such_process)
			);
		}
		else if( state == process_state::crashed )
		{
			claim.consume();
			return sys_unexpected (
				make_system_error_code(std::errc::io_error)
			);
		}
		else if( state == process_state::exited )
		{
			claim.consume();
			return m_exit_code.load();
		}
		std::unique_lock locker(m_cv_mutex);
		if( timeout == 0ns )
		{
			m_cv.wait(locker, [self = shared_from_this()]{
				return self->m_state != process_state::running;
			});
		}
		else
		{
			m_cv.wait_for(locker, timeout, [self = shared_from_this()]{
				return self->m_state != process_state::running;
			});
		}
		state = m_state.load();
		if( state == process_state::running )
		{
			return sys_unexpected (
				make_error_code(errc::timed_out)
			);
		}
		else if( state != process_state::exited )
		{
			claim.consume();
			return sys_unexpected (
				make_system_error_code(std::errc::io_error)
			);
		}
		claim.consume();
		return m_exit_code.load();
	}

	[[nodiscard]] awaitable<sys_expected<int>> co_join
	(std::chrono::nanoseconds timeout, asio::cancellation_slot cancel_slot) noexcept
	{
		// cancel(release=true) replaces the process object's control block. Keep
		// this one alive until the suspended join has observed its cancellation.
		auto self = shared_from_this();
		if( auto claim_error = claim_join() )
			co_return sys_unexpected(claim_error);

		join_claim claim(*this);
		auto state = m_state.load();

		if( state == process_state::idle )
		{
			claim.consume();
			co_return sys_unexpected (
				make_system_error_code(std::errc::no_such_process)
			);
		}
		else if( state == process_state::crashed )
		{
			claim.consume();
			co_return sys_unexpected (
				make_system_error_code(std::errc::io_error)
			);
		}
		else if( state == process_state::exited )
		{
			claim.consume();
			co_return m_exit_code.load();
		}
		auto exec = co_await asio::this_coro::executor;
		auto timer = std::make_shared<asio::steady_timer>(exec);

		if( timeout == 0ns )
		{
			for(;;)
			{
				timer->expires_after(24h);
				if( not register_join_timer(timer) )
				{
					state = m_state.load();
					if( state != process_state::exited )
					{
						claim.consume();
						co_return sys_unexpected(make_system_error_code(std::errc::io_error));
					}
					break;
				}
				error_code error;
				if( cancel_slot.is_connected() )
					co_await timer->async_wait(use_awaitable | cancel_slot | error);
				else
					co_await timer->async_wait(use_awaitable | error);

				state = m_state.load();
				if( state == process_state::running )
				{
					if( error == errc::operation_aborted )
						co_return sys_unexpected(error);
					continue;
				}
				else if( state != process_state::exited )
				{
					claim.consume();
					co_return sys_unexpected (
						make_system_error_code(std::errc::io_error)
					);
				}
				break;
			}
		}
		else
		{
			timer->expires_after(timeout);
			const bool timer_registered = register_join_timer(timer);

			error_code error;
			if( timer_registered )
			{
				if( cancel_slot.is_connected() )
					co_await timer->async_wait(use_awaitable | cancel_slot | error);
				else
					co_await timer->async_wait(use_awaitable | error);
			}
			state = m_state.load();
			if( state == process_state::running )
			{
				co_return sys_unexpected (
					error ? error : errc::timed_out
				);
			}
			else if( state != process_state::exited )
			{
				claim.consume();
				co_return sys_unexpected (
					make_system_error_code(std::errc::io_error)
				);
			}
		}
		claim.consume();
		co_return m_exit_code.load();
	}

public:
	[[nodiscard]] io_expected write(const const_buffer &buf) noexcept
	{
		if( m_state != process_state::running )
		{
			return io_unexpected (
				make_system_error_code(std::errc::no_such_process)
			);
		}
		error_code error;
		error = m_stdin.non_blocking(false, error);
		if( error )
			return {error};

		auto sum = asio::write(m_stdin, buf, error);
		if( error )
			return {error};
		return sum;
	}

	void async_write(const_buffer buf, process::io_handler_t handler)
	{
		if( buf.size() == 0 )
		{
			post_io_result(std::move(handler), {}, 0);
			return ;
		}
		if( m_state != process_state::running )
		{
			post_io_result(std::move(handler),
				make_system_error_code(std::errc::no_such_process), 0
			);
			return ;
		}
		error_code error;
		error = m_stdin.non_blocking(true, error);
		if( error )
		{
			post_io_result(std::move(handler), error, 0);
			return ;
		}
		asio::async_write(m_stdin, buf, std::move(handler));
	}

public:
	[[nodiscard]] io_expected read(read_channel_t channel, const mutable_buffer &buf) noexcept
	{
		if( m_state == process_state::idle )
		{
			return io_unexpected (
				make_system_error_code(std::errc::no_such_process)
			);
		}
		descriptor_t *stream = nullptr;

		if( channel == read_channel_t::std_output )
		{
			if( not m_stdout.is_open() )
			{
				return io_unexpected (
					make_system_error_code(std::errc::no_such_process)
				);
			}
			stream = &m_stdout;
		}
		else
		{
			if( not m_stderr.is_open() )
			{
				return io_unexpected (
					make_system_error_code(std::errc::no_such_process)
				);
			}
			stream = &m_stderr;
		}
		error_code error;
		error = stream->non_blocking(false, error);
		if( error )
			return {error};

		auto size = stream->read_some(buf, error);
		if( error )
			return {error};
		return size;
	}

	void async_read(read_channel_t channel, mutable_buffer buf, process::io_handler_t handler)
	{
		if( buf.size() == 0 )
		{
			post_io_result(std::move(handler), {}, 0);
			return ;
		}
		if( m_state == process_state::idle )
		{
			post_io_result(std::move(handler),
				make_system_error_code(std::errc::no_such_process), 0
			);
			return ;
		}
		error_code error;
		descriptor_t *stream = nullptr;

		if( channel == read_channel_t::std_output )
		{
			if( not m_stdout.is_open() )
			{
				post_io_result(std::move(handler),
					make_system_error_code(std::errc::no_such_process), 0
				);
				return ;
			}
			stream = &m_stdout;
		}
		else
		{
			if( not m_stderr.is_open() )
			{
				post_io_result(std::move(handler),
					make_system_error_code(std::errc::no_such_process), 0
				);
				return ;
			}
			stream = &m_stderr;
		}
		error = stream->non_blocking(true, error);
		if( error )
		{
			post_io_result(std::move(handler), error, 0);
			return ;
		}
		stream->async_read_some(buf, std::move(handler));
	}

public:
	[[nodiscard]] process_state state() const noexcept {
		return m_state.load();
	}
	[[nodiscard]] int exit_code() const noexcept {
		return m_exit_code.load();
	}

	[[nodiscard]] uint64_t pid() const noexcept
	{
		return m_state == process_state::running ?
			static_cast<uint64_t>(m_pid.load(std::memory_order_acquire)) : 0;
	}

private:
	void post_io_result(process::io_handler_t handler, error_code error, size_t size) const {
		post_completion(m_exec, std::move(handler), error, size);
	}

	[[nodiscard]] static optional<std::string> default_shell() noexcept
	{
		if( auto shell = getenv("SHELL"); shell and strlen(shell) > 0 )
			return {shell};

		auto pw = getpwuid(getuid());
		if( pw and pw->pw_shell and strlen(pw->pw_shell) > 0 )
			return {pw->pw_shell};
		return {};
	}

public:
	std::thread m_thread {};
	std::atomic_int m_pid {-1};
	std::atomic_uint64_t m_generation {0};

	std::atomic<process_state> m_state {
		process_state::idle
	};
	std::atomic_int m_exit_code {0};
	std::atomic_bool m_joinable {false};
	std::atomic_bool m_join_in_progress {false};
	std::atomic_bool m_released {false};

	// From the perspective of the child process.
	executor_t m_exec {};
	descriptor_t m_stdin  {m_exec};
	descriptor_t m_stdout {m_exec};
	descriptor_t m_stderr {m_exec};

	std::condition_variable m_cv {};
	std::mutex m_cv_mutex {};

	std::vector <
		std::shared_ptr<asio::steady_timer>
	> m_co_join_list {};
	std::mutex m_timer_mutex {};
};

using vindicator_ptr = vindicator::ptr_t;

} //namespace

class RIWO_DECL_HIDDEN process::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(executor_t exec) :
		m_exec(std::move(exec)),
		m_vindicator(std::make_shared<vindicator>(m_exec)) {}

	~impl() noexcept
	{
		if( m_vindicator->joinable() )
			m_vindicator->_throw();
	}

public:
	std::string m_cmd {};
	args_t m_args {};

	path_t m_work_path {};
	envs_t m_envs {};

	executor_t m_exec {};
	vindicator_ptr m_vindicator {};
	bool m_is_pipe = false;
};

process::process(const executor_t &exec) :
	m_impl(std::make_shared<impl>(exec))
{

}

process::~process() = default;

void process::set(const path_t &cmd, const std::vector<path_t> &args) const
{
	if( cmd.empty() )
		return ;

	auto cmd_str = strtls::trimmed(cmd.string());
	wordexp_t word {};

	auto res = wordexp(cmd_str.c_str(), &word, 0);
	if( res != 0 )
	{
		if( res == WRDE_NOSPACE )
			wordfree(&word);

		for(auto &arg : args)
			cmd_str += " " + strtls::trimmed(arg.string());

		m_impl->m_cmd = std::move(cmd_str);
		m_impl->m_is_pipe = true;
		return ;
	}
	if( word.we_wordc == 0 )
	{
		wordfree(&word);
		m_impl->m_cmd.clear();
		m_impl->m_args.clear();
		m_impl->m_is_pipe = false;
		return ;
	}
	if( is_shell_assignment(word.we_wordv[0]) )
	{
		wordfree(&word);
		for(auto &arg : args)
			cmd_str += " " + strtls::trimmed(arg.string());

		m_impl->m_cmd = std::move(cmd_str);
		m_impl->m_args.clear();
		m_impl->m_is_pipe = true;
		return ;
	}
	m_impl->m_cmd = word.we_wordv[0];
	m_impl->m_args.clear();

	for(size_t i=1; i<word.we_wordc; i++)
		m_impl->m_args.emplace_back(word.we_wordv[i]);

	wordfree(&word);
	m_impl->m_is_pipe = false;

	for(auto &arg : args)
		add_arg(arg);
}

void process::add_arg(const path_t &arg) const
{
	if( arg.empty() )
		return ;

	else if( m_impl->m_is_pipe )
	{
		m_impl->m_cmd += " " + strtls::trimmed(arg.string());
		return ;
	}
	// Arguments supplied separately are already tokenized by the caller.  In
	// particular, a value containing spaces must remain one argv entry (matching
	// the Windows implementation and normal execvp semantics).
	m_impl->m_args.emplace_back(strtls::trimmed(arg.string()));
}

sys_expected<> process::start() const
{
	return m_impl->m_vindicator->start (
		m_impl->m_is_pipe, m_impl->m_cmd, m_impl->m_args,
		m_impl->m_work_path.string(), m_impl->m_envs
	);
}

void process::terminate() const noexcept
{
	m_impl->m_vindicator->terminate();
}

void process::kill() const noexcept
{
	m_impl->m_vindicator->kill();
}

sys_expected<> process::detach() const noexcept
{
	// Detach releases only the public join ownership.  The old control block is
	// kept alive by its monitor thread until waitpid() has reaped the child; the
	// replacement lets this process object be reused without abandoning that
	// parent-side resource management.
	vindicator_ptr replacement;
	try {
		replacement = std::make_shared<vindicator>(m_impl->m_exec);
	}
	catch(const std::system_error &exception) {
		return sys_unexpected(exception.code());
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {
		return sys_unexpected(make_system_error_code(std::errc::io_error));
	}
	if( auto expected = m_impl->m_vindicator->detach(); not expected )
		return expected;

	m_impl->m_vindicator = std::move(replacement);
	return {};
}

void process::cancel(bool release) const noexcept
{
	auto current = m_impl->m_vindicator;
	if( not release )
	{
		current->cancel(false);
		return ;
	}
	vindicator_ptr replacement;
	try {
		replacement = std::make_shared<vindicator>(m_impl->m_exec);
	}
	catch(...) {}

	// Releasing join ownership must not wait for an outstanding co_join() to
	// unwind.  The monitor thread keeps the old control block alive and reaps
	// the child after the pending operations have been cancelled.
	current->cancel(true);
	if( replacement )
		m_impl->m_vindicator = std::move(replacement);
}

bool process::joinable() const noexcept
{
	return m_impl->m_vindicator->joinable();
}

sys_expected<int> process::join
(const std::chrono::nanoseconds &timeout) const noexcept
{
	return m_impl->m_vindicator->join(timeout);
}

awaitable<sys_expected<int>> process::co_join
(asio::cancellation_slot cancel_slot, std::chrono::nanoseconds timeout) const noexcept
{
	return m_impl->m_vindicator->co_join(timeout, cancel_slot);
}

awaitable<sys_expected<int>> process::co_join(std::error_code &error,
	asio::cancellation_slot cancel_slot, std::chrono::nanoseconds timeout) const noexcept
{
	error.clear();
	auto expected = co_await m_impl->m_vindicator
		->co_join(timeout, cancel_slot);

	if( not expected )
		error = expected.error();
	co_return expected;
}

io_expected process::write(const const_buffer &buf) const noexcept
{
	if( buf.size() > 0 )
		return m_impl->m_vindicator->write(buf);
	return 0;
}

void process::async_write(const const_buffer &buf, io_handler_t handler) const
{
	m_impl->m_vindicator->async_write(buf, std::move(handler));
}

io_expected process::read(read_channel channel, const mutable_buffer &buf) const noexcept
{
	if( buf.size() > 0 )
		return m_impl->m_vindicator->read(channel, buf);
	return 0;
}

void process::async_read(read_channel channel, const mutable_buffer &buf, io_handler_t handler) const
{
	m_impl->m_vindicator->async_read(channel, buf, std::move(handler));
}

void process::normalize_read_error(error_code &error) const noexcept
{
	if( error.default_error_condition() == std::errc::broken_pipe )
		error = make_error_code(asio::error::eof);
}

void process::protect_io_error(const error_code &error) const noexcept
{
	if( not error or state() != process_state::running )
		return ;

	if( const auto condition = error.default_error_condition();
		condition == std::errc::bad_file_descriptor or
		condition == std::errc::io_error or
		condition == std::errc::bad_address )
	{
		m_impl->m_vindicator->kill();
	}
}

void process::set_work_path(path_t path) noexcept
{
	m_impl->m_work_path = std::move(path);
}

void process::setenv(std::string_view key, riwo::value value) noexcept
{
	m_impl->m_envs[std::string(key)] = std::move(*value);
}

void process::unsetenv(std::string_view key) noexcept
{
	m_impl->m_envs.erase(std::string(key));
}

process_state process::state() const noexcept
{
	return m_impl->m_vindicator->state();
}

int process::exit_code() const noexcept
{
	return m_impl->m_vindicator->exit_code();
}

pid_t process::pid() const noexcept
{
	return m_impl->m_vindicator->pid();
}

sys_expected<uint64_t> process::self_pid() noexcept
{
	// Always successful on unix/linux.
	return getpid();
}

sys_expected<> process::terminate(pid_t pid) noexcept
{
	if( ::kill(static_cast<::pid_t>(pid), SIGTERM) == 0 )
		return {};
	return sys_unexpected(sys_error());
}

sys_expected<> process::kill(pid_t pid) noexcept
{
	if( ::kill(static_cast<::pid_t>(pid), SIGKILL) == 0 )
		return {};
	return sys_unexpected(sys_error());
}

static fs::path g_pid_file {};

static void singleton_cleanup()
{
	if( not g_pid_file.empty() )
		unlink(g_pid_file.c_str());
}

static sys_expected<uint64_t> do_set_single(const fs::path &path, std::string_view key)
{
	sys_expected<uint64_t> result;
	{
		auto g_pid_path = path.empty() ? "/tmp" : path.string();
		while( g_pid_path.ends_with('/') )
			g_pid_path.pop_back();

		g_pid_path += "/.riwo.utils.process";
		namespace fs = std::filesystem;

		if( not fs::exists(g_pid_path) )
		{
			if( std::error_code error; not fs::create_directories(g_pid_path, error) )
				return result.despair(error);
		}
		g_pid_file = g_pid_path + std::format("/{}", key);
	}
	auto curr_pid = getpid();
	bool created = false;

	int fd = open(g_pid_file.c_str(),
		O_WRONLY | O_CREAT | O_EXCL, 0644
	);
	if( fd >= 0 )
	{
		auto pid_str = std::to_string(curr_pid);
		auto len = ::write(fd, pid_str.c_str(), pid_str.size());

		RIWO_UNUSED(len);
		close(fd);

		if( chmod(g_pid_file.c_str(), 0444) == 0 )
			created = true;
		else
		{
			result.despair(sys_error());
			chmod(g_pid_file.c_str(), 0644);
			unlink(g_pid_file.c_str());
			return result;
		}
	}
	if( created )
	{
		atexit(singleton_cleanup);
		result = curr_pid;
		return result;
	}
	::pid_t existing_pid = 0;
	auto fp = fopen(g_pid_file.c_str(), "r");
	if( not fp )
		return result.despair(sys_error());

	char buf[1024] {0};
	if( std::fgets(buf, sizeof(buf), fp) )
	{
		if( auto opt = strtls::to_int32(strtls::trimmed(buf)) )
			existing_pid = *opt;
		else
		{
			fclose(fp);
			unlink(g_pid_file.c_str());
			return do_set_single(path, key);
		}
	}
	fclose(fp);

	if( ::kill(existing_pid, 0) == 0 )
	{
		result = existing_pid;
		return result;
	}
	if( unlink(g_pid_file.c_str()) == 0 )
		return do_set_single(path, key);
	return result.despair(sys_error());
}

sys_expected<uint64_t> process::set_single(const fs::path &path, std::string_view key)
{
	static std::atomic_bool flag {false};
	if( bool expected = false;
		not flag.compare_exchange_strong(expected, true,
		std::memory_order_acquire, std::memory_order_relaxed) )
	{
		runtime_error::loc_throw (
			"riwo::app::set_single: Another instance is running (Prohibition of concurrent operation)."
		);
	}
	return do_set_single(path, key);
}

} //namespace riwo::utils::detail

#endif //__unix__
