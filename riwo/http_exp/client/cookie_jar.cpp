// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "cookie_jar.h"

namespace riwo::http_exp { namespace
{

[[nodiscard]] std::string lower(std::string_view value)
{
	std::string result(value);
	std::ranges::transform(result, result.begin(), [](unsigned char byte) {
		return static_cast<char>(std::tolower(byte));
	});
	return result;
}

[[nodiscard]] std::string canonical_host(std::string_view host)
{
	auto result = lower(host);
	while( not result.empty() and result.back() == '.' ) result.pop_back();
	return result;
}

[[nodiscard]] bool ip_address(std::string_view host) noexcept
{
	if( host.find(':') != std::string_view::npos ) return true;
	return not host.empty() and std::ranges::all_of(host, [](unsigned char byte) {
		return std::isdigit(byte) != 0 or byte == '.';
	});
}

[[nodiscard]] bool domain_match(std::string_view host,
	std::string_view domain) noexcept
{
	if( host == domain ) return true;
	return not ip_address(host) and host.size() > domain.size() and
		host.ends_with(domain) and
		host[host.size() - domain.size() - 1] == '.';
}

[[nodiscard]] std::string default_path(std::string_view request_path)
{
	if( request_path.empty() or request_path.front() != '/' ) return "/";
	const auto slash = request_path.rfind('/');
	if( slash == 0 ) return "/";
	return std::string(request_path.substr(0, slash));
}

[[nodiscard]] bool path_match(std::string_view request_path,
	std::string_view cookie_path) noexcept
{
	if( request_path == cookie_path ) return true;
	if( not request_path.starts_with(cookie_path) ) return false;
	return cookie_path.ends_with('/') or
		(request_path.size() > cookie_path.size() and
		 request_path[cookie_path.size()] == '/');
}

[[nodiscard]] bool secure_scheme(const riwo::url &value) noexcept
{
	try {
		return lower(value.protocol()) == "https";
	}
	catch(...) {
		return false;
	}
}

[[nodiscard]] bool applicable_same_site(const cookie_entry &item,
	const cookie_request_context &context) noexcept
{
	if( context.site != cookie_site_context::cross_site ) return true;
	if( item.same_site == cookie_same_site::none ) return true;
	if( item.same_site == cookie_same_site::strict ) return false;
	return context.http_api and context.top_level_navigation and
		context.request_method.is_safe();
}

[[nodiscard]] std::chrono::system_clock::time_point expiry_after(
	std::chrono::system_clock::time_point now,
	std::chrono::seconds age) noexcept
{
	using clock = std::chrono::system_clock;
	if( age.count() <= 0 ) return now;
	const auto available = std::chrono::duration_cast<std::chrono::seconds>(
		clock::time_point::max() - now);
	if( age >= available ) return clock::time_point::max();
	return now + age;
}

[[nodiscard]] bool earlier_access(const cookie_entry &lhs,
	const cookie_entry &rhs) noexcept
{
	if( lhs.last_access_index != rhs.last_access_index )
		return lhs.last_access_index < rhs.last_access_index;
	return lhs.creation_index < rhs.creation_index;
}

} //namespace

class RIWO_DECL_HIDDEN cookie_jar::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(cookie_jar_config value) : config(std::move(value)) {}

	void remove_expired(std::chrono::system_clock::time_point now)
	{
		std::erase_if(values, [now](const cookie_entry &item) {
			return item.expires and *item.expires <= now;
		});
	}

	void remove_excess_for_domain(std::string_view domain)
	{
		while( static_cast<size_t>(std::ranges::count_if(values,
			[domain](const cookie_entry &item) { return item.domain == domain; })) >
			config.max_cookies_per_domain )
		{
			auto candidate = values.end();
			for( auto iterator = values.begin(); iterator != values.end(); ++iterator )
			{
				if( iterator->domain != domain ) continue;
				if( candidate == values.end() or earlier_access(*iterator, *candidate) )
					candidate = iterator;
			}
			if( candidate == values.end() ) break;
			values.erase(candidate);
		}
	}

	void remove_excess()
	{
		while( values.size() > config.max_cookies )
		{
			auto candidate = std::min_element(values.begin(), values.end(),
				earlier_access);
			if( candidate == values.end() ) break;
			values.erase(candidate);
		}
	}

	cookie_jar_config config;
	std::vector<cookie_entry> values;
	uint64_t sequence = 0;
	mutable std::mutex mutex;
};

cookie_jar::cookie_jar(cookie_jar_config config) :
	m_impl(std::make_unique<impl>(std::move(config)))
{
}

cookie_jar::~cookie_jar() = default;

