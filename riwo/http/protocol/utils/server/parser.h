// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_SERVER_PARSER_H
#define RIWO_HTTP_PROTOCOL_UTILS_SERVER_PARSER_H

#include <riwo/http/protocol/utils/core/container_helper.h>
#include <riwo/http/protocol/utils/core/parser_types.h>

namespace riwo::http
{

template <>
class RIWO_HTTP_API parser<protocol_model::server> final :
	public const_parameters<parser<protocol_model::server>>,
	public const_headers<parser<protocol_model::server>>,
	public const_cookies<value,parser<protocol_model::server>>
{
	RIWO_DISABLE_COPY(parser)

public:
	using stage_t = http::stage;
	using value_t = riwo::value;
	using path_args_t = parameter_map;

	using parameters_t = http::parameters;
	using headers_t = http::headers;

public:
	explicit parser(size_t init_buf_size = default_parser_buffer_size);
	~parser() override;

	parser(parser &&other) noexcept;
	parser &operator=(parser &&other) noexcept;

public:
	sys_expected<bool> append(const const_buffer &buf);
	parser &operator<<(const const_buffer &buf);
	[[nodiscard]] int32_t path_match(std::string_view rule);

public:
	[[nodiscard]] method_enum method() const noexcept;
	[[nodiscard]] request_target_form target_form() const noexcept;
	[[nodiscard]] std::string_view target() const noexcept;
	[[nodiscard]] std::string_view path() const noexcept;
	[[nodiscard]] version_enum version() const noexcept;

public:
	[[nodiscard]] optional<value_t> path_arg (const core_concepts::text_p<char> auto &key) const noexcept;
	[[nodiscard]] optional<value_t> path_arg(size_t index) const;
	[[nodiscard]] const path_args_t &path_args() const noexcept;

public:
	[[nodiscard]] bool keep_alive() const noexcept;
	[[nodiscard]] bool support_gzip() const noexcept;

	[[nodiscard]] std::string take_partial_body(size_t size);
	[[nodiscard]] size_t read_partial_body(const mutable_buffer &buffer) noexcept;
	[[nodiscard]] size_t partial_body_size() const noexcept;

	[[nodiscard]] std::string take_body();
	[[nodiscard]] std::string take_pending_data();

	[[nodiscard]] size_t prepare_direct_body_read(size_t size) const noexcept;
	[[nodiscard]] bool commit_direct_body_read(size_t size) noexcept;

	[[nodiscard]] stage_t stage() const noexcept;
	parser &reset();

private:
	class impl;
	impl *m_impl;
};

using server_parser = parser<protocol_model::server>;

} //namespace riwo::http
#include <riwo/http/protocol/utils/server/detail/parser.h>


#endif //RIWO_HTTP_PROTOCOL_UTILS_SERVER_PARSER_H
