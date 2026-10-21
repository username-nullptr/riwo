// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include "wire.h"
#include <riwo/http_exp/protocol/duplex_channel.h>
#include <riwo/http_exp/server/detail/session.h>
#include <riwo/http_exp/transport/tcp_connection.h>

namespace riwo::http_exp::v1::detail
{
using ::riwo::http_exp::detail::client_session;
using ::riwo::http_exp::detail::body_reader_adapter;
using ::riwo::http_exp::detail::body_reader_state;
using ::riwo::http_exp::detail::body_writer_adapter;
using ::riwo::http_exp::detail::body_writer_state;
using ::riwo::http_exp::detail::duplex_state;
using ::riwo::http_exp::detail::make_body_reader_state;
using ::riwo::http_exp::detail::make_body_duplex_state;
using ::riwo::http_exp::detail::make_body_writer_state;
using ::riwo::http_exp::detail::server_session;
using ::riwo::http_exp::detail::stream;

namespace
{

struct connection final
{
	explicit connection(asio::any_io_executor executor) :
		transport(std::make_shared<::riwo::http_exp::tcp_connection>(
			std::move(executor)))
	{
	}

	explicit connection(asio::ip::tcp::socket value) :
		transport(std::make_shared<::riwo::http_exp::tcp_connection>(
			std::move(value)))
	{
	}

	explicit connection(
		std::shared_ptr<::riwo::http_exp::connection> value) :
		transport(std::move(value))
	{
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept
	{
		return transport->read_some_sync(buffer, error);
	}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept
	{
		return transport->write_sync(buffer, error);
	}

	void read_some(mutable_buffer buffer,
		::riwo::http_exp::connection::io_handler handler)
	{
		transport->read_some(buffer, std::move(handler));
	}

	void write(const_buffer buffer,
		::riwo::http_exp::connection::io_handler handler)
	{
		transport->write(buffer, std::move(handler));
	}

	void connect_sync(std::string_view host, uint16_t port,
		error_code &error) noexcept
	{
		transport->connect_sync(host, port, error);
	}

	void connect_async(std::string_view host, uint16_t port,
		::riwo::http_exp::connection::close_handler handler)
	{
		transport->connect_async(host, port, std::move(handler));
	}

	[[nodiscard]] bool is_open() const noexcept {
		return not closed and transport->is_open();
	}

	void cancel() noexcept
	{
		if( closed or detached ) return;
		closed = true;
		transport->cancel();
	}

	std::shared_ptr<::riwo::http_exp::connection> transport;
	std::string pending;
	bool closed = false;
	bool detached = false;
};

class h1_duplex_state final : public duplex_state
{
public:
	h1_duplex_state(std::shared_ptr<connection> value, protocol version) :
		m_connection(std::move(value)), m_version(version)
	{
		m_connection->detached = true;
		m_connection->transport->set_read_timeout({});
		m_connection->transport->set_write_timeout({});
	}

	[[nodiscard]] asio::any_io_executor get_executor() const noexcept override {
		return m_connection->transport->get_executor();
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_version;
	}
	[[nodiscard]] bool is_open() const noexcept override {
		return not m_connection->closed and m_connection->transport->is_open();
	}
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		if( not m_connection->pending.empty() )
		{
			const auto size = std::min(buffer.size(), m_connection->pending.size());
			if( size != 0 )
				std::memcpy(buffer.data(), m_connection->pending.data(), size);
			m_connection->pending.erase(0, size);
			return size;
		}
		return m_connection->transport->read_some_sync(buffer, error);
	}
	void read_some(mutable_buffer buffer, io_handler handler) override
	{
		if( not m_connection->pending.empty() )
		{
			const auto size = std::min(buffer.size(), m_connection->pending.size());
			if( size != 0 )
				std::memcpy(buffer.data(), m_connection->pending.data(), size);
			m_connection->pending.erase(0, size);
			std::move(handler)(error_code{}, size);
			return;
		}
		m_connection->transport->read_some(buffer, std::move(handler));
	}
	[[nodiscard]] size_t write_sync(std::span<const const_buffer> buffers,
		error_code &error) noexcept override
	{
		error.clear();
		size_t total = 0;
		for( const auto &buffer : buffers )
		{
			auto size = m_connection->transport->write_sync(buffer, error);
			total += size;
			if( error or size != buffer.size() ) break;
		}
		return total;
	}
	void write(std::span<const const_buffer> buffers, io_handler handler) override
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
		m_connection->transport->write(riwo::buffer(*bytes),
		[bytes, handler = std::move(handler)]
		(error_code error, size_t size) mutable {
			std::move(handler)(error, size);
		});
	}
	void close_sync(error_code &error) noexcept override
	{
		error.clear();
		if( m_connection->closed ) return;
		m_connection->closed = true;
		m_connection->transport->cancel();
	}
	void close(close_handler handler) override
	{
		if( m_connection->closed )
		{
			std::move(handler)(error_code{});
			return;
		}
		m_connection->closed = true;
		m_connection->transport->close(std::move(handler));
	}
	void cancel() noexcept override
	{
		if( m_connection->closed ) return;
		m_connection->closed = true;
		m_connection->transport->cancel();
	}
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override {
		return m_connection->transport->remote_endpoint(error);
	}
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override {
		return m_connection->transport->local_endpoint(error);
	}

private:
	std::shared_ptr<connection> m_connection;
	protocol m_version;
};

[[nodiscard]] bool is_eof(const error_code &error) noexcept {
	return error == asio::error::make_error_code(asio::error::eof);
}

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
}

[[nodiscard]] error_code timeout_error() noexcept {
	return asio::error::make_error_code(asio::error::timed_out);
}

[[nodiscard]] std::chrono::milliseconds bounded_timeout(
	std::chrono::milliseconds idle, const timeout_policy &timeouts)
{
	if( timeouts.overall and *timeouts.overall < idle )
		return *timeouts.overall;
	return idle;
}

[[nodiscard]] bool has_token(const fields &values, std::string_view name,
	std::string_view token)
{
	auto range = values.find_all(name);
	for( auto iterator = range.begin(); iterator != range.end(); ++iterator )
	{
		std::string_view text = iterator->value.to_string();
		while( not text.empty() )
		{
			const auto comma = text.find(',');
			auto item = text.substr(0, comma);
			while( not item.empty() and (item.front() == ' ' or item.front() == '\t') )
				item.remove_prefix(1);
			while( not item.empty() and (item.back() == ' ' or item.back() == '\t') )
				item.remove_suffix(1);
			if( item.size() == token.size() and
				std::ranges::equal(item, token,
					[](char lhs, char rhs) {
						return std::tolower(static_cast<unsigned char>(lhs)) ==
							std::tolower(static_cast<unsigned char>(rhs));
					}) )
				return true;
			if( comma == std::string_view::npos ) break;
			text.remove_prefix(comma + 1);
		}
	}
	return false;
}

[[nodiscard]] bool has_unsupported_expectation(const fields &values)
{
	auto range = values.find_all("Expect");
	for( auto iterator = range.begin(); iterator != range.end(); ++iterator )
	{
		std::string_view text = iterator->value.to_string();
		while( not text.empty() )
		{
			const auto comma = text.find(',');
			auto item = text.substr(0, comma);
			while( not item.empty() and
				(item.front() == ' ' or item.front() == '\t') )
				item.remove_prefix(1);
			while( not item.empty() and
				(item.back() == ' ' or item.back() == '\t') )
				item.remove_suffix(1);
			if( item.empty() or item.size() != 12 or
				not std::ranges::equal(item, std::string_view("100-continue"),
					[](char lhs, char rhs) {
						return std::tolower(static_cast<unsigned char>(lhs)) ==
							std::tolower(static_cast<unsigned char>(rhs));
					}) )
				return true;
			if( comma == std::string_view::npos ) break;
			text.remove_prefix(comma + 1);
		}
	}
	return false;
}

