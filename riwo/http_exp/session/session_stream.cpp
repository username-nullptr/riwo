// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session_stream.h"
#include <riwo/http_exp/protocol/duplex_channel.h>

#include <utility>

namespace riwo::http_exp
{

using detail::body_reader_state_ptr;
using detail::body_writer_state_ptr;
using detail::duplex_state;

namespace
{

[[nodiscard]] error_code unsupported() noexcept
{
	return make_error_code(errc::operation_not_supported);
}

class RIWO_DECL_HIDDEN body_duplex_state final : public duplex_state,
	public std::enable_shared_from_this<body_duplex_state>
{
public:
	body_duplex_state (
		asio::any_io_executor executor, protocol version,
		body_reader_state_ptr reader, body_writer_state_ptr writer,
		endpoint_t remote, endpoint_t local,
		std::shared_ptr<void> owner, std::function<void()> cancel
	) :
		m_exec(std::move(executor)), m_version(version),
		m_reader(std::move(reader)), m_writer(std::move(writer)),
		m_remote(std::move(remote)), m_local(std::move(local)), m_owner(std::move(owner)),
		m_cancel(std::move(cancel)) {}

public:
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer, error_code &error) noexcept override
	{
		auto result = m_reader->read_some(buffer, error);
		if( not error and result.bytes == 0 and result.eof )
			error = asio::error::make_error_code(asio::error::eof);
		return result.bytes;
	}

	void read_some(mutable_buffer buffer, io_handler_t handler) override
	{
		m_reader->read_some(buffer,
		[handler = std::move(handler)](error_code error, body_read_result result) mutable
		{
			if( not error and result.bytes == 0 and result.eof )
				error = asio::error::make_error_code(asio::error::eof);
			std::move(handler)(error, result.bytes);
		});
	}

	[[nodiscard]] size_t write_sync
	(std::span<const const_buffer> buffers, error_code &error) noexcept override
	{
		error.clear();
		size_t total = 0;

		for(const auto &buffer : buffers)
		{
			auto size = m_writer->write(buffer, error);
			total += size;

			if( error or size != buffer.size() )
				break;
		}
		return total;
	}

	void write(std::span<const const_buffer> buffers, io_handler_t handler) override
	{
		size_t total = 0;
		for(const auto &buffer : buffers)
			total += buffer.size();

		auto bytes = std::make_shared<std::vector<std::byte>>(total);
		size_t offset = 0;

		for(const auto &buffer : buffers)
		{
			if( buffer.size() != 0 )
				std::memcpy(bytes->data() + offset, buffer.data(), buffer.size());
			offset += buffer.size();
		}
		m_writer->write(buffer(*bytes),
		[bytes, handler = std::move(handler)](error_code error, size_t size) mutable
		{
			RIWO_UNUSED(bytes);
			std::move(handler)(error, size);
		});
	}

	void close_sync(error_code &error) noexcept override
	{
		if( m_closed )
		{
			error.clear();
			return ;
		}
		m_writer->finish({}, error);
		if( not error )
			m_closed = true;
	}

	void close(close_handler_t handler) override
	{
		if( m_closed )
		{
			std::move(handler)(error_code{});
			return ;
		}
		auto self = shared_from_this();

		m_writer->finish({},
		[self = std::move(self), owner = m_owner, handler = std::move(handler)](error_code error) mutable
		{
			ignore_unused(owner);
			if( not error )
				self->m_closed = true;
			std::move(handler)(error);
		});
	}

	void cancel() noexcept override
	{
		if( m_cancelled )
			return ;

		m_cancelled = true;
		if( m_cancel )
			m_cancel();
	}

public:
	[[nodiscard]] endpoint_t remote_endpoint(error_code &error) const noexcept override {
		error.clear(); return m_remote;
	}
	[[nodiscard]] endpoint_t local_endpoint(error_code &error) const noexcept override {
		error.clear(); return m_local;
	}

	[[nodiscard]] asio::any_io_executor get_executor() const noexcept override  {
		return m_exec;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_version;
	}

	[[nodiscard]] bool is_open() const noexcept override {
		return not m_cancelled and not m_closed;
	}

private:
	asio::any_io_executor m_exec {};
	protocol m_version {};

	body_reader_state_ptr m_reader {};
	body_writer_state_ptr m_writer {};

	endpoint_t m_remote {};
	endpoint_t m_local {};

	std::shared_ptr<void> m_owner {};
	std::function<void()> m_cancel {};

	bool m_cancelled = false;
	bool m_closed = false;
};

} //namespace

response_head session_stream::wait_response_sync(error_code &error) noexcept
{
	error = unsupported();
	return {};
}

void session_stream::wait_response(response_handler_t handler)
{
	std::move(handler)(unsupported(), response_head{});
}

void session_stream::respond_sync(const response_head&, const body_spec&, error_code &error) noexcept
{
	error = unsupported();
}

void session_stream::respond(const response_head&, const body_spec&, operation_handler_t handler)
{
	std::move(handler)(unsupported());
}

void session_stream::inform_sync(const response_head&, error_code &error) noexcept
{
	error = unsupported();
}

void session_stream::inform(const response_head&, operation_handler_t handler)
{
	std::move(handler)(unsupported());
}

session_stream::duplex_state_ptr
session_stream::open_duplex_sync(const optional<response_head>&, error_code &error) noexcept
{
	error = unsupported();
	return {};
}

protocol session_stream::response_protocol() const noexcept
{
	return selected_protocol();
}

void session_stream::open_duplex(const optional<response_head>&, duplex_handler_t handler)
{
	std::move(handler)(unsupported(), duplex_state_ptr{});
}

auto session_stream::remote_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error = unsupported();
	return {};
}

auto session_stream::local_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error = unsupported();
	return {};
}

bool session_stream::reusable() const noexcept
{
	return false;
}

request_delivery session_stream::delivery() const noexcept
{
	return request_delivery::possibly_processed;
}

void *session_stream::protocol_extension(const std::type_info &) noexcept
{
	return nullptr;
}

std::shared_ptr<duplex_state> make_body_duplex_state(protocol version,
	body_reader_state_ptr reader, body_writer_state_ptr writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel,
	asio::any_io_executor executor)
{
	return make_body_duplex_state(std::move(executor),
		version, std::move(reader), std::move(writer),
		std::move(remote), std::move(local),
		std::move(owner), std::move(cancel)
	);
}

std::shared_ptr<duplex_state> make_body_duplex_state(
	asio::any_io_executor executor, protocol version,
	body_reader_state_ptr reader, body_writer_state_ptr writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel)
{
	return std::make_shared<body_duplex_state>(
		std::move(executor), version,
		std::move(reader), std::move(writer),
		std::move(remote), std::move(local),
		std::move(owner), std::move(cancel)
	);
}

} //namespace riwo::http_exp
