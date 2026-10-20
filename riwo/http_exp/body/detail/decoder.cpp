// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "decoder.h"

#if RIWO_HTTP_EXP_ZLIB_SUPPORT

#include <zlib.h>

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace riwo::http_exp::detail
{

struct decoding_body_reader_adapter::impl
{
	impl(std::shared_ptr<body_reader_state> source_value,
		std::string encoding_value) :
		m_source(std::move(source_value)), m_encoding(std::move(encoding_value))
	{
		const auto window = m_encoding == "gzip" ? 16 + MAX_WBITS : MAX_WBITS;
		m_stream.zalloc = Z_NULL;
		m_stream.zfree = Z_NULL;
		m_stream.opaque = Z_NULL;
		if( inflateInit2(&m_stream, window) != Z_OK )
			throw std::runtime_error("cannot initialize HTTP content decoder");
		m_initialized = true;
	}

	~impl()
	{
		if( m_initialized ) inflateEnd(&m_stream);
	}

	std::shared_ptr<body_reader_state> m_source;
	std::string m_encoding;
	z_stream m_stream {};
	std::array<std::byte,16384> m_input {};
	std::size_t m_input_begin = 0;
	std::size_t m_input_end = 0;
	bool m_initialized = false;
	bool m_source_eof = false;
	bool m_ended = false;
};

namespace
{

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
}

} //namespace

decoding_body_reader_adapter::decoding_body_reader_adapter(
	std::shared_ptr<body_reader_state> source, std::string encoding) :
	m_impl(std::make_shared<impl>(std::move(source), std::move(encoding)))
{
}

decoding_body_reader_adapter::~decoding_body_reader_adapter() = default;

[[nodiscard]] bool decoding_body_reader_adapter::fill_input_sync(
	error_code &error)
{
	auto &state = *m_impl;
	while( state.m_input_begin == state.m_input_end and not state.m_source_eof )
	{
		const auto read = state.m_source->read_some(riwo::buffer(state.m_input),
			error);
		if( error ) return false;
		state.m_input_begin = 0;
		state.m_input_end = read.bytes;
		if( read.bytes == 0 )
		{
			if( read.eof )
			{
				state.m_source_eof = true;
				error = make_error_code(errc::body_length_mismatch);
				return false;
			}
			continue;
		}
		if( read.eof ) state.m_source_eof = true;
		return true;
	}
	if( state.m_input_begin == state.m_input_end )
	{
		error = make_error_code(errc::body_length_mismatch);
		return false;
	}
	return true;
}

[[nodiscard]] bool decoding_body_reader_adapter::inflate_step(
	mutable_buffer buffer, std::size_t &produced, bool &ended, error_code &error)
{
	auto &state = *m_impl;
	state.m_stream.next_in = reinterpret_cast<Bytef*>(
		state.m_input.data() + state.m_input_begin);
	state.m_stream.avail_in = static_cast<uInt>(
		std::min<std::size_t>(state.m_input_end - state.m_input_begin,
			std::numeric_limits<uInt>::max()));
	state.m_stream.next_out = reinterpret_cast<Bytef*>(
		static_cast<std::byte*>(buffer.data()) + produced);
	state.m_stream.avail_out = static_cast<uInt>(
		std::min<std::size_t>(buffer.size() - produced,
			std::numeric_limits<uInt>::max()));

	const auto input_before = state.m_stream.avail_in;
	const auto output_before = state.m_stream.avail_out;
	const auto result = inflate(&state.m_stream, Z_NO_FLUSH);
	state.m_input_begin += input_before - state.m_stream.avail_in;
	produced += output_before - state.m_stream.avail_out;
	if( result == Z_STREAM_END )
	{
		ended = true;
		return true;
	}
	if( result != Z_OK and result != Z_BUF_ERROR )
	{
		error = protocol_error();
		return false;
	}
	return true;
}

[[nodiscard]] bool decoding_body_reader_adapter::drain_source_sync(
	error_code &error)
{
	auto &state = *m_impl;
	std::array<std::byte,4096> buffer {};
	while( not state.m_source_eof )
	{
		const auto read = state.m_source->read_some(riwo::buffer(buffer), error);
		if( error ) return false;
		if( read.eof ) state.m_source_eof = true;
		if( read.bytes == 0 and not read.eof ) return false;
	}
	return true;
}

[[nodiscard]] body_read_result decoding_body_reader_adapter::decode_sync(
	mutable_buffer buffer, error_code &error)
{
	auto &state = *m_impl;
	if( state.m_ended )
	{
		if( not drain_source_sync(error) ) return {};
		return {0, true};
	}
	if( buffer.size() == 0 ) return {0, false};

	std::size_t produced = 0;
	for(;;)
	{
		if( state.m_input_begin == state.m_input_end and not fill_input_sync(error) )
			return {};
		bool ended = false;
		if( not inflate_step(buffer, produced, ended, error) ) return {};
		if( ended )
		{
			state.m_ended = true;
			if( not drain_source_sync(error) ) return {};
			return {produced, true};
		}
		if( produced == buffer.size() ) return {produced, false};
		if( state.m_input_begin == state.m_input_end ) continue;
		if( produced == 0 and state.m_stream.avail_in == 0 ) continue;
	}
}

