// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/jthread.h>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>

namespace
{

using namespace std::chrono_literals;

static_assert(RIWO_HAS_STD_JTHREAD == 0);
static_assert(std::is_default_constructible_v<riwo::jthread>);
static_assert(not std::is_copy_constructible_v<riwo::jthread>);
static_assert(not std::is_copy_assignable_v<riwo::jthread>);
static_assert(std::is_nothrow_move_constructible_v<riwo::jthread>);
static_assert(std::is_nothrow_move_assignable_v<riwo::jthread>);
static_assert(std::is_nothrow_copy_constructible_v<riwo::stop_token>);
static_assert(std::is_nothrow_copy_constructible_v<riwo::stop_source>);

using callback_probe = riwo::stop_callback<void(*)()>;
static_assert(not std::is_copy_constructible_v<callback_probe>);
static_assert(not std::is_move_constructible_v<callback_probe>);
static_assert(not std::is_copy_assignable_v<callback_probe>);
static_assert(not std::is_move_assignable_v<callback_probe>);

struct rvalue_callback
{
	void operator()() && noexcept
	{
		calls->fetch_add(1, std::memory_order_relaxed);
	}

	std::atomic_int *calls;
};

struct lvalue_callback
{
	void operator()() & noexcept
	{
		calls->fetch_add(1, std::memory_order_relaxed);
	}

	std::atomic_int *calls;
};

struct overload_callable
{
	void operator()(int) const
	{
		selected->store(1, std::memory_order_release);
	}

	void operator()(riwo::stop_token token, int) const
	{
		selected->store(token.stop_possible() ? 2 : -1,
			std::memory_order_release);
	}

