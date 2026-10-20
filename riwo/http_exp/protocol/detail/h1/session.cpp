// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include "wire.h"
#include <riwo/http_exp/server/detail/session.h>
#include <riwo/http_exp/transport/tcp_connection.h>

namespace riwo::http_exp::detail::h1 { namespace
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
		if( closed ) return;
		closed = true;
		transport->cancel();
	}

	std::shared_ptr<::riwo::http_exp::connection> transport;
	std::string pending;
	bool closed = false;
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
		std::string_view text = iterator->value;
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
		std::string_view text = iterator->value;
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
		handler_type completion) : connection_(std::move(value)), limit_(maximum),
		handler_(std::move(completion))
	{
	}

	void start()
	{
		auto head = take_head(*connection_, limit_);
		if( head )
		{
			std::move(handler_)(error_code{}, std::move(*head));
			return;
		}
		if( head.error() != make_error_code(errc::operation_in_progress) )
		{
			std::move(handler_)(head.error(), std::string{});
			return;
		}
		auto self = shared_from_this();
		connection_->read_some(asio::buffer(buffer_),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( error )
			{
				std::move(self->handler_)(error, std::string{});
				return;
			}
			self->connection_->pending.append(self->buffer_.data(), size);
			self->start();
		});
	}

private:
	std::shared_ptr<connection> connection_;
	size_t limit_;
	handler_type handler_;
	std::array<char,4096> buffer_ {};
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
		connection_(std::move(connection_value)), framing_(framing_value),
		remaining_(length), limits_(std::move(limits)), timeouts_(timeouts)
	{
	}

	void configure(body_framing framing, uint64_t length) noexcept
	{
		framing_ = framing;
		remaining_ = length;
	}

	void set_end_handler(std::function<void(error_code)> handler) {
		end_handler_ = std::move(handler);
	}

	[[nodiscard]] body_framing framing() const noexcept { return framing_; }

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
		auto value = timeouts_.body_idle;
		if( timeouts_.overall )
		{
			const auto remaining = *deadline_ - std::chrono::steady_clock::now();
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
			connection_->transport->set_read_timeout(value);
		return true;
	}

	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override
	{
		error.clear();
		if( not ended_ ) error = make_error_code(errc::operation_not_permitted);
		return trailers_;
	}

	void trailers(trailers_handler handler) override
	{
		if( not ended_ )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				fields{});
			return;
		}
		std::move(handler)(error_code{}, trailers_);
	}

	void cancel() noexcept override { connection_->cancel(); }

	[[nodiscard]] bool request_complete() const noexcept
	{
		return framing_ == body_framing::none or
			(framing_ == body_framing::content_length and remaining_ == 0) or
			(framing_ == body_framing::chunked and ended_);
	}

