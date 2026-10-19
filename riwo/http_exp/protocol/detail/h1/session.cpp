// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include "wire.h"
#include <riwo/http_exp/server/detail/session.h>

namespace riwo::http_exp::detail::h1 { namespace
{

struct connection final
{
	explicit connection(asio::any_io_executor executor) : socket(executor) {}
	explicit connection(asio::ip::tcp::socket value) : socket(std::move(value)) {}

	void cancel() noexcept
	{
		if( closed ) return;
		closed = true;
		error_code ignored;
		socket.cancel(ignored);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		socket.close(ignored);
	}

	asio::ip::tcp::socket socket;
	std::string pending;
	bool closed = false;
};

[[nodiscard]] bool is_eof(const error_code &error) noexcept {
	return error == asio::error::make_error_code(asio::error::eof);
}

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
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
			auto size = value.socket.read_some(asio::buffer(buffer), error);
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
		connection_->socket.async_read_some(asio::buffer(buffer_),
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
		body_framing framing_value, uint64_t length, message_limits limits) :
		connection_(std::move(connection_value)), framing_(framing_value),
		remaining_(length), limits_(std::move(limits))
	{
	}

	void configure(body_framing framing, uint64_t length) noexcept
	{
		framing_ = framing;
		remaining_ = length;
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		try {
			return read_sync(buffer, error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return 0;
		}
	}

	void read_some(mutable_buffer buffer, read_handler handler) override;

	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override
	{
		error.clear();
		if( !ended_ ) error = make_error_code(errc::operation_not_permitted);
		return trailers_;
	}

	void trailers(trailers_handler handler) override
	{
		if( !ended_ )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				fields{});
			return;
		}
		std::move(handler)(error_code{}, trailers_);
	}

	void cancel() noexcept override { connection_->cancel(); }

private:
	[[nodiscard]] bool account(size_t size, error_code &error) noexcept
	{
		if( limits_.max_body_size &&
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
		if( !account(size, error) ) return 0;
		return size;
	}

	[[nodiscard]] bool fill_sync(error_code &error)
	{
		std::array<char,8192> data {};
		auto size = connection_->socket.read_some(asio::buffer(data), error);
		if( error ) return false;
		connection_->pending.append(data.data(), size);
		return true;
	}

	[[nodiscard]] bool consume_chunk_crlf_sync(error_code &error)
	{
		while( connection_->pending.size() < 2 )
			if( !fill_sync(error) ) return false;
		if( !connection_->pending.starts_with("\r\n") )
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
				if( !size )
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
			if( !fill_sync(error) ) return false;
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
				if( !fill_sync(error) ) return false;
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
			if( colon == std::string_view::npos || trailers_.size() >=
				limits_.max_field_count )
			{
				error = protocol_error();
				return false;
			}
			auto value = line.substr(colon + 1);
			while( !value.empty() && (value.front() == ' ' || value.front() == '\t') )
				value.remove_prefix(1);
			trailers_.add(std::string(line.substr(0, colon)), std::string(value));
			connection_->pending.erase(0, end + 2);
		}
	}

	[[nodiscard]] size_t read_sync(mutable_buffer buffer, error_code &error)
	{
		if( ended_ || framing_ == body_framing::none )
		{
			ended_ = true;
			error = asio::error::make_error_code(asio::error::eof);
			return 0;
		}
		if( framing_ == body_framing::content_length )
		{
			if( remaining_ == 0 )
			{
				ended_ = true;
				error = asio::error::make_error_code(asio::error::eof);
				return 0;
			}
			auto size = copy_pending(buffer, static_cast<size_t>(
				std::min<uint64_t>(remaining_, SIZE_MAX)), error);
			if( size == 0 && !error )
			{
				auto target = mutable_buffer(buffer.data(), static_cast<size_t>(
					std::min<uint64_t>(buffer.size(), remaining_)));
				size = connection_->socket.read_some(target, error);
				if( !error && !account(size, error) ) return 0;
			}
			remaining_ -= std::min<uint64_t>(remaining_, size);
			return size;
		}
		if( framing_ == body_framing::until_eof )
		{
			auto size = copy_pending(buffer, SIZE_MAX, error);
			if( size != 0 || error ) return size;
			size = connection_->socket.read_some(buffer, error);
			if( is_eof(error) ) ended_ = true;
			else if( !error && !account(size, error) ) return 0;
			return size;
		}
		for(;;)
		{
			if( reading_trailers_ )
			{
				if( !read_trailers_sync(error) ) return 0;
				error = asio::error::make_error_code(asio::error::eof);
				return 0;
			}
			if( need_chunk_crlf_ && !consume_chunk_crlf_sync(error) ) return 0;
			if( chunk_remaining_ == 0 && !read_chunk_line_sync(error) ) return 0;
			if( reading_trailers_ ) continue;
			auto size = copy_pending(buffer, static_cast<size_t>(
				std::min<uint64_t>(chunk_remaining_, SIZE_MAX)), error);
			if( size == 0 && !error )
			{
				auto target = mutable_buffer(buffer.data(), static_cast<size_t>(
					std::min<uint64_t>(buffer.size(), chunk_remaining_)));
				size = connection_->socket.read_some(target, error);
				if( !error && !account(size, error) ) return 0;
			}
			chunk_remaining_ -= std::min<uint64_t>(chunk_remaining_, size);
			if( chunk_remaining_ == 0 ) need_chunk_crlf_ = true;
			return size;
		}
	}

