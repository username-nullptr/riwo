// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "url.h"
#include "algorithm/misc.h"
#include "string_vector.h"

namespace riwo { namespace
{

[[nodiscard]] bool ascii_alpha(char value) noexcept
{
	return (value >= 'a' and value <= 'z') or
		   (value >= 'A' and value <= 'Z');
}

[[nodiscard]] bool valid_scheme(std::string_view value) noexcept
{
	if( value.empty() or not ascii_alpha(value.front()) )
		return false;

	return std::ranges::all_of(value.substr(1), [](char item)
	{
		return ascii_alpha(item) or (item >= '0' and item <= '9') or
			item == '+' or item == '-' or item == '.';
	});
}

[[nodiscard]] bool ascii_hex(char value) noexcept
{
	return (value >= '0' and value <= '9') or
		   (value >= 'a' and value <= 'f') or
		   (value >= 'A' and value <= 'F');
}

[[nodiscard]] bool unreserved(char value) noexcept
{
	return ascii_alpha(value) or (value >= '0' and value <= '9') or
		   value == '-' or value == '.' or value == '_' or value == '~';
}

[[nodiscard]] bool valid_host(std::string_view value) noexcept
{
	return std::ranges::none_of(value, [](unsigned char item)
	{
		return item <= 0x20 or item == 0x7F or
			item == '[' or item == ']' or item == '/' or item == '?' or
			item == '#' or item == '@';
	});
}

[[nodiscard]] std::string normalize_encoded_component
(std::string_view value, std::string_view allowed)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string result;
	result.reserve(value.size());

	for(size_t i = 0; i < value.size(); ++i)
	{
		auto item = value[i];
		if( item == '%' )
		{
			if( i + 2 >= value.size() or not ascii_hex(value[i + 1]) or
				not ascii_hex(value[i + 2]) )
				invalid_argument::loc_throw("Invalid URL percent-encoding.");
			result.append(value.substr(i, 3));
			i += 2;
		}
		else if( unreserved(item) or allowed.find(item) != std::string_view::npos )
			result += item;
		else
		{
			auto byte = static_cast<unsigned char>(item);
			result += '%';
			result += hex[byte >> 4];
			result += hex[byte & 0x0F];
		}
	}
	return result;
}

[[nodiscard]] std::string remove_dot_segments(const std::string &input)
{
	std::string output;
	output.reserve(input.size());
	std::string_view remaining(input);

	const auto remove_last_segment = [&output]
	{
		auto pos = output.rfind('/');
		if( pos == std::string::npos )
			output.clear();
		else
			output.erase(pos);
	};
	while( not remaining.empty() )
	{
		if( remaining.starts_with("../") )
			remaining.remove_prefix(3);

		else if( remaining.starts_with("./") or remaining.starts_with("/./") )
			remaining.remove_prefix(2);

		else if( remaining == "/." )
			remaining.remove_suffix(1);

		else if( remaining.starts_with("/../") )
		{
			remaining.remove_prefix(3);
			remove_last_segment();
		}
		else if( remaining == "/.." )
		{
			remaining.remove_suffix(2);
			remove_last_segment();
		}
		else if( remaining == "." or remaining == ".." )
			remaining = {};
		else
		{
			auto end = remaining.front() == '/' ?
				remaining.find('/', 1) : remaining.find('/');

			if( end == std::string::npos )
			{
				output += remaining;
				remaining = {};
			}
			else
			{
				output += remaining.substr(0, end);
				remaining.remove_prefix(end);
			}
		}
	}
	return output;
}

} //namespace

class RIWO_DECL_HIDDEN url::impl
{
	RIWO_DISABLE_MOVE(impl)

public:
	explicit impl(std::string_view url = {}) {
		set(url);
	}
	impl(const impl &other) = default;
	impl &operator=(const impl &other) = default;

public:
	void set(std::string_view url)
	{
		reset();
		if( url.empty() )
			return ;

		m_valid = false;
		try {
			parse(url);
			refresh_validity();
		}
		catch(const std::invalid_argument&) {
			invalidate();
		}
	}