private:
	void notify_end(error_code error) noexcept
	{
		if( end_notified_ ) return;
		end_notified_ = true;
		try {
			if( end_handler_ ) end_handler_(error);
		}
		catch(...) {
		}
	}

	[[nodiscard]] bool account(size_t size, error_code &error) noexcept
	{
		if( limits_.max_body_size and
			size > *limits_.max_body_size -
				std::min(*limits_.max_body_size, delivered_) )
		{
			error = make_error_code(errc::buffer_limit_exceeded);
			connection_->cancel();
			return false;
		}
		delivered_ += size;
		return true;
	}

	[[nodiscard]] size_t copy_pending(mutable_buffer buffer, size_t maximum,
		error_code &error)
	{
		const auto size = std::min({buffer.size(), maximum,
			connection_->pending.size()});
		if( size == 0 ) return 0;
		std::memcpy(buffer.data(), connection_->pending.data(), size);
		connection_->pending.erase(0, size);
		if( not account(size, error) ) return 0;
		return size;
	}

	[[nodiscard]] bool fill_sync(error_code &error)
	{
		std::array<char,8192> data {};
		auto size = connection_->read_some_sync(asio::buffer(data), error);
		if( error ) return false;
		connection_->pending.append(data.data(), size);
		return true;
	}

	[[nodiscard]] bool consume_chunk_crlf_sync(error_code &error)
	{
		while( connection_->pending.size() < 2 )
			if( not fill_sync(error) ) return false;
		if( not connection_->pending.starts_with("\r\n") )
		{
			error = protocol_error();
			return false;
		}
		connection_->pending.erase(0, 2);
		need_chunk_crlf_ = false;
		return true;
	}

	[[nodiscard]] bool read_chunk_line_sync(error_code &error)
	{
		for(;;)
		{
			auto end = connection_->pending.find("\r\n");
			if( end != std::string::npos )
			{
				auto line = std::string_view(connection_->pending).substr(0, end);
				auto size = parse_chunk_size(line);
				if( not size )
				{
					error = size.error();
					return false;
				}
				connection_->pending.erase(0, end + 2);
				chunk_remaining_ = *size;
				reading_trailers_ = chunk_remaining_ == 0;
				return true;
			}
			if( connection_->pending.size() > limits_.max_start_line_size )
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
			auto end = connection_->pending.find("\r\n");
			if( end == std::string::npos )
			{
				if( connection_->pending.size() > limits_.max_header_list_size )
				{
					error = make_error_code(errc::header_limit_exceeded);
					return false;
				}
				if( not fill_sync(error) ) return false;
				continue;
			}
			if( end == 0 )
			{
				connection_->pending.erase(0, 2);
				ended_ = true;
				return true;
			}
			auto line = std::string_view(connection_->pending).substr(0, end);
			auto colon = line.find(':');
			if( colon == std::string_view::npos or trailers_.size() >=
				limits_.max_field_count )
			{
				error = protocol_error();
				return false;
			}
			auto value = line.substr(colon + 1);
			while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
				value.remove_prefix(1);
			trailers_.add(std::string(line.substr(0, colon)), std::string(value));
			connection_->pending.erase(0, end + 2);
		}
	}

	[[nodiscard]] body_read_result read_sync(
		mutable_buffer buffer, error_code &error)
	{
		if( ended_ or framing_ == body_framing::none )
		{
			ended_ = true;
			return {0, true};
		}
		if( framing_ == body_framing::content_length )
		{
			if( remaining_ == 0 )
			{
				ended_ = true;
				return {0, true};
			}
			auto size = copy_pending(buffer, static_cast<size_t>(
				std::min<uint64_t>(remaining_, SIZE_MAX)), error);
			if( size == 0 and not error )
			{
				auto target = mutable_buffer(buffer.data(), static_cast<size_t>(
					std::min<uint64_t>(buffer.size(), remaining_)));
				size = connection_->read_some_sync(target, error);
				remaining_ -= std::min<uint64_t>(remaining_, size);
				if( is_eof(error) )
					error = remaining_ == 0 ? error_code{} :
						make_error_code(errc::body_length_mismatch);
				if( not error and size != 0 and not account(size, error) ) return {};
			}
			else remaining_ -= std::min<uint64_t>(remaining_, size);
			const bool eof = not error and remaining_ == 0;
			if( eof ) ended_ = true;
			return {size, eof};
		}
		if( framing_ == body_framing::until_eof )
		{
			auto size = copy_pending(buffer, SIZE_MAX, error);
			if( size != 0 or error ) return {size, false};
			size = connection_->read_some_sync(buffer, error);
			const bool eof = is_eof(error);
			if( eof )
			{
				error.clear();
				ended_ = true;
			}
			if( not error and size != 0 and not account(size, error) ) return {};
			return {size, eof and not error};
		}
		for(;;)
		{
			if( reading_trailers_ )
			{
				if( not read_trailers_sync(error) ) return {};
				return {0, true};
			}
			if( need_chunk_crlf_ and not consume_chunk_crlf_sync(error) ) return {};
			if( chunk_remaining_ == 0 and not read_chunk_line_sync(error) ) return {};
			if( reading_trailers_ ) continue;
			auto size = copy_pending(buffer, static_cast<size_t>(
				std::min<uint64_t>(chunk_remaining_, SIZE_MAX)), error);
			if( size == 0 and not error )
			{
				auto target = mutable_buffer(buffer.data(), static_cast<size_t>(
					std::min<uint64_t>(buffer.size(), chunk_remaining_)));
				size = connection_->read_some_sync(target, error);
				if( not error and not account(size, error) ) return {};
			}
			chunk_remaining_ -= std::min<uint64_t>(chunk_remaining_, size);
			if( chunk_remaining_ == 0 ) need_chunk_crlf_ = true;
			return {size, false};
		}
	}

	class async_read_operation;
	friend class async_read_operation;

	std::shared_ptr<connection> connection_;
	body_framing framing_;
	uint64_t remaining_ = 0;
	message_limits limits_;
	timeout_policy timeouts_;
	std::optional<std::chrono::steady_clock::time_point> deadline_ =
		timeouts_.overall ? std::optional<std::chrono::steady_clock::time_point>(
			std::chrono::steady_clock::now() + *timeouts_.overall) : std::nullopt;
	fields trailers_;
	uint64_t delivered_ = 0;
	uint64_t chunk_remaining_ = 0;
	bool need_chunk_crlf_ = false;
	bool reading_trailers_ = false;
	bool ended_ = false;
	bool end_notified_ = false;
	std::function<void(error_code)> end_handler_;
};