	std::atomic_int *selected;
};

void source_and_token_state()
{
	riwo::stop_token empty_token;
	RIWO_TEST_CHECK(not empty_token.stop_possible());
	RIWO_TEST_CHECK(not empty_token.stop_requested());

	riwo::stop_source disabled(riwo::nostopstate);
	RIWO_TEST_CHECK(not disabled.stop_possible());
	RIWO_TEST_CHECK(not disabled.stop_requested());
	RIWO_TEST_CHECK(not disabled.request_stop());
	RIWO_TEST_CHECK(not disabled.get_token().stop_possible());

	riwo::stop_token abandoned_token;
	{
		riwo::stop_source source;
		abandoned_token = source.get_token();
		RIWO_TEST_CHECK(source.stop_possible());
		RIWO_TEST_CHECK(abandoned_token.stop_possible());
		RIWO_TEST_CHECK(not abandoned_token.stop_requested());

		auto source_copy = source;
		RIWO_TEST_CHECK(source_copy == source);
		riwo::stop_source moved_source(std::move(source_copy));
		RIWO_TEST_CHECK(not source_copy.stop_possible());
		RIWO_TEST_CHECK(moved_source == source);
	}
	RIWO_TEST_CHECK(not abandoned_token.stop_possible());
	RIWO_TEST_CHECK(not abandoned_token.stop_requested());

	riwo::stop_token requested_token;
	{
		riwo::stop_source source;
		requested_token = source.get_token();
		RIWO_TEST_CHECK(source.request_stop());
		RIWO_TEST_CHECK(not source.request_stop());
		RIWO_TEST_CHECK(source.stop_requested());
	}
	RIWO_TEST_CHECK(requested_token.stop_possible());
	RIWO_TEST_CHECK(requested_token.stop_requested());
}

void source_and_token_assignment_and_swap()
{
	riwo::stop_source first;
	riwo::stop_source second;
	auto first_token = first.get_token();
	auto second_token = second.get_token();
	RIWO_TEST_CHECK(first != second);
	RIWO_TEST_CHECK(first_token != second_token);

	riwo::swap(first, second);
	RIWO_TEST_CHECK(first.get_token() == second_token);
	RIWO_TEST_CHECK(second.get_token() == first_token);

	riwo::swap(first_token, second_token);
	RIWO_TEST_CHECK(first_token == first.get_token());
	RIWO_TEST_CHECK(second_token == second.get_token());

	riwo::stop_source assigned(riwo::nostopstate);
	assigned = first;
	RIWO_TEST_CHECK(assigned == first);
	riwo::stop_source move_assigned;
	move_assigned = std::move(assigned);
	RIWO_TEST_CHECK(move_assigned == first);
	RIWO_TEST_CHECK(not assigned.stop_possible());
}

void callback_registration_and_invocation()
{
	std::atomic_int impossible_calls {0};
	riwo::stop_callback impossible(riwo::stop_token {}, [&]
	{
		impossible_calls.fetch_add(1, std::memory_order_relaxed);
	});
	RIWO_TEST_CHECK_EQ(impossible_calls.load(std::memory_order_relaxed), 0);

	riwo::stop_source removed_source;
	std::atomic_int removed_calls {0};
	{
		riwo::stop_callback removed(removed_source.get_token(), [&]
		{
			removed_calls.fetch_add(1, std::memory_order_relaxed);
		});
	}
	RIWO_TEST_CHECK(removed_source.request_stop());
	RIWO_TEST_CHECK_EQ(removed_calls.load(std::memory_order_relaxed), 0);

	riwo::stop_source source;
	std::atomic_int calls {0};
	const auto requester_id = std::this_thread::get_id();
	std::thread::id callback_thread;
	riwo::stop_callback first(source.get_token(), [&]
	{
		callback_thread = std::this_thread::get_id();
		calls.fetch_add(1, std::memory_order_relaxed);
	});
	riwo::stop_callback second(source.get_token(), [&]
	{
		calls.fetch_add(1, std::memory_order_relaxed);
	});

	RIWO_TEST_CHECK(source.request_stop());
	RIWO_TEST_CHECK(not source.request_stop());
	RIWO_TEST_CHECK_EQ(calls.load(std::memory_order_relaxed), 2);
	RIWO_TEST_CHECK(callback_thread == requester_id);

	std::thread::id late_thread;
	riwo::stop_callback late(source.get_token(), [&]
	{
		late_thread = std::this_thread::get_id();
		calls.fetch_add(1, std::memory_order_relaxed);
	});
	RIWO_TEST_CHECK_EQ(calls.load(std::memory_order_relaxed), 3);
	RIWO_TEST_CHECK(late_thread == std::this_thread::get_id());

	riwo::stop_source value_category_source;
	riwo::stop_callback rvalue_only(
		value_category_source.get_token(), rvalue_callback {&calls}
	);
	lvalue_callback lvalue {&calls};
	riwo::stop_callback<lvalue_callback&> lvalue_only(
		value_category_source.get_token(), lvalue
	);
	RIWO_TEST_CHECK(value_category_source.request_stop());
	RIWO_TEST_CHECK_EQ(calls.load(std::memory_order_relaxed), 5);
}

void callback_lifetime_synchronization()
{
	using callback_t = riwo::stop_callback<std::function<void()>>;

	riwo::stop_source self_source;
	std::atomic_int self_calls {0};
	std::unique_ptr<callback_t> self_destroying;
	self_destroying = std::make_unique<callback_t>(
		self_source.get_token(), [&]
		{
			self_calls.fetch_add(1, std::memory_order_relaxed);
			self_destroying.reset();
		}
	);
	RIWO_TEST_CHECK(self_source.request_stop());
	RIWO_TEST_CHECK(not self_destroying);
	RIWO_TEST_CHECK_EQ(self_calls.load(std::memory_order_relaxed), 1);

	riwo::stop_source source;
	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool release = false;
	auto callback = std::make_unique<callback_t>(source.get_token(), [&]
	{
		std::unique_lock lock(mutex);
		entered = true;
		condition.notify_all();
		condition.wait(lock, [&] { return release; });
	});

	std::thread requester([&] { source.request_stop(); });
	{
		std::unique_lock lock(mutex);
		RIWO_TEST_CHECK(condition.wait_for(lock, 2s, [&] { return entered; }));
	}

	std::atomic_bool destroy_started {false};
	std::atomic_bool destroyed {false};
	std::thread destroyer([&]
	{
		destroy_started.store(true, std::memory_order_release);
		callback.reset();
		destroyed.store(true, std::memory_order_release);
	});
	while( not destroy_started.load(std::memory_order_acquire) )
		std::this_thread::yield();
	std::this_thread::sleep_for(10ms);
	const bool destroyed_before_release =
		destroyed.load(std::memory_order_acquire);
	{
		std::lock_guard lock(mutex);
		release = true;
	}
	condition.notify_all();
	destroyer.join();
	requester.join();

	RIWO_TEST_CHECK(not destroyed_before_release);
	RIWO_TEST_CHECK(destroyed.load(std::memory_order_acquire));
}

void construction_and_callable_selection()
{
	riwo::jthread empty;
	RIWO_TEST_CHECK(not empty.joinable());
	RIWO_TEST_CHECK(empty.get_id() == riwo::jthread::id {});
	RIWO_TEST_CHECK(not empty.get_stop_source().stop_possible());
	RIWO_TEST_CHECK(not empty.get_stop_token().stop_possible());
	RIWO_TEST_CHECK(not empty.request_stop());
	RIWO_TEST_CHECK_THROWS(empty.join(), std::system_error);
	RIWO_TEST_CHECK_THROWS(empty.detach(), std::system_error);

	std::atomic_int selected {0};
	riwo::jthread preferred(overload_callable {&selected}, 42);
	preferred.join();
	RIWO_TEST_CHECK_EQ(selected.load(std::memory_order_acquire), 2);

	std::atomic_int moved_value {0};
	int referenced_value = 0;
	riwo::jthread plain([&](std::unique_ptr<int> argument, int &referenced)
	{
		moved_value.store(*argument, std::memory_order_release);
		referenced = 7;
	}, std::make_unique<int>(42), std::ref(referenced_value));
	RIWO_TEST_CHECK(plain.get_id() != riwo::jthread::id {});
	RIWO_UNUSED(plain.native_handle());
	plain.join();
	RIWO_TEST_CHECK_EQ(moved_value.load(std::memory_order_acquire), 42);
	RIWO_TEST_CHECK_EQ(referenced_value, 7);
	RIWO_UNUSED(riwo::jthread::hardware_concurrency());
}

void stop_join_and_detach()
{
	std::atomic_bool started {false};
	std::atomic_bool stopped {false};
	{
		riwo::jthread worker([&](riwo::stop_token token)
		{
			started.store(true, std::memory_order_release);
			while( not token.stop_requested() )
				std::this_thread::yield();
			stopped.store(true, std::memory_order_release);
		});
		while( not started.load(std::memory_order_acquire) )
			std::this_thread::yield();
	}
	RIWO_TEST_CHECK(stopped.load(std::memory_order_acquire));

	std::atomic_bool externally_stopped {false};
	riwo::jthread external([&](riwo::stop_token token)
	{
		while( not token.stop_requested() )
			std::this_thread::yield();
		externally_stopped.store(true, std::memory_order_release);
	});
	auto source = external.get_stop_source();
	RIWO_TEST_CHECK(source.request_stop());
	external.join();
	RIWO_TEST_CHECK(externally_stopped.load(std::memory_order_acquire));
	RIWO_TEST_CHECK(not external.request_stop());
	RIWO_TEST_CHECK_THROWS(external.join(), std::system_error);

	std::promise<void> detached_done;
	auto detached_future = detached_done.get_future();
	riwo::jthread detached([&] { detached_done.set_value(); });
	detached.detach();
	RIWO_TEST_CHECK(not detached.joinable());
	RIWO_TEST_CHECK(detached_future.wait_for(2s) == std::future_status::ready);
}

void move_and_swap_preserve_stop_state()
{
	std::atomic_bool first_started {false};
	std::atomic_bool second_started {false};
	std::atomic_bool first_stopped {false};
	std::atomic_bool second_stopped {false};
	riwo::jthread first([&](riwo::stop_token token)
	{
		first_started.store(true, std::memory_order_release);
		while( not token.stop_requested() )
			std::this_thread::yield();
		first_stopped.store(true, std::memory_order_release);
	});
	riwo::jthread second([&](riwo::stop_token token)
	{
		second_started.store(true, std::memory_order_release);
		while( not token.stop_requested() )
			std::this_thread::yield();
		second_stopped.store(true, std::memory_order_release);
	});
	while( not first_started.load(std::memory_order_acquire) or
		not second_started.load(std::memory_order_acquire) )
	{
		std::this_thread::yield();
	}

	riwo::swap(first, second);
	RIWO_TEST_CHECK(first.request_stop());
	first.join();
	RIWO_TEST_CHECK(second_stopped.load(std::memory_order_acquire));
	RIWO_TEST_CHECK(not first_stopped.load(std::memory_order_acquire));

	riwo::jthread moved(std::move(second));
	RIWO_TEST_CHECK(not second.joinable());
	RIWO_TEST_CHECK(moved.request_stop());
	moved.join();
	RIWO_TEST_CHECK(first_stopped.load(std::memory_order_acquire));

	std::atomic_bool replacement_started {false};
	std::atomic_bool replacement_stopped {false};
	riwo::jthread replacement([&](riwo::stop_token token)
	{
		replacement_started.store(true, std::memory_order_release);
		while( not token.stop_requested() )
			std::this_thread::yield();
		replacement_stopped.store(true, std::memory_order_release);
	});
	while( not replacement_started.load(std::memory_order_acquire) )
		std::this_thread::yield();

	riwo::jthread completed([] {});
	replacement = std::move(completed);
	RIWO_TEST_CHECK(replacement_stopped.load(std::memory_order_acquire));
	replacement.join();
	RIWO_TEST_CHECK(not completed.joinable());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"source and token state", source_and_token_state},
		{"source and token assignment", source_and_token_assignment_and_swap},
		{"callback registration", callback_registration_and_invocation},
		{"callback lifetime synchronization", callback_lifetime_synchronization},
		{"construction and callable selection", construction_and_callable_selection},
		{"stop, join, and detach", stop_join_and_detach},
		{"move and swap", move_and_swap_preserve_stop_state},
	});
}
