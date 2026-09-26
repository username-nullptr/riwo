// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "form_data.h"
#include <riwo/core/algorithm/uuid.h>
#include <riwo/core/string_vector.h>

namespace riwo::http { namespace
{

[[nodiscard]] bool safe_header(std::string_view name, std::string_view value) noexcept
{
	constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
	return not name.empty() and
		std::ranges::all_of(name, [&](unsigned char ch) {
			return std::isalnum(ch) or punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
		}) and
		value.find_first_of("\r\n") == std::string_view::npos and
		value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool valid_boundary(std::string_view value) noexcept
{
	constexpr std::string_view punctuation = "'()+_,-./:=?";
	return not value.empty() and value.size() <= 70 and value.back() != ' ' and
		std::ranges::all_of(value, [&](unsigned char ch) {
			return std::isalnum(ch) or ch == ' ' or
				punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
		});
}

[[nodiscard]] bool boundary_needs_quotes(std::string_view value) noexcept
{
	constexpr std::string_view token_punctuation = "!#$%&'*+-.^_`|~";
	return not std::ranges::all_of(value, [&](unsigned char ch) {
		return std::isalnum(ch) or
			token_punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
	});
}

[[nodiscard]] std::string quote_parameter(std::string_view value)
{
	std::string result;
	result.reserve(value.size() + 2);
	result += '"';
	for(auto ch : value)
	{
		if( ch == '"' or ch == '\\' )
			result += '\\';
		if( ch != '\r' and ch != '\n' )
			result += ch;
	}
	result += '"';
	return result;
}

[[nodiscard]] std::vector<std::string_view>
semicolon_statements(std::string_view value) noexcept
{
	std::vector<std::string_view> result;
	size_t begin = 0;
	bool quoted = false;
	bool escaped = false;
	for(size_t i=0; i<=value.size(); ++i)
	{
		if( i == value.size() or (value[i] == ';' and not quoted) )
		{
			result.emplace_back(value.substr(begin, i - begin));
			begin = i + 1;
			continue;
		}
		if( escaped )
		{
			escaped = false;
			continue;
		}
		if( quoted and value[i] == '\\' )
			escaped = true;
		else if( value[i] == '"' )
			quoted = not quoted;
	}
	return result;
}

[[nodiscard]] std::optional<std::string> disposition_parameter
(std::string_view value, std::string_view wanted) noexcept
{
	for(auto statement : semicolon_statements(value))
	{
		auto equal = statement.find('=');
		if( equal == std::string::npos )
			continue;
		auto name = strtls::to_lower(strtls::trimmed(statement.substr(0, equal)));
		if( name != wanted )
			continue;
		auto result = strtls::trimmed(statement.substr(equal + 1));
		if( result.size() >= 2 and result.front() == '"' and result.back() == '"' )
		{
			result = result.substr(1, result.size() - 2);
			std::string unescaped;
			for(size_t i=0; i<result.size(); ++i)
			{
				if( result[i] == '\\' and i + 1 < result.size() )
					++i;
				unescaped += result[i];
			}
			return unescaped;
		}
		return result;
	}
	return std::nullopt;
}

} //namespace

class RIWO_DECL_HIDDEN multipart_form_data::impl
{
public:
	explicit impl(std::string boundary) :
		m_boundary(std::move(boundary))
	{
		if( m_boundary.empty() )
			m_boundary = "riwo-" + uuid::generate().to_string();
		if( not valid_boundary(m_boundary) )
			invalid_argument::loc_throw("Invalid multipart/form-data boundary.");
	}

	impl(const impl &other) = default;
	impl &operator=(const impl &other) = default;