class reader_adapter::async_read_operation final :
	public std::enable_shared_from_this<async_read_operation>
{
public:
	async_read_operation(std::shared_ptr<reader_adapter> owner,
		mutable_buffer target, read_handler completion) : owner_(std::move(owner)),
		target_(target), handler_(std::move(completion))
	{
	}

	void start()
	{
		error_code error;
		if( not owner_->apply_read_timeout(error) )
		{
			owner_->ended_ = true;
			finish(error, {});
			return;
		}
		if( owner_->ended_ or owner_->framing_ == body_framing::none )
		{
			owner_->ended_ = true;
			finish({}, {0, true});
			return;
		}
		if( owner_->framing_ == body_framing::content_length )
		{
			if( owner_->remaining_ == 0 )
			{
				owner_->ended_ = true;
				finish({}, {0, true});
				return;
			}
			auto size = owner_->copy_pending(target_, static_cast<size_t>(
				std::min<uint64_t>(owner_->remaining_, SIZE_MAX)), error);
			if( size != 0 or error )
			{
				owner_->remaining_ -= std::min<uint64_t>(owner_->remaining_, size);
				const bool eof = not error and owner_->remaining_ == 0;
				if( eof ) owner_->ended_ = true;
				finish(error, {size, eof});
				return;
			}
			direct_read(static_cast<size_t>(std::min<uint64_t>(
				target_.size(), owner_->remaining_)), true);
			return;
		}
		if( owner_->framing_ == body_framing::until_eof )
		{
			auto size = owner_->copy_pending(target_, SIZE_MAX, error);
			if( size != 0 or error )
			{
				finish(error, {size, false});
				return;
			}
			direct_read(target_.size(), false);
			return;
		}
		chunk_step();
	}

private:
	void direct_read(size_t size, bool fixed)
	{
		auto self = shared_from_this();
		owner_->connection_->read_some(
			mutable_buffer(target_.data(), size),
		[self = std::move(self), fixed](error_code error, size_t count) mutable {
			const bool transport_eof = is_eof(error);
			if( fixed ) self->owner_->remaining_ -=
				std::min<uint64_t>(self->owner_->remaining_, count);
			if( transport_eof )
				error = not fixed or self->owner_->remaining_ == 0 ? error_code{} :
					make_error_code(errc::body_length_mismatch);
			if( not error and count != 0 and
				not self->owner_->account(count, error) ) count = 0;
			bool eof = false;
			if( fixed )
				eof = not error and self->owner_->remaining_ == 0;
			else eof = transport_eof and not error;
			if( eof ) self->owner_->ended_ = true;
			self->finish(error, {count, eof});
		});
	}

	void fill()
	{
		auto self = shared_from_this();
		owner_->connection_->read_some(asio::buffer(buffer_),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( error )
			{
				self->finish(error, {});
				return;
			}
			self->owner_->connection_->pending.append(
				self->buffer_.data(), size);
			self->chunk_step();
		});
	}

	void chunk_step()
	{
		try {
			for(;;)
			{
				if( owner_->reading_trailers_ )
				{
					auto end = owner_->connection_->pending.find("\r\n");
					if( end == std::string::npos )
					{
						if( owner_->connection_->pending.size() >
							owner_->limits_.max_header_list_size )
						{
							finish(make_error_code(errc::header_limit_exceeded), {});
							return;
						}
						fill();
						return;
					}
					if( end == 0 )
					{
						owner_->connection_->pending.erase(0, 2);
						owner_->ended_ = true;
						finish({}, {0, true});
						return;
					}
					auto line = std::string_view(owner_->connection_->pending)
						.substr(0, end);
					auto colon = line.find(':');
					if( colon == std::string_view::npos or owner_->trailers_.size() >=
						owner_->limits_.max_field_count )
					{
						finish(protocol_error(), {});
						return;
					}
					auto value = line.substr(colon + 1);
					while( not value.empty() and
						(value.front() == ' ' or value.front() == '\t') )
						value.remove_prefix(1);
					owner_->trailers_.add(std::string(line.substr(0, colon)),
						std::string(value));
					owner_->connection_->pending.erase(0, end + 2);
					continue;
				}
				if( owner_->need_chunk_crlf_ )
				{
					if( owner_->connection_->pending.size() < 2 )
					{
						fill();
						return;
					}
					if( not owner_->connection_->pending.starts_with("\r\n") )
					{
						finish(protocol_error(), {});
						return;
					}
					owner_->connection_->pending.erase(0, 2);
					owner_->need_chunk_crlf_ = false;
				}
				if( owner_->chunk_remaining_ == 0 )
				{
					auto end = owner_->connection_->pending.find("\r\n");
					if( end == std::string::npos )
					{
						if( owner_->connection_->pending.size() >
							owner_->limits_.max_start_line_size )
						{
							finish(make_error_code(errc::header_limit_exceeded), {});
							return;
						}
						fill();
						return;
					}
					auto line = std::string_view(owner_->connection_->pending)
						.substr(0, end);
					auto chunk = parse_chunk_size(line);
					if( not chunk )
					{
						finish(chunk.error(), {});
						return;
					}
					owner_->connection_->pending.erase(0, end + 2);
					owner_->chunk_remaining_ = *chunk;
					owner_->reading_trailers_ = *chunk == 0;
					continue;
				}
				error_code error;
				auto size = owner_->copy_pending(target_, static_cast<size_t>(
					std::min<uint64_t>(owner_->chunk_remaining_, SIZE_MAX)), error);
				if( size != 0 or error )
				{
					owner_->chunk_remaining_ -=
						std::min<uint64_t>(owner_->chunk_remaining_, size);
					if( owner_->chunk_remaining_ == 0 )
						owner_->need_chunk_crlf_ = true;
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
			target_.size(), owner_->chunk_remaining_));
		auto self = shared_from_this();
		owner_->connection_->read_some(
			mutable_buffer(target_.data(), count),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( not error and not self->owner_->account(size, error) ) size = 0;
			self->owner_->chunk_remaining_ -=
				std::min<uint64_t>(self->owner_->chunk_remaining_, size);
			if( self->owner_->chunk_remaining_ == 0 )
				self->owner_->need_chunk_crlf_ = true;
			self->finish(error, {size, false});
		});
	}

	void finish(error_code error, body_read_result result)
	{
		if( is_eof(error) )
			error = make_error_code(errc::body_length_mismatch);
		if( error or result.eof ) owner_->notify_end(error);
		std::move(handler_)(error, result);
	}

	std::shared_ptr<reader_adapter> owner_;
	mutable_buffer target_;
	read_handler handler_;
	std::array<char,8192> buffer_ {};
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
		connection_(std::move(connection_value)),
		suppress_body_(suppress_body), timeouts_(timeouts),
		deadline_(timeouts_.overall ? std::optional<
			std::chrono::steady_clock::time_point>(
				std::chrono::steady_clock::now() + *timeouts_.overall) :
				std::nullopt)
	{
	}

	void configure(body_spec spec) noexcept {
		chunked_ = spec.type() == body_spec::mode::streaming;
	}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		try {
			if( not apply_write_timeout(error) ) return 0;
			if( suppress_body_ ) return buffer.size();
			if( chunked_ )
			{
				auto header = serialize_chunk_header(buffer.size());
				(void)connection_->write_sync(asio::buffer(header), error);
				if( error ) return 0;
				auto size = connection_->write_sync(buffer, error);
				if( error or size != buffer.size() ) return size;
				constexpr std::string_view ending = "\r\n";
				(void)connection_->write_sync(asio::buffer(ending), error);
				return size;
			}
			return connection_->write_sync(buffer, error);
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
		if( suppress_body_ )
		{
			std::move(handler)(error_code{}, buffer.size());
			return;
		}
		if( not chunked_ )
		{
			connection_->write(buffer, std::move(handler));
			return;
		}

		auto connection = connection_;
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
		if( chunked_ and not suppress_body_ )
		{
			try {
				auto wire = serialize_last_chunk(trailers);
				(void)connection_->write_sync(asio::buffer(wire), error);
			}
			catch(...) {
				error = exception_error(std::current_exception());
			}
		}
		if( finish_handler_ ) finish_handler_(error);
	}

	void finish(fields trailers, finish_handler handler) override
	{
		if( not chunked_ or suppress_body_ )
		{
			if( finish_handler_ ) finish_handler_(error_code{});
			std::move(handler)(error_code{});
			return;
		}
		auto wire = std::make_shared<std::string>(
			serialize_last_chunk(trailers));
		auto finish = finish_handler_;
		connection_->write(asio::buffer(*wire),
		[finish = std::move(finish), wire, handler = std::move(handler)]
		(error_code error, size_t) mutable {
			if( finish ) finish(error);
			std::move(handler)(error);
		});
	}

	void cancel() noexcept override { connection_->cancel(); }

	void set_finish_handler(std::function<void(error_code)> handler) {
		finish_handler_ = std::move(handler);
	}

