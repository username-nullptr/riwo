// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/experimental/http/body/reader.h>

namespace riwo::experimental::http::detail { namespace
{

[[nodiscard]] error_code operation_error(errc value) noexcept
{
	return make_error_code(value);
}

[[nodiscard]] error_code invalid_read_result(body_read_result result, size_t capacity) noexcept
{
	if( result.bytes > capacity )
		return make_system_error_code(std::errc::io_error);

	if( result.bytes == 0 and not result.eof )
		return operation_error(errc::body_length_mismatch);
	return {};
}

} //namespace

body_reader_adapter::~body_reader_adapter() = default;

class RIWO_DECL_HIDDEN body_reader_state::discard_operation final :
	public std::enable_shared_from_this<discard_operation>
{
public:
	discard_operation(body_reader_state &input_state,
		std::shared_ptr<void> input_lifetime, operation_handler_t completion) :
		state(input_state), lifetime(std::move(input_lifetime)), handler(std::move(completion)) {}

	body_reader_state &state;

	std::shared_ptr<void> lifetime {};
	operation_handler_t handler {};

	std::array<std::byte,8192> buffer{};
	uint64_t discarded = 0;

	void start()
	{
		auto self = shared_from_this();
		try {
			state.m_adapter->read_some(riwo::buffer(buffer),
			[self = std::move(self)](error_code error, body_read_result result) mutable {
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
			return ;
		}
		if( auto invalid = invalid_read_result(result, buffer.size()) )
		{
			state.m_cancelled = true;
			state.m_adapter->cancel();
			finish(invalid);
			return ;
		}
		if( result.bytes > state.m_max_discard - std::min(state.m_max_discard, discarded) )
		{
			state.m_adapter->cancel();
			finish(operation_error(errc::buffer_limit_exceeded));
			return ;
		}
		discarded += result.bytes;
		if( result.eof )
		{
			state.m_finished = true;
			finish({});
			return ;
		}
		start();
	}

	void finish(error_code error)
	{
		state.m_pending = false;
		std::move(handler)(error);
	}
};

body_reader_state::body_reader_state
(body_reader_adapter_ptr adapter, bool ready, uint64_t max_discard) :
	m_owned_adapter(std::move(adapter)), m_adapter(m_owned_adapter.get()),
	m_max_discard(max_discard), m_ready(ready)
{

}

body_reader_state::body_reader_state
(body_reader_adapter &adapter, bool ready, uint64_t max_discard) :
	m_adapter(std::addressof(adapter)), m_max_discard(max_discard), m_ready(ready)
{

}

body_reader_state::~body_reader_state() = default;

body_read_result body_reader_state::read_some(const mutable_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	if( m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return {};
	}
	if( not m_ready or not m_adapter or m_cancelled )
	{
		error = operation_error(errc::operation_not_permitted);
		return {};
	}
	if( m_finished )
		return {0, true};

	if( buffer.size() == 0 )
		return {};

	m_pending = true;
	auto result = m_adapter->read_some_sync(buffer, error);

	m_pending = false;
	if( not error )
		error = invalid_read_result(result, buffer.size());

	if( error )
	{
		m_cancelled = true;
		m_adapter->cancel();
	}
	if( not error and result.eof )
		m_finished = true;
	return result;
}

void body_reader_state::read_some(const mutable_buffer &buffer, read_handler_t handler)
{
	if( m_pending )
	{
		std::move(handler) (
			operation_error(errc::operation_in_progress),
			body_read_result {}
		);
		return ;
	}
	if( not m_ready or not m_adapter or m_cancelled )
	{
		std::move(handler) (
			operation_error(errc::operation_not_permitted),
			body_read_result {}
		);
		return ;
	}
	if( m_finished )
	{
		std::move(handler)(error_code{},
			body_read_result {.bytes = 0, .eof = true}
		);
		return ;
	}
	if( buffer.size() == 0 )
	{
		std::move(handler)(error_code{}, body_read_result{});
		return ;
	}
	auto lifetime = keep_alive();
	if( not lifetime )
	{
		std::move(handler) (
			operation_error(errc::operation_not_permitted),
			body_read_result {}
		);
		return ;
	}
	auto callback_lifetime = m_parent_lifetime_bound ?
		std::shared_ptr<void>{} : lifetime;

	m_pending = true;
	try {
		m_adapter->read_some(buffer, [
			state = this, lifetime = std::move(callback_lifetime),  capacity = buffer.size(),
			handler = std::move(handler)
		](error_code error, body_read_result result) mutable
		{
			RIWO_UNUSED(lifetime);
			state->m_pending = false;

			if( not error )
				error = invalid_read_result(result, capacity);

			if( error )
			{
				state->m_cancelled = true;
				state->m_adapter->cancel();
			}
			if( not error and result.eof )
				state->m_finished = true;

			std::move(handler)(error, result);
		});
	}
	catch(...)
	{
		m_pending = false;
		std::move(handler) (
			exception_error(std::current_exception()),
			body_read_result {}
		);
	}
}

void body_reader_state::discard(error_code &error) noexcept
{
	error.clear();
	if( m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return ;
	}
	if( not m_ready or not m_adapter or m_cancelled )
	{
		error = operation_error(errc::operation_not_permitted);
		return ;
	}
	if( m_finished )
		return ;

	m_pending = true;
	std::array<std::byte,8192> buffer {};
	uint64_t discarded = 0;
	for(;;)
	{
		auto result = m_adapter->read_some_sync(riwo::buffer(buffer), error);
		if( error )
			break;

		if( auto invalid = invalid_read_result(result, buffer.size()) )
		{
			error = invalid;
			m_cancelled = true;
			m_adapter->cancel();
			break;
		}
		if( result.bytes > m_max_discard - std::min(m_max_discard, discarded) )
		{
			error = operation_error(errc::buffer_limit_exceeded);
			m_adapter->cancel();
			break;
		}
		discarded += result.bytes;
		if( result.eof )
		{
			m_finished = true;
			break;
		}
	}
	m_pending = false;
}

void body_reader_state::discard(operation_handler_t handler)
{
	if( m_pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress));
		return ;
	}
	if( not m_ready or not m_adapter or m_cancelled )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted));
		return ;
	}
	if( m_finished )
	{
		std::move(handler)(error_code{});
		return ;
	}
	auto lifetime = keep_alive();
	if( not lifetime )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted));
		return ;
	}
	auto callback_lifetime = m_parent_lifetime_bound ?
		std::shared_ptr<void>{} : lifetime;

	m_pending = true;
	std::make_shared<discard_operation>(*this,
		std::move(callback_lifetime), std::move(handler))->start();
}

