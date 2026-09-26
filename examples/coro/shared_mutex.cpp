// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/coro.h>

#include <iostream>

int main()
{
	using namespace riwo::coro::literals;

	riwo::coro::shared_mutex mutex;
	int value = 0;

	riwo::dispatch([&]() -> riwo::awaitable<void>
	{
		riwo::coro::shared_unique_lock lock(mutex);
		co_await lock.lock();
		value = 42;
		co_return;
	});

	riwo::dispatch([&]() -> riwo::awaitable<void>
	{
		co_await 10_ms;
		riwo::coro::shared_lock lock(mutex);
		co_await lock.lock_shared();

		std::cout << "Shared read: " << value << '\n';
		riwo::exit();
		co_return;
	});

	return riwo::exec();
}
