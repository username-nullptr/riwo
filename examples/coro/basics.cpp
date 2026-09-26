// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/coro.h>
#include <iostream>
#include <chrono>
#include <future>

int main()
{
	using namespace std::chrono_literals;
	using namespace riwo::coro::literals;

	riwo::dispatch([]() -> riwo::awaitable<void>
	{
		std::cout << "Coroutine started on thread " << riwo::this_thread_id() << '\n';
		co_await 20_ms;

		auto answer = std::async(std::launch::async, []
		{
			std::this_thread::sleep_for(20ms);
			return 42;
		});
		std::cout << "Future result: " << co_await riwo::coro::wait(answer) << '\n';

		co_await riwo::coro::goto_thread();
		std::cout << "Moved to worker thread " << riwo::this_thread_id() << '\n';

		riwo::exit();
		co_return;
	});
	return riwo::exec();
}
