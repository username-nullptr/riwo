// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

#include "nghttp2_compat.h"
#include <charconv>
#include <deque>

namespace riwo::http_exp::detail::h2 { namespace
{

class server_session_impl;
class server_stream_impl;

[[nodiscard]] error_code aborted() noexcept {
	return asio::error::make_error_code(asio::error::operation_aborted);
}

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
}

[[nodiscard]] std::string lower(std::string_view value)
{
	std::string result(value);
	std::ranges::transform(result, result.begin(), [](unsigned char byte) {
		return static_cast<char>(std::tolower(byte));
	});
	return result;
}

[[nodiscard]] bool forbidden_header(std::string_view name) noexcept
{
	static constexpr std::array<std::string_view,5> values {
		"connection", "keep-alive", "proxy-connection",
		"transfer-encoding", "upgrade"
	};
	return std::ranges::find(values, name) != values.end();
}

struct nv_block
{
	std::vector<std::string> storage;
	std::vector<nghttp2_nv> values;
};

void add_nv(nv_block &block, std::string name, std::string value)
{
	block.storage.push_back(std::move(name));
	block.storage.push_back(std::move(value));
	auto &stored_name = block.storage[block.storage.size() - 2];
	auto &stored_value = block.storage.back();
	block.values.push_back({
		reinterpret_cast<uint8_t *>(stored_name.data()),
		reinterpret_cast<uint8_t *>(stored_value.data()),
		stored_name.size(), stored_value.size(), NGHTTP2_NV_FLAG_NONE
	});
}

[[nodiscard]] sys_expected<nv_block> response_headers(
	const response_head &head, const body_spec &spec, size_t limit) noexcept
{
	try {
		const auto code = head.status.code();
		const bool body_allowed = code >= 200 and code != 204 and code != 304;
		if( not body_allowed and spec.type() != body_spec::mode::empty )
			return sys_unexpected(make_error_code(errc::body_not_allowed));
		nv_block result;
		result.storage.reserve((head.headers.size() + 2) * 2);
		result.values.reserve(head.headers.size() + 2);
		add_nv(result, ":status", std::to_string(code));
		size_t decoded_size = 32 + std::string_view(":status").size() + 3;
		if( decoded_size > limit )
			return sys_unexpected(
				make_error_code(errc::header_limit_exceeded));
		for( const auto &field : head.headers )
		{
			auto name = lower(field.name);
			if( name.starts_with(':') or forbidden_header(name) )
				return sys_unexpected(make_error_code(errc::invalid_field_name));
			if( name == "content-length" or name == "trailer" ) continue;
			if( name == "te" and lower(field.value) != "trailers" )
				return sys_unexpected(make_error_code(errc::invalid_field_value));
			if( name.size() + field.value.size() + 32 >
				limit - std::min(limit, decoded_size) )
				return sys_unexpected(
					make_error_code(errc::header_limit_exceeded));
			decoded_size += name.size() + field.value.size() + 32;
			add_nv(result, std::move(name), field.value);
		}
		if( body_allowed and (spec.type() == body_spec::mode::known_length or
			spec.type() == body_spec::mode::head) )
		{
			auto length = std::to_string(*spec.length());
			if( std::string_view("content-length").size() + length.size() + 32 >
				limit - std::min(limit, decoded_size) )
				return sys_unexpected(
					make_error_code(errc::header_limit_exceeded));
			add_nv(result, "content-length", std::move(length));
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] sys_expected<nv_block> trailer_headers(
	const fields &trailers, size_t limit) noexcept
{
	try {
		nv_block result;
		result.storage.reserve(trailers.size() * 2);
		result.values.reserve(trailers.size());
		size_t decoded_size = 0;
		for( const auto &field : trailers )
		{
			auto name = lower(field.name);
			if( name.starts_with(':') or forbidden_header(name) or name == "te" or
				name == "content-length" or name == "host" or name == "trailer" )
				return sys_unexpected(make_error_code(errc::invalid_trailer));
			if( name.size() + field.value.size() + 32 >
				limit - std::min(limit, decoded_size) )
				return sys_unexpected(
					make_error_code(errc::header_limit_exceeded));
			decoded_size += name.size() + field.value.size() + 32;
			add_nv(result, std::move(name), field.value);
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

class server_reader_adapter final : public body_reader_adapter
{
public:
	explicit server_reader_adapter(std::weak_ptr<server_stream_impl> owner) :
		owner_(std::move(owner)) {}

	[[nodiscard]] body_read_result read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override;
	void read_some(mutable_buffer buffer, read_handler handler) override;
	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override;
	void trailers(trailers_handler handler) override;
	void cancel() noexcept override;

private:
	std::weak_ptr<server_stream_impl> owner_;
};

class server_writer_adapter final : public body_writer_adapter
{
public:
	explicit server_writer_adapter(std::weak_ptr<server_stream_impl> owner) :
		owner_(std::move(owner)) {}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override;
	void write(const_buffer buffer, write_handler handler) override;
	void finish_sync(fields trailers, error_code &error) noexcept override;
	void finish(fields trailers, finish_handler handler) override;
	void cancel() noexcept override;

private:
	std::weak_ptr<server_stream_impl> owner_;
};

class server_stream_impl final : public stream,
	public std::enable_shared_from_this<server_stream_impl>
{
public:
	server_stream_impl(std::shared_ptr<server_session_impl> session,
		int32_t id) : session_(std::move(session)), id_(id) {}
	~server_stream_impl() override;

	void initialize();
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] stream_capabilities capabilities() const noexcept override {
		return {.trailers = true, .full_duplex = true};
	}
	[[nodiscard]] request_head request() const override { return request_; }
	[[nodiscard]] std::shared_ptr<body_reader_state>
	inbound_body() const noexcept override { return inbound_; }
	[[nodiscard]] std::shared_ptr<body_writer_state>
	outbound_body() const noexcept override { return outbound_; }
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override;
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override;
	void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept override;
	void respond(response_head head, body_spec spec,
		operation_handler handler) override;
	void inform_sync(response_head head, error_code &error) noexcept override;
	void inform(response_head head, operation_handler handler) override;
	[[nodiscard]] bool reusable() const noexcept override;
	void cancel() noexcept override;

	std::shared_ptr<server_session_impl> session_;
	int32_t id_;
	request_head request_;
	std::string method_;
	std::string scheme_;
	std::string authority_;
	std::string path_;
	bool have_method_ = false;
	bool have_scheme_ = false;
	bool have_authority_ = false;
	bool have_path_ = false;
	bool collecting_trailers_ = false;
	bool request_ready_ = false;
	bool invalid_headers_ = false;
	size_t header_bytes_ = 0;
	size_t header_count_ = 0;
	optional<uint64_t> content_length_;

	std::shared_ptr<server_reader_adapter> reader_adapter_;
	std::shared_ptr<server_writer_adapter> writer_adapter_;
	std::shared_ptr<body_reader_state> inbound_;
	std::shared_ptr<body_writer_state> outbound_;

	std::deque<std::vector<std::byte>> inbound_chunks_;
	size_t inbound_offset_ = 0;
	size_t inbound_size_ = 0;
	uint64_t inbound_total_ = 0;
	bool remote_end_ = false;
	fields inbound_trailers_;
	optional<mutable_buffer> pending_read_buffer_;
	body_reader_adapter::read_handler pending_read_handler_;

	std::deque<std::vector<std::byte>> outbound_chunks_;
	size_t outbound_offset_ = 0;
	size_t outbound_size_ = 0;
	bool response_started_ = false;
	bool outbound_finished_ = false;
	bool trailers_submitted_ = false;
	fields outbound_trailers_;
	struct pending_write
	{
		const_buffer buffer;
		body_writer_adapter::write_handler handler;
	};
	optional<pending_write> pending_write_;

	error_code terminal_error_;
	bool closed_ = false;
	bool cancelled_ = false;
};

class server_session_impl final : public server_session,
	public std::enable_shared_from_this<server_session_impl>
{
public:
	server_session_impl(
		std::shared_ptr<::riwo::http_exp::connection> transport,
		server_config config, std::string initial_input) :
		executor_(transport->get_executor()), strand_(asio::make_strand(executor_)),
		transport_(std::move(transport)), config_(std::move(config)),
		initial_input_(std::move(initial_input))
	{
		error_code error;
		remote_ = transport_->remote_endpoint(error);
		error.clear();
		local_ = transport_->local_endpoint(error);
	}

	~server_session_impl() override
	{
		if( native_ ) nghttp2_session_del(native_);
		if( callbacks_ ) nghttp2_session_callbacks_del(callbacks_);
		if( option_ ) nghttp2_option_del(option_);
	}

	[[nodiscard]] error_code initialize() noexcept
	{
		if( nghttp2_session_callbacks_new(&callbacks_) != 0 or
			nghttp2_option_new(&option_) != 0 )
			return make_error_code(errc::operation_not_supported);
		nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks_,
			&server_session_impl::on_begin_headers);
		nghttp2_session_callbacks_set_on_header_callback(callbacks_,
			&server_session_impl::on_header);
		nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks_,
			&server_session_impl::on_frame_recv);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks_,
			&server_session_impl::on_data_chunk);
		nghttp2_session_callbacks_set_on_stream_close_callback(callbacks_,
			&server_session_impl::on_stream_close);
		nghttp2_option_set_no_auto_window_update(option_, 1);
		nghttp2_option_set_max_deflate_dynamic_table_size(option_,
			config_.h2.max_dynamic_table_size);
		if( nghttp2_session_server_new2(&native_, callbacks_, this, option_) != 0 )
			return make_error_code(errc::operation_not_supported);

		std::array<nghttp2_settings_entry,5> settings {{
			{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
				config_.h2.initial_stream_window},
			{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
				config_.h2.max_concurrent_streams},
			{NGHTTP2_SETTINGS_MAX_FRAME_SIZE, config_.h2.max_frame_size},
			{NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
				static_cast<uint32_t>(std::min<size_t>(
					config_.h2.max_dynamic_table_size, UINT32_MAX))},
			{NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
				static_cast<uint32_t>(std::min(
					config_.h2.max_header_list_size,
					config_.request_limits.max_header_list_size))}
		}};
		if( nghttp2_submit_settings(native_, NGHTTP2_FLAG_NONE,
			settings.data(), settings.size()) != 0 ) return protocol_error();
		if( config_.h2.initial_connection_window > 65'535 )
		{
			const auto increment = static_cast<int32_t>(
				config_.h2.initial_connection_window - 65'535);
			if( nghttp2_submit_window_update(native_, NGHTTP2_FLAG_NONE,
				0, increment) != 0 ) return protocol_error();
		}
		if( not initial_input_.empty() )
		{
			const auto consumed = nghttp2_api::session_mem_recv(native_,
				reinterpret_cast<const uint8_t *>(initial_input_.data()),
				initial_input_.size());
			if( consumed < 0 or static_cast<size_t>(consumed) !=
				initial_input_.size() ) return protocol_error();
			initial_input_.clear();
		}
		transport_->set_read_timeout(config_.timeout.body_idle);
		transport_->set_write_timeout(config_.timeout.body_idle);
		return {};
	}

	void start()
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self)] {
			self->write_pump();
			self->read_loop();
		});
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] bool draining() const noexcept override { return draining_; }
	[[nodiscard]] bool closed() const noexcept override { return closed_; }