[[nodiscard]] std::string error_response(std::uint16_t code,
	std::string_view reason)
{
	return "HTTP/1.1 " + std::to_string(code) + " " +
		std::string(reason) + "\r\nContent-Length: 0\r\n"
		"Connection: close\r\n\r\n";
}

[[nodiscard]] bool reusable_response(const response_head &head,
	protocol version)
{
	if( head.status.code() == 101 ) return false;
	if( has_token(head.headers, "connection", "close") ) return false;
	if( version == protocol::v1_0 )
		return has_token(head.headers, "connection", "keep-alive");
	return true;
}

[[nodiscard]] sys_expected<std::string> take_head(
	connection &value, size_t limit) noexcept
{
	auto end = value.pending.find("\r\n\r\n");
	if( end == std::string::npos )
	{
		if( value.pending.size() > limit )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));
		return sys_unexpected(make_error_code(errc::operation_in_progress));
	}
	if( end + 4 > limit )
		return sys_unexpected(make_error_code(errc::header_limit_exceeded));
	std::string result = value.pending.substr(0, end + 4);
	value.pending.erase(0, end + 4);
	return result;
}

[[nodiscard]] std::string read_head_sync(connection &value, size_t limit,
	error_code &error) noexcept
{
	error.clear();
	try {
		std::array<char,4096> buffer {};
		for(;;)
		{
			auto head = take_head(value, limit);
			if( head ) return std::move(*head);
			if( head.error() != make_error_code(errc::operation_in_progress) )
			{
				error = head.error();
				return {};
			}
			auto size = value.read_some_sync(asio::buffer(buffer), error);
			if( error ) return {};
			value.pending.append(buffer.data(), size);
		}
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return {};
	}
}

class head_read_operation final :
	public std::enable_shared_from_this<head_read_operation>
{
public:
	using handler_type = asio::any_completion_handler<void(error_code,std::string)>;

	head_read_operation(std::shared_ptr<connection> value, size_t maximum,
		handler_type completion) : m_connection(std::move(value)), m_limit(maximum),
		m_handler(std::move(completion))
	{
	}

	void start()
	{
		auto head = take_head(*m_connection, m_limit);
		if( head )
		{
			std::move(m_handler)(error_code{}, std::move(*head));
			return;
		}
		if( head.error() != make_error_code(errc::operation_in_progress) )
		{
			std::move(m_handler)(head.error(), std::string{});
			return;
		}
		auto self = shared_from_this();
		m_connection->read_some(asio::buffer(m_buffer),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( error )
			{
				std::move(self->m_handler)(error, std::string{});
				return;
			}
			self->m_connection->pending.append(self->m_buffer.data(), size);
			self->start();
		});
	}

private:
	std::shared_ptr<connection> m_connection;
	size_t m_limit;
	handler_type m_handler;
	std::array<char,4096> m_buffer {};
};

void read_head(std::shared_ptr<connection> value, size_t limit,
	head_read_operation::handler_type handler)
{
	std::make_shared<head_read_operation>(std::move(value), limit,
		std::move(handler))->start();
}

class reader_adapter final : public body_reader_adapter,
	public std::enable_shared_from_this<reader_adapter>
{
public:
	reader_adapter(std::shared_ptr<connection> connection_value,
		body_framing framing_value, uint64_t length, message_limits limits,
		timeout_policy timeouts = {}) :
		m_connection(std::move(connection_value)), m_framing(framing_value),
		m_remaining(length), m_limits(std::move(limits)), m_timeouts(timeouts)
	{
	}

	void configure(body_framing framing, uint64_t length) noexcept
	{
		m_framing = framing;
		m_remaining = length;
	}

	void set_end_handler(std::function<void(error_code)> handler) {
		m_end_handler = std::move(handler);
	}

	[[nodiscard]] body_framing framing() const noexcept { return m_framing; }

	[[nodiscard]] body_read_result read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		try {
			if( not apply_read_timeout(error) ) return {};
			auto result = read_sync(buffer, error);
			if( is_eof(error) )
				error = make_error_code(errc::body_length_mismatch);
			if( error or result.eof ) notify_end(error);
			return result;
		}
		catch(...) {
			error = exception_error(std::current_exception());
			notify_end(error);
			return {};
		}
	}

	void read_some(mutable_buffer buffer, read_handler handler) override;

	[[nodiscard]] bool apply_read_timeout(error_code &error) noexcept
	{
		auto value = m_timeouts.body_idle;
		if( m_timeouts.overall )
		{
			const auto remaining = *m_deadline - std::chrono::steady_clock::now();
			if( remaining <= std::chrono::milliseconds(0) )
			{
				error = timeout_error();
				return false;
			}
			const auto remaining_ms = std::chrono::duration_cast<
				std::chrono::milliseconds>(remaining);
			if( value.count() <= 0 or remaining_ms < value )
				value = remaining_ms;
		}
		if( value.count() > 0 )
			m_connection->transport->set_read_timeout(value);
		return true;
	}

	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override
	{
		error.clear();
		if( not m_ended ) error = make_error_code(errc::operation_not_permitted);
		return m_trailers;
	}

	void trailers(trailers_handler handler) override
	{
		if( not m_ended )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				fields{});
			return;
		}
		std::move(handler)(error_code{}, m_trailers);
	}

	void cancel() noexcept override { m_connection->cancel(); }

	[[nodiscard]] bool request_complete() const noexcept
	{
		return m_framing == body_framing::none or
			(m_framing == body_framing::content_length and m_remaining == 0) or
			(m_framing == body_framing::chunked and m_ended);
	}

