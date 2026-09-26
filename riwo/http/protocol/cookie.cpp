// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "cookie.h"

namespace riwo::http
{

class RIWO_DECL_HIDDEN cookie::impl
{
public:
	impl() = default;
	explicit impl(value_t value) :
		m_value(std::move(value)) {}

	impl(const impl &other) = default;
	impl &operator=(const impl &other) = default;

	impl(impl &&other) noexcept = default;
	impl &operator=(impl &&other) noexcept = default;

public:
	value_t m_value;
	attributes_t m_attributes {
		{ cookie_attribute::path, "/" }
	};
};

cookie::cookie() :
	m_impl(new impl())
{

}

cookie::cookie(value_t value) :
	m_impl(new impl(std::move(value)))
{

}

cookie::~cookie()
{
	delete m_impl;
}

cookie::cookie(const cookie &other) :
	m_impl(new impl(*other.m_impl))
{

}

cookie &cookie::operator=(const cookie &other)
{
	if( this != &other )
		*m_impl = *other.m_impl;
	return *this;
}

cookie::cookie(cookie &&other) noexcept :
	m_impl(new impl(std::move(*other.m_impl)))
{

}

cookie &cookie::operator=(cookie &&other) noexcept
{
	if( this != &other )
		*m_impl = std::move(*other.m_impl);
	return *this;
}

cookie &cookie::set_value(value_t value) noexcept
{
	m_impl->m_value = std::move(value);
	return *this;
}

cookie &cookie::operator=(value_t v) noexcept
{
	m_impl->m_value = std::move(v);
	return *this;
}

cookie::value_t cookie::value() const noexcept
{
	return m_impl->m_value;
}

cookie::operator value_t() const noexcept
{
	return m_impl->m_value;
}

optional<std::string> cookie::domain() const noexcept
{
	auto it = attributes().find(cookie_attribute::domain);
	return it == attributes().end() ?
		optional<std::string>() : riwo::make_optional(*it->second);
}

optional<std::string> cookie::path() const noexcept
{
	auto it = attributes().find(cookie_attribute::path);
	return it == attributes().end() ?
		optional<std::string>() : riwo::make_optional(*it->second);
}

optional<std::string> cookie::same_site() const noexcept
{
	auto it = attributes().find(cookie_attribute::same_site);
	return it == attributes().end() ?
		optional<std::string>() : riwo::make_optional(*it->second);
}

optional<std::string> cookie::priority() const noexcept
{
	auto it = attributes().find(cookie_attribute::priority);
	return it == attributes().end() ?
		optional<std::string>() : riwo::make_optional(*it->second);
}

optional<uint64_t> cookie::expires() const noexcept
{
	auto it = attributes().find(cookie_attribute::expires);
	return it == attributes().end() ?
		optional<uint64_t>() : it->second.get<uint64_t>();
}

optional<uint64_t> cookie::max_age() const noexcept
{
	auto it = attributes().find(cookie_attribute::max_age);
	return it == attributes().end() ?
		optional<uint64_t>() : it->second.get<uint64_t>();
}

optional<size_t> cookie::size() const noexcept
{
	auto it = attributes().find(cookie_attribute::size);
	return it == attributes().end() ?
		optional<size_t>() : it->second.get<size_t>();
}

optional<bool> cookie::http_only() const noexcept
{
	auto it = attributes().find(cookie_attribute::http_only);
	return it == attributes().end() ?
		optional<bool>() : it->second.to_bool();
}

optional<bool> cookie::secure() const noexcept
{
	auto it = attributes().find(cookie_attribute::secure);
	return it == attributes().end() ?
		optional<bool>() : it->second.to_bool();
}

cookie &cookie::set_domain(value_t domain)
{
	attributes()[cookie_attribute::domain] = std::move(domain);
	return *this;
}

cookie &cookie::set_path(value_t path)
{
	attributes()[cookie_attribute::path] = std::move(path);
	return *this;
}

cookie &cookie::set_same_site(value_t sst)
{
	attributes()[cookie_attribute::same_site] = std::move(sst);
	return *this;
}

cookie &cookie::set_priority(value_t pt)
{
	attributes()[cookie_attribute::priority] = std::move(pt);
	return *this;
}

cookie &cookie::set_expires(uint64_t seconds)
{
	attributes()[cookie_attribute::expires] = seconds;
	return *this;
}

cookie &cookie::set_max_age(uint64_t seconds)
{
	attributes()[cookie_attribute::max_age] = seconds;
	return *this;
}

cookie &cookie::set_size(size_t size)
{
	attributes()[cookie_attribute::size] = size;
	return *this;
}

cookie &cookie::set_http_only(bool flag)
{
	attributes()[cookie_attribute::http_only] = flag;
	return *this;
}

cookie &cookie::set_secure(bool flag)
{
	attributes()[cookie_attribute::secure] = flag;
	return *this;
}

cookie &cookie::unset_domain()
{
	attributes().erase(cookie_attribute::domain);
	return *this;
}

cookie &cookie::unset_path()
{
	attributes().erase(cookie_attribute::path);
	return *this;
}

cookie &cookie::unset_same_site()
{
	attributes().erase(cookie_attribute::same_site);
	return *this;
}

cookie &cookie::unset_priority()
{
	attributes().erase(cookie_attribute::priority);
	return *this;
}

cookie &cookie::unset_expires()
{
	attributes().erase(cookie_attribute::expires);
	return *this;
}

cookie &cookie::unset_max_age()
{
	attributes().erase(cookie_attribute::max_age);
	return *this;
}

cookie &cookie::unset_size()
{
	attributes().erase(cookie_attribute::size);
	return *this;
}

cookie &cookie::unset_http_only()
{
	attributes().erase(cookie_attribute::http_only);
	return *this;
}

cookie &cookie::unset_secure()
{
	attributes().erase(cookie_attribute::secure);
	return *this;
}

const cookie::attributes_t &cookie::attributes() const noexcept
{
	return m_impl->m_attributes;
}

cookie::attributes_t &cookie::attributes() noexcept
{
	return m_impl->m_attributes;
}

} //namespace riwo::http
