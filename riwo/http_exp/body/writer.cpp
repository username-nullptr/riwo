// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "writer.h"

namespace riwo::http_exp::detail
{

body_writer_adapter::~body_writer_adapter() = default;

class body_writer_state::impl
{
public:
	impl(std::shared_ptr<body_writer_adapter> output, body_spec description,
		bool output_ready) :
		adapter(std::move(output)), spec(std::move(description)), ready(output_ready),
		ended((spec.type() == body_spec::mode::empty ||
			spec.type() == body_spec::mode::head) && output_ready)
	{
	}

	std::shared_ptr<body_writer_adapter> adapter;
	body_spec spec;
	uint64_t accepted = 0;
	bool ready = false;
	bool pending = false;
	bool ended = false;
	bool terminated = false;
};

namespace
{

[[nodiscard]] error_code operation_error(errc value) noexcept {
	return make_error_code(value);
}

[[nodiscard]] bool equal_name(std::string_view lhs,
	std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() &&
		std::ranges::equal(lhs, rhs, [](char left, char right) {
			return std::tolower(static_cast<unsigned char>(left)) ==
				std::tolower(static_cast<unsigned char>(right));
		});
}

[[nodiscard]] bool valid_trailers(const body_spec &spec,
	const fields &values) noexcept
{
	for( const auto &value : values )
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

body_writer_state::body_writer_state(std::shared_ptr<body_writer_adapter> adapter,
	body_spec spec, bool ready) :
	m_impl(std::make_shared<impl>(std::move(adapter), std::move(spec), ready))
{
}

body_writer_state::~body_writer_state() = default;

size_t body_writer_state::write(const_buffer buffer, error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;
	if( state->pending )
	{
		error = operation_error(errc::operation_in_progress);
		return 0;
	}
	if( !state->ready || !state->adapter || state->ended || state->terminated )
	{
		error = operation_error(errc::operation_not_permitted);
		return 0;
	}
	if( buffer.size() == 0 )
		return 0;

	auto accepted_buffer = buffer;
	bool overflow = false;
	if( auto length = state->spec.length() )
	{
		const auto remaining = *length -
			std::min(*length, state->accepted);
		if( buffer.size() > remaining )
		{
			accepted_buffer = const_buffer(buffer.data(),
				static_cast<size_t>(remaining));
			overflow = true;
		}
	}
	if( accepted_buffer.size() == 0 )
	{
		state->terminated = true;
		state->adapter->cancel();
		error = operation_error(errc::body_length_mismatch);
		return 0;
	}

	state->pending = true;
	auto size = state->adapter->write_sync(accepted_buffer, error);
	state->pending = false;
	state->accepted += size;
	if( !error && size != accepted_buffer.size() )
		error = operation_error(errc::body_length_mismatch);
	if( !error && overflow )
		error = operation_error(errc::body_length_mismatch);
	if( error )
	{
		state->terminated = true;
		state->adapter->cancel();
	}
	return size;
}

void body_writer_state::write(const_buffer buffer, write_handler handler)
{
	auto state = m_impl;
	if( state->pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress), 0);
		return;
	}
	if( !state->ready || !state->adapter || state->ended || state->terminated )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted), 0);
		return;
	}
	if( buffer.size() == 0 )
	{
		std::move(handler)(error_code{}, 0);
		return;
	}

	auto accepted_buffer = buffer;
	bool overflow = false;
	if( auto length = state->spec.length() )
	{
		const auto remaining = *length -
			std::min(*length, state->accepted);
		if( buffer.size() > remaining )
		{
			accepted_buffer = const_buffer(buffer.data(),
				static_cast<size_t>(remaining));
			overflow = true;
		}
	}
	if( accepted_buffer.size() == 0 )
	{
		state->terminated = true;
		state->adapter->cancel();
		std::move(handler)(operation_error(errc::body_length_mismatch), 0);
		return;
	}

	state->pending = true;
	try {
		state->adapter->write(accepted_buffer,
		[state, handler = std::move(handler),
		 expected = accepted_buffer.size(), overflow]
		(error_code error, size_t size) mutable
		{
			state->pending = false;
			state->accepted += size;
			if( !error && size != expected )
				error = operation_error(errc::body_length_mismatch);
			if( !error && overflow )
				error = operation_error(errc::body_length_mismatch);
			if( error )
			{
				state->terminated = true;
				state->adapter->cancel();
			}
			std::move(handler)(error, size);
		});
	}
	catch(...) {
		state->pending = false;
		state->terminated = true;
		state->adapter->cancel();
		std::move(handler)(exception_error(std::current_exception()), 0);
	}
}

