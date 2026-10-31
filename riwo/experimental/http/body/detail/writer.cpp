// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/experimental/http/body/writer.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http::detail { namespace
{

[[nodiscard]] error_code operation_error(errc value) noexcept
{
	return make_error_code(value);
}

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

[[nodiscard]] bool valid_trailers(const body_spec &spec, const fields &values) noexcept
{
	for(const auto &value : values)
	{
		if( std::ranges::none_of(spec.trailers().names,
			[&](const std::string &name) {
				return equal_name(name, value.name);
			}) )
			return false;
	}
	return true;
}

} //namespace

body_writer_adapter::~body_writer_adapter() = default;

body_writer_state::body_writer_state
(std::shared_ptr<body_writer_adapter> adapter, body_spec spec, bool ready) :
	m_owned_adapter(std::move(adapter)), m_adapter(m_owned_adapter.get()),
	m_spec(std::move(spec)), m_ready(ready),
	m_ended((m_spec.type() == body_spec::mode::empty or m_spec.type() == body_spec::mode::head) and ready)
{

}

body_writer_state::body_writer_state
(body_writer_adapter &adapter, body_spec spec, bool ready) :
	m_adapter(std::addressof(adapter)), m_spec(std::move(spec)), m_ready(ready),
	m_ended((m_spec.type() == body_spec::mode::empty or m_spec.type() == body_spec::mode::head) and ready)
{

}

body_writer_state::~body_writer_state() = default;

void body_writer_state::bind_lifetime(std::weak_ptr<void> lifetime) noexcept
{
	m_lifetime = std::move(lifetime);
	m_parent_lifetime_bound = true;
}

std::shared_ptr<void> body_writer_state::keep_alive() noexcept
{
	if( auto owner = m_lifetime.lock() )
		return owner;

	try {
		return shared_from_this();
	}
	catch(...) {
		return {};
	}
}

size_t body_writer_state::write(const const_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	if( m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return 0;
	}
	if( not m_ready or not m_adapter or m_ended or m_terminated )
	{
		error = operation_error(errc::operation_not_permitted);
		return 0;
	}
	if( buffer.size() == 0 )
		return 0;

	auto accepted_buffer = buffer;
	bool overflow = false;

	if( auto length = m_spec.length() )
	{
		const auto remaining = *length - std::min(*length, m_accepted);
		if( buffer.size() > remaining )
		{
			accepted_buffer = const_buffer(buffer.data(), remaining);
			overflow = true;
		}
	}
	if( accepted_buffer.size() == 0 )
	{
		m_terminated = true;
		m_adapter->cancel();
		error = operation_error(errc::body_length_mismatch);
		return 0;
	}
	m_pending = true;
	auto size = m_adapter->write_sync(accepted_buffer, error);

	m_pending = false;
	m_accepted += size;

	if( not error and size != accepted_buffer.size() )
		error = operation_error(errc::body_length_mismatch);

	if( not error and overflow )
		error = operation_error(errc::body_length_mismatch);

	if( error )
	{
		m_terminated = true;
		m_adapter->cancel();
	}
	return size;
}

void body_writer_state::write(const const_buffer &buffer, write_handler_t handler)
{
	if( m_pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress), 0);
		return ;
	}
	if( not m_ready or not m_adapter or m_ended or m_terminated )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted), 0);
		return ;
	}
	if( buffer.size() == 0 )
	{
		std::move(handler)(error_code{}, 0);
		return ;
	}
	auto accepted_buffer = buffer;
	bool overflow = false;

	if( auto length = m_spec.length() )
	{
		const auto remaining = *length - std::min(*length, m_accepted);
		if( buffer.size() > remaining )
		{
			accepted_buffer = const_buffer(buffer.data(), remaining);
			overflow = true;
		}
	}
	if( accepted_buffer.size() == 0 )
	{
		m_terminated = true;
		m_adapter->cancel();
		std::move(handler)(operation_error(errc::body_length_mismatch), 0);
		return ;
	}
	auto lifetime = keep_alive();
	if( not lifetime )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted), 0);
		return ;
	}
	auto callback_lifetime = m_parent_lifetime_bound ?
		std::shared_ptr<void>{} : lifetime;

	m_pending = true;
	try {
		m_adapter->write(accepted_buffer, [
			state = this, lifetime = std::move(callback_lifetime), handler = std::move(handler),
			expected = accepted_buffer.size(), overflow
		](error_code error, size_t size) mutable
		{
			RIWO_UNUSED(lifetime);
			state->m_pending = false;
			state->m_accepted += size;

			if( not error and size != expected )
				error = operation_error(errc::body_length_mismatch);

			if( not error and overflow )
				error = operation_error(errc::body_length_mismatch);

			if( error )
			{
				state->m_terminated = true;
				state->m_adapter->cancel();
			}
			std::move(handler)(error, size);
		});
	}
	catch(...)
	{
		m_pending = false;
		m_terminated = true;
		m_adapter->cancel();

		std::move(handler) (
			exception_error(std::current_exception()), 0
		);
	}
}