	void set_path(std::string_view path)
	{
		auto value = from_percent_encoding(strtls::trimmed(path));
		if( value.empty() )
			value = "/";

		else if( not value.starts_with('/') )
			value.insert(value.begin(), '/');

		m_path = std::move(value);
		m_encoded_path = to_percent_encoding(m_path, "!$&'()*+,;=:@/");
		refresh_validity();
	}

	void set_encoded_path(std::string_view path)
	{
		auto value = strtls::trimmed(path);
		if( value.empty() )
			value = "/";
		else if( not value.starts_with('/') )
			value.insert(value.begin(), '/');

		m_encoded_path = normalize_encoded_component (
			value, "!$&'()*+,;=:@/"
		);
		m_path = from_percent_encoding(m_encoded_path);
		refresh_validity();
	}

	void set_fragment(std::string_view fragment)
	{
		m_has_fragment = true;
		m_fragment = from_percent_encoding(fragment);

		m_encoded_fragment = to_percent_encoding (
			m_fragment, "!$&'()*+,;=:@/?"
		);
	}

	void set_encoded_fragment(std::string_view fragment)
	{
		m_has_fragment = true;
		m_encoded_fragment = normalize_encoded_component (
			fragment, "!$&'()*+,;=:@/?"
		);
		m_fragment = from_percent_encoding(m_encoded_fragment);
	}

	void clear_fragment() noexcept
	{
		m_has_fragment = false;
		m_fragment.clear();
		m_encoded_fragment.clear();
	}

	[[nodiscard]] bool query_unchanged() const noexcept
	{
		return m_parameters == m_parsed_parameters;
	}

	[[nodiscard]] bool has_query() const noexcept
	{
		return query_unchanged() ? m_has_query : not m_parameters.empty();
	}

	[[nodiscard]] std::string encoded_query() const
	{
		if( query_unchanged() )
			return m_encoded_query;

		std::string result;
		for(auto &[key,value] : m_parameters)
		{
			result += to_percent_encoding(key) + "=" +
				to_percent_encoding(value.to_string()) + "&";
		}
		if( not result.empty() )
			result.pop_back();
		return result;
	}

	void refresh_validity() noexcept
	{
		m_valid = not m_protocol.empty() and not m_path.empty() and
			m_path.front() == '/' and valid_host(m_host);
	}

private:
	void parse(std::string_view url)
	{
		auto resource = set_header(strtls::trimmed(url));
		auto fragment = resource.find('#');

		if( fragment != std::string::npos )
		{
			set_encoded_fragment(std::string_view(resource).substr(fragment + 1));
			resource.erase(fragment);
		}
		auto authority_end = resource.find_first_of("/?");
		auto authority = resource.substr(0, authority_end);

		auto path_query = authority_end == std::string::npos ?
			std::string("/") : resource.substr(authority_end);

		if( path_query.starts_with('?') )
			path_query.insert(path_query.begin(), '/');

		auto path = parse_parameters(std::move(path_query));
		set_encoded_path(path);
		m_parsed_parameters = m_parameters;

		if( authority.find('@') != std::string::npos )
			invalid_argument::loc_throw("Invalid URL authority.");

		m_port = 0;
		if( authority.starts_with('[') )
		{
			auto close = authority.find(']');
			if( close == std::string::npos )
				invalid_argument::loc_throw("Invalid IPv6 URL authority.");

			m_host = authority.substr(1, close - 1);
			if( close + 1 < authority.size() )
			{
				if( authority[close + 1] != ':' )
					invalid_argument::loc_throw("Invalid URL authority.");
				set_port_text(authority.substr(close + 2));
			}
		}
		else
		{
			auto colon = authority.rfind(':');
			if( colon != std::string::npos and authority.find(':') == colon )
			{
				m_host = authority.substr(0, colon);
				set_port_text(authority.substr(colon + 1));
			}
			else
				m_host = std::move(authority);
		}
	}