private:
	void notify_end(error_code error) noexcept
	{
		if( m_end_notified ) return;
		m_end_notified = true;
		try {
			if( m_end_handler ) m_end_handler(error);
		}
		catch(...) {
		}
	}

	[[nodiscard]] bool account(size_t size, error_code &error) noexcept
	{
		if( m_limits.max_body_size and
			size > *m_limits.max_body_size -
				std::min(*m_limits.max_body_size, m_delivered) )
		{
			error = make_error_code(errc::buffer_limit_exceeded);
			m_connection->cancel();
			return false;
		}
		m_delivered += size;
		return true;
	}

	[[nodiscard]] size_t copy_pending(mutable_buffer buffer, size_t maximum,
		error_code &error)
	{
		const auto size = std::min({buffer.size(), maximum,
			m_connection->pending.size()});
		if( size == 0 ) return 0;
		std::memcpy(buffer.data(), m_connection->pending.data(), size);
		m_connection->pending.erase(0, size);
		if( not account(size, error) ) return 0;
		return size;
	}

	[[nodiscard]] bool fill_sync(error_code &error)
	{
		std::array<char,8192> data {};
		auto size = m_connection->read_some_sync(asio::buffer(data), error);
		if( error ) return false;
		m_connection->pending.append(data.data(), size);
		return true;
	}

	[[nodiscard]] bool consume_chunk_crlf_sync(error_code &error)
	{
		while( m_connection->pending.size() < 2 )
			if( not fill_sync(error) ) return false;
		if( not m_connection->pending.starts_with("\r\n") )
		{
			error = protocol_error();
			return false;
		}
		m_connection->pending.erase(0, 2);
		m_need_chunk_crlf = false;
		return true;
	}

	[[nodiscard]] bool read_chunk_line_sync(error_code &error)
	{
		for(;;)
		{
			auto end = m_connection->pending.find("\r\n");
			if( end != std::string::npos )
			{
				auto line = std::string_view(m_connection->pending).substr(0, end);
				auto size = parse_chunk_size(line);
				if( not size )
				{
					error = size.error();
					return false;
				}
				m_connection->pending.erase(0, end + 2);
				m_chunk_remaining = *size;
				m_reading_trailers = m_chunk_remaining == 0;
				return true;
			}
			if( m_connection->pending.size() > m_limits.max_start_line_size )
			{
				error = make_error_code(errc::header_limit_exceeded);
				return false;
			}
			if( not fill_sync(error) ) return false;
		}
	}

	[[nodiscard]] bool read_trailers_sync(error_code &error)
	{
		for(;;)
		{
			auto end = m_connection->pending.find("\r\n");
			if( end == std::string::npos )
			{
				if( m_connection->pending.size() > m_limits.max_header_list_size )
				{
					error = make_error_code(errc::header_limit_exceeded);
					return false;
				}
				if( not fill_sync(error) ) return false;
				continue;
			}
			if( end == 0 )
			{
				m_connection->pending.erase(0, 2);
				m_ended = true;
				return true;
			}
			auto line = std::string_view(m_connection->pending).substr(0, end);
			auto colon = line.find(':');
			if( colon == std::string_view::npos or m_trailers.size() >=
				m_limits.max_field_count )
			{
				error = protocol_error();
				return false;
			}
			auto value = line.substr(colon + 1);
			while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
				value.remove_prefix(1);
			m_trailers.add(std::string(line.substr(0, colon)), std::string(value));
			m_connection->pending.erase(0, end + 2);
		}
	}

	[[nodiscard]] body_read_result read_sync(
		mutable_buffer buffer, error_code &error)
	{
		if( m_ended or m_framing == body_framing::none )
		{
			m_ended = true;
			return {0, true};
		}
		if( m_framing == body_framing::content_length )
		{
			if( m_remaining == 0 )
			{
				m_ended = true;
				return {0, true};
			}
			auto size = copy_pending(buffer, static_cast<size_t>(
				std::min<uint64_t>(m_remaining, SIZE_MAX)), error);
			if( size == 0 and not error )
			{
				auto target = mutable_buffer(buffer.data(), static_cast<size_t>(
					std::min<uint64_t>(buffer.size(), m_remaining)));
				size = m_connection->read_some_sync(target, error);
				m_remaining -= std::min<uint64_t>(m_remaining, size);
				if( is_eof(error) )
					error = m_remaining == 0 ? error_code{} :
						make_error_code(errc::body_length_mismatch);
				if( not error and size != 0 and not account(size, error) ) return {};
			}
			else m_remaining -= std::min<uint64_t>(m_remaining, size);
			const bool eof = not error and m_remaining == 0;
			if( eof ) m_ended = true;
			return {size, eof};
		}
		if( m_framing == body_framing::until_eof )
		{
			auto size = copy_pending(buffer, SIZE_MAX, error);
			if( size != 0 or error ) return {size, false};
			size = m_connection->read_some_sync(buffer, error);
			const bool eof = is_eof(error);
			if( eof )
			{
				error.clear();
				m_ended = true;
			}
			if( not error and size != 0 and not account(size, error) ) return {};
			return {size, eof and not error};
		}
		for(;;)
		{
			if( m_reading_trailers )
			{
				if( not read_trailers_sync(error) ) return {};
				return {0, true};
			}
			if( m_need_chunk_crlf and not consume_chunk_crlf_sync(error) ) return {};
			if( m_chunk_remaining == 0 and not read_chunk_line_sync(error) ) return {};
			if( m_reading_trailers ) continue;
			auto size = copy_pending(buffer, static_cast<size_t>(
				std::min<uint64_t>(m_chunk_remaining, SIZE_MAX)), error);
			if( size == 0 and not error )
			{
				auto target = mutable_buffer(buffer.data(), static_cast<size_t>(
					std::min<uint64_t>(buffer.size(), m_chunk_remaining)));
				size = m_connection->read_some_sync(target, error);
				if( not error and not account(size, error) ) return {};
			}
			m_chunk_remaining -= std::min<uint64_t>(m_chunk_remaining, size);
			if( m_chunk_remaining == 0 ) m_need_chunk_crlf = true;
			return {size, false};
		}
	}

	class async_read_operation;
	friend class async_read_operation;

	std::shared_ptr<connection> m_connection;
	body_framing m_framing;
	uint64_t m_remaining = 0;
	message_limits m_limits;
	timeout_policy m_timeouts;
	std::optional<std::chrono::steady_clock::time_point> m_deadline =
		m_timeouts.overall ? std::optional<std::chrono::steady_clock::time_point>(
			std::chrono::steady_clock::now() + *m_timeouts.overall) : std::nullopt;
	fields m_trailers;
	uint64_t m_delivered = 0;
	uint64_t m_chunk_remaining = 0;
	bool m_need_chunk_crlf = false;
	bool m_reading_trailers = false;
	bool m_ended = false;
	bool m_end_notified = false;
	std::function<void(error_code)> m_end_handler;
};

