// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "parser.h"
#include <riwo/http/protocol/utils/core/parser.h>
#include <riwo/http/protocol/utils/core/compression.h>
#include <riwo/core/algorithm/misc.h>
#include <riwo/core/string_vector.h>

namespace riwo::http
{

class RIWO_DECL_HIDDEN parser<protocol_model::server>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(size_t init_buf_size) :
		m_parser(init_buf_size)
	{
		m_parser
		.on_parse_begin([this](std::string_view line_buf)
		{
			sys_expected<version_enum> result = static_cast<version_enum>(0);
			auto request_line_parts = string_vector::from_string(line_buf, ' ');

			if( request_line_parts.size() != 3 or
				not strtls::to_upper(request_line_parts[2]).starts_with("HTTP/") )
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_req_line)
				);
			}
			const auto method = method::from_string(request_line_parts[0]);
			if( method == method::none )
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_method)
				);
			}
			m_method = method;

			try {
				result = version::from_string(request_line_parts[2].substr(5,3));
			}
			catch(const std::exception&)
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_req_line)
				);
			}
			m_target = request_line_parts[1];
			if( m_target.find('#') != std::string::npos )
				return result.despair(base_parser::make_error_code(parse_errc::invalid_path));

			std::string url_line = m_target;
			if( method == method::connect )
			{
				m_target_form = request_target_form::authority;
				if( url_line.find('/') != std::string::npos or
					url_line.find(':') == std::string::npos )
					return result.despair(base_parser::make_error_code(parse_errc::invalid_path));

				m_path = "/";
				return result;
			}
			if( url_line == "*" )
			{
				if( method != method::options )
					return result.despair(base_parser::make_error_code(parse_errc::invalid_path));

				m_target_form = request_target_form::asterisk;
				m_path = "*";
				return result;
			}
			auto lower_target = strtls::to_lower(url_line);
			if( lower_target.starts_with("http://") or lower_target.starts_with("https://") )
			{
				m_target_form = request_target_form::absolute;

				auto authority_begin = url_line.find("://") + 3;
				auto path_begin = url_line.find_first_of("/?", authority_begin);

				if( path_begin == std::string::npos )
					url_line = "/";
				else if( url_line[path_begin] == '?' )
					url_line = "/" + url_line.substr(path_begin);
				else
					url_line.erase(0, path_begin);
			}
			else
				m_target_form = request_target_form::origin;

			auto pos = url_line.find('?');
			m_path = from_percent_encoding(url_line.substr(0, pos));

			if( pos != std::string::npos )
			{
				for(auto parameters_string = url_line.substr(pos + 1);
					auto &para_str : string_vector::from_string(parameters_string, '&'))
				{
					pos = para_str.find('=');
					if( pos == std::string::npos )
					{
						auto value = from_percent_encoding(para_str);
						m_parameters.emplace_back(value, std::move(value));
					}
					else
					{
						m_parameters.emplace_back (
							from_percent_encoding(para_str.substr(0, pos)),
							from_percent_encoding(para_str.substr(pos + 1))
						);
					}
				}
			}
			if( not m_path.starts_with('/') )
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_path)
				);
			}
			auto n_it = std::ranges::unique(m_path, [](char c0, char c1) {
				return c0 == c1 and c0 == '/';
			}).begin();

			if( n_it != m_path.end() )
				m_path.erase(n_it, m_path.end());

			if( m_path.size() > 1 and m_path.ends_with('/') )
				m_path.pop_back();
			return result;
		})
		.on_parse_cookie([this](std::string_view line_buf)
		{
			auto vector = string_vector::from_string(line_buf, ';');
			if( vector.empty() )
				return base_parser::make_error_code(parse_errc::invalid_cookie_line);

			for(auto &statement : vector)
			{
				statement = strtls::trimmed(statement);
				auto pos = statement.find('=');

				if( pos == std::string::npos )
					return base_parser::make_error_code(parse_errc::invalid_cookie_line);

				auto key = strtls::trimmed(statement.substr(0,pos));
				auto value = strtls::trimmed(statement.substr(pos+1));
				m_cookies[std::move(key)] = std::move(value);
			}
			return error_code();
		});
	}

