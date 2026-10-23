// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include <riwo/http_exp/protocol/duplex_channel.h>

namespace riwo::http_exp::detail
{

namespace
{
[[nodiscard]] error_code unsupported() noexcept {
	return make_error_code(errc::operation_not_supported);
}
} //namespace

response_head stream::wait_response_sync(error_code &error) noexcept
{
	error = unsupported();
	return {};
}

void stream::wait_response(response_handler_t handler) {
	std::move(handler)(unsupported(), response_head{});
}

auto stream::remote_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error = unsupported();
	return {};
}

auto stream::local_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error = unsupported();
	return {};
}

void stream::respond_sync(response_head, body_spec, error_code &error) noexcept {
	error = unsupported();
}

void stream::respond(response_head, body_spec, operation_handler_t handler) {
	std::move(handler)(unsupported());
}

void stream::inform_sync(response_head, error_code &error) noexcept {
	error = unsupported();
}

void stream::inform(response_head, operation_handler_t handler) {
	std::move(handler)(unsupported());
}

std::shared_ptr<duplex_state> stream::open_duplex_sync(
	optional<response_head>, error_code &error) noexcept
{
	error = unsupported();
	return {};
}

void stream::open_duplex(optional<response_head>, duplex_handler_t handler) {
	std::move(handler)(unsupported(), std::shared_ptr<duplex_state>{});
}

void *stream::protocol_extension(const std::type_info &) noexcept {
	return nullptr;
}

namespace
{

class body_duplex_state final : public duplex_state,
	public std::enable_shared_from_this<body_duplex_state>
{
public:
	body_duplex_state(asio::any_io_executor executor, protocol version,
		std::shared_ptr<body_reader_state> reader,
		std::shared_ptr<body_writer_state> writer,
		endpoint_t remote, endpoint_t local,
		std::shared_ptr<void> owner, std::function<void()> cancel) :
		m_executor(std::move(executor)), m_version(version),
		m_reader(std::move(reader)), m_writer(std::move(writer)),
		m_remote(remote), m_local(local), m_owner(std::move(owner)),
		m_cancel(std::move(cancel))
	{
	}

	[[nodiscard]] asio::any_io_executor get_executor() const noexcept override {
		return m_executor;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_version;
	}
	[[nodiscard]] bool is_open() const noexcept override {
		return not m_cancelled and not m_closed;
	}
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		auto result = m_reader->read_some(buffer, error);
		if( not error and result.bytes == 0 and result.eof )
			error = asio::error::make_error_code(asio::error::eof);
		return result.bytes;
	}
	void read_some(mutable_buffer buffer, io_handler_t handler) override
	{
		m_reader->read_some(buffer,
		[handler = std::move(handler)]
		(error_code error, body_read_result result) mutable {
			if( not error and result.bytes == 0 and result.eof )
				error = asio::error::make_error_code(asio::error::eof);
			std::move(handler)(error, result.bytes);
		});
	}
	[[nodiscard]] size_t write_sync(std::span<const const_buffer> buffers,
		error_code &error) noexcept override
	{
		error.clear();
		size_t total = 0;
		for( const auto &buffer : buffers )
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
		for( const auto &buffer : buffers ) total += buffer.size();
		auto bytes = std::make_shared<std::vector<std::byte>>(total);
		size_t offset = 0;
		for( const auto &buffer : buffers )
		{
			if( buffer.size() != 0 )
				std::memcpy(bytes->data() + offset, buffer.data(), buffer.size());
			offset += buffer.size();
		}
		m_writer->write(riwo::buffer(*bytes),
		[bytes, handler = std::move(handler)]
		(error_code error, size_t size) mutable {
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
		[self = std::move(self), owner = m_owner, handler = std::move(handler)]
		(error_code error) mutable {
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
	[[nodiscard]] endpoint_t remote_endpoint(
		error_code &error) const noexcept override { error.clear(); return m_remote; }
	[[nodiscard]] endpoint_t local_endpoint(
		error_code &error) const noexcept override { error.clear(); return m_local; }

private:
	asio::any_io_executor m_executor;
	protocol m_version;
	std::shared_ptr<body_reader_state> m_reader;
	std::shared_ptr<body_writer_state> m_writer;
	endpoint_t m_remote;
	endpoint_t m_local;
	std::shared_ptr<void> m_owner;
	std::function<void()> m_cancel;
	bool m_cancelled = false;
	bool m_closed = false;
};

} //namespace

std::shared_ptr<duplex_state> make_body_duplex_state(protocol version,
	std::shared_ptr<body_reader_state> reader,
	std::shared_ptr<body_writer_state> writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel,
	asio::any_io_executor executor)
{
	return make_body_duplex_state(std::move(executor),
		version, std::move(reader), std::move(writer), remote, local,
		std::move(owner), std::move(cancel));
}

std::shared_ptr<duplex_state> make_body_duplex_state(
	asio::any_io_executor executor, protocol version,
	std::shared_ptr<body_reader_state> reader,
	std::shared_ptr<body_writer_state> writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel)
{
	return std::make_shared<body_duplex_state>(std::move(executor), version,
		std::move(reader), std::move(writer), remote, local,
		std::move(owner), std::move(cancel));
}

std::shared_ptr<stream> client_session::open_sync(request_head, body_spec,
	error_code &error) noexcept
{
	error = unsupported();
	return {};
}

} //namespace riwo::http_exp::detail