	void accept(accept_handler handler) override
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self),
			handler = std::move(handler)]() mutable {
			if( self->accept_handler_ )
				return std::move(handler)(
					make_error_code(errc::operation_in_progress),
					std::shared_ptr<stream>{});
			if( not self->ready_streams_.empty() )
			{
				auto value = std::move(self->ready_streams_.front());
				self->ready_streams_.pop_front();
				return std::move(handler)(error_code{}, std::move(value));
			}
			if( self->closed_ or self->draining_ )
				return std::move(handler)(aborted(), std::shared_ptr<stream>{});
			self->accept_handler_ = std::move(handler);
		});
	}

	void graceful_shutdown(shutdown_handler handler) override
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self),
			handler = std::move(handler)]() mutable {
			if( self->closed_ )
				return std::move(handler)(error_code{});
			self->draining_ = true;
			(void)nghttp2_submit_goaway(self->native_, NGHTTP2_FLAG_NONE,
				self->last_processed_stream_, NGHTTP2_NO_ERROR, nullptr, 0);
			self->shutdown_handlers_.push_back(std::move(handler));
			if( self->accept_handler_ )
				std::move(self->accept_handler_)(aborted(),
					std::shared_ptr<stream>{});
			self->write_pump();
			self->finish_shutdown_if_idle();
		});
	}

	[[nodiscard]] const server_config &config() const noexcept { return config_; }
	[[nodiscard]] asio::ip::tcp::endpoint remote_endpoint(
		error_code &error) const noexcept { error.clear(); return remote_; }
	[[nodiscard]] asio::ip::tcp::endpoint local_endpoint(
		error_code &error) const noexcept { error.clear(); return local_; }

	[[nodiscard]] body_read_result read_sync(server_stream_impl &value,
		mutable_buffer buffer, error_code &error) noexcept
	{
		error.clear();
		if( value.terminal_error_ ) { error = value.terminal_error_; return {}; }
		if( value.inbound_size_ == 0 and not value.remote_end_ )
		{
			error = make_error_code(errc::operation_in_progress);
			return {};
		}
		auto result = take_body(value, buffer);
		if( result.bytes != 0 ) consume(value, result.bytes);
		return result;
	}

	void read(const std::shared_ptr<server_stream_impl> &value,
		mutable_buffer buffer, body_reader_adapter::read_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value, buffer,
			handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				return std::move(handler)(value->terminal_error_,
					body_read_result{});
			if( value->pending_read_handler_ )
				return std::move(handler)(
					make_error_code(errc::operation_in_progress),
					body_read_result{});
			if( value->inbound_size_ != 0 or value->remote_end_ )
			{
				auto result = self->take_body(*value, buffer);
				if( result.bytes != 0 ) self->consume(*value, result.bytes);
				return std::move(handler)(error_code{}, result);
			}
			value->pending_read_buffer_ = buffer;
			value->pending_read_handler_ = std::move(handler);
		});
	}

	[[nodiscard]] fields trailers_sync(server_stream_impl &value,
		error_code &error) noexcept
	{
		error.clear();
		if( not value.remote_end_ )
			error = make_error_code(errc::operation_not_permitted);
		return error ? fields{} : value.inbound_trailers_;
	}

	void trailers(const std::shared_ptr<server_stream_impl> &value,
		body_reader_adapter::trailers_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			handler = std::move(handler)]() mutable {
			ignore_unused(self);
			if( not value->remote_end_ )
				std::move(handler)(make_error_code(
					errc::operation_not_permitted), fields{});
			else std::move(handler)(error_code{}, value->inbound_trailers_);
		});
	}

	void respond_sync(server_stream_impl &value, response_head head,
		body_spec spec, error_code &error) noexcept
	{
		error.clear();
		if( value.response_started_ or value.closed_ )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		if( spec.type() == body_spec::mode::head and
			value.request_.method.known() != method::head )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		submit_response(value, std::move(head), std::move(spec), error);
		if( not error ) write_pump();
	}

	void respond(const std::shared_ptr<server_stream_impl> &value,
		response_head head, body_spec spec, stream::operation_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			head = std::move(head), spec = std::move(spec),
			handler = std::move(handler)]() mutable {
			if( value->response_started_ or value->closed_ )
				return std::move(handler)(
					make_error_code(errc::operation_not_permitted));
			if( spec.type() == body_spec::mode::head and
				value->request_.method.known() != method::head )
				return std::move(handler)(
					make_error_code(errc::operation_not_permitted));
			error_code error;
			self->submit_response(*value, std::move(head), std::move(spec), error);
			if( not error ) self->write_pump();
			std::move(handler)(error);
		});
	}

	void inform_sync(server_stream_impl &value, response_head head,
		error_code &error) noexcept
	{
		error.clear();
		auto headers = response_headers(head, body_spec::empty(),
			config_.h2.max_header_list_size);
		if( not headers ) { error = headers.error(); return; }
		if( nghttp2_submit_headers(native_, NGHTTP2_FLAG_NONE, value.id_, nullptr,
			headers->values.data(), headers->values.size(), nullptr) != 0 )
			error = make_error_code(errc::stream_protocol_error);
		if( not error ) write_pump();
	}

	void inform(const std::shared_ptr<server_stream_impl> &value,
		response_head head, stream::operation_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			head = std::move(head), handler = std::move(handler)]() mutable {
			error_code error;
			self->inform_sync(*value, std::move(head), error);
			std::move(handler)(error);
		});
	}

	[[nodiscard]] size_t write_sync(server_stream_impl &value,
		const_buffer buffer, error_code &error) noexcept
	{
		error.clear();
		if( value.terminal_error_ ) { error = value.terminal_error_; return 0; }
		if( buffer.size() > config_.h2.outbound_stream_buffer or
			value.outbound_size_ + buffer.size() >
				config_.h2.outbound_stream_buffer or
			outbound_buffered_ + buffer.size() >
				config_.h2.outbound_connection_buffer )
		{
			error = make_error_code(errc::buffer_limit_exceeded);
			return 0;
		}
		enqueue(value, buffer);
		(void)nghttp2_session_resume_data(native_, value.id_);
		write_pump();
		return buffer.size();
	}

	void write(const std::shared_ptr<server_stream_impl> &value,
		const_buffer buffer, body_writer_adapter::write_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value, buffer,
			handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				return std::move(handler)(value->terminal_error_, 0);
			if( value->pending_write_ )
				return std::move(handler)(
					make_error_code(errc::operation_in_progress), 0);
			if( buffer.size() > self->config_.h2.outbound_stream_buffer )
				return std::move(handler)(
					make_error_code(errc::buffer_limit_exceeded), 0);
			if( value->outbound_size_ + buffer.size() >
					self->config_.h2.outbound_stream_buffer or
				self->outbound_buffered_ + buffer.size() >
					self->config_.h2.outbound_connection_buffer )
			{
				value->pending_write_.emplace(
					server_stream_impl::pending_write{
						buffer, std::move(handler)});
				return;
			}
			const auto size = buffer.size();
			self->enqueue(*value, buffer);
			(void)nghttp2_session_resume_data(self->native_, value->id_);
			self->write_pump();
			std::move(handler)(error_code{}, size);
		});
	}

	void finish_sync(server_stream_impl &value, fields trailers,
		error_code &error) noexcept
	{
		error.clear();
		if( value.terminal_error_ ) { error = value.terminal_error_; return; }
		auto checked = trailer_headers(trailers,
			config_.h2.max_header_list_size);
		if( not checked ) { error = checked.error(); return; }
		value.outbound_finished_ = true;
		value.outbound_trailers_ = std::move(trailers);
		(void)nghttp2_session_resume_data(native_, value.id_);
		write_pump();
	}

	void finish(const std::shared_ptr<server_stream_impl> &value,
		fields trailers, body_writer_adapter::finish_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			trailers = std::move(trailers), handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				return std::move(handler)(value->terminal_error_);
			auto checked = trailer_headers(trailers,
				self->config_.h2.max_header_list_size);
			if( not checked ) return std::move(handler)(checked.error());
			value->outbound_finished_ = true;
			value->outbound_trailers_ = std::move(trailers);
			(void)nghttp2_session_resume_data(self->native_, value->id_);
			self->write_pump();
			std::move(handler)(error_code{});
		});
	}

	void cancel(const std::shared_ptr<server_stream_impl> &value) noexcept
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value] {
			self->cancel_stream(*value);
			self->write_pump();
		});
	}

	void release_buffer_budget(size_t inbound, size_t outbound) noexcept
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), inbound, outbound] {
			self->inbound_buffered_ -= std::min(
				self->inbound_buffered_, inbound);
			self->outbound_buffered_ -= std::min(
				self->outbound_buffered_, outbound);
		});
	}