public:
	void set_attribute()
	{
		auto request_headers = m_parser.headers();
		auto it = request_headers.find(header::connection);
		if( it == request_headers.end() )
			m_keep_alive = m_parser.version() != version::v10;
		else
		{
			m_keep_alive = m_parser.version() != version::v10;
			for(auto &token : string_vector::from_string(it->second.to_string(), ','))
			{
				if( auto value = strtls::to_lower(strtls::trimmed(token));
					value == "close" )
					m_keep_alive = false;

				else if( value == "keep-alive" )
					m_keep_alive = true;
			}
		}
		it = request_headers.find(header::accept_encoding);
		if( it == request_headers.end() )
		{
			m_support_gzip = false;
			return ;
		}
		m_support_gzip = content_coding_quality (
			it->second.to_string(), "gzip"
		) > 0;
	}

public:
	base_parser m_parser;
	method_enum m_method = method_enum::get;
	request_target_form m_target_form = request_target_form::origin;
	std::string m_target {};

	std::string m_path {};
	parameters_t m_parameters {};
	path_args_t m_path_args {};
	cookie_values m_cookies {};

	bool m_keep_alive = false;
	bool m_support_gzip = false;
};

parser<protocol_model::server>::parser(size_t init_buf_size) :
	const_parameters(nullptr),
	const_headers(nullptr),
	const_cookies(nullptr),
	m_impl(new impl(init_buf_size))
{
	m_parameters = &m_impl->m_parameters;
	m_headers = &m_impl->m_parser.headers();
	m_cookies = &m_impl->m_cookies;
}

parser<protocol_model::server>::~parser()
{
	delete m_impl;
}

parser<protocol_model::server>::parser(parser &&other) noexcept :
	const_parameters(other.m_parameters),
	const_headers(other.m_headers),
	const_cookies(other.m_cookies),
	m_impl(other.m_impl)
{
	other.m_impl = new impl(0);
	other.m_parameters = &other.m_impl->m_parameters;
	other.m_headers = &other.m_impl->m_parser.headers();
	other.m_cookies = &other.m_impl->m_cookies;
}

parser<protocol_model::server> &parser<protocol_model::server>::operator=(parser &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	m_parameters = other.m_parameters;
	m_headers = other.m_headers;
	m_cookies = other.m_cookies;

	other.m_impl = new impl(0);
	other.m_parameters = &other.m_impl->m_parameters;
	other.m_headers = &other.m_impl->m_parser.headers();
	other.m_cookies = &other.m_impl->m_cookies;
	return *this;
}

sys_expected<bool> parser<protocol_model::server>::append(const const_buffer &buf)
{
	auto expected = m_impl->m_parser.append(buf);
	if( not expected )
		return expected;

	if( *expected )
	{
		auto host = m_impl->m_parser.headers().find(header::host);
		if( m_impl->m_parser.version() == version::v11 and
			(host == m_impl->m_parser.headers().end() or
			 host->second.to_string().find(',') != std::string::npos) )
			return sys_unexpected(base_parser::make_error_code(parse_errc::invalid_header_line));
		m_impl->set_attribute();
	}
	return expected;
}

parser<protocol_model::server> &parser<protocol_model::server>::operator<<(const const_buffer &buf)
{
	append(buf);
	return *this;
}

