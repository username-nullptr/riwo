// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_MULTIPART_H
#define RIWO_HTTP_EXP_PROTOCOL_MULTIPART_H

#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp
{

struct multipart_part
{
	fields headers;
	std::string name;
	std::string filename;
	std::string data;
};

class RIWO_HTTP_EXP_API multipart_form
{
public:
	explicit multipart_form (
		core_concepts::text_p<char> auto &&boundary
	);
	multipart_form();

public:
	multipart_form &add_field (
		core_concepts::text_p<char> auto &&name,
		core_concepts::text_p<char> auto &&value
	);
	multipart_form &add_file (
		core_concepts::text_p<char> auto &&name,
		core_concepts::text_p<char> auto &&filename,
		core_concepts::text_p<char> auto &&data
	);
	multipart_form &add_file (
		core_concepts::text_p<char> auto &&name,
		core_concepts::text_p<char> auto &&filename,
		core_concepts::text_p<char> auto &&data,
		core_concepts::text_p<char> auto &&content_type
	);
	multipart_form &add_part(multipart_part part);

public:
	[[nodiscard]] std::string_view boundary() const noexcept;
	[[nodiscard]] std::string content_type() const;

	[[nodiscard]] std::string body() const;
	[[nodiscard]] const std::vector<multipart_part> &parts() const noexcept;

private:
	void initialize_boundary(std::string boundary);

	void add_field_impl (
		std::string name, std::string value
	);
	void add_file_impl (
		std::string name, std::string filename,
		std::string data, std::string content_type
	);
	std::vector<multipart_part> m_parts {};
	std::string m_boundary {};
};

[[nodiscard]] RIWO_HTTP_EXP_API optional<std::string>
multipart_boundary(std::string_view content_type) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API sys_expected<std::vector<multipart_part>>
parse_multipart_form(std::string_view content_type, std::string_view body) noexcept;

} //namespace riwo::http_exp
#include <riwo/http_exp/protocol/detail/multipart.h>


#endif //RIWO_HTTP_EXP_PROTOCOL_MULTIPART_H