	void set_port_text(std::string_view text)
	{
		auto port = strtls::to_uint16(text);
		if( not port or *port == 0 )
			invalid_argument::loc_throw("Invalid URL port.");
		m_port = *port;
	}

	void reset()
	{
		m_protocol = "local";
		m_path = "/";
		m_encoded_path = "/";
		m_host.clear();
		m_port = 0;
		m_parameters.clear();
		m_parsed_parameters.clear();
		m_has_query = false;
		m_encoded_query.clear();
		clear_fragment();
		m_valid = true;
	}

	void invalidate() noexcept
	{
		m_protocol.clear();
		m_path.clear();
		m_encoded_path.clear();
		m_host.clear();
		m_port = 0;
		m_parameters.clear();
		m_parsed_parameters.clear();
		m_has_query = false;
		m_encoded_query.clear();
		clear_fragment();
		m_valid = false;
	}

	[[nodiscard]] std::string set_header(const std::string &resource_line)
	{
		auto scheme_end = resource_line.find("://");
		if( scheme_end == std::string::npos or scheme_end == 0 )
			invalid_argument::loc_throw("URL scheme is missing.");

		auto scheme = std::string_view(resource_line).substr(0, scheme_end);
		if( not valid_scheme(scheme) )
			invalid_argument::loc_throw("Invalid URL scheme.");

		m_protocol = strtls::to_lower(scheme);
		return resource_line.substr(scheme_end + 3);
	}

	[[nodiscard]] std::string parse_parameters(std::string resource_line)
	{
		auto pos = resource_line.find('?');
		if( pos == std::string::npos )
			return resource_line;

		auto addpth = resource_line.substr(0,pos);
		m_has_query = true;

		m_encoded_query = normalize_encoded_component (
			std::string_view(resource_line).substr(pos + 1),
			"!$&'()*+,;=:@/?"
		);
		for(auto parameters_string = m_encoded_query;
			auto &para_str : string_vector::from_string(parameters_string, "&"))
		{
			pos = para_str.find('=');
			if( pos == std::string::npos )
			{
				para_str = from_percent_encoding(para_str);
				auto key = para_str;
				m_parameters.emplace_back(std::move(key), std::move(para_str));
			}
			else
			{
				m_parameters.emplace_back (
					from_percent_encoding(para_str.substr(0, pos)),
					from_percent_encoding(para_str.substr(pos + 1))
				);
			}
		}
		return addpth;
	}

public:
	std::string m_protocol = "local";
	std::string m_path = "/";
	std::string m_encoded_path = "/";

	std::string m_host {};
	uint16_t m_port = 0;

	parameters_t m_parameters {};
	parameters_t m_parsed_parameters {};

	std::string m_fragment {};
	std::string m_encoded_fragment {};
	std::string m_encoded_query {};

	bool m_has_fragment = false;
	bool m_has_query = false;
	bool m_valid = true;
};

url::url(std::string_view url_text) :
	mutable_parameters(nullptr),
	m_impl(new impl(url_text))
{
	m_parameters = &m_impl->m_parameters;
}

url::url(const std::string &url_text) :
	url(std::string_view(url_text))
{

}

url::url(const char *url_text) :
	url(std::string_view(url_text))
{

}

url::url() : url(std::string_view())
{

}

url::~url()
{
	delete m_impl;
}

url::url(const url &other) :
	mutable_parameters(nullptr),
	m_impl(new impl(*other.m_impl))
{
	m_parameters = &m_impl->m_parameters;
}

url &url::operator=(const url &other)
{
	if( this != &other )
		*m_impl = *other.m_impl;
	return *this;
}

url::url(url &&other) noexcept :
	mutable_parameters(other.m_parameters),
	m_impl(other.m_impl)
{
	other.m_impl = new impl();
	other.m_parameters = &other.m_impl->m_parameters;
}

url &url::operator=(url &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	m_parameters = other.m_parameters;

	other.m_impl = new impl();
	other.m_parameters = &other.m_impl->m_parameters;
	return *this;
}

url &url::emplace(std::string_view url_text)
{
	m_impl->set(url_text);
	return *this;
}