class reader_adapter::async_read_operation final :
	public std::enable_shared_from_this<async_read_operation>
{
public:
	async_read_operation(std::shared_ptr<reader_adapter> owner,
		mutable_buffer target, read_handler completion) : m_owner(std::move(owner)),
		m_target(target), m_handler(std::move(completion))
	{
	}

	void start()
	{
		error_code error;
		if( not m_owner->apply_read_timeout(error) )
		{
			m_owner->m_ended = true;
			finish(error, {});
			return;
		}
		if( m_owner->m_ended or m_owner->m_framing == body_framing::none )
		{
			m_owner->m_ended = true;
			finish({}, {0, true});
			return;
		}
		if( m_owner->m_framing == body_framing::content_length )
		{
			if( m_owner->m_remaining == 0 )
			{
				m_owner->m_ended = true;
				finish({}, {0, true});
				return;
			}
			auto size = m_owner->copy_pending(m_target, static_cast<size_t>(
				std::min<uint64_t>(m_owner->m_remaining, SIZE_MAX)), error);
			if( size != 0 or error )
			{
				m_owner->m_remaining -= std::min<uint64_t>(m_owner->m_remaining, size);
				const bool eof = not error and m_owner->m_remaining == 0;
				if( eof ) m_owner->m_ended = true;
				finish(error, {size, eof});
				return;
			}
			direct_read(static_cast<size_t>(std::min<uint64_t>(
				m_target.size(), m_owner->m_remaining)), true);
			return;
		}
		if( m_owner->m_framing == body_framing::until_eof )
		{
			auto size = m_owner->copy_pending(m_target, SIZE_MAX, error);
			if( size != 0 or error )
			{
				finish(error, {size, false});
				return;
			}
			direct_read(m_target.size(), false);
			return;
		}
		chunk_step();
	}

private:
	void direct_read(size_t size, bool fixed)
	{
		auto self = shared_from_this();
		m_owner->m_connection->read_some(
			mutable_buffer(m_target.data(), size),
		[self = std::move(self), fixed](error_code error, size_t count) mutable {
			const bool transport_eof = is_eof(error);
			if( fixed ) self->m_owner->m_remaining -=
				std::min<uint64_t>(self->m_owner->m_remaining, count);
			if( transport_eof )
				error = not fixed or self->m_owner->m_remaining == 0 ? error_code{} :
					make_error_code(errc::body_length_mismatch);
			if( not error and count != 0 and
				not self->m_owner->account(count, error) ) count = 0;
			bool eof = false;
			if( fixed )
				eof = not error and self->m_owner->m_remaining == 0;
			else eof = transport_eof and not error;
			if( eof ) self->m_owner->m_ended = true;
			self->finish(error, {count, eof});
		});
	}

	void fill()
	{
		auto self = shared_from_this();
		m_owner->m_connection->read_some(asio::buffer(m_buffer),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( error )
			{
				self->finish(error, {});
				return;
			}
			self->m_owner->m_connection->pending.append(
				self->m_buffer.data(), size);
			self->chunk_step();
		});
	}

	void chunk_step()
	{
		try {
			for(;;)
			{
				if( m_owner->m_reading_trailers )
				{
					auto end = m_owner->m_connection->pending.find("\r\n");
					if( end == std::string::npos )
					{
						if( m_owner->m_connection->pending.size() >
							m_owner->m_limits.max_header_list_size )
						{
							finish(make_error_code(errc::header_limit_exceeded), {});
							return;
						}
						fill();
						return;
					}
					if( end == 0 )
					{
						m_owner->m_connection->pending.erase(0, 2);
						m_owner->m_ended = true;
						finish({}, {0, true});
						return;
					}
					auto line = std::string_view(m_owner->m_connection->pending)
						.substr(0, end);
					auto colon = line.find(':');
					if( colon == std::string_view::npos or m_owner->m_trailers.size() >=
						m_owner->m_limits.max_field_count )
					{
						finish(protocol_error(), {});
						return;
					}
					auto value = line.substr(colon + 1);
					while( not value.empty() and
						(value.front() == ' ' or value.front() == '\t') )
						value.remove_prefix(1);
					m_owner->m_trailers.add(std::string(line.substr(0, colon)),
						std::string(value));
					m_owner->m_connection->pending.erase(0, end + 2);
					continue;
				}
				if( m_owner->m_need_chunk_crlf )
				{
					if( m_owner->m_connection->pending.size() < 2 )
					{
						fill();
						return;
					}
					if( not m_owner->m_connection->pending.starts_with("\r\n") )
					{
						finish(protocol_error(), {});
						return;
					}
					m_owner->m_connection->pending.erase(0, 2);
					m_owner->m_need_chunk_crlf = false;
				}
				if( m_owner->m_chunk_remaining == 0 )
				{
					auto end = m_owner->m_connection->pending.find("\r\n");
					if( end == std::string::npos )
					{
						if( m_owner->m_connection->pending.size() >
							m_owner->m_limits.max_start_line_size )
						{
							finish(make_error_code(errc::header_limit_exceeded), {});
							return;
						}
						fill();
						return;
					}
					auto line = std::string_view(m_owner->m_connection->pending)
						.substr(0, end);
					auto chunk = parse_chunk_size(line);
					if( not chunk )
					{
						finish(chunk.error(), {});
						return;
					}
					m_owner->m_connection->pending.erase(0, end + 2);
					m_owner->m_chunk_remaining = *chunk;
					m_owner->m_reading_trailers = *chunk == 0;
					continue;
				}
				error_code error;
				auto size = m_owner->copy_pending(m_target, static_cast<size_t>(
					std::min<uint64_t>(m_owner->m_chunk_remaining, SIZE_MAX)), error);
				if( size != 0 or error )
				{
					m_owner->m_chunk_remaining -=
						std::min<uint64_t>(m_owner->m_chunk_remaining, size);
					if( m_owner->m_chunk_remaining == 0 )
						m_owner->m_need_chunk_crlf = true;
					finish(error, {size, false});
					return;
				}
				direct_chunk_read();
				return;
			}
		}
		catch(...) {
			finish(exception_error(std::current_exception()), {});
		}
	}

	void direct_chunk_read()
	{
		auto count = static_cast<size_t>(std::min<uint64_t>(
			m_target.size(), m_owner->m_chunk_remaining));
		auto self = shared_from_this();
		m_owner->m_connection->read_some(
			mutable_buffer(m_target.data(), count),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( not error and not self->m_owner->account(size, error) ) size = 0;
			self->m_owner->m_chunk_remaining -=
				std::min<uint64_t>(self->m_owner->m_chunk_remaining, size);
			if( self->m_owner->m_chunk_remaining == 0 )
				self->m_owner->m_need_chunk_crlf = true;
			self->finish(error, {size, false});
		});
	}

	void finish(error_code error, body_read_result result)
	{
		if( is_eof(error) )
			error = make_error_code(errc::body_length_mismatch);
		if( error or result.eof ) m_owner->notify_end(error);
		std::move(m_handler)(error, result);
	}

	std::shared_ptr<reader_adapter> m_owner;
	mutable_buffer m_target;
	read_handler m_handler;
	std::array<char,8192> m_buffer {};
};

void reader_adapter::read_some(mutable_buffer buffer, read_handler handler)
{
	std::make_shared<async_read_operation>(shared_from_this(), buffer,
		std::move(handler))->start();
}

class writer_adapter final : public body_writer_adapter
{
public:
	explicit writer_adapter(std::shared_ptr<connection> connection_value,
		bool suppress_body = false, timeout_policy timeouts = {}) :
		m_connection(std::move(connection_value)),
		m_suppress_body(suppress_body), m_timeouts(timeouts),
		m_deadline(m_timeouts.overall ? std::optional<
			std::chrono::steady_clock::time_point>(
				std::chrono::steady_clock::now() + *m_timeouts.overall) :
				std::nullopt)
	{
	}

	void configure(body_spec spec) noexcept {
		m_chunked = spec.type() == body_spec::mode::streaming;
	}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		try {
			if( not apply_write_timeout(error) ) return 0;
			if( m_suppress_body ) return buffer.size();
			if( m_chunked )
			{
				auto header = serialize_chunk_header(buffer.size());
				(void)m_connection->write_sync(asio::buffer(header), error);
				if( error ) return 0;
				auto size = m_connection->write_sync(buffer, error);
				if( error or size != buffer.size() ) return size;
				constexpr std::string_view ending = "\r\n";
				(void)m_connection->write_sync(asio::buffer(ending), error);
				return size;
			}
			return m_connection->write_sync(buffer, error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return 0;
		}
	}

	void write(const_buffer buffer, write_handler handler) override
	{
		error_code timeout;
		if( not apply_write_timeout(timeout) )
		{
			std::move(handler)(timeout, 0);
			return;
		}
		if( m_suppress_body )
		{
			std::move(handler)(error_code{}, buffer.size());
			return;
		}
		if( not m_chunked )
		{
			m_connection->write(buffer, std::move(handler));
			return;
		}

		auto connection = m_connection;
		auto header = std::make_shared<std::string>(
			serialize_chunk_header(buffer.size()));
		connection->write(asio::buffer(*header),
		[connection = std::move(connection), header, buffer,
		 handler = std::move(handler)](error_code error, size_t) mutable {
			if( error )
			{
				std::move(handler)(error, 0);
				return;
			}
			connection->write(buffer,
			[connection = std::move(connection), buffer,
			 handler = std::move(handler)]
			(error_code body_error, size_t size) mutable {
				if( body_error or size != buffer.size() )
				{
					std::move(handler)(body_error, size);
					return;
				}
				constexpr std::string_view ending = "\r\n";
				connection->write(asio::buffer(ending),
				[size, handler = std::move(handler)]
				(error_code ending_error, size_t) mutable {
					std::move(handler)(ending_error, size);
				});
			});
		});
	}