fields body_reader_state::trailers(error_code &error) noexcept
{
	error.clear();
	if( m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return {};
	}
	if( not m_ready or not m_adapter or m_cancelled or not m_finished )
	{
		error = operation_error(errc::operation_not_permitted);
		return {};
	}
	if( m_trailer_cache )
		return *m_trailer_cache;

	m_pending = true;
	auto result = m_adapter->trailers_sync(error);

	m_pending = false;
	if( not error )
		m_trailer_cache = result;

	return result;
}

void body_reader_state::trailers(trailers_handler_t handler)
{
	if( m_pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress), fields{});
		return ;
	}
	if( not m_ready or not m_adapter or m_cancelled or not m_finished )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted), fields{});
		return ;
	}
	if( m_trailer_cache )
	{
		std::move(handler)(error_code{}, *m_trailer_cache);
		return ;
	}
	auto lifetime = keep_alive();
	if( not lifetime )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted), fields {});
		return ;
	}
	auto callback_lifetime = m_parent_lifetime_bound ?
		std::shared_ptr<void>{} : lifetime;

	m_pending = true;
	try {
		m_adapter->trailers (
		[state = this, lifetime = std::move(callback_lifetime), handler = std::move(handler)]
		(error_code error, fields value) mutable
		{
			RIWO_UNUSED(lifetime);
			state->m_pending = false;

			if( not error )
				state->m_trailer_cache = value;

			std::move(handler)(error, std::move(value));
		});
	}
	catch(...)
	{
		m_pending = false;
		std::move(handler)(exception_error(std::current_exception()), fields{});
	}
}

void body_reader_state::cancel() noexcept
{
	m_cancelled = true;
	if( m_adapter )
		m_adapter->cancel();
}

void body_reader_state::bind_lifetime(std::weak_ptr<void> lifetime) noexcept
{
	m_lifetime = std::move(lifetime);
	m_parent_lifetime_bound = true;
}

void body_reader_state::set_ready(bool ready) noexcept
{
	m_ready = ready;
}

void body_reader_state::set_finished(bool finished) noexcept
{
	m_finished = finished;
}

bool body_reader_state::is_finished() const noexcept
{
	return m_finished;
}

std::shared_ptr<void> body_reader_state::keep_alive() noexcept
{
	if( auto owner = m_lifetime.lock() )
		return owner;
	try {
		return shared_from_this();
	}
	catch(...) {}
	return {};
}

std::shared_ptr<body_reader_state> make_body_reader_state
(body_reader_adapter_ptr adapter, bool ready, uint64_t max_discard)
{
	return std::make_shared<body_reader_state>(
		std::move(adapter), ready, max_discard
	);
}

} //namespace riwo::experimental::http::detail
