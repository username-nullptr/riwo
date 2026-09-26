// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/coro.h>

#include <iostream>

int main()
{
	using namespace riwo::coro::literals;
	constexpr int worker_count = 4;

	riwo::coro::mutex mutex;
	int next_value = 0;
	int completed = 0;

	for(int worker = 0; worker < worker_count; ++worker)
	{
		riwo::dispatch([&, worker]() -> riwo::awaitable<void>
		{
			riwo::coro::unique_lock lock(mutex);
			co_await lock.lock();

			const auto value = next_value++;
			co_await 10_ms;

			std::cout << "worker " << worker << " observed " << value << '\n';
			lock.unlock();

			if(++completed == worker_count)
				riwo::exit();
			co_return;
		});
	}
	return riwo::exec();
}
