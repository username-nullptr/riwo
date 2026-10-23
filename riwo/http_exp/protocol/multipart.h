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
	explicit multipart_form(std::string boundary = {});
	multipart_form &add_field(std::string name, std::string value);
	multipart_form &add_file(std::string name, std::string filename,
		std::string data,
		std::string content_type = "application/octet-stream");
	multipart_form &add_part(multipart_part part);

	[[nodiscard]] std::string_view boundary() const noexcept;
	[[nodiscard]] std::string content_type() const;
	[[nodiscard]] std::string body() const;
	[[nodiscard]] const std::vector<multipart_part> &parts() const noexcept;

private:
	std::string m_boundary;
	std::vector<multipart_part> m_parts;
};

[[nodiscard]] RIWO_HTTP_EXP_API optional<std::string>
multipart_boundary(std::string_view content_type) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API sys_expected<std::vector<multipart_part>>
parse_multipart_form(std::string_view content_type,
	std::string_view body) noexcept;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_MULTIPART_H
