// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "reader.h"

namespace riwo::http_exp::detail
{

body_reader_adapter::~body_reader_adapter() = default;

class body_reader_state::impl
{
public:
	impl(std::shared_ptr<body_reader_adapter> input, bool input_ready,
		uint64_t discard_limit) :
		adapter(std::move(input)), max_discard(discard_limit), ready(input_ready)
	{
	}

	std::shared_ptr<body_reader_adapter> adapter;
	uint64_t max_discard;
	optional<fields> trailer_cache;
	bool ready = false;
	bool pending = false;
	bool finished = false;
	bool cancelled = false;
};

namespace
{

[[nodiscard]] error_code operation_error(errc value) noexcept {
	return make_error_code(value);
}

} //namespace

body_reader_state::body_reader_state(std::shared_ptr<body_reader_adapter> adapter,
	bool ready, uint64_t max_discard) :
	m_impl(std::make_shared<impl>(std::move(adapter), ready, max_discard))
{
}

body_reader_state::~body_reader_state() = default;

body_read_result body_reader_state::read_some(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;
	if( state->pending )
	{
		error = operation_error(errc::operation_in_progress);
		return {};
	}
	if( !state->ready || !state->adapter || state->cancelled )
	{
		error = operation_error(errc::operation_not_permitted);
		return {};
	}
	if( state->finished ) return {0, true};
	if( buffer.size() == 0 ) return {};

	state->pending = true;
	auto result = state->adapter->read_some_sync(buffer, error);
	state->pending = false;
	if( !error && result.bytes == 0 && !result.eof )
		error = operation_error(errc::body_length_mismatch);
	if( !error && result.eof ) state->finished = true;
	return result;
}

void body_reader_state::read_some(mutable_buffer buffer, read_handler handler)
{
	auto state = m_impl;
	if( state->pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress),
			body_read_result{});
		return;
	}
	if( !state->ready || !state->adapter || state->cancelled )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted),
			body_read_result{});
		return;
	}
	if( state->finished )
	{
		std::move(handler)(error_code{}, body_read_result{0, true});
		return;
	}
	if( buffer.size() == 0 )
	{
		std::move(handler)(error_code{}, body_read_result{});
		return;
	}

	state->pending = true;
	try {
		state->adapter->read_some(buffer,
		[state, handler = std::move(handler)]
		(error_code error, body_read_result result) mutable
		{
			state->pending = false;
			if( !error && result.bytes == 0 && !result.eof )
				error = operation_error(errc::body_length_mismatch);
			if( !error && result.eof ) state->finished = true;
			std::move(handler)(error, result);
		});
	}
	catch(...) {
		state->pending = false;
		std::move(handler)(exception_error(std::current_exception()),
			body_read_result{});
	}
}

void body_reader_state::discard(error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;
	if( state->pending )
	{
		error = operation_error(errc::operation_in_progress);
		return;
	}
	if( !state->ready || !state->adapter || state->cancelled )
	{
		error = operation_error(errc::operation_not_permitted);
		return;
	}
	if( state->finished )
		return;

	state->pending = true;
	std::array<std::byte,8192> buffer;
	uint64_t discarded = 0;
	for(;;)
	{
		auto result = state->adapter->read_some_sync(riwo::buffer(buffer), error);
		if( error )
			break;
		if( result.bytes == 0 && !result.eof )
		{
			error = operation_error(errc::body_length_mismatch);
			break;
		}
		if( result.bytes > state->max_discard -
			std::min(state->max_discard, discarded) )
		{
			error = operation_error(errc::buffer_limit_exceeded);
			state->adapter->cancel();
			break;
		}
		discarded += result.bytes;
		if( result.eof )
		{
			state->finished = true;
			break;
		}
	}
	state->pending = false;
}

void body_reader_state::discard(operation_handler handler)
{
	auto state = m_impl;
	if( state->pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress));
		return;
	}
	if( !state->ready || !state->adapter || state->cancelled )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted));
		return;
	}
	if( state->finished )
	{
		std::move(handler)(error_code{});
		return;
	}

	struct operation : std::enable_shared_from_this<operation>
	{
		operation(std::shared_ptr<impl> input_state,
			operation_handler completion) :
			state(std::move(input_state)), handler(std::move(completion))
		{
		}

		std::shared_ptr<impl> state;
		operation_handler handler;
		std::array<std::byte,8192> buffer;
		uint64_t discarded = 0;

		void start()
		{
			auto self = shared_from_this();
			try {
				state->adapter->read_some(riwo::buffer(buffer),
				[self = std::move(self)]
				(error_code error, body_read_result result) mutable {
					self->complete(error, result);
				});
			}
			catch(...) {
				finish(exception_error(std::current_exception()));
			}
		}

		void complete(error_code error, body_read_result result)
		{
			if( error )
			{
				finish(error);
				return;
			}
			if( result.bytes == 0 && !result.eof )
			{
				finish(operation_error(errc::body_length_mismatch));
				return;
			}
			if( result.bytes > state->max_discard -
				std::min(state->max_discard, discarded) )
			{
				state->adapter->cancel();
				finish(operation_error(errc::buffer_limit_exceeded));
				return;
			}
			discarded += result.bytes;
			if( result.eof )
			{
				state->finished = true;
				finish({});
				return;
			}
			start();
		}

		void finish(error_code error)
		{
			state->pending = false;
			std::move(handler)(error);
		}
	};

	state->pending = true;
	std::make_shared<operation>(state, std::move(handler))->start();
}

fields body_reader_state::trailers(error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;
	if( state->pending )
	{
		error = operation_error(errc::operation_in_progress);
		return {};
	}
	if( !state->ready || !state->adapter || state->cancelled || !state->finished )
	{
		error = operation_error(errc::operation_not_permitted);
		return {};
	}
	if( state->trailer_cache )
		return *state->trailer_cache;

	state->pending = true;
	auto result = state->adapter->trailers_sync(error);
	state->pending = false;
	if( !error )
		state->trailer_cache = result;
	return result;
}

void body_reader_state::trailers(trailers_handler handler)
{
	auto state = m_impl;
	if( state->pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress), fields{});
		return;
	}
	if( !state->ready || !state->adapter || state->cancelled || !state->finished )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted), fields{});
		return;
	}
	if( state->trailer_cache )
	{
		std::move(handler)(error_code{}, *state->trailer_cache);
		return;
	}

	state->pending = true;
	try {
		state->adapter->trailers(
		[state, handler = std::move(handler)]
		(error_code error, fields value) mutable
		{
			state->pending = false;
			if( !error )
				state->trailer_cache = value;
			std::move(handler)(error, std::move(value));
		});
	}
	catch(...) {
		state->pending = false;
		std::move(handler)(exception_error(std::current_exception()), fields{});
	}
}

void body_reader_state::cancel() noexcept
{
	auto state = m_impl;
	state->cancelled = true;
	if( state->adapter )
		state->adapter->cancel();
}

void body_reader_state::set_ready(bool ready) noexcept {
	m_impl->ready = ready;
}

void body_reader_state::set_finished(bool finished) noexcept {
	m_impl->finished = finished;
}

bool body_reader_state::is_finished() const noexcept {
	return m_impl->finished;
}

std::shared_ptr<body_reader_state> make_body_reader_state(
	std::shared_ptr<body_reader_adapter> adapter, bool ready,
	uint64_t max_discard)
{
	return std::make_shared<body_reader_state>(
		std::move(adapter), ready, max_discard);
}

} //namespace riwo::http_exp::detail
