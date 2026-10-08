// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__WINNT__) || defined(_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include <riwo/utils/process.h>
#include <riwo/coro/utils.h>

#if RIWO_USING_BOOST_ASIO
# include <boost/asio/readable_pipe.hpp>
# include <boost/asio/writable_pipe.hpp>
#else
# include <asio/readable_pipe.hpp>
# include <asio/writable_pipe.hpp>
#endif //RIWO_USING_BOOST_ASIO

#include <utility>
#include <map>

#ifdef _MSC_VER
# pragma comment(lib, "shell32.lib")
#endif //_MSC_VER

namespace riwo::utils::detail
{

namespace fs = std::filesystem;

using namespace std::chrono_literals;
using namespace operators;

using executor_t = process::executor_t;
using path_t = process::path_t;

using read_pipe_t = asio::readable_pipe;
using write_pipe_t = asio::writable_pipe;

using read_channel_t = process::read_channel;

using args_t = std::vector<std::wstring>;
using envs_t = std::map<std::string, value>;

[[nodiscard]] static error_code sys_error()
{
	return error_code(std::error_code (
		static_cast<int>(GetLastError()), std::system_category()
	));
}

[[nodiscard]] static std::wstring quote_arg(std::wstring_view arg)
{
	if( arg.empty() )
		return L"\"\"";

	bool need_quote = false;
	for(auto ch : arg)
	{
		if( ch == L' ' or ch == L'\t' or ch == L'\n' or ch == L'\v' or ch == L'"' )
		{
			need_quote = true;
			break;
		}
	}
	if( not need_quote )
		return std::wstring(arg);

	std::wstring result;
	result.reserve(arg.size() + 2);
	result += L'"';

	size_t backslashes = 0;
	for(auto ch : arg)
	{
		if( ch == L'\\' )
		{
			++backslashes;
			continue;
		}
		if( ch == L'"' )
		{
			result.append(backslashes * 2 + 1, L'\\');
			result += ch;
		}
		else
		{
			result.append(backslashes, L'\\');
			result += ch;
		}
		backslashes = 0;
	}
	result.append(backslashes * 2, L'\\');
	result += L'"';
	return result;
}

[[nodiscard]] static std::wstring make_cmdline(std::wstring_view cmd, const args_t &args)
{
	auto cmdline = quote_arg(cmd);
	for(auto &arg : args)
	{
		cmdline += L' ';
		cmdline += quote_arg(arg);
	}
	return cmdline;
}

[[nodiscard]] static bool has_shell_meta(std::wstring_view str) noexcept
{
	return std::ranges::any_of(str, [](auto ch) {
		return ch == L'|' or ch == L'&' or ch == L'<' or ch == L'>';
	});
}

[[nodiscard]] static std::wstring to_wstring(std::string_view str)
{
	if( str.empty() )
		return {};

	int size = MultiByteToWideChar(CP_UTF8, 0, str.data(),
		static_cast<int>(str.size()), nullptr, 0
	);
	if( size <= 0 )
		return std::wstring(str.begin(), str.end());

	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.data(),
		static_cast<int>(str.size()), result.data(), size
	);
	return result;
}

[[nodiscard]] static std::vector<wchar_t> make_environment(const envs_t &envs)
{
	std::map<std::wstring,std::wstring> merged;
	if( auto block = GetEnvironmentStringsW() )
	{
		for(auto ptr = block; *ptr != L'\0'; )
		{
			std::wstring item = ptr;
			ptr += item.size() + 1;

			auto pos = item.find(L'=', item.starts_with(L'=') ? 1 : 0);
			if( pos != std::wstring::npos )
				merged.emplace(item.substr(0, pos), item.substr(pos + 1));
		}
		FreeEnvironmentStringsW(block);
	}
	for(auto &[key, val] : envs)
		merged[to_wstring(key)] = to_wstring(*val);

	std::vector<wchar_t> result;
	for(auto &[key, val] : merged)
	{
		result.insert(result.end(), key.begin(), key.end());
		result.push_back(L'=');
		result.insert(result.end(), val.begin(), val.end());
		result.push_back(L'\0');
	}
	result.push_back(L'\0');
	return result;
}

