// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V1_CODEC_H
#define RIWO_HTTP_EXP_PROTOCOL_V1_CODEC_H

#include <riwo/http_exp/protocol/body_spec.h>
#include <riwo/http_exp/protocol/limits.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/version.h>

namespace riwo::http_exp::v1
{

enum class body_framing {
	none, content_length, chunked, until_eof
};
enum class request_target_form {
	origin, absolute, authority, asterisk
};
enum class connection_disposition {
	keep_alive, close
};

struct serialized_head
{
	std::string wire {};
	body_framing framing = body_framing::none;
	bool close_after_message = false;
};

struct parsed_request_head
{
	request_head head {};
	protocol version = protocol::v1_1;

	body_framing framing = body_framing::none;
	uint64_t content_length = 0;
	bool close_after_message = true;
};

struct parsed_response_head
{
	response_head head {};
	protocol version = protocol::v1_1;

	body_framing framing = body_framing::none;
	uint64_t content_length = 0;
	bool close_after_message = true;
};

template<class Head>
struct head_parse_result
{
	size_t consumed = 0;
	optional<Head> head {};
	bool needs_input = false;
};

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<serialized_head> serialize_request_head (
	const request_head&, const body_spec&, protocol,
	request_target_form = request_target_form::origin
) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<serialized_head> serialize_response_head (
	const response_head&, const body_spec&, protocol,
	connection_disposition = connection_disposition::keep_alive
) noexcept;

class RIWO_HTTP_EXP_API request_parser
{
public:
	explicit request_parser(const message_limits &limits = {});
	~request_parser();

	request_parser(request_parser &&other) noexcept;
	request_parser &operator=(request_parser &&other) noexcept;

	[[nodiscard]] sys_expected<head_parse_result<parsed_request_head>>
	ingest(const const_buffer& input) noexcept;

	void reset() noexcept;
	[[nodiscard]] bool complete() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

class RIWO_HTTP_EXP_API response_parser
{
public:
	explicit response_parser(const message_limits &limits = {}, bool head_request = false);
	~response_parser();

	response_parser(response_parser &&other) noexcept;
	response_parser &operator=(response_parser &&other) noexcept;

	[[nodiscard]] sys_expected<head_parse_result<parsed_response_head>>
	ingest(const const_buffer &input) noexcept;

	void reset(bool head_request = false) noexcept;
	[[nodiscard]] bool complete() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

struct chunk_decode_result
{
	size_t consumed = 0;
	std::vector<std::byte> data {};

	optional<fields> trailers {};
	bool message_end = false;
	bool needs_input = false;
};

class RIWO_HTTP_EXP_API chunk_decoder
{
public:
	explicit chunk_decoder(const message_limits &limits = {});
	~chunk_decoder();

	chunk_decoder(chunk_decoder &&other) noexcept;
	chunk_decoder &operator=(chunk_decoder &&other) noexcept;

	[[nodiscard]] sys_expected<chunk_decode_result> ingest(const const_buffer &input) noexcept;
	[[nodiscard]] sys_expected<void> finish_input() noexcept;

	void reset() noexcept;
	[[nodiscard]] bool complete() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

class RIWO_HTTP_EXP_API chunk_encoder
{
public:
	[[nodiscard]] sys_expected<std::string> encode(const const_buffer &input) const;
	[[nodiscard]] sys_expected<std::string> finish(const fields &trailers = {}) const;
};

} //namespace riwo::http_exp::v1


#endif //RIWO_HTTP_EXP_PROTOCOL_V1_CODEC_H
