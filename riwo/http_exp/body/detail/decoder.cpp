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
		source(std::move(source_value)), encoding(std::move(encoding_value))
	{
		const auto window = encoding == "gzip" ? 16 + MAX_WBITS : MAX_WBITS;
		stream.zalloc = Z_NULL;
		stream.zfree = Z_NULL;
		stream.opaque = Z_NULL;
		if( inflateInit2(&stream, window) != Z_OK )
			throw std::runtime_error("cannot initialize HTTP content decoder");
		initialized = true;
	}

	~impl()
	{
		if( initialized ) inflateEnd(&stream);
	}

	std::shared_ptr<body_reader_state> source;
	std::string encoding;
	z_stream stream {};
	std::array<std::byte,16384> input {};
	std::size_t input_begin = 0;
	std::size_t input_end = 0;
	bool initialized = false;
	bool source_eof = false;
	bool ended = false;
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
	while( state.input_begin == state.input_end and not state.source_eof )
	{
		const auto read = state.source->read_some(riwo::buffer(state.input),
			error);
		if( error ) return false;
		state.input_begin = 0;
		state.input_end = read.bytes;
		if( read.bytes == 0 )
		{
			if( read.eof )
			{
				state.source_eof = true;
				error = make_error_code(errc::body_length_mismatch);
				return false;
			}
			continue;
		}
		if( read.eof ) state.source_eof = true;
		return true;
	}
	if( state.input_begin == state.input_end )
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
	state.stream.next_in = reinterpret_cast<Bytef*>(
		state.input.data() + state.input_begin);
	state.stream.avail_in = static_cast<uInt>(
		std::min<std::size_t>(state.input_end - state.input_begin,
			std::numeric_limits<uInt>::max()));
	state.stream.next_out = reinterpret_cast<Bytef*>(
		static_cast<std::byte*>(buffer.data()) + produced);
	state.stream.avail_out = static_cast<uInt>(
		std::min<std::size_t>(buffer.size() - produced,
			std::numeric_limits<uInt>::max()));

	const auto input_before = state.stream.avail_in;
	const auto output_before = state.stream.avail_out;
	const auto result = inflate(&state.stream, Z_NO_FLUSH);
	state.input_begin += input_before - state.stream.avail_in;
	produced += output_before - state.stream.avail_out;
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
	while( not state.source_eof )
	{
		const auto read = state.source->read_some(riwo::buffer(buffer), error);
		if( error ) return false;
		if( read.eof ) state.source_eof = true;
		if( read.bytes == 0 and not read.eof ) return false;
	}
	return true;
}

[[nodiscard]] body_read_result decoding_body_reader_adapter::decode_sync(
	mutable_buffer buffer, error_code &error)
{
	auto &state = *m_impl;
	if( state.ended )
	{
		if( not drain_source_sync(error) ) return {};
		return {0, true};
	}
	if( buffer.size() == 0 ) return {0, false};

	std::size_t produced = 0;
	for(;;)
	{
		if( state.input_begin == state.input_end and not fill_input_sync(error) )
			return {};
		bool ended = false;
		if( not inflate_step(buffer, produced, ended, error) ) return {};
		if( ended )
		{
			state.ended = true;
			if( not drain_source_sync(error) ) return {};
			return {produced, true};
		}
		if( produced == buffer.size() ) return {produced, false};
		if( state.input_begin == state.input_end ) continue;
		if( produced == 0 and state.stream.avail_in == 0 ) continue;
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
		owner(std::move(owner_value)), target(target),
		handler(std::move(completion))
	{
	}

	void start()
	{
		step();
	}

private:
	void step()
	{
		auto &state = *owner->m_impl;
		if( state.ended )
		{
			drain_source([self = shared_from_this()](error_code error)
			{
				self->finish(error, self->produced, true);
			});
			return;
		}
		if( state.input_begin == state.input_end )
		{
			owner->m_impl->source->read_some(riwo::buffer(state.input),
			[self = shared_from_this()](error_code error,
				body_read_result read)
			{
				auto &state = *self->owner->m_impl;
				if( error )
				{
					self->finish(error, self->produced, false);
					return;
				}
				state.input_begin = 0;
				state.input_end = read.bytes;
				if( read.bytes == 0 and read.eof )
				{
					self->finish(make_error_code(errc::body_length_mismatch),
						self->produced, false);
					return;
				}
				if( read.eof ) state.source_eof = true;
				self->pump();
			});
			return;
		}
		pump();
	}

	void pump()
	{
		auto &state = *owner->m_impl;
		bool ended = false;
		error_code error;
		if( not owner->inflate_step(target, produced, ended, error) )
		{
			finish(error, produced, false);
			return;
		}
		if( ended )
		{
			state.ended = true;
			drain_source([self = shared_from_this()](error_code drain_error)
			{
				self->finish(drain_error, self->produced, true);
			});
			return;
		}
		if( produced == target.size() )
		{
			finish(error_code{}, produced, false);
			return;
		}
		step();
	}

	void drain_source(std::function<void(error_code)> completion)
	{
		auto &state = *owner->m_impl;
		if( state.source_eof )
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
				owner->owner->m_impl->source->read_some(riwo::buffer(*buffer),
				[self = shared_from_this()](error_code error,
					body_read_result read)
				{
					if( error )
					{
						std::move(self->completion)(error);
						return;
					}
					auto &state = *self->owner->owner->m_impl;
					if( read.eof ) state.source_eof = true;
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
		if( finished ) return;
		finished = true;
		std::move(handler)(error, body_read_result {size, eof});
	}

	std::shared_ptr<decoding_body_reader_adapter> owner;
	mutable_buffer target;
	read_handler handler;
	std::size_t produced = 0;
	bool finished = false;
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
	if( not m_impl->ended )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	return m_impl->source->trailers(error);
}

void decoding_body_reader_adapter::trailers(trailers_handler handler)
{
	if( not m_impl->ended )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			fields{});
		return;
	}
	m_impl->source->trailers(std::move(handler));
}

void decoding_body_reader_adapter::cancel() noexcept {
	m_impl->source->cancel();
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