url &url::set_address(std::string addr)
{
	m_impl->m_host = std::move(addr);
	m_impl->refresh_validity();
	return *this;
}

url &url::set_port(uint16_t port)
{
	m_impl->m_port = port;
	m_impl->refresh_validity();
	return *this;
}

url &url::set_path(std::string_view path)
{
	m_impl->set_path(path);
	return *this;
}

url &url::set_fragment(std::string_view fragment)
{
	m_impl->set_fragment(fragment);
	return *this;
}

url &url::clear_fragment() noexcept
{
	m_impl->clear_fragment();
	return *this;
}

std::string_view url::protocol() const noexcept
{
	return m_impl->m_protocol;
}

std::string_view url::host() const noexcept
{
	return m_impl->m_host;
}

uint16_t url::port() const noexcept
{
	return m_impl->m_port;
}

std::string_view url::path() const noexcept
{
	return m_impl->m_path;
}

std::string_view url::fragment() const noexcept
{
	return m_impl->m_fragment;
}

std::string_view url::encoded_path() const noexcept
{
	return m_impl->m_encoded_path;
}

std::string url::encoded_query() const
{
	return m_impl->encoded_query();
}

bool url::has_fragment() const noexcept
{
	return m_impl->m_has_fragment;
}

bool url::has_query() const noexcept
{
	return m_impl->has_query();
}

bool url::is_valid() const noexcept
{
	return m_impl->m_valid;
}

std::string url::to_string() const noexcept
{
	if( not is_valid() )
		return {};

	auto authority = m_impl->m_host;
	if( authority.find(':') != std::string::npos and not authority.starts_with('[') )
		authority = '[' + authority + ']';

	auto buf = std::format("{}://{}", m_impl->m_protocol, authority);
	if( m_impl->m_port != 0 )
		buf += ':' + std::to_string(m_impl->m_port);

	buf += m_impl->m_encoded_path;
	if( m_impl->has_query() )
		buf += '?' + m_impl->encoded_query();

	if( m_impl->m_has_fragment )
		buf += '#' + m_impl->m_encoded_fragment;
	return buf;
}

url::operator std::string() const noexcept
{
	return to_string();
}

url url::resolve(const url &base, std::string_view reference)
{
	auto value = strtls::trimmed(reference);
	if( auto scheme_end = value.find("://"); scheme_end != std::string::npos )
	{
		if( valid_scheme(std::string_view(value).substr(0, scheme_end)) )
			return { value };
	}
	auto host = std::string(base.host());
	if( host.find(':') != std::string::npos and not host.starts_with('[') )
		host = '[' + host + ']';

	auto origin = std::format("{}://{}", base.protocol(), host);
	if( base.port() != 0 )
		origin += ':' + std::to_string(base.port());

	if( value.starts_with("//") )
		return { std::string(base.protocol()) + ":" + value };

	if( value.empty() )
	{
		auto result = base;
		result.clear_fragment();
		return result;
	}
	std::string fragment_suffix;
	if( auto pos = value.find('#'); pos != std::string::npos )
	{
		fragment_suffix = value.substr(pos);
		value.erase(pos);
	}
	if( value.empty() )
	{
		auto result = base;
		result.clear_fragment();

		if( not fragment_suffix.empty() )
			result.m_impl->set_encoded_fragment(fragment_suffix.substr(1));
		return result;
	}
	std::string query_suffix;
	if( auto pos = value.find('?'); pos != std::string::npos )
	{
		query_suffix = value.substr(pos);
		value.erase(pos);
	}
	std::string path;
	if( value.empty() )
	{
		path = base.encoded_path();
		if( query_suffix.empty() and base.has_query() )
			query_suffix = '?' + base.encoded_query();
	}
	else if( value.front() == '/' )
		path = remove_dot_segments(value);
	else
	{
		path = base.encoded_path();
		path.erase(path.rfind('/') + 1);
		path += value;
		path = remove_dot_segments(path);
	}
	return { origin + path + query_suffix + fragment_suffix };
}

} //namespace riwo
