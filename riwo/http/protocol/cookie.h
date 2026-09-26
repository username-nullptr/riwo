// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_COOKIE_H
#define RIWO_HTTP_PROTOCOL_COOKIE_H

#include <riwo/http/global.h>
#include <riwo/core/value.h>

namespace riwo::http
{

struct cookie_attribute
{
static constexpr const char
	*domain    = "Domain"  ,
	*path      = "Path"    ,
	*size      = "Size"    ,
	*expires   = "Expires" ,
	*max_age   = "Max-Age" ,
	*http_only = "HttpOnly",
	*secure    = "Secure"  ,
	*same_site = "SameSite",
	*priority  = "Priority";
};

class RIWO_HTTP_API cookie final
{
public:
	using value_t = riwo::value;
	using attributes_t = value_map;

public:
	cookie();
	cookie(value_t value);
	~cookie();

	cookie(const cookie &other);
	cookie &operator=(const cookie &other);

	cookie(cookie &&other) noexcept;
	cookie &operator=(cookie &&other) noexcept;

public:
	cookie &set_value(value_t value) noexcept;
	cookie &operator=(value_t v) noexcept;

	template <typename T>
	[[nodiscard]] decltype(auto) value() requires
		core_concepts::value_get<T,char>;

	[[nodiscard]] value_t value() const noexcept;
	operator value_t() const noexcept;

public:
	[[nodiscard]] optional<std::string> domain() const noexcept;
	[[nodiscard]] optional<std::string> path() const noexcept;

	[[nodiscard]] optional<std::string> same_site() const noexcept;
	[[nodiscard]] optional<std::string> priority() const noexcept;

	[[nodiscard]] optional<uint64_t> expires() const noexcept;
	[[nodiscard]] optional<uint64_t> max_age() const noexcept;
	[[nodiscard]] optional<size_t> size() const noexcept;

	[[nodiscard]] optional<bool> http_only() const noexcept;
	[[nodiscard]] optional<bool> secure() const noexcept;

public:
	cookie &set_domain(value_t domain);
	cookie &set_path(value_t path);

	cookie &set_same_site(value_t sst);
	cookie &set_priority(value_t pt);

	cookie &set_expires(uint64_t seconds);
	cookie &set_max_age(uint64_t seconds);
	cookie &set_size(size_t size);

	cookie &set_http_only(bool flag);
	cookie &set_secure(bool flag);

public:
	cookie &unset_domain();
	cookie &unset_path();

	cookie &unset_same_site();
	cookie &unset_priority();

	cookie &unset_expires();
	cookie &unset_max_age();
	cookie &unset_size();

	cookie &unset_http_only();
	cookie &unset_secure();

public:
	cookie &set_attribute(core_concepts::text_p<char> auto &&key, value_t attr) noexcept;
	cookie &unset_attribute(const core_concepts::text_p<char> auto &key) noexcept;

	[[nodiscard]] optional<value_t> attribute(const core_concepts::text_p<char> auto &key) noexcept;
	[[nodiscard]] const attributes_t &attributes() const noexcept;
	[[nodiscard]] attributes_t &attributes() noexcept;

private:
	class impl;
	impl *m_impl;
};

using cookie_attributes = cookie::attributes_t;
using cookie_values = value_map;
using cookies = map<cookie>;

} //namespace riwo::http::concepts
#include <riwo/http/protocol/detail/cookie.h>


#endif //RIWO_HTTP_PROTOCOL_COOKIE_H