	void finish_sync(fields trailers, error_code &error) noexcept override
	{
		error.clear();
		if( m_chunked and not m_suppress_body )
		{
			try {
				auto wire = serialize_last_chunk(trailers);
				(void)m_connection->write_sync(asio::buffer(wire), error);
			}
			catch(...) {
				error = exception_error(std::current_exception());
			}
		}
		if( m_finish_handler ) m_finish_handler(error);
	}

	void finish(fields trailers, finish_handler handler) override
	{
		if( not m_chunked or m_suppress_body )
		{
			if( m_finish_handler ) m_finish_handler(error_code{});
			std::move(handler)(error_code{});
			return;
		}
		auto wire = std::make_shared<std::string>(
			serialize_last_chunk(trailers));
		auto finish = m_finish_handler;
		m_connection->write(asio::buffer(*wire),
		[finish = std::move(finish), wire, handler = std::move(handler)]
		(error_code error, size_t) mutable {
			if( finish ) finish(error);
			std::move(handler)(error);
		});
	}

	void cancel() noexcept override { m_connection->cancel(); }

	void set_finish_handler(std::function<void(error_code)> handler) {
		m_finish_handler = std::move(handler);
	}

private:
	[[nodiscard]] bool apply_write_timeout(error_code &error) noexcept
	{
		auto value = m_timeouts.body_idle;
		if( m_timeouts.overall )
		{
			const auto remaining = *m_deadline - std::chrono::steady_clock::now();
			if( remaining <= std::chrono::milliseconds(0) )
			{
				error = timeout_error();
				return false;
			}
			const auto remaining_ms = std::chrono::duration_cast<
				std::chrono::milliseconds>(remaining);
			if( value.count() <= 0 or remaining_ms < value )
				value = remaining_ms;
		}
		if( value.count() > 0 )
			m_connection->transport->set_write_timeout(value);
		return true;
	}

	std::shared_ptr<connection> m_connection;
	bool m_chunked = false;
	bool m_suppress_body = false;
	timeout_policy m_timeouts;
	std::optional<std::chrono::steady_clock::time_point> m_deadline;
	std::function<void(error_code)> m_finish_handler;
};

class h1_stream final : public stream,
	public std::enable_shared_from_this<h1_stream>
{
public:
	h1_stream(std::shared_ptr<connection> connection_value, protocol version,
		body_spec outbound_spec, body_framing inbound_framing,
		uint64_t inbound_length, message_limits limits, bool client,
		bool head_request = false, request_head request_value = {},
		timeout_policy timeouts = {}) :
		m_connection(std::move(connection_value)),
		m_version(version), m_response_protocol(version),
		m_limits(std::move(limits)), m_timeouts(timeouts), m_client(client),
		m_head_request(head_request), m_request(std::move(request_value)),
		m_reader(std::make_shared<reader_adapter>(
			m_connection, inbound_framing, inbound_length, m_limits, m_timeouts)),
		m_writer(std::make_shared<writer_adapter>(m_connection,
			not client and head_request, m_timeouts)),
		m_inbound(make_body_reader_state(m_reader, not m_client,
			m_limits.max_discard_body)),
		m_outbound(make_body_writer_state(m_writer, outbound_spec, m_client))
	{
		if( not m_client and inbound_framing == body_framing::none )
			m_inbound->set_finished();
		m_connection->transport->set_read_timeout(bounded_timeout(
			m_client ? m_timeouts.header : m_timeouts.body_idle, m_timeouts));
		m_connection->transport->set_write_timeout(m_timeouts.body_idle);
		m_writer->configure(std::move(outbound_spec));
		if( not m_client )
			m_keep_alive = m_version != protocol::v1_0 and
				not has_token(m_request.headers, "connection", "close");
	}

	~h1_stream() override {
		release(false);
	}

	void set_release_handler(std::function<void(bool)> handler)
	{
		m_release_handler = std::move(handler);
		auto weak = weak_from_this();
		m_reader->set_end_handler(
		[weak](error_code error) {
			if( auto self = weak.lock() ) self->body_finished(error);
		});
		if( not m_client )
			m_writer->set_finish_handler(
			[weak](error_code error) {
				if( auto self = weak.lock() )
					self->response_finished(error);
			});
	}

	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_version;
	}
	[[nodiscard]] protocol response_protocol() const noexcept override {
		return m_response_protocol;
	}
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override {
		return m_connection->transport->remote_endpoint(error);
	}
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override {
		return m_connection->transport->local_endpoint(error);
	}

	[[nodiscard]] stream_capabilities capabilities() const noexcept override {
		return {.trailers = true, .full_duplex = true};
	}

	[[nodiscard]] request_head request() const override { return m_request; }

	[[nodiscard]] std::shared_ptr<body_reader_state>
	inbound_body() const noexcept override { return m_inbound; }

	[[nodiscard]] std::shared_ptr<body_writer_state>
	outbound_body() const noexcept override { return m_outbound; }

	[[nodiscard]] response_head wait_response_sync(
		error_code &error) noexcept override
	{
		if( not m_client or m_response_ready )
		{
			error = m_response_ready ? error_code{} :
				make_error_code(errc::operation_not_permitted);
			return m_response;
		}
		for(;;)
		{
			auto wire = read_head_sync(*m_connection,
				m_limits.max_header_list_size + m_limits.max_start_line_size, error);
			if( error ) return {};
			auto parsed = parse_response(wire, m_limits, m_head_request);
			if( not parsed )
			{
				error = parsed.error();
				return {};
			}
			if( parsed->head.status.code() < 200 and
				parsed->head.status.code() != status::switching_protocols )
				continue;
			m_response = parsed->head;
			m_response_protocol = parsed->version;
			m_connection->transport->set_read_timeout(m_timeouts.body_idle);
			m_keep_alive = reusable_response(m_response, parsed->version);
			m_response_ready = true;
			m_reader->configure(parsed->framing, parsed->content_length);
			if( parsed->framing == body_framing::none and m_client )
			{
				m_inbound->set_finished();
				const auto code = m_response.status.code();
				if( code != status::switching_protocols and
					(m_request.method.known() != method::connect or
					 code < 200 or code >= 300) )
					body_finished({});
			}
			return m_response;
		}
	}

	void wait_response(response_handler handler) override
	{
		if( not m_client )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				response_head{});
			return;
		}
		if( m_response_ready )
		{
			std::move(handler)(error_code{}, m_response);
			return;
		}
		read_response(std::move(handler));
	}

	void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept override
	{
		if( m_client )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		if( spec.type() == body_spec::mode::head and not m_head_request )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		auto wire = serialize_response(head, spec, m_version, not m_keep_alive);
		if( not wire )
		{
			error = wire.error();
			return;
		}
		const bool empty = spec.type() == body_spec::mode::empty or
			spec.type() == body_spec::mode::head;
		m_writer->configure(std::move(spec));
		(void)m_connection->write_sync(asio::buffer(*wire), error);
		if( not m_client and not error and empty )
			response_finished(error);
	}

	void respond(response_head head, body_spec spec,
		operation_handler handler) override
	{
		if( m_client )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted));
			return;
		}
		if( spec.type() == body_spec::mode::head and not m_head_request )
		{
			std::move(handler)(
				make_error_code(errc::operation_not_permitted));
			return;
		}
		auto wire_value = serialize_response(head, spec, m_version,
			not m_keep_alive);
		if( not wire_value )
		{
			std::move(handler)(wire_value.error());
			return;
		}
		const bool empty = spec.type() == body_spec::mode::empty or
			spec.type() == body_spec::mode::head;
		m_writer->configure(std::move(spec));
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		auto weak = weak_from_this();
		m_connection->write(asio::buffer(*wire),
		[weak, wire, empty, handler = std::move(handler)]
		(error_code error, size_t) mutable {
			if( empty )
				if( auto self = weak.lock() )
					if( not self->m_client ) self->response_finished(error);
			std::move(handler)(error);
		});
	}

	void inform_sync(response_head head, error_code &error) noexcept override
	{
		auto wire = serialize_response(head, body_spec::empty(), m_version, false);
		if( not wire )
		{
			error = wire.error();
			return;
		}
		(void)m_connection->write_sync(asio::buffer(*wire), error);
	}

	void inform(response_head head, operation_handler handler) override
	{
		auto wire_value = serialize_response(
			head, body_spec::empty(), m_version, false);
		if( not wire_value )
		{
			std::move(handler)(wire_value.error());
			return;
		}
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		m_connection->write(asio::buffer(*wire),
		[wire, handler = std::move(handler)]
		(error_code error, size_t) mutable { std::move(handler)(error); });
	}

	void cancel() noexcept override {
		if( m_released and m_reusable ) return;
		release(false);
	}

	[[nodiscard]] bool reusable() const noexcept override {
		return m_released and m_reusable;
	}

