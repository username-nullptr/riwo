// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/coro.h>

#include <iostream>

int main()
{
	using namespace riwo::coro::literals;

	riwo::coro::mutex mutex;
	riwo::coro::condition_variable changed;
	bool ready = false;

	riwo::dispatch([&]() -> riwo::awaitable<void>
	{
		riwo::coro::unique_lock lock(mutex);
		co_await lock.lock();
		co_await changed.wait(lock, [&] { return ready; });

		std::cout << "consumer observed ready = true\n";
		riwo::exit();
		co_return;
	});

	riwo::dispatch([&]() -> riwo::awaitable<void>
	{
		co_await 20_ms;
		riwo::coro::unique_lock lock(mutex);
		co_await lock.lock();

		ready = true;
		lock.unlock();
		changed.notify_one();
		co_return;
	});

	return riwo::exec();
}