bool cookie_jar::store(const riwo::url &origin,
	const response_cookie &value)
{
	std::string serialized;
	try {
		serialized = serialize_set_cookie(value);
	}
	catch(const std::invalid_argument&) {
		return false;
	}
	if( value.name.empty() ) return false;
	if( serialized.size() > m_impl->config.max_cookie_size )
		return false;
	const auto host = canonical_host(origin.host());
	if( host.empty() ) return false;
	const auto secure_origin = secure_scheme(origin);
	if( value.secure and not secure_origin ) return false;
	if( value.same_site == cookie_same_site::none and not value.secure )
		return false;
	if( value.name.starts_with("__Secure-") and
		(not value.secure or not secure_origin) )
		return false;
	if( (value.name.starts_with("__Http-") or
		 value.name.starts_with("__Host-Http-")) and
		(not value.secure or not value.http_only or not secure_origin) )
		return false;

	cookie_entry item {
		.name = value.name,
		.value = value.value,
		.domain = host,
		.path = default_path(origin.encoded_path()),
		.same_site = value.same_site,
		.secure = value.secure,
		.http_only = value.http_only
	};
	if( value.domain )
	{
		if( value.domain->empty() or value.domain->back() == '.' ) return false;
		auto candidate = canonical_host(*value.domain);
		while( candidate.starts_with('.') ) candidate.erase(candidate.begin());
		if( candidate.empty() or not domain_match(host, candidate) ) return false;
		if( m_impl->config.public_suffix and
			m_impl->config.public_suffix(candidate) )
		{
			if( candidate != host ) return false;
		}
		else
		{
			item.domain = std::move(candidate);
			item.host_only = false;
		}
	}
	if( value.path and not value.path->empty() and value.path->front() == '/' )
		item.path = *value.path;
	if( value.name.starts_with("__Host-") and
		(not value.secure or not secure_origin or not item.host_only or
		 item.path != "/") )
		return false;

	const auto now = std::chrono::system_clock::now();
	bool remove = false;
	if( value.max_age )
	{
		item.persistent = true;
		if( value.max_age->count() <= 0 ) remove = true;
		else
		{
			auto age = *value.max_age;
			if( m_impl->config.max_persistent_age and
				age > *m_impl->config.max_persistent_age )
				age = *m_impl->config.max_persistent_age;
			if( age.count() <= 0 ) remove = true;
			else item.expires = expiry_after(now, age);
		}
	}
	else if( value.expires )
	{
		item.persistent = true;
		item.expires = *value.expires;
		if( m_impl->config.max_persistent_age )
		{
			const auto limit = expiry_after(now,
				*m_impl->config.max_persistent_age);
			if( *item.expires > limit ) item.expires = limit;
		}
		remove = *item.expires <= now;
	}

	std::scoped_lock lock(m_impl->mutex);
	m_impl->remove_expired(now);
	if( not secure_origin and std::ranges::any_of(m_impl->values,
		[&item](const cookie_entry &stored) {
			return stored.secure and stored.name == item.name and
				(domain_match(stored.domain, item.domain) or
				 domain_match(item.domain, stored.domain)) and
				path_match(item.path, stored.path);
		}) )
		return false;
	const auto stored_domain = item.domain;
	auto existing = std::ranges::find_if(m_impl->values,
		[&](const cookie_entry &stored) {
			return stored.name == item.name and stored.domain == item.domain and
				stored.path == item.path;
		});
	if( remove )
	{
		if( existing != m_impl->values.end() ) m_impl->values.erase(existing);
		return true;
	}
	if( existing != m_impl->values.end() )
	{
		item.creation_index = existing->creation_index;
		item.last_access_index = ++m_impl->sequence;
		*existing = std::move(item);
	}
	else
	{
		item.creation_index = ++m_impl->sequence;
		item.last_access_index = item.creation_index;
		m_impl->values.emplace_back(std::move(item));
	}
	m_impl->remove_excess_for_domain(stored_domain);
	m_impl->remove_excess();
	return true;
}

size_t cookie_jar::store(const riwo::url &origin,
	const fields &response_headers)
{
	size_t result = 0;
	for( const auto &header : response_headers.find_all("Set-Cookie") )
	{
		auto parsed = parse_set_cookie(header.value);
		if( parsed and store(origin, *parsed) ) ++result;
	}
	return result;
}

std::vector<cookie_pair> cookie_jar::select(const riwo::url &target,
	const cookie_request_context &context)
{
	const auto host = canonical_host(target.host());
	if( host.empty() ) return {};
	auto path = std::string(target.encoded_path());
	if( path.empty() or path.front() != '/' ) path = "/";
	const auto secure = secure_scheme(target);
	const auto now = std::chrono::system_clock::now();
	std::scoped_lock lock(m_impl->mutex);
	m_impl->remove_expired(now);
	std::vector<cookie_entry*> matches;
	for( auto &item : m_impl->values )
	{
		if( item.host_only ? item.domain != host :
			not domain_match(host, item.domain) )
			continue;
		if( not path_match(path, item.path) or (item.secure and not secure) )
			continue;
		if( item.http_only and not context.http_api ) continue;
		if( not applicable_same_site(item, context) ) continue;
		item.last_access_index = ++m_impl->sequence;
		matches.emplace_back(std::addressof(item));
	}
	std::ranges::sort(matches, [](const cookie_entry *lhs,
		const cookie_entry *rhs) {
		if( lhs->path.size() != rhs->path.size() )
			return lhs->path.size() > rhs->path.size();
		return lhs->creation_index < rhs->creation_index;
	});
	std::vector<cookie_pair> result;
	result.reserve(matches.size());
	for( const auto *item : matches )
		result.push_back({item->name, item->value});
	return result;
}

std::vector<cookie_entry> cookie_jar::entries() const
{
	std::scoped_lock lock(m_impl->mutex);
	m_impl->remove_expired(std::chrono::system_clock::now());
	return m_impl->values;
}

size_t cookie_jar::size() const
{
	std::scoped_lock lock(m_impl->mutex);
	m_impl->remove_expired(std::chrono::system_clock::now());
	return m_impl->values.size();
}

void cookie_jar::clear() noexcept
{
	std::scoped_lock lock(m_impl->mutex);
	m_impl->values.clear();
}

} //namespace riwo::http_exp