private:
	void read_response(response_handler handler)
	{
		auto self = shared_from_this();
		read_head(m_connection,
			m_limits.max_header_list_size + m_limits.max_start_line_size,
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, std::string wire) mutable {
			if( error )
			{
				std::move(handler)(error, response_head{});
				return;
			}
			auto parsed = parse_response(wire, self->m_limits, self->m_head_request);
			if( not parsed )
			{
				std::move(handler)(parsed.error(), response_head{});
				return;
			}
			if( parsed->head.status.code() < 200 and
				parsed->head.status.code() != status::switching_protocols )
			{
				self->read_response(std::move(handler));
				return;
			}
			self->m_response = parsed->head;
			self->m_response_protocol = parsed->version;
			self->m_connection->transport->set_read_timeout(
				self->m_timeouts.body_idle);
			self->m_keep_alive = reusable_response(self->m_response,
				parsed->version);
			self->m_response_ready = true;
			self->m_reader->configure(parsed->framing, parsed->content_length);
			if( parsed->framing == body_framing::none and self->m_client )
			{
				self->m_inbound->set_finished();
				const auto code = self->m_response.status.code();
				if( code != status::switching_protocols and
					(self->m_request.method.known() != method::connect or
					 code < 200 or code >= 300) )
					self->body_finished({});
			}
			std::move(handler)(error_code{}, self->m_response);
		});
	}

	[[nodiscard]] std::shared_ptr<duplex_state> open_duplex_sync(
		optional<response_head> response, error_code &error) noexcept override
	{
		error.clear();
		if( m_duplex_opened or (m_client and not m_response_ready) )
		{
			error = make_error_code(errc::operation_not_permitted);
			return {};
		}
		if( m_client )
		{
			const auto code = m_response.status.code();
			if( code != status::switching_protocols and
				(m_request.method.known() != method::connect or
				 code < 200 or code >= 300) )
			{
				error = make_error_code(errc::operation_not_permitted);
				return {};
			}
		}
		else
		{
			if( not response )
			{
				error = make_error_code(errc::operation_not_permitted);
				return {};
			}
			const auto code = response->status.code();
			if( code != status::switching_protocols and
				(m_request.method.known() != method::connect or
				 code < 200 or code >= 300) )
			{
				error = make_error_code(errc::invalid_status);
				return {};
			}
			auto wire = serialize_response(*response, body_spec::empty(),
				m_version, false);
			if( not wire )
			{
				error = wire.error();
				return {};
			}
			(void)m_connection->write_sync(asio::buffer(*wire), error);
			if( error ) return {};
		}
		m_duplex_opened = true;
		m_keep_alive = false;
		return std::make_shared<h1_duplex_state>(m_connection, m_version);
	}

	void open_duplex(optional<response_head> response,
		duplex_handler handler) override
	{
		if( m_duplex_opened or (m_client and not m_response_ready) )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				std::shared_ptr<duplex_state>{});
			return;
		}
		if( m_client )
		{
			error_code error;
			auto channel = open_duplex_sync({}, error);
			std::move(handler)(error, std::move(channel));
			return;
		}
		if( not response )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				std::shared_ptr<duplex_state>{});
			return;
		}
		const auto code = response->status.code();
		if( code != status::switching_protocols and
			(m_request.method.known() != method::connect or
			 code < 200 or code >= 300) )
		{
			std::move(handler)(make_error_code(errc::invalid_status),
				std::shared_ptr<duplex_state>{});
			return;
		}
		auto wire_value = serialize_response(*response, body_spec::empty(),
			m_version, false);
		if( not wire_value )
		{
			std::move(handler)(wire_value.error(),
				std::shared_ptr<duplex_state>{});
			return;
		}
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		auto self = shared_from_this();
		m_connection->write(asio::buffer(*wire),
		[self = std::move(self), wire, handler = std::move(handler)]
		(error_code error, size_t) mutable {
			if( error )
			{
				std::move(handler)(error, std::shared_ptr<duplex_state>{});
				return;
			}
			self->m_duplex_opened = true;
			self->m_keep_alive = false;
			std::move(handler)(error_code{},
				std::make_shared<h1_duplex_state>(
					self->m_connection, self->m_version));
		});
	}

	void body_finished(error_code error)
	{
		if( not m_client ) return;
		release(not error and m_keep_alive and
			m_reader->framing() != body_framing::until_eof);
	}

	void response_finished(error_code error)
	{
		if( m_client or m_released ) return;
		release(not error and m_keep_alive and m_reader->request_complete());
	}

	void release(bool reusable)
	{
		if( m_released ) return;
		m_released = true;
		m_reusable = reusable;
		if( not reusable ) m_connection->cancel();
		try {
			if( m_release_handler ) m_release_handler(reusable);
		}
		catch(...) {
		}
	}

	std::shared_ptr<connection> m_connection;
	protocol m_version;
	protocol m_response_protocol;
	message_limits m_limits;
	timeout_policy m_timeouts;
	bool m_client;
	bool m_head_request;
	request_head m_request {};
	std::shared_ptr<reader_adapter> m_reader;
	std::shared_ptr<writer_adapter> m_writer;
	std::shared_ptr<body_reader_state> m_inbound;
	std::shared_ptr<body_writer_state> m_outbound;
	response_head m_response {};
	bool m_response_ready = false;
	bool m_keep_alive = true;
	bool m_released = false;
	bool m_reusable = false;
	bool m_duplex_opened = false;
	std::function<void(bool)> m_release_handler;
};