[[nodiscard]] body_read_result decoding_body_reader_adapter::read_some_sync(
	mutable_buffer buffer, error_code &error) noexcept
{
	error.clear();
	try {
		return decode_sync(buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return {};
	}
}

class decoding_body_reader_adapter::async_operation final :
	public std::enable_shared_from_this<async_operation>
{
public:
	async_operation(std::shared_ptr<decoding_body_reader_adapter> owner_value,
		mutable_buffer target, read_handler completion) :
		m_owner(std::move(owner_value)), m_target(target),
		m_handler(std::move(completion))
	{
	}

	void start()
	{
		step();
	}

private:
	void step()
	{
		auto &state = *m_owner->m_impl;
		if( state.m_ended )
		{
			drain_source([self = shared_from_this()](error_code error)
			{
				self->finish(error, self->m_produced, true);
			});
			return;
		}
		if( state.m_input_begin == state.m_input_end )
		{
			m_owner->m_impl->m_source->read_some(riwo::buffer(state.m_input),
			[self = shared_from_this()](error_code error,
				body_read_result read)
			{
				auto &state = *self->m_owner->m_impl;
				if( error )
				{
					self->finish(error, self->m_produced, false);
					return;
				}
				state.m_input_begin = 0;
				state.m_input_end = read.bytes;
				if( read.bytes == 0 and read.eof )
				{
					self->finish(make_error_code(errc::body_length_mismatch),
						self->m_produced, false);
					return;
				}
				if( read.eof ) state.m_source_eof = true;
				self->pump();
			});
			return;
		}
		pump();
	}

	void pump()
	{
		auto &state = *m_owner->m_impl;
		bool ended = false;
		error_code error;
		if( not m_owner->inflate_step(m_target, m_produced, ended, error) )
		{
			finish(error, m_produced, false);
			return;
		}
		if( ended )
		{
			state.m_ended = true;
			drain_source([self = shared_from_this()](error_code drain_error)
			{
				self->finish(drain_error, self->m_produced, true);
			});
			return;
		}
		if( m_produced == m_target.size() )
		{
			finish(error_code{}, m_produced, false);
			return;
		}
		step();
	}

	void drain_source(std::function<void(error_code)> completion)
	{
		auto &state = *m_owner->m_impl;
		if( state.m_source_eof )
		{
			completion(error_code{});
			return;
		}
		auto buffer = std::make_shared<std::array<std::byte,4096>>();
		auto self = shared_from_this();
		struct drain_state : std::enable_shared_from_this<drain_state>
		{
			std::shared_ptr<async_operation> owner;
			std::shared_ptr<std::array<std::byte,4096>> buffer;
			std::function<void(error_code)> completion;

			void step()
			{
				owner->m_owner->m_impl->m_source->read_some(riwo::buffer(*buffer),
				[self = shared_from_this()](error_code error,
					body_read_result read)
				{
					if( error )
					{
						std::move(self->completion)(error);
						return;
					}
					auto &state = *self->owner->m_owner->m_impl;
					if( read.eof ) state.m_source_eof = true;
					if( read.eof or read.bytes == 0 )
					{
						std::move(self->completion)(error_code{});
						return;
					}
					self->step();
				});
			}
		};
		auto drain = std::make_shared<drain_state>();
		drain->owner = std::move(self);
		drain->buffer = std::move(buffer);
		drain->completion = std::move(completion);
		drain->step();
	}

	void finish(error_code error, std::size_t size, bool eof)
	{
		if( m_finished ) return;
		m_finished = true;
		std::move(m_handler)(error, body_read_result {size, eof});
	}

	std::shared_ptr<decoding_body_reader_adapter> m_owner;
	mutable_buffer m_target;
	read_handler m_handler;
	std::size_t m_produced = 0;
	bool m_finished = false;
};

void decoding_body_reader_adapter::read_some(mutable_buffer buffer,
	read_handler handler)
{
	std::make_shared<async_operation>(shared_from_this(), buffer,
		std::move(handler))->start();
}

[[nodiscard]] fields decoding_body_reader_adapter::trailers_sync(
	error_code &error) noexcept
{
	error.clear();
	if( not m_impl->m_ended )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	return m_impl->m_source->trailers(error);
}

void decoding_body_reader_adapter::trailers(trailers_handler handler)
{
	if( not m_impl->m_ended )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			fields{});
		return;
	}
	m_impl->m_source->trailers(std::move(handler));
}

void decoding_body_reader_adapter::cancel() noexcept {
	m_impl->m_source->cancel();
}

std::shared_ptr<body_reader_state> make_decoding_body_reader(
	std::shared_ptr<body_reader_state> source, std::string encoding)
{
	if( not source or encoding.empty() ) return source;
	auto adapter = std::make_shared<decoding_body_reader_adapter>(
		std::move(source), std::move(encoding));
	return make_body_reader_state(adapter, true);
}

} //namespace riwo::http_exp::detail

#else

namespace riwo::http_exp::detail
{

std::shared_ptr<body_reader_state> make_decoding_body_reader(
	std::shared_ptr<body_reader_state> source, std::string encoding)
{
	ignore_unused(encoding);
	return source;
}

} //namespace riwo::http_exp::detail

#endif