[[nodiscard]] static std::wstring make_pipe_name()
{
	static std::atomic_long counter {0};
	return std::format (
		LR"(\\.\pipe\riwo-utils-process-{}-{})",
		GetCurrentProcessId(), counter.fetch_add(1, std::memory_order_relaxed)
	);
}

static void close_handle(HANDLE handle) noexcept
{
	if( handle != nullptr and handle != INVALID_HANDLE_VALUE )
		CloseHandle(handle);
}

[[nodiscard]] static bool connect_pipe_server(HANDLE handle) noexcept
{
	if( ConnectNamedPipe(handle, nullptr) )
		return true;
	return GetLastError() == ERROR_PIPE_CONNECTED;
}

[[nodiscard]] static sys_expected<> make_stdout_pipe(read_pipe_t &parent, HANDLE &child) noexcept
{
	sys_expected<> expected;
	auto name = make_pipe_name();

	auto parent_handle = CreateNamedPipeW(name.c_str(),
		PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1, 8192, 8192, 0, nullptr
	);
	if( parent_handle == INVALID_HANDLE_VALUE )
		return expected.despair(sys_error());

	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	child = CreateFileW(name.c_str(), GENERIC_WRITE, 0, &sa,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
	);
	if( child == INVALID_HANDLE_VALUE )
	{
		auto error = sys_error();
		close_handle(parent_handle);
		child = nullptr;
		return expected.despair(error);
	}
	if( not connect_pipe_server(parent_handle) )
	{
		auto error = sys_error();
		close_handle(parent_handle);
		close_handle(child);
		child = nullptr;
		return expected.despair(error);
	}
	error_code error;
	error = parent.assign(parent_handle, error);
	if( error )
	{
		close_handle(parent_handle);
		close_handle(child);
		child = nullptr;
		return expected.despair(error);
	}
	return expected;
}