	class async_read_operation;
	friend class async_read_operation;

	std::shared_ptr<connection> connection_;
	body_framing framing_;
	uint64_t remaining_ = 0;
	message_limits limits_;
	fields trailers_;
	uint64_t delivered_ = 0;
	uint64_t chunk_remaining_ = 0;
	bool need_chunk_crlf_ = false;
	bool reading_trailers_ = false;
	bool ended_ = false;
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
		if( owner_->ended_ || owner_->framing_ == body_framing::none )
		{
			owner_->ended_ = true;
			finish(asio::error::make_error_code(asio::error::eof), 0);
			return;
		}
		if( owner_->framing_ == body_framing::content_length )
		{
			if( owner_->remaining_ == 0 )
			{
				owner_->ended_ = true;
				finish(asio::error::make_error_code(asio::error::eof), 0);
				return;
			}
			auto size = owner_->copy_pending(target_, static_cast<size_t>(
				std::min<uint64_t>(owner_->remaining_, SIZE_MAX)), error);
			if( size != 0 || error )
			{
				owner_->remaining_ -= std::min<uint64_t>(owner_->remaining_, size);
				finish(error, size);
				return;
			}
			direct_read(static_cast<size_t>(std::min<uint64_t>(
				target_.size(), owner_->remaining_)), true);
			return;
		}
		if( owner_->framing_ == body_framing::until_eof )
		{
			auto size = owner_->copy_pending(target_, SIZE_MAX, error);
			if( size != 0 || error )
			{
				finish(error, size);
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
		owner_->connection_->socket.async_read_some(
			mutable_buffer(target_.data(), size),
		[self = std::move(self), fixed](error_code error, size_t count) mutable {
			if( is_eof(error) && !fixed ) self->owner_->ended_ = true;
			if( !error && !self->owner_->account(count, error) ) count = 0;
			if( fixed ) self->owner_->remaining_ -=
				std::min<uint64_t>(self->owner_->remaining_, count);
			self->finish(error, count);
		});
	}

	void fill()
	{
		auto self = shared_from_this();
		owner_->connection_->socket.async_read_some(asio::buffer(buffer_),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( error )
			{
				self->finish(error, 0);
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
							finish(make_error_code(errc::header_limit_exceeded), 0);
							return;
						}
						fill();
						return;
					}
					if( end == 0 )
					{
						owner_->connection_->pending.erase(0, 2);
						owner_->ended_ = true;
						finish(asio::error::make_error_code(asio::error::eof), 0);
						return;
					}
					auto line = std::string_view(owner_->connection_->pending)
						.substr(0, end);
					auto colon = line.find(':');
					if( colon == std::string_view::npos || owner_->trailers_.size() >=
						owner_->limits_.max_field_count )
					{
						finish(protocol_error(), 0);
						return;
					}
					auto value = line.substr(colon + 1);
					while( !value.empty() &&
						(value.front() == ' ' || value.front() == '\t') )
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
					if( !owner_->connection_->pending.starts_with("\r\n") )
					{
						finish(protocol_error(), 0);
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
							finish(make_error_code(errc::header_limit_exceeded), 0);
							return;
						}
						fill();
						return;
					}
					auto line = std::string_view(owner_->connection_->pending)
						.substr(0, end);
					auto chunk = parse_chunk_size(line);
					if( !chunk )
					{
						finish(chunk.error(), 0);
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
				if( size != 0 || error )
				{
					owner_->chunk_remaining_ -=
						std::min<uint64_t>(owner_->chunk_remaining_, size);
					if( owner_->chunk_remaining_ == 0 )
						owner_->need_chunk_crlf_ = true;
					finish(error, size);
					return;
				}
				direct_chunk_read();
				return;
			}
		}
		catch(...) {
			finish(exception_error(std::current_exception()), 0);
		}
	}

	void direct_chunk_read()
	{
		auto count = static_cast<size_t>(std::min<uint64_t>(
			target_.size(), owner_->chunk_remaining_));
		auto self = shared_from_this();
		owner_->connection_->socket.async_read_some(
			mutable_buffer(target_.data(), count),
		[self = std::move(self)](error_code error, size_t size) mutable {
			if( !error && !self->owner_->account(size, error) ) size = 0;
			self->owner_->chunk_remaining_ -=
				std::min<uint64_t>(self->owner_->chunk_remaining_, size);
			if( self->owner_->chunk_remaining_ == 0 )
				self->owner_->need_chunk_crlf_ = true;
			self->finish(error, size);
		});
	}

	void finish(error_code error, size_t size) {
		std::move(handler_)(error, size);
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
	explicit writer_adapter(std::shared_ptr<connection> connection_value) :
		connection_(std::move(connection_value))
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
			if( chunked_ )
			{
				auto wire = serialize_chunk(buffer);
				asio::write(connection_->socket, asio::buffer(wire), error);
			}
			else
				asio::write(connection_->socket, buffer, error);
			return error ? 0 : buffer.size();
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return 0;
		}
	}

	void write(const_buffer buffer, write_handler handler) override
	{
		auto wire = std::make_shared<std::string>();
		if( chunked_ ) *wire = serialize_chunk(buffer);
		else wire->assign(static_cast<const char*>(buffer.data()), buffer.size());
		const auto payload_size = buffer.size();
		asio::async_write(connection_->socket, asio::buffer(*wire),
		[wire, payload_size,
		 handler = std::move(handler)](error_code error, size_t) mutable {
			std::move(handler)(error, error ? 0 : payload_size);
		});
	}

	void finish_sync(fields trailers, error_code &error) noexcept override
	{
		error.clear();
		if( !chunked_ ) return;
		try {
			auto wire = serialize_last_chunk(trailers);
			asio::write(connection_->socket, asio::buffer(wire), error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
	}

	void finish(fields trailers, finish_handler handler) override
	{
		if( !chunked_ )
		{
			std::move(handler)(error_code{});
			return;
		}
		auto wire = std::make_shared<std::string>(
			serialize_last_chunk(trailers));
		asio::async_write(connection_->socket, asio::buffer(*wire),
		[wire, handler = std::move(handler)]
		(error_code error, size_t) mutable {
			std::move(handler)(error);
		});
	}

	void cancel() noexcept override { connection_->cancel(); }

private:
	std::shared_ptr<connection> connection_;
	bool chunked_ = false;
};

class h1_stream final : public stream,
	public std::enable_shared_from_this<h1_stream>
{
public:
	h1_stream(std::shared_ptr<connection> connection_value, protocol version,
		body_spec outbound_spec, body_framing inbound_framing,
		uint64_t inbound_length, message_limits limits, bool client,
		bool head_request = false, request_head request_value = {}) :
		connection_(std::move(connection_value)),
		version_(version), limits_(std::move(limits)), client_(client),
		head_request_(head_request), request_(std::move(request_value)),
		reader_(std::make_shared<reader_adapter>(
			connection_, inbound_framing, inbound_length, limits_)),
		writer_(std::make_shared<writer_adapter>(connection_)),
		inbound_(make_body_reader_state(reader_, !client_,
			limits_.max_discard_body)),
		outbound_(make_body_writer_state(writer_, outbound_spec, client_))
	{
		writer_->configure(std::move(outbound_spec));
	}

	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
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
		if( !client_ || response_ready_ )
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
			if( !parsed )
			{
				error = parsed.error();
				return {};
			}
			if( parsed->head.status.code() < 200 ) continue;
			response_ = parsed->head;
			response_ready_ = true;
			reader_->configure(parsed->framing, parsed->content_length);
			return response_;
		}
	}

	void wait_response(response_handler handler) override
	{
		if( !client_ )
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
		auto wire = serialize_response(head, spec, version_);
		if( !wire )
		{
			error = wire.error();
			return;
		}
		writer_->configure(std::move(spec));
		asio::write(connection_->socket, asio::buffer(*wire), error);
	}

	void respond(response_head head, body_spec spec,
		operation_handler handler) override
	{
		if( client_ )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted));
			return;
		}
		auto wire_value = serialize_response(head, spec, version_);
		if( !wire_value )
		{
			std::move(handler)(wire_value.error());
			return;
		}
		writer_->configure(std::move(spec));
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		asio::async_write(connection_->socket, asio::buffer(*wire),
		[wire, handler = std::move(handler)]
		(error_code error, size_t) mutable { std::move(handler)(error); });
	}

	void inform_sync(response_head head, error_code &error) noexcept override
	{
		auto wire = serialize_response(head, body_spec::empty(), version_, false);
		if( !wire )
		{
			error = wire.error();
			return;
		}
		asio::write(connection_->socket, asio::buffer(*wire), error);
	}

	void inform(response_head head, operation_handler handler) override
	{
		auto wire_value = serialize_response(
			head, body_spec::empty(), version_, false);
		if( !wire_value )
		{
			std::move(handler)(wire_value.error());
			return;
		}
		auto wire = std::make_shared<std::string>(std::move(*wire_value));
		asio::async_write(connection_->socket, asio::buffer(*wire),
		[wire, handler = std::move(handler)]
		(error_code error, size_t) mutable { std::move(handler)(error); });
	}

	void cancel() noexcept override { connection_->cancel(); }

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
			if( !parsed )
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
			self->response_ready_ = true;
			self->reader_->configure(parsed->framing, parsed->content_length);
			std::move(handler)(error_code{}, self->response_);
		});
	}

	std::shared_ptr<connection> connection_;
	protocol version_;
	message_limits limits_;
	bool client_;
	bool head_request_;
	request_head request_ {};
	std::shared_ptr<reader_adapter> reader_;
	std::shared_ptr<writer_adapter> writer_;
	std::shared_ptr<body_reader_state> inbound_;
	std::shared_ptr<body_writer_state> outbound_;
	response_head response_ {};
	bool response_ready_ = false;
};

