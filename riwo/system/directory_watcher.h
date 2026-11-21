// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_DIRECTORY_WATCHER_H
#define RIWO_SYSTEM_DIRECTORY_WATCHER_H

#include <riwo/system/global.h>
#include <riwo/core/async_expected.h>

namespace riwo::system
{

enum class directory_change_kind {
	created, modified, removed
};

struct directory_change
{
	directory_change_kind kind {};
	std::filesystem::path path {};
};

// Watch the immediate entries of an existing directory. Changes are coalesced
// between snapshots. The executor's context must outlive the watcher.
template <concepts::exec Exec = asio::any_io_executor>
class RIWO_SYSTEM_TAPI directory_watcher
{
	RIWO_DISABLE_COPY(directory_watcher)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using path_t = std::filesystem::path;
	using changes_t = std::vector<directory_change>;

	using clock_t = std::chrono::steady_clock;
	using interval_t = clock_t::duration;

public:
	directory_watcher(directory_watcher &&other) noexcept;
	directory_watcher &operator=(directory_watcher &&other) noexcept;
	~directory_watcher();

public:
	template <concepts::sched Scheduler>
	[[nodiscard]] static sys_expected<directory_watcher> open(Scheduler &&scheduler,
		const path_t &directory, interval_t interval = std::chrono::milliseconds(100)
	) requires concepts::match_sched<Scheduler,executor_t>;

	[[nodiscard]] static sys_expected<directory_watcher> open (
		const path_t &directory, interval_t interval = std::chrono::milliseconds(100)
	) requires concepts::match_sched<io_executor_t,executor_t>;

public:
	// One wait may be active. Synchronous waits run on the calling thread.
	template <typename Token = use_sync_t>
	auto wait(Token &&token = {}) requires
		concepts::tf_opt_token<Token,error_code,changes_t> or concepts::time_p<Token>;

	void cancel() noexcept;
	[[nodiscard]] executor_t get_executor() const noexcept;

private:
	using strand_t = asio::strand<executor_t>;
	class impl;

	strand_t m_strand;
	std::shared_ptr<impl> m_impl {};

	directory_watcher(const strand_t &strand,
		std::shared_ptr<impl> implementation
	) noexcept;
};

} //namespace riwo::system
#include <riwo/system/detail/directory_watcher.h>


#endif //RIWO_SYSTEM_DIRECTORY_WATCHER_H