[[nodiscard]] static sys_expected<> make_stdin_pipe(write_pipe_t &parent, HANDLE &child) noexcept
{
	sys_expected<> expected;
	auto name = make_pipe_name();

	auto parent_handle = CreateNamedPipeW(name.c_str(),
		PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1, 8192, 8192, 0, nullptr
	);
	if( parent_handle == INVALID_HANDLE_VALUE )
		return expected.despair(sys_error());

	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	child = CreateFileW(name.c_str(), GENERIC_READ, 0, &sa,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
	);
	if( child == INVALID_HANDLE_VALUE )
	{
		auto error = sys_error();
		close_handle(parent_handle);
		child = nullptr;
		return expected.despair(error);
	}
	if( not connect_pipe_server(parent_handle) )
	{
		auto error = sys_error();
		close_handle(parent_handle);
		close_handle(child);
		child = nullptr;
		return expected.despair(error);
	}
	error_code error;
	error = parent.assign(parent_handle, error);
	if( error )
	{
		close_handle(parent_handle);
		close_handle(child);
		child = nullptr;
		return expected.despair(error);
	}
	return expected;
}

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
	[[nodiscard]] sys_expected<> start(bool is_pipe, std::wstring_view cmd,
		const args_t &args, const path_t &work_path, const envs_t &envs)
	{
		error_code error; RIWO_UNUSED(error);
		sys_expected<> expected;

		if( m_joinable.load(std::memory_order_acquire) )
		{
			return expected.despair(std::make_error_code(
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

		write_pipe_t stdin_write(m_exec);
		read_pipe_t stdout_read(m_exec);
		read_pipe_t stderr_read(m_exec);

		HANDLE child_stdin = nullptr;
		HANDLE child_stdout = nullptr;
		HANDLE child_stderr = nullptr;

		auto pipe_expected = make_stdin_pipe(stdin_write, child_stdin);
		if( not pipe_expected )
			return pipe_expected;

		pipe_expected = make_stdout_pipe(stdout_read, child_stdout);
		if( not pipe_expected )
		{
			close_handle(child_stdin);
			return pipe_expected;
		}
		pipe_expected = make_stdout_pipe(stderr_read, child_stderr);
		if( not pipe_expected )
		{
			close_handle(child_stdin);
			close_handle(child_stdout);
			return pipe_expected;
		}
		std::wstring command_line;
		if( is_pipe )
		{
			wchar_t comspec[MAX_PATH] {};
			auto len = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH);

			std::wstring shell = len > 0 ? std::wstring(comspec, len) : L"cmd.exe";
			command_line = make_cmdline(shell, {L"/C", std::wstring(cmd)});
		}
		else
			command_line = make_cmdline(cmd, args);

		STARTUPINFOW si {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = child_stdin;
		si.hStdOutput = child_stdout;
		si.hStdError = child_stderr;

		PROCESS_INFORMATION pi {};
		auto env_block = envs.empty() ? std::vector<wchar_t>{} : make_environment(envs);
		auto work_path_str = work_path.empty() ? std::wstring{} : work_path.wstring();

		HANDLE job_handle = CreateJobObjectW(nullptr, nullptr);
		if( job_handle == nullptr )
		{
			auto job_error = sys_error();
			close_handle(child_stdin);
			close_handle(child_stdout);
			close_handle(child_stderr);
			return expected.despair(job_error);
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info {};
		job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

		if( not SetInformationJobObject(job_handle,
			JobObjectExtendedLimitInformation, &job_info, sizeof(job_info)) )
		{
			auto job_error = sys_error();
			close_handle(job_handle);
			close_handle(child_stdin);
			close_handle(child_stdout);
			close_handle(child_stderr);
			return expected.despair(job_error);
		}
		if( not CreateProcessW (
			nullptr, command_line.data(), nullptr, nullptr, TRUE,
			CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
			env_block.empty() ? nullptr : env_block.data(),
			work_path_str.empty() ? nullptr : work_path_str.c_str(), &si, &pi) )
		{
			auto create_error = sys_error();
			close_handle(job_handle);
			close_handle(child_stdin);
			close_handle(child_stdout);
			close_handle(child_stderr);
			return expected.despair(create_error);
		}
		if( not AssignProcessToJobObject(job_handle, pi.hProcess) )
		{
			auto assign_error = sys_error();
			TerminateProcess(pi.hProcess, static_cast<UINT>(-9));
			WaitForSingleObject(pi.hProcess, INFINITE);

			close_handle(pi.hThread);
			close_handle(pi.hProcess);
			close_handle(job_handle);
			close_handle(child_stdin);
			close_handle(child_stdout);
			close_handle(child_stderr);
			return expected.despair(assign_error);
		}
		if( ResumeThread(pi.hThread) == static_cast<DWORD>(-1) )
		{
			auto resume_error = sys_error();
			TerminateProcess(pi.hProcess, static_cast<UINT>(-9));
			WaitForSingleObject(pi.hProcess, INFINITE);

			close_handle(pi.hThread);
			close_handle(pi.hProcess);
			close_handle(job_handle);
			close_handle(child_stdin);
			close_handle(child_stdout);
			close_handle(child_stderr);
			return expected.despair(resume_error);
		}
		close_handle(pi.hThread);
		close_handle(child_stdin);
		close_handle(child_stdout);
		close_handle(child_stderr);

		error = m_stdin.close(error);
		error = m_stdout.close(error);
		error = m_stderr.close(error);

		error.clear();
		m_stdin = std::move(stdin_write);

		error.clear();
		m_stdout = std::move(stdout_read);

		error.clear();
		m_stderr = std::move(stderr_read);
		{
			std::lock_guard resource_lock(m_resource_mutex);
			m_process = pi.hProcess;
			m_job = job_handle;
			m_pid.store(pi.dwProcessId, std::memory_order_release);
		}
		m_state = process_state::running;
		try {
			m_thread = std::thread (
			[self = shared_from_this(), process_handle = pi.hProcess, generation]{
				self->monitor_child(process_handle, generation);
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

	void monitor_child(HANDLE process_handle, std::uint64_t generation) noexcept
	{
		const DWORD wait_result = WaitForSingleObject(process_handle, INFINITE);
		DWORD child_exit_code = 255;

		const bool got_exit_code = wait_result == WAIT_OBJECT_0 and
			GetExitCodeProcess(process_handle, &child_exit_code);

		const bool forced = generation ==
			m_forced_generation.load(std::memory_order_acquire);

		if( generation == m_generation.load(std::memory_order_acquire) )
		{
			process_state state;
			int exit_code;
			if( got_exit_code and not forced )
			{
				exit_code = static_cast<int>(child_exit_code);
				state = process_state::exited;
			}
			else
			{
				exit_code = got_exit_code ?
					static_cast<int>(child_exit_code) : 255;
				state = process_state::crashed;
			}
			publish_terminal_state(state, exit_code);
		}
		try {
			if( m_released.load(std::memory_order_acquire) )
			{
				// A released control block has no public owner. Keep it alive until
				// handle cleanup has run on its executor.
				riwo::post(m_exec, [self = shared_from_this(), generation] {
					self->finish_io(generation, false);
				});
			}
			else
			{
				// A joined process retains its public owner. Avoid extending that
				// lifetime merely because executor progress has stopped.
				std::weak_ptr weak = shared_from_this();
				riwo::post(m_exec, [weak = std::move(weak), generation] {
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

		HANDLE process_handle = nullptr;
		HANDLE job_handle = nullptr;
		{
			std::lock_guard resource_lock(m_resource_mutex);
			error_code close_error;
			close_error = m_stdin.close(close_error);
			if( close_output )
			{
				close_error.clear();
				close_error = m_stdout.close(close_error);
				close_error.clear();
				close_error = m_stderr.close(close_error);
				RIWO_UNUSED(close_error);
			}
			process_handle = std::exchange(m_process, nullptr);
			job_handle = std::exchange(m_job, nullptr);
			m_pid.store(0, std::memory_order_release);
		}
		close_handle(process_handle);
		close_handle(job_handle);

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
		const auto generation = m_generation.load(std::memory_order_acquire);
		const bool must_stop = force or state == process_state::running;

		if( must_stop )
		{
			m_forced_generation.store(generation, std::memory_order_release);
			std::lock_guard resource_lock(m_resource_mutex);

			if( m_process != nullptr )
				TerminateProcess(m_process, static_cast<UINT>(-9));
		}
		if( m_thread.joinable() )
			join_monitor_thread();

		else if( must_stop )
		{
			HANDLE process_handle = nullptr;
			{
				std::lock_guard resource_lock(m_resource_mutex);
				process_handle = m_process;
			}
			if( process_handle != nullptr )
				WaitForSingleObject(process_handle, INFINITE);

			publish_terminal_state(process_state::crashed, 255);
		}
		if( m_state.load(std::memory_order_acquire) == process_state::running )
			publish_terminal_state(process_state::crashed, 255);
		finish_io(generation, true);
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
		{
			m_forced_generation.store (
				m_generation.load(std::memory_order_acquire),
				std::memory_order_release
			);
			std::lock_guard resource_lock(m_resource_mutex);
			if( m_process != nullptr )
				TerminateProcess(m_process, static_cast<UINT>(-9));
		}
	}

	void kill() const noexcept {
		terminate();
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
				self->m_stdin.cancel(error);

				error.clear();
				self->m_stdout.cancel(error);

				error.clear();
				self->m_stderr.cancel(error);

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
				co_await timer->async_wait (
					use_awaitable | cancel_slot | error
				);
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
				co_await timer->async_wait (
					use_awaitable | cancel_slot | error
				);
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
		auto sum = asio::write(m_stdin, buf, error);
		if( error )
			return {error};
		return sum;
	}

	void async_write(const const_buffer &buf, process::io_handler_t handler)
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
		auto stream = read_stream(channel);
		if( stream == nullptr or not stream->is_open() )
		{
			return io_unexpected (
				make_system_error_code(std::errc::no_such_process)
			);
		}
		error_code error;
		auto sum = stream->read_some(buf, error);

		if( error.value() == ERROR_BROKEN_PIPE or
			error.value() == ERROR_HANDLE_EOF or
			error.value() == ERROR_NO_DATA )
			error = make_error_code(asio::error::eof);

		if( error )
			return {error};
		return sum;
	}

	void async_read(read_channel_t channel, const mutable_buffer &buf, process::io_handler_t handler)
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
		auto stream = read_stream(channel);
		if( stream == nullptr or not stream->is_open() )
		{
			post_io_result(std::move(handler),
				make_system_error_code(std::errc::no_such_process), 0
			);
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
	[[nodiscard]] read_pipe_t *read_stream(read_channel_t channel) noexcept {
		return channel == read_channel_t::std_output ? &m_stdout : &m_stderr;
	}

	void post_io_result(process::io_handler_t handler, error_code error, size_t size) {
		riwo::post_completion(m_exec, std::move(handler), error, size);
	}

public:
	std::thread m_thread {};
	HANDLE m_process = nullptr;
	HANDLE m_job = nullptr;

	std::atomic<DWORD> m_pid {0};
	std::atomic_uint64_t m_generation {0};

	mutable std::atomic_uint64_t m_forced_generation {
		static_cast<std::uint64_t>(-1)
	};
	mutable std::mutex m_resource_mutex {};

	std::atomic<process_state> m_state {
		process_state::idle
	};
	std::atomic_int m_exit_code {0};
	std::atomic_bool m_joinable {false};
	std::atomic_bool m_join_in_progress {false};
	std::atomic_bool m_released {false};

	executor_t m_exec {};
	write_pipe_t m_stdin  {m_exec};
	read_pipe_t  m_stdout {m_exec};
	read_pipe_t  m_stderr {m_exec};

	std::condition_variable m_cv {};
	std::mutex m_cv_mutex {};

	std::vector <
		std::shared_ptr<asio::steady_timer>
	> m_co_join_list {};

	std::mutex m_timer_mutex {};
};

using vindicator_ptr = vindicator::ptr_t;

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
	std::wstring m_cmd {};
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

	auto cmd_str = strtls::trimmed(cmd.wstring());
	m_impl->m_cmd = std::move(cmd_str);
	m_impl->m_args.clear();
	m_impl->m_is_pipe = has_shell_meta(m_impl->m_cmd);

	if( not m_impl->m_is_pipe )
	{
		int count = 0;
		auto argv = CommandLineToArgvW(m_impl->m_cmd.c_str(), &count);
		if( argv != nullptr and count > 0 )
		{
			m_impl->m_cmd = argv[0];
			for(int i=1; i<count; ++i)
				m_impl->m_args.emplace_back(argv[i]);
			LocalFree(argv);
		}
		else
		{
			if( argv != nullptr )
				LocalFree(argv);
			m_impl->m_is_pipe = true;
		}
	}
	for(auto &arg : args)
		add_arg(arg);
}

void process::add_arg(const path_t &arg) const
{
	if( arg.empty() )
		return ;

	auto arg_str = strtls::trimmed(arg.wstring());
	if( m_impl->m_is_pipe )
		m_impl->m_cmd += L" " + std::move(arg_str);
	else
		m_impl->m_args.emplace_back(std::move(arg_str));
}

sys_expected<> process::start() const
{
	return m_impl->m_vindicator->start (
		m_impl->m_is_pipe, m_impl->m_cmd, m_impl->m_args,
		m_impl->m_work_path, m_impl->m_envs
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
	// kept alive by its monitor thread until the process has been waited and all
	// handles are closed; the replacement makes this process object reusable.
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
	// unwind.  The monitor thread keeps the old control block alive and closes
	// its process handles after the pending operations have been cancelled.
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
	if( error.value() == ERROR_BROKEN_PIPE or
		error.value() == ERROR_HANDLE_EOF or
		error.value() == ERROR_NO_DATA )
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
		m_impl->m_vindicator->kill();
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
	return GetCurrentProcessId();
}

sys_expected<> process::terminate(pid_t pid) noexcept
{
	auto handle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
	if( handle == nullptr )
		return sys_unexpected(sys_error());

	auto ok = TerminateProcess(handle, static_cast<UINT>(-9));
	auto error = sys_error();

	CloseHandle(handle);
	if( ok )
		return {};
	return sys_unexpected(error);
}

sys_expected<> process::kill(pid_t pid) noexcept
{
	return terminate(pid);
}

static fs::path g_pid_file {};

static void singleton_cleanup()
{
	if( not g_pid_file.empty() )
		DeleteFileW(g_pid_file.wstring().c_str());
}

static sys_expected<uint64_t> do_set_single(const fs::path &path, std::string_view key)
{
	sys_expected<uint64_t> result;
	fs::path dir = path;

	if( dir.empty() )
	{
		wchar_t tmp[MAX_PATH] {};
		auto len = GetTempPathW(MAX_PATH, tmp);

		if( len == 0 or len > MAX_PATH )
			return result.despair(sys_error());

		dir = fs::path(std::wstring(tmp, len));
	}
	dir /= L".riwo.utils.process";

	if( std::error_code error; not fs::exists(dir, error) )
	{
		if( not fs::create_directories(dir, error) )
			return result.despair(error);
	}
	g_pid_file = dir / fs::path(std::string(key));
	auto curr_pid = GetCurrentProcessId();

	auto file = CreateFileW(g_pid_file.wstring().c_str(), GENERIC_WRITE, 0,
		nullptr, CREATE_NEW, FILE_ATTRIBUTE_READONLY, nullptr
	);
	if( file != INVALID_HANDLE_VALUE )
	{
		auto pid_str = std::to_string(curr_pid);
		DWORD written = 0; RIWO_UNUSED(written);

		WriteFile(file, pid_str.c_str(), static_cast<DWORD>(pid_str.size()), &written, nullptr);
		CloseHandle(file);

		atexit(singleton_cleanup);
		result = curr_pid;
		return result;
	}
	auto create_error = GetLastError();
	if( create_error != ERROR_FILE_EXISTS and create_error != ERROR_ALREADY_EXISTS )
	{
		return result.despair(error_code(std::error_code (
			static_cast<int>(create_error), std::system_category()
		)));
	}
	file = CreateFileW(g_pid_file.wstring().c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
	);
	if( file == INVALID_HANDLE_VALUE )
		return result.despair(sys_error());

	char buf[1024] {};
	DWORD read_len = 0;
	if( not ReadFile(file, buf, sizeof(buf) - 1, &read_len, nullptr) )
	{
		auto err = sys_error();
		CloseHandle(file);
		return result.despair(err);
	}
	CloseHandle(file);

	auto opt = strtls::to_uint64(strtls::trimmed(std::string(buf, read_len)));
	if( not opt )
	{
		SetFileAttributesW(g_pid_file.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
		DeleteFileW(g_pid_file.wstring().c_str());
		return do_set_single(path, key);
	}
	auto existing_pid = static_cast<DWORD>(*opt);
	auto process = OpenProcess(SYNCHRONIZE, FALSE, existing_pid);

	if( process != nullptr )
	{
		auto wait_res = WaitForSingleObject(process, 0);
		CloseHandle(process);
		if( wait_res == WAIT_TIMEOUT )
		{
			result = existing_pid;
			return result;
		}
	}
	SetFileAttributesW(g_pid_file.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);

	if( DeleteFileW(g_pid_file.wstring().c_str()) )
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

#endif //Windows