private:
	void submit_response(server_stream_impl &value, response_head head,
		body_spec spec, error_code &error) noexcept
	{
		auto headers = response_headers(head, spec,
			config_.h2.max_header_list_size);
		if( not headers ) { error = headers.error(); return; }
		nghttp2_api::data_provider provider {};
		nghttp2_api::data_provider *provider_ptr = nullptr;
		const auto mode = spec.type();
		if( mode != body_spec::mode::empty and mode != body_spec::mode::head )
		{
			provider.source.ptr = &value;
			provider.read_callback = &server_session_impl::data_read;
			provider_ptr = &provider;
		}
		if( nghttp2_api::submit_response(native_, value.id_,
			headers->values.data(), headers->values.size(), provider_ptr) != 0 )
		{
			error = make_error_code(errc::stream_protocol_error);
			return;
		}
		value.response_started_ = true;
		value.outbound_finished_ =
			mode == body_spec::mode::empty or mode == body_spec::mode::head;
	}

	void read_loop()
	{
		if( reading_ or closed_ ) return;
		reading_ = true;
		auto self = shared_from_this();
		transport_->read_some(riwo::buffer(read_buffer_),
			asio::bind_executor(strand_,
			[self = std::move(self)](error_code error, size_t size) mutable {
				self->reading_ = false;
				if( error or size == 0 )
					return self->fail(error ? error : protocol_error());
				const auto consumed = nghttp2_api::session_mem_recv(
					self->native_, reinterpret_cast<const uint8_t *>(
						self->read_buffer_.data()), size);
				if( consumed < 0 or static_cast<size_t>(consumed) != size )
					return self->fail(protocol_error());
				self->write_pump();
				self->read_loop();
			}));
	}

	void write_pump()
	{
		if( writing_ or closed_ ) return;
		const uint8_t *data = nullptr;
		const auto size = nghttp2_api::session_mem_send(native_, &data);
		if( size < 0 ) return fail(protocol_error());
		if( size == 0 ) return;
		if( static_cast<size_t>(size) >
			config_.h2.outbound_connection_buffer )
			return fail(make_error_code(errc::buffer_limit_exceeded));
		writing_ = true;
		auto wire = std::make_shared<std::vector<std::byte>>(
			static_cast<size_t>(size));
		std::memcpy(wire->data(), data, wire->size());
		auto self = shared_from_this();
		transport_->write(riwo::buffer(*wire), asio::bind_executor(strand_,
			[self = std::move(self), wire]
			(error_code error, size_t written) mutable {
				self->writing_ = false;
				if( error or written != wire->size() )
					return self->fail(error ? error : protocol_error());
				self->write_pump();
			}));
	}

	void fail(error_code error)
	{
		if( closed_.exchange(true) ) return;
		draining_ = true;
		for( auto &[id, value] : streams_ )
		{
			ignore_unused(id);
			complete_error(*value, error);
		}
		streams_.clear();
		ready_streams_.clear();
		if( accept_handler_ )
			std::move(accept_handler_)(error, std::shared_ptr<stream>{});
		transport_->cancel();
		complete_shutdown(error);
	}

	void complete_error(server_stream_impl &value, error_code error)
	{
		if( not value.terminal_error_ ) value.terminal_error_ = error;
		if( value.pending_read_handler_ )
		{
			value.pending_read_buffer_.reset();
			std::move(value.pending_read_handler_)(value.terminal_error_,
				body_read_result{});
		}
		if( value.pending_write_ )
		{
			auto pending = std::move(*value.pending_write_);
			value.pending_write_.reset();
			std::move(pending.handler)(value.terminal_error_, 0);
		}
	}

	void complete_shutdown(error_code error)
	{
		auto handlers = std::move(shutdown_handlers_);
		for( auto &handler : handlers ) std::move(handler)(error);
	}

	void finish_shutdown_if_idle()
	{
		if( not draining_ or not streams_.empty() or closed_ ) return;
		closed_ = true;
		transport_->close(
			[self = shared_from_this()](error_code error) mutable {
				self->complete_shutdown(error);
			});
	}

	[[nodiscard]] body_read_result take_body(server_stream_impl &value,
		mutable_buffer buffer)
	{
		size_t copied = 0;
		while( copied < buffer.size() and not value.inbound_chunks_.empty() )
		{
			auto &chunk = value.inbound_chunks_.front();
			const auto available = chunk.size() - value.inbound_offset_;
			const auto count = std::min(available, buffer.size() - copied);
			std::memcpy(static_cast<std::byte *>(buffer.data()) + copied,
				chunk.data() + value.inbound_offset_, count);
			copied += count;
			value.inbound_offset_ += count;
			value.inbound_size_ -= count;
			inbound_buffered_ -= count;
			if( value.inbound_offset_ == chunk.size() )
			{
				value.inbound_chunks_.pop_front();
				value.inbound_offset_ = 0;
			}
		}
		return {copied, value.remote_end_ and value.inbound_size_ == 0};
	}

	void consume(server_stream_impl &value, size_t size)
	{
		if( size == 0 or value.id_ <= 0 ) return;
		if( nghttp2_session_consume(native_, value.id_, size) != 0 )
			return fail(protocol_error());
		write_pump();
	}

	void enqueue(server_stream_impl &value, const_buffer buffer)
	{
		std::vector<std::byte> bytes(buffer.size());
		if( buffer.size() != 0 )
			std::memcpy(bytes.data(), buffer.data(), buffer.size());
		value.outbound_size_ += bytes.size();
		outbound_buffered_ += bytes.size();
		value.outbound_chunks_.push_back(std::move(bytes));
	}

	void drain_pending(server_stream_impl &value)
	{
		if( not value.pending_write_ ) return;
		auto &pending = *value.pending_write_;
		if( value.outbound_size_ + pending.buffer.size() >
				config_.h2.outbound_stream_buffer or
			outbound_buffered_ + pending.buffer.size() >
				config_.h2.outbound_connection_buffer ) return;
		auto item = std::move(pending);
		value.pending_write_.reset();
		const auto size = item.buffer.size();
		enqueue(value, item.buffer);
		std::move(item.handler)(error_code{}, size);
	}

	void deliver_pending_read(server_stream_impl &value)
	{
		if( not value.pending_read_handler_ or
			(value.inbound_size_ == 0 and not value.remote_end_ and
				not value.terminal_error_) ) return;
		auto handler = std::move(value.pending_read_handler_);
		auto buffer = *value.pending_read_buffer_;
		value.pending_read_buffer_.reset();
		if( value.terminal_error_ )
			return std::move(handler)(value.terminal_error_,
				body_read_result{});
		auto result = take_body(value, buffer);
		if( result.bytes != 0 ) consume(value, result.bytes);
		std::move(handler)(error_code{}, result);
	}

	void remote_end(server_stream_impl &value)
	{
		value.remote_end_ = true;
		if( value.content_length_ and
			*value.content_length_ != value.inbound_total_ )
		{
			complete_error(value,
				make_error_code(errc::body_length_mismatch));
			(void)nghttp2_submit_rst_stream(native_, NGHTTP2_FLAG_NONE,
				value.id_, NGHTTP2_PROTOCOL_ERROR);
		}
		// A header-only request has no read operation through which
		// body_reader_state could observe EOF. Mark it complete here so the
		// public reader destructor does not cancel an otherwise successful
		// response stream.
		if( value.inbound_size_ == 0 and value.inbound_ )
			value.inbound_->set_finished();
		deliver_pending_read(value);
	}

	void cancel_stream(server_stream_impl &value)
	{
		if( value.cancelled_ or value.closed_ ) return;
		value.cancelled_ = true;
		complete_error(value, aborted());
		(void)nghttp2_submit_rst_stream(native_, NGHTTP2_FLAG_NONE,
			value.id_, NGHTTP2_CANCEL);
	}

	void deliver_stream(std::shared_ptr<server_stream_impl> value)
	{
		last_processed_stream_ = std::max(last_processed_stream_, value->id_);
		if( accept_handler_ )
			std::move(accept_handler_)(error_code{}, std::move(value));
		else ready_streams_.push_back(std::move(value));
	}

	[[nodiscard]] bool finish_request(server_stream_impl &value)
	{
		if( not value.have_method_ or not value.have_scheme_ or
			not value.have_authority_ or not value.have_path_ or
			value.method_.empty() or value.authority_.empty() or
			(value.path_ != "*" and not value.path_.starts_with('/')) )
			return false;
		if( (value.scheme_ != "http" and value.scheme_ != "https") or
			std::ranges::any_of(value.authority_, [](unsigned char byte) {
				return byte <= 0x20U or byte == 0x7fU or byte == '/' or
					byte == '?' or byte == '#' or byte == '@';
			}) or std::ranges::any_of(value.path_, [](unsigned char byte) {
				return byte <= 0x20U or byte == 0x7fU or byte == '#';
			}) ) return false;
		try {
			value.request_.method = http_exp::method(value.method_);
			value.request_.scheme = value.scheme_;
			value.request_.authority = value.authority_;
			value.request_.target = value.path_;
			if( value.path_ != "*" )
				value.request_.url = riwo::url(value.scheme_ + "://" +
					value.authority_ + value.path_);
			optional<uint64_t> content_length_value;
			for( const auto &field : value.request_.headers )
			{
				if( lower(field.name) != "content-length" ) continue;
				const std::string_view content_length = field.value;
				uint64_t length = 0;
				const auto parsed = std::from_chars(content_length.data(),
					content_length.data() + content_length.size(), length);
				if( parsed.ec != std::errc{} or
					parsed.ptr != content_length.data() + content_length.size() or
					(content_length_value and
						*content_length_value != length) ) return false;
				content_length_value = length;
			}
			value.content_length_ = content_length_value;
			if( value.content_length_ and
				config_.request_limits.max_body_size and
				*value.content_length_ >
					*config_.request_limits.max_body_size ) return false;
			value.request_ready_ = true;
			return true;
		}
		catch(...) { return false; }
	}

	static int on_begin_headers(nghttp2_session *,
		const nghttp2_frame *frame, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		if( frame->hd.type != NGHTTP2_HEADERS ) return 0;
		if( frame->headers.cat == NGHTTP2_HCAT_REQUEST )
		{
			if( self.draining_ or self.streams_.size() >=
				self.config_.h2.max_concurrent_streams )
			{
				(void)nghttp2_submit_rst_stream(self.native_, NGHTTP2_FLAG_NONE,
					frame->hd.stream_id, NGHTTP2_REFUSED_STREAM);
				return 0;
			}
			try {
				auto value = std::make_shared<server_stream_impl>(
					self.shared_from_this(), frame->hd.stream_id);
				value->initialize();
				self.streams_.emplace(frame->hd.stream_id, value);
				(void)nghttp2_session_set_stream_user_data(self.native_,
					frame->hd.stream_id, value.get());
			}
			catch(...) { return NGHTTP2_ERR_CALLBACK_FAILURE; }
			return 0;
		}
		auto found = self.streams_.find(frame->hd.stream_id);
		if( found != self.streams_.end() )
		{
			found->second->collecting_trailers_ = true;
			found->second->header_bytes_ = 0;
			found->second->header_count_ = 0;
		}
		return 0;
	}

	static int on_header(nghttp2_session *, const nghttp2_frame *frame,
		const uint8_t *name, size_t name_size, const uint8_t *field_value,
		size_t value_size, uint8_t, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto found = self.streams_.find(frame->hd.stream_id);
		if( found == self.streams_.end() ) return 0;
		auto &value = *found->second;
		const auto header_limit = std::min(self.config_.h2.max_header_list_size,
			self.config_.request_limits.max_header_list_size);
		value.header_bytes_ += name_size + value_size + 32;
		++value.header_count_;
		if( value.header_bytes_ > header_limit or
			value.header_count_ > self.config_.request_limits.max_field_count or
			name_size > self.config_.request_limits.max_field_name_size or
			value_size > self.config_.request_limits.max_field_value_size )
		{
			value.invalid_headers_ = true;
			(void)nghttp2_submit_rst_stream(self.native_, NGHTTP2_FLAG_NONE,
				value.id_, NGHTTP2_ENHANCE_YOUR_CALM);
			return 0;
		}
		const std::string_view header_name(
			reinterpret_cast<const char *>(name), name_size);
		const std::string_view header_value(
			reinterpret_cast<const char *>(field_value), value_size);
		if( header_name.starts_with(':') )
		{
			if( value.collecting_trailers_ ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			auto set_once = [&](std::string &target, bool &present) {
				if( present ) return false;
				target.assign(header_value);
				present = true;
				return true;
			};
			if( header_name == ":method" )
				return set_once(value.method_, value.have_method_) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":scheme" )
				return set_once(value.scheme_, value.have_scheme_) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":authority" )
				return set_once(value.authority_, value.have_authority_) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":path" )
				return set_once(value.path_, value.have_path_) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		}
		if( forbidden_header(header_name) or
			(header_name == "te" and lower(header_value) != "trailers") or
			(value.collecting_trailers_ and
				(header_name == "content-length" or header_name == "host" or
					header_name == "trailer" or header_name == "te")) )
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		try {
			if( value.collecting_trailers_ )
				value.inbound_trailers_.add(std::string(header_name),
					std::string(header_value));
			else value.request_.headers.add(std::string(header_name),
				std::string(header_value));
		}
		catch(...) { return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_frame_recv(nghttp2_session *, const nghttp2_frame *frame,
		void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		if( frame->hd.type == NGHTTP2_GOAWAY )
		{
			self.draining_ = true;
			return 0;
		}
		if( frame->hd.type != NGHTTP2_HEADERS and
			frame->hd.type != NGHTTP2_DATA and
			frame->hd.type != NGHTTP2_RST_STREAM ) return 0;
		auto found = self.streams_.find(frame->hd.stream_id);
		if( found == self.streams_.end() ) return 0;
		auto value = found->second;
		const bool end_stream =
			(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0;
		if( frame->hd.type == NGHTTP2_HEADERS and not value->request_ready_ and
			frame->headers.cat == NGHTTP2_HCAT_REQUEST )
		{
			if( value->invalid_headers_ or not self.finish_request(*value) )
			{
				(void)nghttp2_submit_rst_stream(self.native_, NGHTTP2_FLAG_NONE,
					value->id_, NGHTTP2_PROTOCOL_ERROR);
				self.complete_error(*value,
					make_error_code(errc::stream_protocol_error));
			}
			else
			{
				// Publish a header-only request only after its inbound direction is
				// observably complete. The service executor may run inline.
				if( end_stream ) self.remote_end(*value);
				self.deliver_stream(value);
			}
		}
		if( frame->hd.type == NGHTTP2_RST_STREAM )
			self.complete_error(*value,
				make_error_code(errc::stream_protocol_error));
		if( end_stream and not value->remote_end_ ) self.remote_end(*value);
		return 0;
	}

	static int on_data_chunk(nghttp2_session *, uint8_t, int32_t stream_id,
		const uint8_t *data, size_t size, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto found = self.streams_.find(stream_id);
		if( found == self.streams_.end() ) return 0;
		auto &value = *found->second;
		if( value.inbound_size_ + size >
				self.config_.h2.inbound_stream_buffer or
			self.inbound_buffered_ + size >
				self.config_.h2.inbound_connection_buffer or
			(self.config_.request_limits.max_body_size and
				value.inbound_total_ + size >
					*self.config_.request_limits.max_body_size) )
		{
			(void)nghttp2_submit_rst_stream(self.native_, NGHTTP2_FLAG_NONE,
				stream_id, NGHTTP2_ENHANCE_YOUR_CALM);
			self.complete_error(value,
				make_error_code(errc::buffer_limit_exceeded));
			return 0;
		}
		try {
			std::vector<std::byte> bytes(size);
			if( size != 0 ) std::memcpy(bytes.data(), data, size);
			value.inbound_chunks_.push_back(std::move(bytes));
			value.inbound_size_ += size;
			value.inbound_total_ += size;
			self.inbound_buffered_ += size;
			self.deliver_pending_read(value);
		}
		catch(...) { return NGHTTP2_ERR_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_stream_close(nghttp2_session *, int32_t stream_id,
		uint32_t error_value, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto found = self.streams_.find(stream_id);
		if( found == self.streams_.end() ) return 0;
		auto value = found->second;
		value->closed_ = true;
		if( error_value != NGHTTP2_NO_ERROR and not value->terminal_error_ )
			self.complete_error(*value,
				make_error_code(errc::stream_protocol_error));
		if( error_value == NGHTTP2_NO_ERROR and not value->remote_end_ )
			self.remote_end(*value);
		self.streams_.erase(found);
		self.finish_shutdown_if_idle();
		return 0;
	}

	static nghttp2_api::ssize_type data_read(nghttp2_session *session,
		int32_t, uint8_t *buffer, size_t length, uint32_t *flags,
		nghttp2_data_source *source, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto &value = *static_cast<server_stream_impl *>(source->ptr);
		if( value.terminal_error_ ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		if( value.outbound_size_ == 0 )
		{
			if( not value.outbound_finished_ ) return NGHTTP2_ERR_DEFERRED;
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.outbound_trailers_.empty() and
				not value.trailers_submitted_ )
			{
				auto trailers = trailer_headers(value.outbound_trailers_,
					self.config_.h2.max_header_list_size);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
				if( nghttp2_submit_trailer(session, value.id_,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				value.trailers_submitted_ = true;
			}
			return 0;
		}
		size_t copied = 0;
		while( copied < length and not value.outbound_chunks_.empty() )
		{
			auto &chunk = value.outbound_chunks_.front();
			const auto available = chunk.size() - value.outbound_offset_;
			const auto count = std::min(available, length - copied);
			std::memcpy(buffer + copied,
				chunk.data() + value.outbound_offset_, count);
			copied += count;
			value.outbound_offset_ += count;
			value.outbound_size_ -= count;
			self.outbound_buffered_ -= count;
			if( value.outbound_offset_ == chunk.size() )
			{
				value.outbound_chunks_.pop_front();
				value.outbound_offset_ = 0;
			}
		}
		self.drain_pending(value);
		if( value.outbound_size_ == 0 and value.outbound_finished_ )
		{
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.outbound_trailers_.empty() and
				not value.trailers_submitted_ )
			{
				auto trailers = trailer_headers(value.outbound_trailers_,
					self.config_.h2.max_header_list_size);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
				if( nghttp2_submit_trailer(session, value.id_,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				value.trailers_submitted_ = true;
			}
		}
		return static_cast<nghttp2_api::ssize_type>(copied);
	}

	executor_type executor_;
	asio::strand<asio::any_io_executor> strand_;
	std::shared_ptr<::riwo::http_exp::connection> transport_;
	server_config config_;
	std::string initial_input_;
	asio::ip::tcp::endpoint remote_;
	asio::ip::tcp::endpoint local_;
	nghttp2_session_callbacks *callbacks_ = nullptr;
	nghttp2_option *option_ = nullptr;
	nghttp2_session *native_ = nullptr;
	std::unordered_map<int32_t,std::shared_ptr<server_stream_impl>> streams_;
	std::deque<std::shared_ptr<server_stream_impl>> ready_streams_;
	accept_handler accept_handler_;
	std::vector<shutdown_handler> shutdown_handlers_;
	std::array<std::byte,16 * 1024> read_buffer_ {};
	size_t inbound_buffered_ = 0;
	size_t outbound_buffered_ = 0;
	int32_t last_processed_stream_ = 0;
	std::atomic<bool> draining_ {false};
	std::atomic<bool> closed_ {false};
	bool reading_ = false;
	bool writing_ = false;
};

server_stream_impl::~server_stream_impl()
{
	if( session_ )
		session_->release_buffer_budget(inbound_size_, outbound_size_);
}

void server_stream_impl::initialize()
{
	reader_adapter_ = std::make_shared<server_reader_adapter>(weak_from_this());
	writer_adapter_ = std::make_shared<server_writer_adapter>(weak_from_this());
	inbound_ = make_body_reader_state(reader_adapter_, true,
		session_->config().request_limits.max_discard_body);
	outbound_ = make_body_writer_state(writer_adapter_);
}

auto server_stream_impl::remote_endpoint(error_code &error) const noexcept
	-> endpoint_type { return session_->remote_endpoint(error); }

auto server_stream_impl::local_endpoint(error_code &error) const noexcept
	-> endpoint_type { return session_->local_endpoint(error); }

void server_stream_impl::respond_sync(response_head head, body_spec spec,
	error_code &error) noexcept {
	session_->respond_sync(*this, std::move(head), std::move(spec), error);
}

void server_stream_impl::respond(response_head head, body_spec spec,
	operation_handler handler) {
	session_->respond(shared_from_this(), std::move(head), std::move(spec),
		std::move(handler));
}

void server_stream_impl::inform_sync(response_head head,
	error_code &error) noexcept {
	session_->inform_sync(*this, std::move(head), error);
}

void server_stream_impl::inform(response_head head,
	operation_handler handler) {
	session_->inform(shared_from_this(), std::move(head), std::move(handler));
}

bool server_stream_impl::reusable() const noexcept {
	// server_exchange destruction means the application handler is done, not
	// that nghttp2 has already emitted the final DATA/trailer frame.  Once both
	// application directions are complete, keep the stream alive so the Session
	// write pump can finish it instead of turning every successful response into
	// RST_STREAM(CANCEL).
	return not terminal_error_ and not cancelled_ and remote_end_ and
		response_started_ and outbound_finished_;
}

void server_stream_impl::cancel() noexcept {
	if( session_ ) session_->cancel(shared_from_this());
}

body_read_result server_reader_adapter::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner ) { error = aborted(); return {}; }
	return owner->session_->read_sync(*owner, buffer, error);
}

void server_reader_adapter::read_some(mutable_buffer buffer,
	read_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted(), body_read_result{});
	owner->session_->read(owner, buffer, std::move(handler));
}

fields server_reader_adapter::trailers_sync(error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner ) { error = aborted(); return {}; }
	return owner->session_->trailers_sync(*owner, error);
}

void server_reader_adapter::trailers(trailers_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted(), fields{});
	owner->session_->trailers(owner, std::move(handler));
}

void server_reader_adapter::cancel() noexcept {
	if( auto owner = owner_.lock() ) owner->cancel();
}

size_t server_writer_adapter::write_sync(const_buffer buffer,
	error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner ) { error = aborted(); return 0; }
	return owner->session_->write_sync(*owner, buffer, error);
}