class h1_client_session final : public client_session,
	public std::enable_shared_from_this<h1_client_session>
{
public:
	h1_client_session(asio::any_io_executor executor, protocol version,
		message_limits limits, timeout_policy timeouts = {},
		bool absolute_form = false) :
		m_executor(std::move(executor)), m_version(version),
		m_limits(std::move(limits)), m_timeouts(timeouts),
		m_absolute_form(absolute_form), m_connection(
			std::make_shared<connection>(m_executor)), m_resolver(m_executor)
	{
		m_connection->transport->set_read_timeout(
			bounded_timeout(m_timeouts.body_idle, m_timeouts));
		m_connection->transport->set_write_timeout(
			bounded_timeout(m_timeouts.body_idle, m_timeouts));
	}

	h1_client_session(asio::any_io_executor executor, protocol version,
		message_limits limits,
		std::shared_ptr<::riwo::http_exp::connection> transport,
		timeout_policy timeouts = {}, bool absolute_form = false) :
		m_executor(std::move(executor)), m_version(version),
		m_limits(std::move(limits)), m_timeouts(timeouts),
		m_absolute_form(absolute_form),
		m_connection(std::make_shared<connection>(std::move(transport))),
		m_resolver(m_executor), m_preconnected(true)
	{
		m_connection->transport->set_read_timeout(
			bounded_timeout(m_timeouts.body_idle, m_timeouts));
		m_connection->transport->set_write_timeout(
			bounded_timeout(m_timeouts.body_idle, m_timeouts));
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return m_executor;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_version;
	}
	[[nodiscard]] size_t available_stream_capacity() const noexcept override {
		return not m_in_use and not m_draining and not closed() ? 1 : 0;
	}
	[[nodiscard]] bool draining() const noexcept override { return m_draining; }
	[[nodiscard]] bool closed() const noexcept override {
		return not m_connection->is_open();
	}
	[[nodiscard]] bool try_reserve_stream() noexcept override
	{
		if( m_in_use or m_draining or closed() ) return false;
		m_in_use = true;
		m_reserved = true;
		return true;
	}
	void release_stream(bool reusable) noexcept override
	{
		if( not m_in_use ) return;
		m_in_use = false;
		m_reserved = false;
		if( not reusable or m_draining )
		{
			m_draining = true;
			m_connection->cancel();
		}
		try {
			if( m_release_handler ) m_release_handler(reusable);
		}
		catch(...) {
		}
	}
	void set_release_handler(std::function<void(bool)> handler) override {
		m_release_handler = std::move(handler);
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(request_head head,
		body_spec spec, error_code &error) noexcept override
	{
		error.clear();
		if( not begin_open(error) ) return {};
		auto wire = serialize_request(head, spec, m_version, m_absolute_form);
		if( not wire )
		{
			error = wire.error();
			release_stream(false);
			return {};
		}
		try {
			if( not m_preconnected )
			{
				const auto port = static_cast<uint16_t>(
					head.url.port() == 0 ? 80 : head.url.port());
				m_connection->connect_sync(head.url.host(), port, error);
				if( error )
				{
					release_stream(false);
					return {};
				}
			}
			(void)m_connection->write_sync(asio::buffer(*wire), error);
			if( error )
			{
				release_stream(false);
				return {};
			}
			return make_stream(std::move(head), std::move(spec));
		}
		catch(...) {
			error = exception_error(std::current_exception());
			release_stream(false);
			return {};
		}
	}

	void open(request_head head, body_spec spec, open_handler handler) override
	{
		error_code begin_error;
		if( not begin_open(begin_error) )
		{
			std::move(handler)(begin_error, std::shared_ptr<stream>{});
			return;
		}
		auto wire_value = serialize_request(head, spec, m_version, m_absolute_form);
		if( not wire_value )
		{
			std::move(handler)(wire_value.error(), std::shared_ptr<stream>{});
			release_stream(false);
			return;
		}
		struct operation final : std::enable_shared_from_this<operation>
		{
			std::shared_ptr<h1_client_session> session;
			request_head head;
			body_spec spec = body_spec::empty();
			std::shared_ptr<std::string> wire;
			open_handler handler;

			void start()
			{
				if( session->m_preconnected )
				{
					write_head();
					return;
				}
				auto self = shared_from_this();
				const auto port = static_cast<uint16_t>(
					head.url.port() == 0 ? 80 : head.url.port());
				session->m_connection->connect_async(head.url.host(), port,
				[self = std::move(self)](error_code error) mutable {
					if( error ) return self->finish(error);
					self->write_head();
				});
			}

			void write_head()
			{
				auto self = shared_from_this();
				session->m_connection->write(asio::buffer(*wire),
				[self = std::move(self)](error_code error, size_t) mutable {
					self->finish(error);
				});
			}

			void finish(error_code error)
			{
				if( error )
				{
					session->release_stream(false);
					std::move(handler)(error, std::shared_ptr<stream>{});
					return;
				}
				auto result = session->make_stream(std::move(head),
					std::move(spec));
				std::move(handler)(error_code{}, std::move(result));
			}
		};
		auto value = std::make_shared<operation>();
		value->session = shared_from_this();
		value->head = std::move(head);
		value->spec = std::move(spec);
		value->wire = std::make_shared<std::string>(std::move(*wire_value));
		value->handler = std::move(handler);
		value->start();
	}

	void graceful_shutdown(shutdown_handler handler) override
	{
		m_draining = true;
		if( not m_in_use ) m_connection->cancel();
		std::move(handler)(error_code{});
	}

private:
	[[nodiscard]] bool begin_open(error_code &error) noexcept
	{
		if( closed() or m_draining )
		{
			error = make_error_code(errc::operation_not_permitted);
			return false;
		}
		if( m_in_use and not m_reserved )
		{
			error = make_error_code(errc::operation_in_progress);
			return false;
		}
		m_in_use = true;
		m_reserved = false;
		return true;
	}

	[[nodiscard]] std::shared_ptr<h1_stream> make_stream(request_head head,
		body_spec spec)
	{
		const bool head_request = head.method.known() == method::head;
		auto result = std::make_shared<h1_stream>(m_connection, m_version,
			std::move(spec), body_framing::none, 0, m_limits, true,
			head_request, std::move(head), m_timeouts);
		auto weak = weak_from_this();
		result->set_release_handler(
		[weak](bool reusable) {
			if( auto self = weak.lock() )
				self->release_stream(reusable);
		});
		return result;
	}

	executor_type m_executor;
	protocol m_version;
	message_limits m_limits;
	timeout_policy m_timeouts;
	bool m_absolute_form = false;
	std::shared_ptr<connection> m_connection;
	asio::ip::tcp::resolver m_resolver;
	bool m_preconnected = false;
	bool m_in_use = false;
	bool m_reserved = false;
	bool m_draining = false;
	std::function<void(bool)> m_release_handler;
};

class h1_server_session final : public server_session,
	public std::enable_shared_from_this<h1_server_session>
{
public:
	h1_server_session(asio::ip::tcp::socket socket, message_limits limits,
		timeout_policy timeouts = {}) :
		m_executor(socket.get_executor()), m_limits(std::move(limits)),
		m_timeouts(timeouts),
		m_connection(std::make_shared<connection>(std::move(socket)))
	{
	}

	h1_server_session(std::shared_ptr<::riwo::http_exp::connection> transport,
		protocol version, message_limits limits, timeout_policy timeouts = {}) :
		m_executor(transport->get_executor()), m_limits(std::move(limits)),
		m_timeouts(timeouts),
		m_connection(std::make_shared<connection>(std::move(transport))),
		m_version(version)
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return m_executor;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_version;
	}
	[[nodiscard]] bool draining() const noexcept override {
		return m_draining;
	}
	[[nodiscard]] bool closed() const noexcept override {
		return not m_connection->is_open();
	}
	[[nodiscard]] bool try_reserve_stream() noexcept {
		if( m_in_use or m_draining or closed() ) return false;
		m_in_use = true;
		return true;
	}
	void release_stream(bool reusable) noexcept
	{
		if( not m_in_use ) return;
		m_in_use = false;
		if( not reusable ) m_draining = true;
		try {
			if( m_release_handler ) m_release_handler(reusable);
		}
		catch(...) {
		}
	}
	void set_release_handler(std::function<void(bool)> handler) {
		m_release_handler = std::move(handler);
	}

	void accept(accept_handler handler) override
	{
		if( m_in_use )
		{
			std::move(handler)(make_error_code(errc::operation_in_progress),
				std::shared_ptr<stream>{});
			return;
		}
		if( m_draining or closed() )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				std::shared_ptr<stream>{});
			return;
		}
		m_in_use = true;
		m_connection->transport->set_read_timeout(m_timeouts.header);
		m_connection->transport->set_write_timeout(m_timeouts.body_idle);
		auto self = shared_from_this();
		read_head(m_connection,
			m_limits.max_header_list_size + m_limits.max_start_line_size,
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, std::string wire) mutable {
			if( error )
			{
				self->release_stream(false);
				std::move(handler)(error, std::shared_ptr<stream>{});
				return;
			}
			auto parsed = parse_request(wire, self->m_limits);
			if( not parsed )
			{
				const auto code = static_cast<std::uint16_t>(
					parsed.error() ==
						make_error_code(errc::header_limit_exceeded) ? 431 : 400);
				error_code ignored;
				(void)self->m_connection->write_sync(asio::buffer(
					error_response(code, code == 431 ?
						"Request Header Fields Too Large" : "Bad Request")),
					ignored);
				self->release_stream(false);
				std::move(handler)(parsed.error(), std::shared_ptr<stream>{});
				return;
			}
			self->m_version = parsed->version;
			if( parsed->head.headers.find_first("Expect") )
			{
				if( has_unsupported_expectation(parsed->head.headers) )
				{
					error_code ignored;
					(void)self->m_connection->write_sync(asio::buffer(
						error_response(417, "Expectation Failed")), ignored);
					self->release_stream(false);
					std::move(handler)(make_error_code(errc::invalid_field_value),
						std::shared_ptr<stream>{});
					return;
				}
				if( parsed->version == protocol::v1_1 )
				{
					error_code expect_error;
					(void)self->m_connection->write_sync(asio::buffer(
						std::string_view("HTTP/1.1 100 Continue\r\n\r\n")),
						expect_error);
					if( expect_error )
					{
						self->release_stream(false);
						std::move(handler)(expect_error,
							std::shared_ptr<stream>{});
						return;
					}
				}
			}
			auto result = std::make_shared<h1_stream>(self->m_connection,
				parsed->version, body_spec::empty(), parsed->framing,
				parsed->content_length, self->m_limits, false,
				parsed->head.method.known() == method::head,
				std::move(parsed->head), self->m_timeouts);
			std::weak_ptr<h1_server_session> weak = self;
			result->set_release_handler(
			[weak](bool reusable) {
				if( auto session = weak.lock() )
					session->release_stream(reusable);
			});
			std::move(handler)(error_code{}, std::move(result));
		});
	}

	void graceful_shutdown(shutdown_handler handler) override
	{
		m_draining = true;
		m_connection->cancel();
		std::move(handler)(error_code{});
	}