class h1_client_session final : public client_session,
	public std::enable_shared_from_this<h1_client_session>
{
public:
	h1_client_session(asio::any_io_executor executor, protocol version,
		message_limits limits) : executor_(std::move(executor)), version_(version),
		limits_(std::move(limits)), connection_(
			std::make_shared<connection>(executor_)), resolver_(executor_)
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
	}
	[[nodiscard]] size_t available_stream_capacity() const noexcept override {
		return opened_ ? 0 : 1;
	}
	[[nodiscard]] bool draining() const noexcept override { return opened_; }
	[[nodiscard]] bool closed() const noexcept override {
		return connection_->closed;
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(request_head head,
		body_spec spec, error_code &error) noexcept override
	{
		error.clear();
		if( opened_ )
		{
			error = make_error_code(errc::operation_in_progress);
			return {};
		}
		opened_ = true;
		auto wire = serialize_request(head, spec, version_);
		if( !wire )
		{
			error = wire.error();
			return {};
		}
		try {
			const auto port = head.url.port() == 0 ? 80 : head.url.port();
			auto endpoints = resolver_.resolve(std::string(head.url.host()),
				std::to_string(port), error);
			if( error ) return {};
			asio::connect(connection_->socket, endpoints, error);
			if( error ) return {};
			asio::write(connection_->socket, asio::buffer(*wire), error);
			if( error ) return {};
			return std::make_shared<h1_stream>(connection_, version_, spec,
				body_framing::none, 0, limits_, true,
				head.method.known() == method::head);
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return {};
		}
	}

	void open(request_head head, body_spec spec, open_handler handler) override
	{
		if( opened_ )
		{
			std::move(handler)(make_error_code(errc::operation_in_progress),
				std::shared_ptr<stream>{});
			return;
		}
		opened_ = true;
		auto wire_value = serialize_request(head, spec, version_);
		if( !wire_value )
		{
			std::move(handler)(wire_value.error(), std::shared_ptr<stream>{});
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
				const auto port = head.url.port() == 0 ? 80 : head.url.port();
				auto self = shared_from_this();
				session->resolver_.async_resolve(std::string(head.url.host()),
					std::to_string(port),
				[self = std::move(self)](error_code error,
					asio::ip::tcp::resolver::results_type endpoints) mutable {
					if( error ) return self->finish(error);
					self->connect(std::move(endpoints));
				});
			}

			void connect(asio::ip::tcp::resolver::results_type endpoints)
			{
				auto self = shared_from_this();
				asio::async_connect(session->connection_->socket, endpoints,
				[self, endpoints]
				(error_code error, const asio::ip::tcp::endpoint&) mutable {
					if( error ) return self->finish(error);
					self->write_head();
				});
			}

			void write_head()
			{
				auto self = shared_from_this();
				asio::async_write(session->connection_->socket, asio::buffer(*wire),
				[self = std::move(self)](error_code error, size_t) mutable {
					self->finish(error);
				});
			}

			void finish(error_code error)
			{
				if( error )
				{
					std::move(handler)(error, std::shared_ptr<stream>{});
					return;
				}
				auto result = std::make_shared<h1_stream>(session->connection_,
					session->version_, spec, body_framing::none, 0,
					session->limits_, true, head.method.known() == method::head);
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
		connection_->cancel();
		std::move(handler)(error_code{});
	}

private:
	executor_type executor_;
	protocol version_;
	message_limits limits_;
	std::shared_ptr<connection> connection_;
	asio::ip::tcp::resolver resolver_;
	bool opened_ = false;
};

class h1_server_session final : public server_session,
	public std::enable_shared_from_this<h1_server_session>
{
public:
	h1_server_session(asio::ip::tcp::socket socket, message_limits limits) :
		executor_(socket.get_executor()), limits_(std::move(limits)),
		connection_(std::make_shared<connection>(std::move(socket)))
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
	}
	[[nodiscard]] bool draining() const noexcept override { return accepted_; }
	[[nodiscard]] bool closed() const noexcept override {
		return connection_->closed;
	}

	void accept(accept_handler handler) override
	{
		if( accepted_ )
		{
			std::move(handler)(make_error_code(errc::operation_in_progress),
				std::shared_ptr<stream>{});
			return;
		}
		accepted_ = true;
		auto self = shared_from_this();
		read_head(connection_,
			limits_.max_header_list_size + limits_.max_start_line_size,
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, std::string wire) mutable {
			if( error )
			{
				std::move(handler)(error, std::shared_ptr<stream>{});
				return;
			}
			auto parsed = parse_request(wire, self->limits_);
			if( !parsed )
			{
				std::move(handler)(parsed.error(), std::shared_ptr<stream>{});
				return;
			}
			self->version_ = parsed->version;
			auto result = std::make_shared<h1_stream>(self->connection_,
				parsed->version, body_spec::empty(), parsed->framing,
				parsed->content_length, self->limits_, false, false,
				std::move(parsed->head));
			std::move(handler)(error_code{}, std::move(result));
		});
	}

	void graceful_shutdown(shutdown_handler handler) override
	{
		connection_->cancel();
		std::move(handler)(error_code{});
	}

private:
	executor_type executor_;
	message_limits limits_;
	std::shared_ptr<connection> connection_;
	protocol version_ = protocol::v1_1;
	bool accepted_ = false;
};

} //namespace

std::shared_ptr<client_session> make_client_session(
	asio::any_io_executor executor, protocol version, message_limits limits)
{
	return std::make_shared<h1_client_session>(
		std::move(executor), version, std::move(limits));
}

std::shared_ptr<server_session> make_server_session(
	asio::ip::tcp::socket socket, message_limits limits)
{
	return std::make_shared<h1_server_session>(
		std::move(socket), std::move(limits));
}

} //namespace riwo::http_exp::detail::h1

namespace riwo::http_exp::detail
{

void accept_h1(asio::ip::tcp::socket socket, message_limits limits,
	server_accept_handler handler)
{
	auto session = h1::make_server_session(
		std::move(socket), std::move(limits));
	session->accept(
	[session, handler = std::move(handler)]
	(error_code error, std::shared_ptr<stream> value) mutable {
		auto request = value ? value->request() : request_head{};
		std::move(handler)(error, std::move(value), std::move(request));
	});
}

} //namespace riwo::http_exp::detail
