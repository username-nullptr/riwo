// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_PROCESS_H
#define RIWO_UTILS_PROCESS_H

#include <riwo/utils/global.h>
#include <riwo/core/execution.h>
#include <riwo/core/async_expected.h>
#include <riwo/core/value.h>

namespace riwo::utils
{

using pid_t = uint64_t;

enum class process_state {
	idle, running, exited, crashed
};
/**
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * Keep at most one stdin write, one stdout read, and one stderr read
 * outstanding. Serialize lifecycle changes with I/O initiation.
 */
template <concepts::exec Exec = asio::any_io_executor>
class RIWO_UTILS_TAPI basic_process
{
	RIWO_DISABLE_COPY(basic_process)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using state_t = process_state;

	using path_t = std::filesystem::path;
	using args_t = std::vector<path_t>;

	template <typename...Args>
	static constexpr bool is_args_v = (
		concepts::constructible<path_t,Args> and ...
	);

public:
	explicit basic_process(path_t cmd = {}, args_t args = {})
		requires concepts::match_sched<io_executor_t,Exec>;

	template <typename...Args>
	basic_process(path_t cmd, Args&&...args) requires
		concepts::match_sched<io_executor_t,Exec> and is_args_v<Args...>;

	template <typename Scheduler>
	basic_process(Scheduler &&exec, path_t cmd = {}, args_t args = {}) requires
		(not std::same_as<std::remove_cvref_t<Scheduler>,basic_process>) and
		concepts::match_sched<Scheduler,Exec>;

	template <typename Scheduler, typename...Args>
	basic_process(Scheduler &&exec, path_t cmd, Args&&...args) requires
		(not std::same_as<std::remove_cvref_t<Scheduler>,basic_process>) and
		concepts::match_sched<Scheduler,Exec> and is_args_v<Args...>;

	basic_process(basic_process &&other) noexcept;
	basic_process &operator=(basic_process &&other) noexcept;
	~basic_process();

public:
	template <typename...Args>
	sys_expected<> start(const path_t &cmd, Args&&...args) noexcept
		requires is_args_v<Args...>;

	sys_expected<> start (
		const path_t &cmd = {}, const args_t &args = {}
	) noexcept;

	void terminate() noexcept;
	void kill() noexcept;
	void detach();

	enum class cancel_option {
		none, terminate, kill, detach
	};
	// Cancel pending waits and stream operations.  The terminal options release
	// join ownership after signalling the child so cancellation stays
	// non-blocking while the platform monitor finishes resource reclamation.
	void cancel(cancel_option option = cancel_option::none) noexcept;

public:
	template <typename Token, typename...Value>
	static constexpr bool task_token_v =
		concepts::tf_opt_token<Token,error_code,Value...>;

	template <typename Token, typename...Value>
	static constexpr bool dis_detach_token_v =
		task_token_v<Token,Value...> and not is_detached_v<token_unbound_t<Token>>;

public:
	template <typename Token = use_sync_t>
	auto join(Token &&token = {})
		requires dis_detach_token_v<Token,int> or
		concepts::time_p<Token>;

	template <concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(const const_buffer &buf, Token &&token = {});

	template <typename Token = use_sync_t>
	auto read(const mutable_buffer &buf, Token &&token = {}) requires
		dis_detach_token_v<Token,size_t>;

	template <concepts::buffer Buffer, typename Token = use_sync_t>
	auto read(Token &&token = {}) requires
		dis_detach_token_v<Token,Buffer>;

	template <typename Token = use_sync_t>
	auto read(Token &&token = {}) requires
		dis_detach_token_v<Token,std::vector<std::byte>>;

	template <typename Token = use_sync_t>
	auto read_stderr(const mutable_buffer &buf, Token &&token = {}) requires
		dis_detach_token_v<Token,size_t>;

	template <concepts::buffer Buffer, typename Token = use_sync_t>
	auto read_stderr(Token &&token = {}) requires
		dis_detach_token_v<Token,Buffer>;

	template <typename Token = use_sync_t>
	auto read_stderr(Token &&token = {}) requires
		dis_detach_token_v<Token,std::vector<std::byte>>;

public:
	template <typename Token = use_sync_t>
	auto run(const path_t &cmd, const args_t &args, Token &&token = {})
		requires task_token_v<Token,int>;

	template <typename Token = use_sync_t>
	auto run(const path_t &cmd, Token &&token = {})
		requires task_token_v<Token,int>;

	template <typename Token = use_sync_t>
	auto run(Token &&token = {})
		requires task_token_v<Token,int>;

public:
	void set_work_path(path_t path) noexcept;
	void setenv(std::string_view key, path_t value) noexcept;
	void unsetenv(std::string_view key) noexcept;

public:
	[[nodiscard]] state_t state() const noexcept;
	[[nodiscard]] int exit_code() const noexcept;

	[[nodiscard]] pid_t pid() const noexcept;
	[[nodiscard]] bool joinable() const noexcept;
	[[nodiscard]] executor_t get_executor() const noexcept;

public:
	template <typename Token>
	static constexpr bool exec_token_v =
		task_token_v<Token,int> or concepts::time_p<Token>;

	template <typename Token = use_sync_t>
	static auto exec(const path_t &cmd, const args_t &args, Token &&token = {})
		requires exec_token_v<Token>;

	template <typename Token = use_sync_t>
	static auto exec(const path_t &cmd, Token &&token = {})
		requires exec_token_v<Token>;

	template <concepts::match_sched<Exec> Exec0, typename Token = use_sync_t>
	static auto exec(Exec0 &&exec, const path_t &cmd, const args_t &args, Token &&token = {})
		requires exec_token_v<Token>;

	template <concepts::match_sched<Exec> Exec0, typename Token = use_sync_t>
	static auto exec(Exec0 &&exec, const path_t &cmd, Token &&token = {})
		requires exec_token_v<Token>;

public:
	[[nodiscard]] static sys_expected<pid_t> self_pid() noexcept;
	static sys_expected<> terminate(pid_t pid) noexcept;
	static sys_expected<> kill(pid_t pid) noexcept;
	/*
	 * @path lock file path, default to home directory.
	 * @return existing pid or self pid.
	 */
	[[nodiscard]] static sys_expected<pid_t> set_single(const path_t &path, std::string_view key);
	[[nodiscard]] static sys_expected<pid_t> set_single(std::string_view key);

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using process  = basic_process<>;

} //namespace riwo::utils
#include <riwo/utils/detail/process.h>


#endif //RIWO_UTILS_PROCESS_H