void server_writer_adapter::write(const_buffer buffer, write_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted(), 0);
	owner->session_->write(owner, buffer, std::move(handler));
}

void server_writer_adapter::finish_sync(fields trailers,
	error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner ) { error = aborted(); return; }
	owner->session_->finish_sync(*owner, std::move(trailers), error);
}

void server_writer_adapter::finish(fields trailers, finish_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted());
	owner->session_->finish(owner, std::move(trailers), std::move(handler));
}

void server_writer_adapter::cancel() noexcept {
	if( auto owner = owner_.lock() ) owner->cancel();
}

class h2_server_loop final : public std::enable_shared_from_this<h2_server_loop>
{
public:
	std::shared_ptr<server_session_impl> session;
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
				const auto benign =
					error == asio::error::make_error_code(asio::error::eof) or
					error == asio::error::make_error_code(
						asio::error::connection_reset) or
					error == asio::error::make_error_code(
						asio::error::operation_aborted);
				if( not self->served or not benign )
					self->handler(error, std::shared_ptr<stream>{},
						request_head{});
				return;
			}
			self->served = true;
			auto request = value ? value->request() : request_head{};
			self->handler(error_code{}, std::move(value), std::move(request));
			self->start();
		});
	}
};

class h2_server_connection final : public server_connection
{
public:
	explicit h2_server_connection(std::shared_ptr<h2_server_loop> loop) :
		loop_(std::move(loop)) {}
	void cancel() noexcept override {
		loop_->session->graceful_shutdown([](error_code) {});
	}
	[[nodiscard]] bool closed() const noexcept override {
		return loop_->session->closed();
	}
private:
	std::shared_ptr<h2_server_loop> loop_;
};

} //namespace

