// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/body/writer.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp::detail { namespace
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

class RIWO_DECL_HIDDEN body_writer_state::impl
{
public:
	impl(body_writer_adapter_ptr output, body_spec description, bool output_ready) :
		m_adapter(std::move(output)), m_spec(std::move(description)), m_ready(output_ready),
		m_ended ((
			m_spec.type() == body_spec::mode::empty or m_spec.type() == body_spec::mode::head
		)
		and output_ready) {}

public:
	body_writer_adapter_ptr m_adapter {};
	body_spec m_spec;

	uint64_t m_accepted = 0;

	bool m_ready = false;
	bool m_pending = false;

	bool m_ended = false;
	bool m_terminated = false;
};

body_writer_state::body_writer_state
(std::shared_ptr<body_writer_adapter> adapter, body_spec spec, bool ready) :
	m_impl(std::make_shared<impl>(std::move(adapter), std::move(spec), ready))
{

}

body_writer_state::~body_writer_state() = default;

size_t body_writer_state::write(const const_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;

	if( state->m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return 0;
	}
	if( not state->m_ready or not state->m_adapter or state->m_ended or state->m_terminated )
	{
		error = operation_error(errc::operation_not_permitted);
		return 0;
	}
	if( buffer.size() == 0 )
		return 0;

	auto accepted_buffer = buffer;
	bool overflow = false;

	if( auto length = state->m_spec.length() )
	{
		const auto remaining = *length - std::min(*length, state->m_accepted);
		if( buffer.size() > remaining )
		{
			accepted_buffer = const_buffer(buffer.data(), remaining);
			overflow = true;
		}
	}
	if( accepted_buffer.size() == 0 )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
		error = operation_error(errc::body_length_mismatch);
		return 0;
	}
	state->m_pending = true;
	auto size = state->m_adapter->write_sync(accepted_buffer, error);

	state->m_pending = false;
	state->m_accepted += size;

	if( not error and size != accepted_buffer.size() )
		error = operation_error(errc::body_length_mismatch);

	if( not error and overflow )
		error = operation_error(errc::body_length_mismatch);

	if( error )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
	}
	return size;
}

void body_writer_state::write(const const_buffer &buffer, write_handler_t handler)
{
	auto state = m_impl;
	if( state->m_pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress), 0);
		return ;
	}
	if( not state->m_ready or not state->m_adapter or state->m_ended or state->m_terminated )
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

	if( auto length = state->m_spec.length() )
	{
		const auto remaining = *length - std::min(*length, state->m_accepted);
		if( buffer.size() > remaining )
		{
			accepted_buffer = const_buffer(buffer.data(), remaining);
			overflow = true;
		}
	}
	if( accepted_buffer.size() == 0 )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
		std::move(handler)(operation_error(errc::body_length_mismatch), 0);
		return ;
	}
	state->m_pending = true;
	try {
		state->m_adapter->write(accepted_buffer,
		[state, handler = std::move(handler), expected = accepted_buffer.size(), overflow]
		(error_code error, size_t size) mutable
		{
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
		state->m_pending = false;
		state->m_terminated = true;
		state->m_adapter->cancel();

		std::move(handler) (
			exception_error(std::current_exception()), 0
		);
	}
}

void body_writer_state::finish(fields trailers, error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;

	if( state->m_pending )
	{
		error = operation_error(errc::operation_in_progress);
		return ;
	}
	if( not state->m_ready or not state->m_adapter or state->m_ended or state->m_terminated )
	{
		error = operation_error(errc::operation_not_permitted);
		return ;
	}
	if( auto length = state->m_spec.length(); length and state->m_accepted != *length )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
		error = operation_error(errc::body_length_mismatch);
		return ;
	}
	if( not valid_trailers(state->m_spec, trailers) )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
		error = operation_error(errc::invalid_trailer);
		return ;
	}
	state->m_pending = true;
	state->m_adapter->finish_sync(std::move(trailers), error);
	state->m_pending = false;

	if( error )
		state->m_terminated = true;
	else
		state->m_ended = true;
}

void body_writer_state::finish(fields trailers, finish_handler_t handler)
{
	auto state = m_impl;
	if( state->m_pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress));
		return ;
	}
	if( not state->m_ready or not state->m_adapter or state->m_ended or state->m_terminated )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted));
		return ;
	}
	if( auto length = state->m_spec.length(); length and state->m_accepted != *length )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
		std::move(handler)(operation_error(errc::body_length_mismatch));
		return ;
	}
	if( not valid_trailers(state->m_spec, trailers) )
	{
		state->m_terminated = true;
		state->m_adapter->cancel();
		std::move(handler)(operation_error(errc::invalid_trailer));
		return ;
	}
	state->m_pending = true;
	try {
		state->m_adapter->finish(std::move(trailers),
		[state, handler = std::move(handler)](error_code error) mutable
		{
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
		state->m_pending = false;
		state->m_terminated = true;
		state->m_adapter->cancel();

		std::move(handler) (
			exception_error(std::current_exception())
		);
	}
}

void body_writer_state::cancel() noexcept
{
	auto state = m_impl;
	state->m_terminated = true;

	if( state->m_adapter )
		state->m_adapter->cancel();
}

void body_writer_state::configure(body_spec spec, bool ready) noexcept
{
	auto state = m_impl;
	if( state->m_pending or state->m_accepted != 0 or state->m_ended or state->m_terminated )
		return ;

	state->m_spec = std::move(spec);
	state->m_ready = ready;

	state->m_ended = ready and (
		state->m_spec.type() == body_spec::mode::empty or
		state->m_spec.type() == body_spec::mode::head
	);
}

void body_writer_state::set_ready(bool ready) noexcept
{
	m_impl->m_ready = ready;
	if( ready and (
		m_impl->m_spec.type() == body_spec::mode::empty or
		m_impl->m_spec.type() == body_spec::mode::head
	) )
		m_impl->m_ended = true;
}

bool body_writer_state::ended() const noexcept
{
	return m_impl->m_ended;
}

uint64_t body_writer_state::accepted_size() const noexcept
{
	return m_impl->m_accepted;
}

body_writer_state_ptr make_body_writer_state(body_writer_adapter_ptr adapter, body_spec spec, bool ready)
{
	return std::make_shared<body_writer_state>(
		std::move(adapter), std::move(spec), ready
	);
}

} //namespace riwo::http_exp::detail