void body_writer_state::finish(fields trailers, error_code &error) noexcept
{
	error.clear();
	if( m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return ;
	}
	if( not m_ready or not m_adapter or m_ended or m_terminated )
	{
		error = operation_error(errc::operation_not_permitted);
		return ;
	}
	if( auto length = m_spec.length(); length and m_accepted != *length )
	{
		m_terminated = true;
		m_adapter->cancel();
		error = operation_error(errc::body_length_mismatch);
		return ;
	}
	if( not valid_trailers(m_spec, trailers) )
	{
		m_terminated = true;
		m_adapter->cancel();
		error = operation_error(errc::invalid_trailer);
		return ;
	}
	m_pending = true;
	m_adapter->finish_sync(std::move(trailers), error);
	m_pending = false;

	if( error )
		m_terminated = true;
	else
		m_ended = true;
}

void body_writer_state::finish(fields trailers, finish_handler_t handler)
{
	if( m_pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress));
		return ;
	}
	if( not m_ready or not m_adapter or m_ended or m_terminated )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted));
		return ;
	}
	if( auto length = m_spec.length(); length and m_accepted != *length )
	{
		m_terminated = true;
		m_adapter->cancel();
		std::move(handler)(operation_error(errc::body_length_mismatch));
		return ;
	}
	if( not valid_trailers(m_spec, trailers) )
	{
		m_terminated = true;
		m_adapter->cancel();
		std::move(handler)(operation_error(errc::invalid_trailer));
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
	try {
		m_adapter->finish(std::move(trailers),
		[state = this, lifetime = std::move(callback_lifetime), handler = std::move(handler)]
		(error_code error) mutable
		{
			RIWO_UNUSED(lifetime);
			state->m_pending = false;

			if( error )
				state->m_terminated = true;
			else
				state->m_ended = true;

			std::move(handler)(error);
		});
	}
	catch(...)
	{
		m_pending = false;
		m_terminated = true;
		m_adapter->cancel();

		std::move(handler) (
			exception_error(std::current_exception())
		);
	}
}

void body_writer_state::cancel() noexcept
{
	m_terminated = true;
	if( m_adapter )
		m_adapter->cancel();
}

void body_writer_state::set_spec(body_spec spec, bool ready) noexcept
{
	if( m_pending or m_accepted != 0 or m_ended or m_terminated )
		return ;

	m_spec = std::move(spec);
	m_ready = ready;

	m_ended = ready and (
		m_spec.type() == body_spec::mode::empty or
		m_spec.type() == body_spec::mode::head
	);
}

void body_writer_state::set_ready(bool ready) noexcept
{
	m_ready = ready;
	if( ready and (
		m_spec.type() == body_spec::mode::empty or
		m_spec.type() == body_spec::mode::head
	) )
		m_ended = true;
}

bool body_writer_state::ended() const noexcept
{
	return m_ended;
}

uint64_t body_writer_state::accepted_size() const noexcept
{
	return m_accepted;
}

body_writer_state_ptr make_body_writer_state(body_writer_adapter_ptr adapter, body_spec spec, bool ready)
{
	return std::make_shared<body_writer_state>(
		std::move(adapter), std::move(spec), ready
	);
}

} //namespace riwo::experimental::http::detail
