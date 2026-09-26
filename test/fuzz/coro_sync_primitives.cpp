// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/coro/mutex.h>
#include <riwo/coro/semaphore.h>
#include <riwo/coro/shared_mutex.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size == 0 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;

	riwo::coro::mutex mutex;
	riwo::coro::shared_mutex shared_mutex;
	riwo::coro::basic_semaphore<8> semaphore(data[0] % 9);
	size_t semaphore_count = data[0] % 9;
	bool owns_mutex = false;
	bool owns_exclusive = false;
	size_t shared_holds = 0;
	for(size_t index = 0; index < size; ++index)
	{
		switch(data[index] % 10)
		{
		case 0:
			if(not owns_mutex)
			{
				if(not mutex.try_lock())
					std::abort();
				owns_mutex = true;
			}
			break;
		case 1:
			if(owns_mutex)
			{
				mutex.unlock();
				owns_mutex = false;
			}
			break;
		case 2:
			riwo::ignore_unused(mutex.is_locked());
			riwo::ignore_unused(mutex.native_handle().load());
			break;
		case 3:
		{
			const auto acquired = semaphore.try_acquire();
			if(acquired != (semaphore_count != 0))
				std::abort();
			if(acquired)
				--semaphore_count;
			break;
		}
		case 4:
			if(semaphore_count < 8)
			{
				riwo::ignore_unused(semaphore.release());
				++semaphore_count;
			}
			break;
		case 5:
			if(semaphore.count() != semaphore_count)
				std::abort();
			break;
		case 6:
			if(not owns_exclusive)
			{
				if(not shared_mutex.try_lock_shared())
					std::abort();
				++shared_holds;
			}
			break;
		case 7:
			if(shared_holds != 0)
			{
				shared_mutex.unlock_shared();
				--shared_holds;
			}
			break;
		case 8:
			if(not owns_exclusive and shared_holds == 0)
			{
				if(not shared_mutex.try_lock())
					std::abort();
				owns_exclusive = true;
			}
			break;
		default:
			if(owns_exclusive)
			{
				shared_mutex.unlock();
				owns_exclusive = false;
			}
			break;
		}
		if(mutex.is_locked() != owns_mutex or
			semaphore.count() != semaphore_count or
			(owns_exclusive and shared_holds != 0))
			std::abort();
	}
	if(owns_mutex)
		mutex.unlock();
	while(shared_holds != 0)
	{
		shared_mutex.unlock_shared();
		--shared_holds;
	}
	if(owns_exclusive)
		shared_mutex.unlock();
	if(mutex.is_locked() or semaphore.count() != semaphore_count)
		std::abort();
	return 0;
}