sys_expected<std::shared_ptr<server_session>> make_server_session(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input)
{
	if( not transport )
		return sys_unexpected(make_error_code(errc::negotiation_failed));
	if( auto error = validate_server_config(config) )
		return sys_unexpected(error);
	if( config.protocols.requirement == protocol_requirement::require_http1 or
		std::ranges::find(config.protocols.preference, protocol::v2) ==
			config.protocols.preference.end() )
		return sys_unexpected(make_error_code(errc::negotiation_failed));
	auto result = std::make_shared<server_session_impl>(std::move(transport),
		std::move(config), std::move(initial_input));
	if( auto error = result->initialize() ) return sys_unexpected(error);
	result->start();
	return std::static_pointer_cast<server_session>(std::move(result));
}

server_connection_ptr accept_h2(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input,
	server_accept_handler handler)
{
	auto made = make_server_session(std::move(transport), std::move(config),
		std::move(initial_input));
	if( not made )
	{
		std::move(handler)(made.error(), std::shared_ptr<stream>{},
			request_head{});
		return {};
	}
	auto concrete = std::static_pointer_cast<server_session_impl>(*made);
	auto loop = std::make_shared<h2_server_loop>();
	loop->session = std::move(concrete);
	loop->handler = std::move(handler);
	loop->start();
	return std::make_shared<h2_server_connection>(std::move(loop));
}

} //namespace riwo::http_exp::detail::h2

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
