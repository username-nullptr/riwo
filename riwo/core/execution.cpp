// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "execution.h"

namespace riwo { namespace
{

using io_worker_t = asio::executor_work_guard<io_context_t::executor_type>;

struct runtime_state
{
	io_context_t context;
	std::atomic_int exit_code {0};
	std::atomic_bool running {false};

	std::mutex lifecycle_mutex;
	bool active = false;
};

runtime_state &default_runtime() noexcept
{
	// Keep the default context alive until process teardown. Destroying it from
	// static deinitialisation has caused failures on older Windows runtimes.
	static auto *state = new runtime_state();
	return *state;
}

} //namespace

io_context_t &io_context() noexcept
{
	return default_runtime().context;
}

io_executor_t get_executor() noexcept
{
	return io_context().get_executor();
}

int exec()
{
	auto &state = default_runtime();
	std::optional<io_worker_t> worker;
	{
		std::lock_guard lock(state.lifecycle_mutex);
		if( state.active )
		{
			runtime_error::loc_throw (
				"riwo::execution::exec: not reentrant."
			);
		}
		state.context.restart();
		worker.emplace(state.context.get_executor());
		state.active = true;
		state.running.store(true, std::memory_order_release);
	}
	auto finish = [&]() noexcept
	{
		state.running.store(false, std::memory_order_release);
		worker.reset();
		std::lock_guard lock(state.lifecycle_mutex);
		state.active = false;
	};
	try {
		for(;;)
		{
			state.context.run();
			state.context.restart();
			if( not state.running.load(std::memory_order_acquire) )
				break;
		}
	}
	catch(...)
	{
		finish();
		throw;
	}
	const auto code = state.exit_code.load(std::memory_order_acquire);
	finish();
	return code;
}

void exec(io_context_t &ioc)
{
	io_worker_t work(ioc.get_executor()); (void)work;
	ioc.run();
}

void exec_detach(io_context_t &ioc)
{
	std::thread([&ioc]{exec(ioc);}).detach();
}

void exit(int code)
{
	auto &state = default_runtime();
	std::lock_guard lock(state.lifecycle_mutex);

	if( state.running.load(std::memory_order_acquire) )
	{
		state.exit_code.store(code, std::memory_order_release);
		state.running.store(false, std::memory_order_release);
	}
	state.context.stop();
}

bool is_run()
{
	return default_runtime().running.load(std::memory_order_acquire);
}

} //namespace riwo