private:
	[[nodiscard]] bool apply_write_timeout(error_code &error) noexcept
	{
		auto value = timeouts_.body_idle;
		if( timeouts_.overall )
		{
			const auto remaining = *deadline_ - std::chrono::steady_clock::now();
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
			connection_->transport->set_write_timeout(value);
		return true;
	}

	std::shared_ptr<connection> connection_;
	bool chunked_ = false;
	bool suppress_body_ = false;
	timeout_policy timeouts_;
	std::optional<std::chrono::steady_clock::time_point> deadline_;
	std::function<void(error_code)> finish_handler_;
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
		connection_(std::move(connection_value)),
		version_(version), response_protocol_(version),
		limits_(std::move(limits)), timeouts_(timeouts), client_(client),
		head_request_(head_request), request_(std::move(request_value)),
		reader_(std::make_shared<reader_adapter>(
			connection_, inbound_framing, inbound_length, limits_, timeouts_)),
		writer_(std::make_shared<writer_adapter>(connection_,
			not client and head_request, timeouts_)),
		inbound_(make_body_reader_state(reader_, not client_,
			limits_.max_discard_body)),
		outbound_(make_body_writer_state(writer_, outbound_spec, client_))
	{
		if( not client_ and inbound_framing == body_framing::none )
			inbound_->set_finished();
		connection_->transport->set_read_timeout(bounded_timeout(
			client_ ? timeouts_.header : timeouts_.body_idle, timeouts_));
		connection_->transport->set_write_timeout(timeouts_.body_idle);
		writer_->configure(std::move(outbound_spec));
		if( not client_ )
			keep_alive_ = version_ != protocol::v1_0 and
				not has_token(request_.headers, "connection", "close");
	}

	~h1_stream() override {
		release(false);
	}

	void set_release_handler(std::function<void(bool)> handler)
	{
		release_handler_ = std::move(handler);
		auto weak = weak_from_this();
		reader_->set_end_handler(
		[weak](error_code error) {
			if( auto self = weak.lock() ) self->body_finished(error);
		});
		if( not client_ )
			writer_->set_finish_handler(
			[weak](error_code error) {
				if( auto self = weak.lock() )
					self->response_finished(error);
			});
	}

	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
	}
	[[nodiscard]] protocol response_protocol() const noexcept override {
		return response_protocol_;
	}
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override {
		return connection_->transport->remote_endpoint(error);
	}
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override {
		return connection_->transport->local_endpoint(error);
	}

	[[nodiscard]] stream_capabilities capabilities() const noexcept override {
		return {.trailers = true, .full_duplex = true};
	}

	[[nodiscard]] request_head request() const override { return request_; }

	[[nodiscard]] std::shared_ptr<body_reader_state>
	inbound_body() const noexcept override { return inbound_; }

	[[nodiscard]] std::shared_ptr<body_writer_state>
	outbound_body() const noexcept override { return outbound_; }

	[[nodiscard]] response_head wait_response_sync(
		error_code &error) noexcept override
	{
		if( not client_ or response_ready_ )
		{
			error = response_ready_ ? error_code{} :
				make_error_code(errc::operation_not_permitted);
			return response_;
		}
		for(;;)
		{
			auto wire = read_head_sync(*connection_,
				limits_.max_header_list_size + limits_.max_start_line_size, error);
			if( error ) return {};
			auto parsed = parse_response(wire, limits_, head_request_);
			if( not parsed )
			{
				error = parsed.error();
				return {};
			}
			if( parsed->head.status.code() < 200 ) continue;
			response_ = parsed->head;
			response_protocol_ = parsed->version;
			connection_->transport->set_read_timeout(timeouts_.body_idle);
			keep_alive_ = reusable_response(response_, parsed->version);
			response_ready_ = true;
			reader_->configure(parsed->framing, parsed->content_length);
			if( parsed->framing == body_framing::none and client_ )
			{
				inbound_->set_finished();
				body_finished({});
			}
			return response_;
		}
	}

	void wait_response(response_handler handler) override
	{
		if( not client_ )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				response_head{});
			return;
		}
		if( response_ready_ )
		{
			std::move(handler)(error_code{}, response_);
			return;
		}
		read_response(std::move(handler));
	}

	void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept override
	{
		if( client_ )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		if( spec.type() == body_spec::mode::head and not head_request_ )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		auto wire = serialize_response(head, spec, version_, not keep_alive_);
		if( not wire )
		{
			error = wire.error();
			return;
		}
		const bool empty = spec.type() == body_spec::mode::empty or
			spec.type() == body_spec::mode::head;
		writer_->configure(std::move(spec));
		(void)connection_->write_sync(asio::buffer(*wire), error);
		if( not client_ and not error and empty )
			response_finished(error);
	}

	void respond(response_head head, body_spec spec,
		operation_handler handler) override
	{
		if( client_ )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted));
			return;
		}
		if( spec.type() == body_spec::mode::head and not head_request_ )
		{
			std::move(handler)(
				make_error_code(errc::operation_not_permitted));
			return;
		}
		auto wire_value = serialize_response(head, spec, version_,
			not keep_alive_);
		if( not wire_value )
		{
			std::move(handler)(wire_value.error());
			return;
		}
		const bool empty = spec.type() == body_spec::mode::empty or
			spec.type() == body_spec::mode::head;
		writer_->configure(std::move(spec));
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		auto weak = weak_from_this();
		connection_->write(asio::buffer(*wire),
		[weak, wire, empty, handler = std::move(handler)]
		(error_code error, size_t) mutable {
			if( empty )
				if( auto self = weak.lock() )
					if( not self->client_ ) self->response_finished(error);
			std::move(handler)(error);
		});
	}

	void inform_sync(response_head head, error_code &error) noexcept override
	{
		auto wire = serialize_response(head, body_spec::empty(), version_, false);
		if( not wire )
		{
			error = wire.error();
			return;
		}
		(void)connection_->write_sync(asio::buffer(*wire), error);
	}

	void inform(response_head head, operation_handler handler) override
	{
		auto wire_value = serialize_response(
			head, body_spec::empty(), version_, false);
		if( not wire_value )
		{
			std::move(handler)(wire_value.error());
			return;
		}
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		connection_->write(asio::buffer(*wire),
		[wire, handler = std::move(handler)]
		(error_code error, size_t) mutable { std::move(handler)(error); });
	}

	void cancel() noexcept override {
		if( released_ and reusable_ ) return;
		release(false);
	}

	[[nodiscard]] bool reusable() const noexcept override {
		return released_ and reusable_;
	}