int32_t parser<protocol_model::server>::path_match(std::string_view rule)
{
	auto rule_list = rule == "/" ?
		string_vector{{rule.data(), rule.size()}} :
		string_vector::from_string(rule, "/");

	auto path_list = string_vector::from_string(m_impl->m_path, "/");
	if( path_list.empty() )
		path_list.emplace_back("/");
	if( path_list.size() < rule_list.size() )
		return -1;

	path_args_t vector;
	size_t index = rule_list.size();

	for(auto &format : std::ranges::reverse_view(rule_list))
	{
		if( not format.starts_with('{') or not format.ends_with('}')  )
			break;
		else if( format.size() == 2 )
		{
			vector.emplace_back();
			--index;
			continue;
		}
		bool res = true;
		for(size_t i=1; i<format.size()-1; i++)
		{
			if( format[i] == '{' or format[i] == '}' )
			{
				res = false;
				break;
			}
		}
		if( res )
		{
			std::string key(format.c_str() + 1, format.size() - 2);
			vector.emplace_back(std::make_pair(std::move(key), value_t()));
			--index;
		}
	}
	std::ranges::reverse(vector);

	auto rule_before = rule_list.join(0, index, "/");
	std::string path_before;

	index = path_list.size() - vector.size();
	if( vector.empty() )
		path_before = path_list.join("/");
	else
		path_before = path_list.join(0, index, "/");

	auto weight = wildcard_match(rule_before, path_before);
	if( weight < 0 )
		return -1;

	for(auto &[key,value] : vector)
	{
		ignore_unused(key);
		value = path_list[index++];
	}
	m_impl->m_path_args = std::move(vector);
	return weight;
}

method_enum parser<protocol_model::server>::method() const noexcept
{
	return m_impl->m_method;
}

request_target_form parser<protocol_model::server>::target_form() const noexcept
{
	return m_impl->m_target_form;
}

std::string_view parser<protocol_model::server>::target() const noexcept
{
	return m_impl->m_target;
}

std::string_view parser<protocol_model::server>::path() const noexcept
{
	return m_impl->m_path;
}

version_enum parser<protocol_model::server>::version() const noexcept
{
	return m_impl->m_parser.version();
}

optional<value> parser<protocol_model::server>::path_arg(size_t index) const
{
	if( index >= path_args().size() )
	{
		runtime_error::loc_throw (
			"riwo::http::parser<protocol_model::server>::path_arg: index out of range."
		);
	}
	return path_args()[index].second;
}

const parser<protocol_model::server>::path_args_t&
parser<protocol_model::server>::path_args() const noexcept
{
	return m_impl->m_path_args;
}

bool parser<protocol_model::server>::keep_alive() const noexcept
{
	return m_impl->m_keep_alive;
}

bool parser<protocol_model::server>::support_gzip() const noexcept
{
	return m_impl->m_support_gzip;
}

std::string parser<protocol_model::server>::take_partial_body(size_t size)
{
	return m_impl->m_parser.take_partial_body(size);
}

size_t parser<protocol_model::server>::read_partial_body
(const mutable_buffer &buffer) noexcept
{
	return m_impl->m_parser.read_partial_body(buffer);
}

size_t parser<protocol_model::server>::partial_body_size() const noexcept
{
	return m_impl->m_parser.partial_body_size();
}

std::string parser<protocol_model::server>::take_body()
{
	return m_impl->m_parser.take_body();
}

std::string parser<protocol_model::server>::take_pending_data()
{
	return m_impl->m_parser.take_pending_data();
}

size_t parser<protocol_model::server>::prepare_direct_body_read(size_t size) const noexcept
{
	return m_impl->m_parser.prepare_direct_body_read(size);
}

bool parser<protocol_model::server>::commit_direct_body_read(size_t size) noexcept
{
	return m_impl->m_parser.commit_direct_body_read(size);
}

parser<protocol_model::server>::stage_t parser<protocol_model::server>::stage() const noexcept
{
	return m_impl->m_parser.stage();
}

parser<protocol_model::server> &parser<protocol_model::server>::reset()
{
	m_impl->m_parser.reset();
	m_impl->m_method = method::get;
	m_impl->m_target_form = request_target_form::origin;

	m_impl->m_target.clear();
	m_impl->m_path.clear();

	m_impl->m_parameters.clear();
	m_impl->m_path_args.clear();
	m_impl->m_cookies.clear();

	m_impl->m_keep_alive = false;
	m_impl->m_support_gzip = false;
	return *this;
}

} //namespace riwo::http