private:
	executor_type m_executor;
	message_limits m_limits;
	timeout_policy m_timeouts;
	std::shared_ptr<connection> m_connection;
	protocol m_version = protocol::v1_1;
	bool m_in_use = false;
	bool m_draining = false;
	std::function<void(bool)> m_release_handler;
};

} //namespace

std::shared_ptr<client_session> make_client_session(
	asio::any_io_executor executor, protocol version, message_limits limits,
	timeout_policy timeouts, bool absolute_form)
{
	return std::make_shared<h1_client_session>(
		std::move(executor), version, std::move(limits), timeouts,
		absolute_form);
}

std::shared_ptr<client_session> make_client_session(
	asio::any_io_executor executor, protocol version, message_limits limits,
	std::shared_ptr<::riwo::http_exp::connection> transport,
	timeout_policy timeouts, bool absolute_form)
{
	return std::make_shared<h1_client_session>(
		std::move(executor), version, std::move(limits),
		std::move(transport), timeouts, absolute_form);
}

std::shared_ptr<server_session> make_server_session(
	asio::ip::tcp::socket socket, message_limits limits,
	timeout_policy timeouts)
{
	return std::make_shared<h1_server_session>(
		std::move(socket), std::move(limits), timeouts);
}

std::shared_ptr<server_session> make_server_session(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	protocol version,
	message_limits limits, timeout_policy timeouts)
{
	return std::make_shared<h1_server_session>(
		std::move(transport), version, std::move(limits), timeouts);
}

} //namespace riwo::http_exp::v1::detail

namespace riwo::http_exp::detail
{

class h1_server_loop final :
	public std::enable_shared_from_this<h1_server_loop>
{
public:
	std::shared_ptr<::riwo::http_exp::v1::detail::h1_server_session> session;
	server_accept_handler handler;
	bool served = false;

	void start()
	{
		auto self = shared_from_this();
		session->accept(
		[self = std::move(self)](error_code error,
			std::shared_ptr<stream> value) mutable {
			if( error )
			{
				if( not self->served )
					self->handler(error, {}, {});
				return;
			}
			self->served = true;
			auto request = value ? value->request() : request_head{};
			self->handler(error_code{}, std::move(value),
				std::move(request));
		});
	}
};

class h1_server_connection final : public server_connection
{
public:
	explicit h1_server_connection(
		std::shared_ptr<h1_server_loop> loop_value) :
		loop(std::move(loop_value))
	{
	}

	void cancel() noexcept override {
		loop->session->graceful_shutdown([](error_code) {});
	}
	[[nodiscard]] bool closed() const noexcept override {
		return loop->session->closed();
	}

	std::shared_ptr<h1_server_loop> loop;
};

server_connection_ptr accept_h1(asio::ip::tcp::socket socket,
	message_limits limits, timeout_policy timeouts,
	server_accept_handler handler)
{
	error_code option_error;
	(void)socket.set_option(asio::ip::tcp::no_delay(true), option_error);
	return accept_h1(std::make_shared<tcp_connection>(std::move(socket)),
		std::move(limits), timeouts, std::move(handler));
}

server_connection_ptr accept_h1(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	message_limits limits, timeout_policy timeouts,
	server_accept_handler handler)
{
	auto session = std::make_shared<
		::riwo::http_exp::v1::detail::h1_server_session>(
		std::move(transport), protocol::v1_1, std::move(limits), timeouts);
	auto value = std::make_shared<h1_server_loop>();
	value->session = session;
	value->handler = std::move(handler);
	std::weak_ptr<h1_server_loop> weak = value;
	session->set_release_handler(
	[weak](bool reusable) {
		if( not reusable ) return;
		if( auto self = weak.lock() ) self->start();
	});
	value->start();
	return std::make_shared<h1_server_connection>(std::move(value));
}

} //namespace riwo::http_exp::detail