private:
	void read_response(response_handler handler)
	{
		auto self = shared_from_this();
		read_head(connection_,
			limits_.max_header_list_size + limits_.max_start_line_size,
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, std::string wire) mutable {
			if( error )
			{
				std::move(handler)(error, response_head{});
				return;
			}
			auto parsed = parse_response(wire, self->limits_, self->head_request_);
			if( not parsed )
			{
				std::move(handler)(parsed.error(), response_head{});
				return;
			}
			if( parsed->head.status.code() < 200 )
			{
				self->read_response(std::move(handler));
				return;
			}
			self->response_ = parsed->head;
			self->response_protocol_ = parsed->version;
			self->connection_->transport->set_read_timeout(
				self->timeouts_.body_idle);
			self->keep_alive_ = reusable_response(self->response_,
				parsed->version);
			self->response_ready_ = true;
			self->reader_->configure(parsed->framing, parsed->content_length);
			if( parsed->framing == body_framing::none and self->client_ )
			{
				self->inbound_->set_finished();
				self->body_finished({});
			}
			std::move(handler)(error_code{}, self->response_);
		});
	}

	void body_finished(error_code error)
	{
		if( not client_ ) return;
		release(not error and keep_alive_ and
			reader_->framing() != body_framing::until_eof);
	}

	void response_finished(error_code error)
	{
		if( client_ or released_ ) return;
		release(not error and keep_alive_ and reader_->request_complete());
	}

	void release(bool reusable)
	{
		if( released_ ) return;
		released_ = true;
		reusable_ = reusable;
		if( not reusable ) connection_->cancel();
		try {
			if( release_handler_ ) release_handler_(reusable);
		}
		catch(...) {
		}
	}

	std::shared_ptr<connection> connection_;
	protocol version_;
	protocol response_protocol_;
	message_limits limits_;
	timeout_policy timeouts_;
	bool client_;
	bool head_request_;
	request_head request_ {};
	std::shared_ptr<reader_adapter> reader_;
	std::shared_ptr<writer_adapter> writer_;
	std::shared_ptr<body_reader_state> inbound_;
	std::shared_ptr<body_writer_state> outbound_;
	response_head response_ {};
	bool response_ready_ = false;
	bool keep_alive_ = true;
	bool released_ = false;
	bool reusable_ = false;
	std::function<void(bool)> release_handler_;
};