void body_writer_state::finish(fields trailers, error_code &error) noexcept
{
	error.clear();
	auto state = m_impl;
	if( state->pending )
	{
		error = operation_error(errc::operation_in_progress);
		return;
	}
	if( !state->ready || !state->adapter || state->ended || state->terminated )
	{
		error = operation_error(errc::operation_not_permitted);
		return;
	}
	if( auto length = state->spec.length();
		length && state->accepted != *length )
	{
		state->terminated = true;
		state->adapter->cancel();
		error = operation_error(errc::body_length_mismatch);
		return;
	}
	if( !valid_trailers(state->spec, trailers) )
	{
		state->terminated = true;
		state->adapter->cancel();
		error = operation_error(errc::invalid_trailer);
		return;
	}

	state->pending = true;
	state->adapter->finish_sync(std::move(trailers), error);
	state->pending = false;
	if( error )
		state->terminated = true;
	else
		state->ended = true;
}

void body_writer_state::finish(fields trailers, finish_handler handler)
{
	auto state = m_impl;
	if( state->pending )
	{
		std::move(handler)(operation_error(errc::operation_in_progress));
		return;
	}
	if( !state->ready || !state->adapter || state->ended || state->terminated )
	{
		std::move(handler)(operation_error(errc::operation_not_permitted));
		return;
	}
	if( auto length = state->spec.length();
		length && state->accepted != *length )
	{
		state->terminated = true;
		state->adapter->cancel();
		std::move(handler)(operation_error(errc::body_length_mismatch));
		return;
	}
	if( !valid_trailers(state->spec, trailers) )
	{
		state->terminated = true;
		state->adapter->cancel();
		std::move(handler)(operation_error(errc::invalid_trailer));
		return;
	}

	state->pending = true;
	try {
		state->adapter->finish(std::move(trailers),
		[state, handler = std::move(handler)]
		(error_code error) mutable
		{
			state->pending = false;
			if( error )
				state->terminated = true;
			else
				state->ended = true;
			std::move(handler)(error);
		});
	}
	catch(...) {
		state->pending = false;
		state->terminated = true;
		state->adapter->cancel();
		std::move(handler)(exception_error(std::current_exception()));
	}
}

void body_writer_state::cancel() noexcept
{
	auto state = m_impl;
	state->terminated = true;
	if( state->adapter )
		state->adapter->cancel();
}

void body_writer_state::configure(body_spec spec, bool ready) noexcept
{
	auto state = m_impl;
	if( state->pending || state->accepted != 0 || state->ended || state->terminated )
		return;
	state->spec = std::move(spec);
	state->ready = ready;
	state->ended = ready && (state->spec.type() == body_spec::mode::empty ||
		state->spec.type() == body_spec::mode::head);
}

void body_writer_state::set_ready(bool ready) noexcept
{
	m_impl->ready = ready;
	if( ready && (m_impl->spec.type() == body_spec::mode::empty ||
		m_impl->spec.type() == body_spec::mode::head) )
		m_impl->ended = true;
}

bool body_writer_state::ended() const noexcept {
	return m_impl->ended;
}

uint64_t body_writer_state::accepted_size() const noexcept {
	return m_impl->accepted;
}

std::shared_ptr<body_writer_state> make_body_writer_state(
	std::shared_ptr<body_writer_adapter> adapter, body_spec spec, bool ready)
{
	return std::make_shared<body_writer_state>(
		std::move(adapter), std::move(spec), ready);
}

} //namespace riwo::http_exp::detail
