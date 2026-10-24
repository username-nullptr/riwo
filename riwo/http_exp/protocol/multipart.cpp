// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "multipart.h"
#include "detail/multipart_utils.h"

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] std::string quote(std::string_view value)
{
	if( value.find_first_of("\r\n") != std::string_view::npos )
		throw std::invalid_argument("invalid multipart disposition parameter");
	std::string result = "\"";
	for( const auto current : value )
	{
		if( current == '\\' or current == '"' )
			result.push_back('\\');
		result.push_back(current);
	}
	result.push_back('"');
	return result;
}

[[nodiscard]] optional<std::string> disposition_parameter(
	std::string_view value, std::string_view wanted) noexcept
{
	try
	{
		auto rest = value.substr(value.find(';') == std::string_view::npos ?
			value.size() : value.find(';') + 1);
		while( not rest.empty() )
		{
			const auto semicolon = rest.find(';');
			const auto item = detail::trim_ows(rest.substr(0, semicolon));
			const auto equal = item.find('=');
			if( equal != std::string_view::npos and
				strtls::ascii_equal(detail::trim_ows(item.substr(0, equal)),
					wanted, false) )
			{
				auto parameter = detail::trim_ows(item.substr(equal + 1));
				if( parameter.size() >= 2 and parameter.front() == '"' and
					parameter.back() == '"' )
				{
					parameter = parameter.substr(1, parameter.size() - 2);
					std::string decoded;
					for( size_t index = 0; index < parameter.size(); ++index )
					{
						if( parameter[index] == '\\' and index + 1 < parameter.size() )
							++index;
						decoded.push_back(parameter[index]);
					}
					return decoded;
				}
				return std::string(parameter);
			}
			if( semicolon == std::string_view::npos )
				break;
			rest.remove_prefix(semicolon + 1);
		}
		return {};
	}
	catch(...) {
		return {};
	}
}

} //namespace

multipart_form::multipart_form(std::string boundary) :
	m_boundary(boundary.empty() ? detail::make_multipart_boundary() :
		std::move(boundary))
{
	if( not detail::valid_multipart_boundary(m_boundary) )
		throw std::invalid_argument("invalid multipart boundary");
}

multipart_form &multipart_form::add_field(std::string name, std::string value)
{
	multipart_part part;
	part.name = std::move(name);
	part.data = std::move(value);
	return add_part(std::move(part));
}

multipart_form &multipart_form::add_file(std::string name,
	std::string filename, std::string data, std::string content_type_value)
{
	multipart_part part;
	part.headers.add("Content-Type", std::move(content_type_value));
	part.name = std::move(name);
	part.filename = std::move(filename);
	part.data = std::move(data);
	return add_part(std::move(part));
}

multipart_form &multipart_form::add_part(multipart_part part)
{
	if( part.name.empty() )
		throw std::invalid_argument("multipart part name is empty");
	auto disposition = "form-data; name=" + quote(part.name);
	if( not part.filename.empty() )
		disposition += "; filename=" + quote(part.filename);
	part.headers.set("Content-Disposition", std::move(disposition));
	m_parts.emplace_back(std::move(part));
	return *this;
}

std::string_view multipart_form::boundary() const noexcept { return m_boundary; }

std::string multipart_form::content_type() const {
	return "multipart/form-data; boundary=" + m_boundary;
}

std::string multipart_form::body() const
{
	std::string result;
	for( const auto &part : m_parts )
	{
		if( part.data.find("\r\n--" + m_boundary) != std::string::npos )
			throw std::invalid_argument("multipart boundary occurs in part data");
		result += "--" + m_boundary + "\r\n";
		for( const auto &header : part.headers )
		{
			const auto value = header.value.to_string();
			if( header.name.find_first_of("\r\n:") == std::string::npos and
				value.find_first_of("\r\n") == std::string::npos )
				result += header.name + ": " + value + "\r\n";
		}
		result += "\r\n" + part.data + "\r\n";
	}
	return result + "--" + m_boundary + "--\r\n";
}

const std::vector<multipart_part> &multipart_form::parts() const noexcept {
	return m_parts;
}

optional<std::string> multipart_boundary(std::string_view content_type) noexcept
{
	auto result = detail::parse_multipart_boundary(content_type,
		"multipart/form-data");
	if( not result )
		return {};
	return std::move(*result);
}

sys_expected<std::vector<multipart_part>> parse_multipart_form(
	std::string_view content_type, std::string_view body) noexcept
{
	try
	{
		auto boundary = multipart_boundary(content_type);
		if( not boundary )
			return sys_unexpected(
			make_system_error_code(std::errc::invalid_argument));
		const auto delimiter = "--" + *boundary;
		if( not body.starts_with(delimiter) )
			return sys_unexpected(
			make_system_error_code(std::errc::protocol_error));
		std::vector<multipart_part> result;
		size_t cursor = delimiter.size();
		for(;;)
		{
			if( body.substr(cursor).starts_with("--") )
				return result;
			if( not body.substr(cursor).starts_with("\r\n") )
				return sys_unexpected(make_system_error_code(std::errc::protocol_error));
			cursor += 2;
			const auto header_end = body.find("\r\n\r\n", cursor);
			if( header_end == std::string_view::npos )
				return sys_unexpected(
				make_system_error_code(std::errc::protocol_error));
			multipart_part part;
			auto header_text = body.substr(cursor, header_end - cursor);
			while( not header_text.empty() )
			{
				const auto line_end = header_text.find("\r\n");
				const auto line = header_text.substr(0, line_end);
				const auto colon = line.find(':');
				if( colon == std::string_view::npos )
					return sys_unexpected(
					make_system_error_code(std::errc::protocol_error));
				part.headers.add(std::string(detail::trim_ows(
					line.substr(0, colon))), std::string(detail::trim_ows(
					line.substr(colon + 1))));
				if( line_end == std::string_view::npos )
					break;
				header_text.remove_prefix(line_end + 2);
			}
			const auto disposition =
				part.headers.find_first("content-disposition");
			if( not disposition or not strtls::ascii_equal(detail::trim_ows(
				disposition->substr(0, disposition->find(';'))),
				"form-data", false) )
				return sys_unexpected(make_system_error_code(std::errc::protocol_error));
			part.name = disposition_parameter(*disposition, "name").value_or("");
			part.filename = disposition_parameter(*disposition,
				"filename").value_or("");
			if( part.name.empty() )
				return sys_unexpected(
				make_system_error_code(std::errc::protocol_error));
			cursor = header_end + 4;
			const auto next = body.find("\r\n" + delimiter, cursor);
			if( next == std::string_view::npos )
				return sys_unexpected(
				make_system_error_code(std::errc::protocol_error));
			part.data = body.substr(cursor, next - cursor);
			result.emplace_back(std::move(part));
			cursor = next + 2 + delimiter.size();
		}
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

} //namespace riwo::http_exp