class h1_client_session final : public client_session,
	public std::enable_shared_from_this<h1_client_session>
{
public:
	h1_client_session(asio::any_io_executor executor, protocol version,
		message_limits limits, timeout_policy timeouts = {},
		bool absolute_form = false) :
		executor_(std::move(executor)), version_(version),
		limits_(std::move(limits)), timeouts_(timeouts),
		absolute_form_(absolute_form), connection_(
			std::make_shared<connection>(executor_)), resolver_(executor_)
	{
		connection_->transport->set_read_timeout(
			bounded_timeout(timeouts_.body_idle, timeouts_));
		connection_->transport->set_write_timeout(
			bounded_timeout(timeouts_.body_idle, timeouts_));
	}

	h1_client_session(asio::any_io_executor executor, protocol version,
		message_limits limits,
		std::shared_ptr<::riwo::http_exp::connection> transport,
		timeout_policy timeouts = {}, bool absolute_form = false) :
		executor_(std::move(executor)), version_(version),
		limits_(std::move(limits)), timeouts_(timeouts),
		absolute_form_(absolute_form),
		connection_(std::make_shared<connection>(std::move(transport))),
		resolver_(executor_), preconnected_(true)
	{
		connection_->transport->set_read_timeout(
			bounded_timeout(timeouts_.body_idle, timeouts_));
		connection_->transport->set_write_timeout(
			bounded_timeout(timeouts_.body_idle, timeouts_));
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
	}
	[[nodiscard]] size_t available_stream_capacity() const noexcept override {
		return not in_use_ and not draining_ and not closed() ? 1 : 0;
	}
	[[nodiscard]] bool draining() const noexcept override { return draining_; }
	[[nodiscard]] bool closed() const noexcept override {
		return not connection_->is_open();
	}
	[[nodiscard]] bool try_reserve_stream() noexcept override
	{
		if( in_use_ or draining_ or closed() ) return false;
		in_use_ = true;
		reserved_ = true;
		return true;
	}
	void release_stream(bool reusable) noexcept override
	{
		if( not in_use_ ) return;
		in_use_ = false;
		reserved_ = false;
		if( not reusable or draining_ )
		{
			draining_ = true;
			connection_->cancel();
		}
		try {
			if( release_handler_ ) release_handler_(reusable);
		}
		catch(...) {
		}
	}
	void set_release_handler(std::function<void(bool)> handler) override {
		release_handler_ = std::move(handler);
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(request_head head,
		body_spec spec, error_code &error) noexcept override
	{
		error.clear();
		if( not begin_open(error) ) return {};
		auto wire = serialize_request(head, spec, version_, absolute_form_);
		if( not wire )
		{
			error = wire.error();
			release_stream(false);
			return {};
		}
		try {
			if( not preconnected_ )
			{
				const auto port = static_cast<uint16_t>(
					head.url.port() == 0 ? 80 : head.url.port());
				connection_->connect_sync(head.url.host(), port, error);
				if( error )
				{
					release_stream(false);
					return {};
				}
			}
			(void)connection_->write_sync(asio::buffer(*wire), error);
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
		auto wire_value = serialize_request(head, spec, version_, absolute_form_);
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
				if( session->preconnected_ )
				{
					write_head();
					return;
				}
				auto self = shared_from_this();
				const auto port = static_cast<uint16_t>(
					head.url.port() == 0 ? 80 : head.url.port());
				session->connection_->connect_async(head.url.host(), port,
				[self = std::move(self)](error_code error) mutable {
					if( error ) return self->finish(error);
					self->write_head();
				});
			}

			void write_head()
			{
				auto self = shared_from_this();
				session->connection_->write(asio::buffer(*wire),
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
		draining_ = true;
		if( not in_use_ ) connection_->cancel();
		std::move(handler)(error_code{});
	}

private:
	[[nodiscard]] bool begin_open(error_code &error) noexcept
	{
		if( closed() or draining_ )
		{
			error = make_error_code(errc::operation_not_permitted);
			return false;
		}
		if( in_use_ and not reserved_ )
		{
			error = make_error_code(errc::operation_in_progress);
			return false;
		}
		in_use_ = true;
		reserved_ = false;
		return true;
	}

	[[nodiscard]] std::shared_ptr<h1_stream> make_stream(request_head head,
		body_spec spec)
	{
		const bool head_request = head.method.known() == method::head;
		auto result = std::make_shared<h1_stream>(connection_, version_,
			std::move(spec), body_framing::none, 0, limits_, true,
			head_request, std::move(head), timeouts_);
		auto weak = weak_from_this();
		result->set_release_handler(
		[weak](bool reusable) {
			if( auto self = weak.lock() )
				self->release_stream(reusable);
		});
		return result;
	}

	executor_type executor_;
	protocol version_;
	message_limits limits_;
	timeout_policy timeouts_;
	bool absolute_form_ = false;
	std::shared_ptr<connection> connection_;
	asio::ip::tcp::resolver resolver_;
	bool preconnected_ = false;
	bool in_use_ = false;
	bool reserved_ = false;
	bool draining_ = false;
	std::function<void(bool)> release_handler_;
};

class h1_server_session final : public server_session,
	public std::enable_shared_from_this<h1_server_session>
{
public:
	h1_server_session(asio::ip::tcp::socket socket, message_limits limits,
		timeout_policy timeouts = {}) :
		executor_(socket.get_executor()), limits_(std::move(limits)),
		timeouts_(timeouts),
		connection_(std::make_shared<connection>(std::move(socket)))
	{
	}

	h1_server_session(std::shared_ptr<::riwo::http_exp::connection> transport,
		protocol version, message_limits limits, timeout_policy timeouts = {}) :
		executor_(transport->get_executor()), limits_(std::move(limits)),
		timeouts_(timeouts),
		connection_(std::make_shared<connection>(std::move(transport))),
		version_(version)
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
	}
	[[nodiscard]] bool draining() const noexcept override {
		return draining_;
	}
	[[nodiscard]] bool closed() const noexcept override {
		return not connection_->is_open();
	}
	[[nodiscard]] bool try_reserve_stream() noexcept {
		if( in_use_ or draining_ or closed() ) return false;
		in_use_ = true;
		return true;
	}
	void release_stream(bool reusable) noexcept
	{
		if( not in_use_ ) return;
		in_use_ = false;
		if( not reusable ) draining_ = true;
		try {
			if( release_handler_ ) release_handler_(reusable);
		}
		catch(...) {
		}
	}
	void set_release_handler(std::function<void(bool)> handler) {
		release_handler_ = std::move(handler);
	}

	void accept(accept_handler handler) override
	{
		if( in_use_ )
		{
			std::move(handler)(make_error_code(errc::operation_in_progress),
				std::shared_ptr<stream>{});
			return;
		}
		if( draining_ or closed() )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				std::shared_ptr<stream>{});
			return;
		}
		in_use_ = true;
		connection_->transport->set_read_timeout(timeouts_.header);
		connection_->transport->set_write_timeout(timeouts_.body_idle);
		auto self = shared_from_this();
		read_head(connection_,
			limits_.max_header_list_size + limits_.max_start_line_size,
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, std::string wire) mutable {
			if( error )
			{
				self->release_stream(false);
				std::move(handler)(error, std::shared_ptr<stream>{});
				return;
			}
			auto parsed = parse_request(wire, self->limits_);
			if( not parsed )
			{
				const auto code = static_cast<std::uint16_t>(
					parsed.error() ==
						make_error_code(errc::header_limit_exceeded) ? 431 : 400);
				error_code ignored;
				(void)self->connection_->write_sync(asio::buffer(
					error_response(code, code == 431 ?
						"Request Header Fields Too Large" : "Bad Request")),
					ignored);
				self->release_stream(false);
				std::move(handler)(parsed.error(), std::shared_ptr<stream>{});
				return;
			}
			self->version_ = parsed->version;
			if( parsed->head.headers.find_first("Expect") )
			{
				if( has_unsupported_expectation(parsed->head.headers) )
				{
					error_code ignored;
					(void)self->connection_->write_sync(asio::buffer(
						error_response(417, "Expectation Failed")), ignored);
					self->release_stream(false);
					std::move(handler)(make_error_code(errc::invalid_field_value),
						std::shared_ptr<stream>{});
					return;
				}
				if( parsed->version == protocol::v1_1 )
				{
					error_code expect_error;
					(void)self->connection_->write_sync(asio::buffer(
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
			auto result = std::make_shared<h1_stream>(self->connection_,
				parsed->version, body_spec::empty(), parsed->framing,
				parsed->content_length, self->limits_, false,
				parsed->head.method.known() == method::head,
				std::move(parsed->head), self->timeouts_);
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
		draining_ = true;
		connection_->cancel();
		std::move(handler)(error_code{});
	}

private:
	executor_type executor_;
	message_limits limits_;
	timeout_policy timeouts_;
	std::shared_ptr<connection> connection_;
	protocol version_ = protocol::v1_1;
	bool in_use_ = false;
	bool draining_ = false;
	std::function<void(bool)> release_handler_;
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

} //namespace riwo::http_exp::detail::h1

namespace riwo::http_exp::detail
{

class h1_server_loop final :
	public std::enable_shared_from_this<h1_server_loop>
{
public:
	std::shared_ptr<h1::h1_server_session> session;
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
	auto session = std::make_shared<h1::h1_server_session>(
		std::move(socket), std::move(limits), timeouts);
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
