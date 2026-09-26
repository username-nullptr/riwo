// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/execution.h>
#include <iostream>
#include <chrono>

int main()
{
	using namespace std::chrono_literals;

	riwo::dispatch([] {
		std::cout << "dispatch runs immediately on a compatible context\n";
	});
	riwo::post([] {
		std::cout << "post runs from the event queue\n";
	});
	riwo::post(30ms, [] {
		std::cout << "delayed post fired\n";
	});

	auto timer = riwo::start_timer(20ms, [] {
		std::cout << "periodic timer tick\n";
	});
	riwo::post(75ms, [&timer] {
		timer();
		riwo::exit();
	});

	return riwo::exec();
}
