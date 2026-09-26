// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_PARSER_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_PARSER_H

#include <riwo/http/protocol/utils/core/container_helper.h>
#include <riwo/http/protocol/utils/core/parser_types.h>

namespace riwo::http
{

template <>
class RIWO_HTTP_API parser<protocol_model::base> final :
	public const_headers<parser<protocol_model::base>>
{
	RIWO_DISABLE_COPY(parser)

public:
	using stage_t = http::stage;
	using chunk_attributes_t = type_helper::values_t;
	using parse_begin_handler = std::function <
		sys_expected<version_enum>(std::string_view line_buf)
	>;
	using parse_cookie_handler = std::function <
		error_code(std::string_view line_buf)
	>;

	template <typename T>
	static constexpr bool file_opt_token_v = concepts::file_opt_token_p <
		T, char, file_optype::single, io_permission::write
	>;

public:
	explicit parser(size_t init_buf_size = default_parser_buffer_size);
	~parser() override;

	parser(parser &&other) noexcept;
	parser &operator=(parser &&other) noexcept;

public:
	parser &on_parse_begin(parse_begin_handler func);
	parser &on_parse_cookie(parse_cookie_handler func);
	[[nodiscard]] static error_code make_error_code(parse_errc errc);

	sys_expected<bool> append(const const_buffer &buf);
	parser &operator<<(const const_buffer &buf);

	parser &reset();
	parser &skip_body(bool value = true) noexcept;
	parser &read_until_eof(bool value = true) noexcept;

	[[nodiscard]] sys_expected<bool> next_message();
	[[nodiscard]] bool finish_eof() noexcept;

public:
	[[nodiscard]] std::string take_partial_body(size_t size);
	[[nodiscard]] size_t read_partial_body(const mutable_buffer &buffer) noexcept;
	[[nodiscard]] size_t partial_body_size() const noexcept;

	[[nodiscard]] std::string take_body();
	[[nodiscard]] std::string take_pending_data();

	[[nodiscard]] size_t prepare_direct_body_read(size_t size) const noexcept;
	[[nodiscard]] bool commit_direct_body_read(size_t size) noexcept;

	[[nodiscard]] version_enum version() const noexcept;
	[[nodiscard]] stage_t stage() const noexcept;
	[[nodiscard]] const chunk_attributes_t &chunk_attributes() const noexcept;

public:
	parser &unbind_parse_begin();
	parser &unbind_parse_cookie();

public:
	template <typename Opt>
	[[nodiscard]] static auto make_file_opt_token(Opt &&opt)
		noexcept requires file_opt_token_v<Opt>;

private:
	class impl;
	impl *m_impl;
};

using base_parser = parser<protocol_model::base>;

} //namespace riwo::http
#include <riwo/http/protocol/utils/core/detail/parser.h>


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_PARSER_H
