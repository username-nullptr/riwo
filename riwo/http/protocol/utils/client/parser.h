// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CLIENT_PARSER_H
#define RIWO_HTTP_PROTOCOL_UTILS_CLIENT_PARSER_H

#include <riwo/http/protocol/utils/core/container_helper.h>
#include <riwo/http/protocol/utils/core/parser_types.h>
#include <riwo/http/protocol/utils/core/range.h>

namespace riwo::http
{

template <>
class RIWO_HTTP_API parser<protocol_model::client> :
	public const_headers<parser<protocol_model::client>>,
	public const_cookies<cookie,parser<protocol_model::client>>,
	public const_chunk_attributes<parser<protocol_model::client>>
{
	RIWO_DISABLE_COPY(parser)

public:
	using stage_t = http::stage;
	using set_cookie_values_t = std::vector <
		std::pair<std::string,http::cookie>
	>;
	explicit parser(size_t init_buf_size = default_parser_buffer_size);
	~parser() override;

	parser(parser &&other) noexcept;
	parser &operator=(parser &&other) noexcept;

	sys_expected<bool> append(const const_buffer &buf);
	parser &operator<<(const const_buffer &buf);

	[[nodiscard]] sys_expected<bool> next_message();
	[[nodiscard]] bool finish_eof();

public:
	[[nodiscard]] version_enum version() const noexcept;
	[[nodiscard]] status_enum status() const noexcept;

	[[nodiscard]] bool keep_alive() const noexcept;
	[[nodiscard]] bool support_gzip() const noexcept;
	[[nodiscard]] bool content_decoded() const noexcept;
	[[nodiscard]] bool automatic_decompression() const noexcept;

	[[nodiscard]] bool is_chunked() const noexcept;
	[[nodiscard]] bool is_range_response() const noexcept;
	[[nodiscard]] bool is_multipart_byte_ranges() const noexcept;
	[[nodiscard]] bool is_informational() const noexcept;
	[[nodiscard]] bool is_upgrade() const noexcept;

	parser &set_request_method(method_enum request_method) noexcept;
	parser &set_automatic_decompression(bool enabled = true) noexcept;

	[[nodiscard]] method_enum request_method() const noexcept;
	[[nodiscard]] const optional<http::content_range> &content_range() const noexcept;
	[[nodiscard]] optional<size_t> complete_length() const noexcept;

	[[nodiscard]] const body_norms_t &body_norms() const noexcept;
	[[nodiscard]] const set_cookie_values_t &set_cookies() const noexcept;

public:
	[[nodiscard]] std::string take_partial_body(size_t size);
	[[nodiscard]] size_t read_partial_body(const mutable_buffer &buffer) noexcept;
	[[nodiscard]] size_t partial_body_size() const noexcept;

	[[nodiscard]] std::string take_body();
	[[nodiscard]] std::string take_pending_data();
	[[nodiscard]] optional<byte_range_chunk> take_range_body(size_t size);

	[[nodiscard]] size_t prepare_direct_body_read(size_t size) const noexcept;
	[[nodiscard]] bool commit_direct_body_read(size_t size) noexcept;

	[[nodiscard]] stage_t stage() const noexcept;
	parser &reset();

private:
	class impl;
	impl *m_impl;
};

using client_parser = parser<protocol_model::client>;

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CLIENT_PARSER_H
