// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "codec.h"
#include <riwo/http_exp/protocol/v1/detail/wire.h>
#include <charconv>

namespace riwo::http_exp::v1 { namespace
{

[[nodiscard]] body_framing convert(detail::body_framing value) noexcept
{
	switch( value )
	{
	case detail::body_framing::content_length:
		return body_framing::content_length;
	case detail::body_framing::chunked:
		return body_framing::chunked;
	case detail::body_framing::until_eof:
		return body_framing::until_eof;
	default:
		return body_framing::none;
	}
}

[[nodiscard]] body_framing framing_for(const body_spec &spec) noexcept
{
	switch( spec.type() )
	{
	case body_spec::mode::known_length:
		return spec.length().value_or(0) == 0 ? body_framing::none :
			body_framing::content_length;
	case body_spec::mode::streaming:
		return body_framing::chunked;
	case body_spec::mode::head:
		return body_framing::none;
	default:
		return body_framing::none;
	}
}

[[nodiscard]] std::string_view as_chars(const_buffer input) noexcept {
	return {static_cast<const char *>(input.data()), input.size()};
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] sys_expected<fields> parse_trailers(std::string_view block,
	const message_limits &limits) noexcept
{
	try
	{
		fields result;
		size_t count = 0;
		size_t total = 0;
		while( not block.empty() )
		{
			const auto end = block.find("\r\n");
			if( end == std::string_view::npos )
				return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			const auto line = block.substr(0, end);
			block.remove_prefix(end + 2);
			if( line.empty() ) break;
			if( ++count > limits.max_field_count or
				total + line.size() > limits.max_header_list_size )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			total += line.size();
			const auto colon = line.find(':');
			if( colon == 0 or colon == std::string_view::npos )
				return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			const auto name = line.substr(0, colon);
			const auto value = trim(line.substr(colon + 1));
			if( name.size() > limits.max_field_name_size or
				value.size() > limits.max_field_value_size )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			result.add(std::string(name), std::string(value));
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(make_error_code(errc::connection_protocol_error));
	}
}

} //namespace

sys_expected<serialized_head> serialize_request_head(const request_head &value,
	const body_spec &spec, protocol version, request_target_form form) noexcept
{
	try
	{
		auto head = value;
		bool absolute = form == request_target_form::absolute;
		if( form == request_target_form::authority ) head.target = head.authority;
		else if( form == request_target_form::asterisk ) head.target = "*";
		auto wire = detail::serialize_request(head, spec, version, absolute);
		if( not wire ) return sys_unexpected(wire.error());
		return serialized_head {
			.wire = std::move(*wire),
			.framing = framing_for(spec),
			.close_after_message = false
		};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<serialized_head> serialize_response_head(const response_head &head,
	const body_spec &spec, protocol version,
	connection_disposition disposition) noexcept
{
	auto wire = detail::serialize_response(head, spec, version,
		disposition == connection_disposition::close);
	if( not wire ) return sys_unexpected(wire.error());
	return serialized_head {
		.wire = std::move(*wire),
		.framing = framing_for(spec),
		.close_after_message = disposition == connection_disposition::close
	};
}

class request_parser::impl
{
public:
	explicit impl(message_limits value) : limits(std::move(value)) {}
	message_limits limits;
	std::string pending;
	bool done = false;
};

request_parser::request_parser(message_limits limits) :
	m_impl(std::make_unique<impl>(std::move(limits))) {}
request_parser::request_parser(request_parser &&) noexcept = default;
request_parser &request_parser::operator=(request_parser &&) noexcept = default;
request_parser::~request_parser() = default;

sys_expected<head_parse_result<parsed_request_head>>
request_parser::ingest(const_buffer input) noexcept
{
	if( m_impl->done )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));
	try
	{
		const auto text = as_chars(input);
		const auto old_size = m_impl->pending.size();
		const auto marker_in_input = text.find("\r\n\r\n");
		size_t take = text.size();
		if( marker_in_input != std::string_view::npos and old_size == 0 )
			take = marker_in_input + 4;
		else
		{
			m_impl->pending.append(text);
			const auto marker = m_impl->pending.find("\r\n\r\n");
			if( marker != std::string::npos )
				take = marker + 4 > old_size ? marker + 4 - old_size : 0;
			if( marker == std::string::npos )
			{
				if( m_impl->pending.size() > m_impl->limits.max_header_list_size +
					m_impl->limits.max_start_line_size + 4 )
					return sys_unexpected(make_error_code(errc::header_limit_exceeded));
				return head_parse_result<parsed_request_head> {
					.consumed = text.size(), .needs_input = true
				};
			}
			m_impl->pending.resize(marker + 4);
		}
		std::string complete;
		if( old_size == 0 ) complete.assign(text.substr(0, take));
		else complete = m_impl->pending;
		auto parsed = detail::parse_request(complete, m_impl->limits);
		if( not parsed ) return sys_unexpected(parsed.error());
		m_impl->done = true;
		m_impl->pending.clear();
		return head_parse_result<parsed_request_head> {
			.consumed = take,
			.head = parsed_request_head {
				.head = std::move(parsed->head),
				.version = parsed->version,
				.framing = convert(parsed->framing),
				.content_length = parsed->content_length,
				.close_after_message = parsed->close
			}
		};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

void request_parser::reset() noexcept {
	m_impl->pending.clear();
	m_impl->done = false;
}
bool request_parser::complete() const noexcept { return m_impl->done; }

class response_parser::impl
{
public:
	impl(message_limits value, bool head) :
		limits(std::move(value)), head_request(head) {}
	message_limits limits;
	std::string pending;
	bool head_request = false;
	bool done = false;
};

response_parser::response_parser(message_limits limits, bool head_request) :
	m_impl(std::make_unique<impl>(std::move(limits), head_request)) {}
response_parser::response_parser(response_parser &&) noexcept = default;
response_parser &response_parser::operator=(response_parser &&) noexcept = default;
response_parser::~response_parser() = default;

sys_expected<head_parse_result<parsed_response_head>>
response_parser::ingest(const_buffer input) noexcept
{
	if( m_impl->done )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));
	try
	{
		const auto text = as_chars(input);
		const auto old_size = m_impl->pending.size();
		m_impl->pending.append(text);
		const auto marker = m_impl->pending.find("\r\n\r\n");
		if( marker == std::string::npos )
		{
			if( m_impl->pending.size() > m_impl->limits.max_header_list_size +
				m_impl->limits.max_start_line_size + 4 )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			return head_parse_result<parsed_response_head> {
				.consumed = text.size(), .needs_input = true
			};
		}
		const auto total = marker + 4;
		const auto take = total > old_size ? total - old_size : 0;
		m_impl->pending.resize(total);
		auto parsed = detail::parse_response(m_impl->pending,
			m_impl->limits, m_impl->head_request);
		if( not parsed ) return sys_unexpected(parsed.error());
		m_impl->done = true;
		m_impl->pending.clear();
		return head_parse_result<parsed_response_head> {
			.consumed = take,
			.head = parsed_response_head {
				.head = std::move(parsed->head),
				.version = parsed->version,
				.framing = convert(parsed->framing),
				.content_length = parsed->content_length,
				.close_after_message = parsed->close
			}
		};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

void response_parser::reset(bool head_request) noexcept {
	m_impl->pending.clear();
	m_impl->head_request = head_request;
	m_impl->done = false;
}
bool response_parser::complete() const noexcept { return m_impl->done; }

class chunk_decoder::impl
{
public:
	explicit impl(message_limits value) : limits(std::move(value)) {}
	enum class state { size, data, data_crlf, trailers, done } current = state::size;
	message_limits limits;
	std::string pending;
	uint64_t remaining = 0;
	uint64_t total = 0;
};

chunk_decoder::chunk_decoder(message_limits limits) :
	m_impl(std::make_unique<impl>(std::move(limits))) {}
chunk_decoder::chunk_decoder(chunk_decoder &&) noexcept = default;
chunk_decoder &chunk_decoder::operator=(chunk_decoder &&) noexcept = default;
chunk_decoder::~chunk_decoder() = default;

sys_expected<chunk_decode_result> chunk_decoder::ingest(
	const_buffer input) noexcept
{
	try
	{
		chunk_decode_result result;
		const auto text = as_chars(input);
		size_t offset = 0;
		while( offset < text.size() )
		{
			if( m_impl->current == impl::state::done ) break;
			if( m_impl->current == impl::state::data )
			{
				const auto count = static_cast<size_t>(std::min<uint64_t>(
					m_impl->remaining, text.size() - offset));
				result.data.resize(count);
				if( count != 0 ) std::memcpy(result.data.data(),
					text.data() + offset, count);
				offset += count;
				m_impl->remaining -= count;
				m_impl->total += count;
				if( m_impl->limits.max_body_size and
					m_impl->total > *m_impl->limits.max_body_size )
					return sys_unexpected(make_error_code(errc::buffer_limit_exceeded));
				if( m_impl->remaining == 0 )
					m_impl->current = impl::state::data_crlf;
				result.consumed = offset;
				result.needs_input = m_impl->remaining != 0;
				if( count != 0 ) return result;
				continue;
			}

			m_impl->pending.push_back(text[offset++]);
			result.consumed = offset;
			if( m_impl->current == impl::state::data_crlf )
			{
				if( m_impl->pending.size() < 2 ) continue;
				if( m_impl->pending != "\r\n" )
					return sys_unexpected(make_error_code(
						errc::connection_protocol_error));
				m_impl->pending.clear();
				m_impl->current = impl::state::size;
				continue;
			}
			const auto marker = m_impl->pending.find("\r\n");
			if( marker == std::string::npos )
			{
				if( m_impl->pending.size() > m_impl->limits.max_header_list_size )
					return sys_unexpected(make_error_code(errc::header_limit_exceeded));
				continue;
			}
			if( m_impl->current == impl::state::size )
			{
				auto parsed = detail::parse_chunk_size(
					std::string_view(m_impl->pending).substr(0, marker));
				if( not parsed ) return sys_unexpected(parsed.error());
				m_impl->pending.clear();
				m_impl->remaining = *parsed;
				m_impl->current = *parsed == 0 ? impl::state::trailers :
					impl::state::data;
				continue;
			}
			if( m_impl->current == impl::state::trailers and
				(m_impl->pending == "\r\n" or
				 m_impl->pending.ends_with("\r\n\r\n")) )
			{
				auto trailers = parse_trailers(m_impl->pending, m_impl->limits);
				if( not trailers ) return sys_unexpected(trailers.error());
				result.trailers = std::move(*trailers);
				result.message_end = true;
				m_impl->pending.clear();
				m_impl->current = impl::state::done;
				return result;
			}
		}
		result.needs_input = m_impl->current != impl::state::done;
		result.message_end = m_impl->current == impl::state::done;
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<void> chunk_decoder::finish_input() noexcept
{
	if( complete() ) return {};
	return sys_unexpected(make_error_code(errc::unexpected_eof));
}

void chunk_decoder::reset() noexcept {
	m_impl->current = impl::state::size;
	m_impl->pending.clear();
	m_impl->remaining = 0;
	m_impl->total = 0;
}
bool chunk_decoder::complete() const noexcept {
	return m_impl->current == impl::state::done;
}

sys_expected<std::string> chunk_encoder::encode(const_buffer input) const
{
	try
	{
		std::string result = detail::serialize_chunk_header(input.size());
		result.append(static_cast<const char *>(input.data()), input.size());
		result += "\r\n";
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<std::string> chunk_encoder::finish(const fields &trailers) const
{
	try { return detail::serialize_last_chunk(trailers); }
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

} //namespace riwo::http_exp::v1
