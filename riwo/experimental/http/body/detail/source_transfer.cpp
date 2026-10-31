// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "source_transfer.h"

namespace riwo::experimental::http::detail { namespace
{

[[nodiscard]] bool eof_error(const error_code &error) noexcept
{
	return error == asio::error::make_error_code(asio::error::eof);
}

[[nodiscard]] error_code invalid_read(size_t size, size_t capacity) noexcept
{
	return size > capacity ?
		make_system_error_code(std::errc::io_error) :
		make_error_code(errc::body_length_mismatch);
}

void notify_progress(const std::function<void(uint64_t)> &progress, uint64_t total) noexcept
{
	if( not progress )
		return ;
	try {
		progress(total);
	}
	catch(...) {
		/* Observers must not alter transfer semantics. */
	}
}

struct operation final : std::enable_shared_from_this<operation>
{
	body_source_ptr source;
	body_writer_state_ptr writer;

	source_transfer_options options;
	std::array<std::byte,8192> buffer{};

	uint64_t total = 0;
	asio::any_completion_handler<void(error_code,uint64_t)> handler;

	void start() {
		read_next();
	}
	void read_next()
	{
		if( options.length and total == *options.length )
		{
			finish_source();
			return ;
		}
		auto target = riwo::buffer(buffer);
		if( options.length )
		{
			target = mutable_buffer(target.data(),
				std::min<uint64_t>(target.size(), *options.length - total)
			);
		}
		auto self = shared_from_this();
		try {
			source->read_some(target,
			[self = std::move(self), target](error_code error, size_t size) mutable {
				self->read_complete(target, error, size);
			});
		}
		catch(...) {
			complete(exception_error(std::current_exception()));
		}
	}

	void read_complete(const mutable_buffer &target, error_code error, size_t size)
	{
		if( eof_error(error) )
		{
			if( options.length )
				complete(make_error_code(errc::body_length_mismatch));
			else
				finish_source();
			return ;
		}
		if( error )
		{
			complete(error);
			return ;
		}
		if( size == 0 or size > target.size() )
		{
			complete(invalid_read(size, target.size()));
			return ;
		}
		auto self = shared_from_this();
		try {
			writer->write(const_buffer(target.data(), size),
			[self = std::move(self), size](error_code write_error, size_t written) mutable {
				self->write_complete(size, write_error, written);
			});
		}
		catch(...) {
			complete(exception_error(std::current_exception()));
		}
	}

	void write_complete(size_t expected, error_code error, size_t written)
	{
		if( written > std::numeric_limits<uint64_t>::max() - total )
		{
			complete(make_system_error_code(std::errc::value_too_large));
			return ;
		}
		total += written;
		if( error )
		{
			complete(error);
			return ;
		}
		if( written != expected )
		{
			complete(make_error_code(errc::body_length_mismatch));
			return ;
		}
		notify_progress(options.progress, total);
		read_next();
	}

	void finish_source()
	{
		auto self = shared_from_this();
		try {
			source->finish([self = std::move(self)](error_code error) mutable
			{
				if( error )
					self->complete(error);
				else
					self->finish_writer();
			});
		}
		catch(...) {
			complete(exception_error(std::current_exception()));
		}
	}

	void finish_writer()
	{
		auto self = shared_from_this();
		try {
			writer->finish(std::move(options.trailers),
			[self = std::move(self)](error_code error) mutable {
				self->complete(error);
			});
		}
		catch(...) {
			complete(exception_error(std::current_exception()));
		}
	}

	void complete(error_code error) {
		std::move(handler)(error, total);
	}
};

} //namespace

uint64_t transfer_source_sync(body_source &source, body_writer_state &writer,
	source_transfer_options options, error_code &error) noexcept
{
	error.clear();
	std::array<std::byte,8192> buffer {};
	uint64_t total = 0;

	while( not error )
	{
		if( options.length and total == *options.length )
			break;

		auto target = riwo::buffer(buffer);
		if( options.length )
		{
			target = mutable_buffer(target.data(),
				std::min<uint64_t>(target.size(), *options.length - total)
			);
		}
		const auto read = source.read_some_sync(target, error);
		if( eof_error(error) )
		{
			if( options.length )
				error = make_error_code(errc::body_length_mismatch);
			else
				error.clear();
			break;
		}
		if( error )
			break;

		if( read == 0 or read > target.size() )
		{
			error = invalid_read(read, target.size());
			break;
		}
		const auto written = writer.write (
			const_buffer(target.data(), read), error
		);
		if( written > std::numeric_limits<uint64_t>::max() - total )
		{
			error = make_system_error_code(std::errc::value_too_large);
			break;
		}
		total += written;
		if( error )
			break;

		if( written != read )
		{
			error = make_error_code(errc::body_length_mismatch);
			break;
		}
		notify_progress(options.progress, total);
	}
	if( not error )
		source.finish_sync(error);

	if( not error )
		writer.finish(std::move(options.trailers), error);
	return total;
}

void transfer_source(body_source_ptr source, body_writer_state_ptr writer,
	source_transfer_options options, asio::any_completion_handler<void(error_code,uint64_t)> handler)
{
	if( not source or not writer )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted), 0);
		return ;
	}
	auto value = std::make_shared<operation>();
	value->source = std::move(source);
	value->writer = std::move(writer);
	value->options = std::move(options);
	value->handler = std::move(handler);
	value->start();
}

} //namespace riwo::experimental::http::detail