	impl(impl &&other) = default;
	impl &operator=(impl &&other) = default;

public:
	std::string m_boundary {};
	form_data_parts m_parts {};
};

multipart_form_data::multipart_form_data(std::string boundary) :
	m_impl(new impl(std::move(boundary)))
{

}

multipart_form_data::~multipart_form_data()
{
	delete m_impl;
}

multipart_form_data::multipart_form_data(const multipart_form_data &other) :
	m_impl(new impl(*other.m_impl))
{

}

multipart_form_data & multipart_form_data::operator=(const multipart_form_data &other)
{
	if( this != &other )
		*m_impl = *other.m_impl;
	return *this;
}

multipart_form_data::multipart_form_data(multipart_form_data &&other) noexcept :
	m_impl(new impl(std::move(*other.m_impl)))
{

}

multipart_form_data & multipart_form_data::operator=(multipart_form_data &&other) noexcept
{
	if( this != &other )
		*m_impl = std::move(*other.m_impl);
	return *this;
}

multipart_form_data &multipart_form_data::add_file
(std::string name, std::string filename, std::string data, std::string content_type)
{
	form_data_part part {};
	part.name = std::move(name);
	part.filename = std::move(filename);
	part.data = std::move(data);
	part.fields[header::content_type] = std::move(content_type);
	return add_part(std::move(part));
}

multipart_form_data &multipart_form_data::add_field(std::string name, std::string value)
{
	form_data_part part {};
	part.name = std::move(name);
	part.data = std::move(value);
	return add_part(std::move(part));
}

multipart_form_data &multipart_form_data::add_part(form_data_part part)
{
	if( part.name.empty() )
		invalid_argument::loc_throw("multipart/form-data part name is empty.");

	auto disposition = "form-data; name=" + quote_parameter(part.name);
	if( not part.filename.empty() )
		disposition += "; filename=" + quote_parameter(part.filename);

	part.fields[header::content_disposition] = std::move(disposition);
	m_impl->m_parts.emplace_back(std::move(part));
	return *this;
}

std::string_view multipart_form_data::boundary() const noexcept
{
	return m_impl->m_boundary;
}

std::string multipart_form_data::content_type() const
{
	return "multipart/form-data; boundary=" + (
		boundary_needs_quotes(m_impl->m_boundary) ?
			quote_parameter(m_impl->m_boundary) : m_impl->m_boundary
	);
}

std::string multipart_form_data::body() const
{
	std::string result {};
	for(auto &part : m_impl->m_parts)
	{
		if( part.data.find("\r\n--" + m_impl->m_boundary) != std::string::npos )
			invalid_argument::loc_throw("multipart/form-data boundary occurs in part data.");

		result += "--" + m_impl->m_boundary + "\r\n";
		for(auto &[name,item] : part.fields)
		{
			auto value = item.to_string();
			if( safe_header(name, value) )
				result += std::format("{}: {}\r\n", name, value);
		}
		result += "\r\n" + part.data + "\r\n";
	}
	return result + "--" + m_impl->m_boundary + "--\r\n";
}

const form_data_parts &multipart_form_data::parts() const noexcept
{
	return m_impl->m_parts;
}

request_arg &multipart_form_data::apply(request_arg &argument) const
{
	auto data = body();
	return argument
		.set_header(header::content_type, content_type())
		.set_header(header::content_length, data.size());
}

optional<std::string> form_data_boundary(std::string_view content_type) noexcept
{
	auto statements = semicolon_statements(content_type);
	if( statements.empty() or
		strtls::to_lower(strtls::trimmed(statements.front())) != "multipart/form-data" )
		return nullopt;

	for(size_t i=1; i<statements.size(); ++i)
	{
		auto equal = statements[i].find('=');
		if( equal == std::string::npos or
			strtls::to_lower(strtls::trimmed(statements[i].substr(0, equal))) != "boundary" )
			continue;

		auto result = strtls::trimmed(statements[i].substr(equal + 1));
		if( result.size() >= 2 and result.front() == '"' and result.back() == '"' )
			result = result.substr(1, result.size() - 2);

		if( not valid_boundary(result) )
			return nullopt;
		return result;
	}
	return nullopt;
}

sys_expected<form_data_parts> parse_multipart_form_data
(std::string_view content_type, std::string_view body) noexcept
{
	auto boundary = form_data_boundary(content_type);
	if( not boundary )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

	const auto delimiter = "--" + *boundary;
	if( not body.starts_with(delimiter) )
		return sys_unexpected(make_system_error_code(std::errc::protocol_error));

	form_data_parts result {};
	size_t cursor = delimiter.size();

	while( cursor < body.size() )
	{
		if( body.substr(cursor).starts_with("--") )
			return result;

		if( not body.substr(cursor).starts_with("\r\n") )
			return sys_unexpected(make_system_error_code(std::errc::protocol_error));

		cursor += 2;
		auto header_end = body.find("\r\n\r\n", cursor);

		if( header_end == std::string_view::npos )
			return sys_unexpected(make_system_error_code(std::errc::protocol_error));

		form_data_part part {};
		for(auto &line : string_vector::from_string(body.substr(cursor, header_end - cursor), "\r\n"))
		{
			auto colon = line.find(':');
			if( colon == std::string::npos )
				return sys_unexpected(make_system_error_code(std::errc::protocol_error));

			part.fields[strtls::trimmed(line.substr(0, colon))] =
				strtls::trimmed(line.substr(colon + 1));
		}
		auto disposition = part.fields.find(header::content_disposition);
		if( disposition == part.fields.end() )
			return sys_unexpected(make_system_error_code(std::errc::protocol_error));

		auto disposition_value = disposition->second.to_string();

		if( auto disposition_type = disposition_value.substr(0, disposition_value.find(';'));
			strtls::to_lower(strtls::trimmed(disposition_type)) != "form-data" )
			return sys_unexpected(make_system_error_code(std::errc::protocol_error));

		part.name = disposition_parameter(disposition_value, "name").value_or("");
		part.filename = disposition_parameter(disposition_value, "filename").value_or("");

		if( part.name.empty() )
			return sys_unexpected(make_system_error_code(std::errc::protocol_error));

		cursor = header_end + 4;
		auto next = body.find("\r\n" + delimiter, cursor);

		if( next == std::string_view::npos )
			return sys_unexpected(make_system_error_code(std::errc::protocol_error));

		part.data = body.substr(cursor, next - cursor);
		result.emplace_back(std::move(part));
		cursor = next + 2 + delimiter.size();
	}
	return sys_unexpected(make_system_error_code(std::errc::protocol_error));
}

} //namespace riwo::http
