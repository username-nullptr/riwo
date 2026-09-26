// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "cookie_jar.h"
#include <riwo/http/protocol/utils/core/conditional.h>

namespace riwo::http { namespace
{

[[nodiscard]] std::string canonical_host(std::string_view host)
{
	auto result = strtls::to_lower(strtls::trimmed(host));
	while( not result.empty() and result.back() == '.' )
		result.pop_back();
	return result;
}

[[nodiscard]] bool domain_match(std::string_view host, std::string_view domain) noexcept
{
	return host == domain or (host.size() > domain.size() and
		host.ends_with(domain) and host[host.size() - domain.size() - 1] == '.');
}

[[nodiscard]] std::string default_path(std::string_view request_path)
{
	if( request_path.empty() or request_path.front() != '/' )
		return "/";
	auto slash = request_path.rfind('/');
	if( slash == 0 )
		return "/";
	return std::string(request_path.substr(0, slash));
}

[[nodiscard]] bool path_match(std::string_view request_path,
	std::string_view cookie_path) noexcept
{
	if( request_path == cookie_path )
		return true;
	if( not request_path.starts_with(cookie_path) )
		return false;
	return cookie_path.ends_with('/') or
		(request_path.size() > cookie_path.size() and
		 request_path[cookie_path.size()] == '/');
}

[[nodiscard]] optional<int64_t> signed_seconds(const value &input) noexcept
{
	auto text = strtls::trimmed(input.to_string());
	int64_t result = 0;

	if( auto [end,error] = std::from_chars(text.data(), text.data() + text.size(), result);
		error != std::errc() or end != text.data() + text.size() )
		return nullopt;
	return result;
}

[[nodiscard]] const value *attribute(const cookie &input, std::string_view name) noexcept
{
	for(auto &[key,item] : input.attributes())
	{
		if( strtls::to_lower(key) == strtls::to_lower(name) )
			return &item;
	}
	return nullptr;
}

[[nodiscard]] bool boolean_attribute(const cookie &input, std::string_view name) noexcept
{
	auto item = attribute(input, name);
	if( not item )
		return false;

	auto result = item->to_bool();
	return result and *result;
}

} //namespace

class RIWO_DECL_HIDDEN cookie_jar::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

private:
	struct cookie_key
	{
		std::string name {};
		std::string domain {};
		std::string path {};

		[[nodiscard]] bool operator==(const cookie_key&)
			const noexcept = default;
	};

	struct cookie_key_view
	{
		std::string_view name {};
		std::string_view domain {};
		std::string_view path {};
	};

	struct cookie_key_hash
	{
		using is_transparent = void;

		[[nodiscard]] size_t operator()(const cookie_key_view &key) const noexcept
		{
			auto seed = std::hash<std::string_view>{}(key.name);

			seed ^= std::hash<std::string_view>{}(key.domain) + 0x9e3779b9U +
				(seed << 6U) + (seed >> 2U);

			seed ^= std::hash<std::string_view>{}(key.path) + 0x9e3779b9U +
				(seed << 6U) + (seed >> 2U);

			return seed;
		}

		[[nodiscard]] size_t operator()(const cookie_key &key) const noexcept
		{
			return operator()(cookie_key_view {
				.name = key.name, .domain = key.domain, .path = key.path
			});
		}
	};

	struct cookie_key_equal
	{
		using is_transparent = void;

		[[nodiscard]] bool operator()(const cookie_key_view &lhs, const cookie_key_view &rhs) const noexcept
		{
			return lhs.name == rhs.name and lhs.domain == rhs.domain and
				lhs.path == rhs.path;
		}

		[[nodiscard]] bool operator()(const cookie_key &lhs, const cookie_key &rhs) const noexcept
		{
			return operator()
			(
				cookie_key_view {
					.name = lhs.name, .domain = lhs.domain, .path = lhs.path
				},
				cookie_key_view {
					.name = rhs.name, .domain = rhs.domain, .path = rhs.path
				}
			);
		}

		[[nodiscard]] bool operator()(const cookie_key &lhs, const cookie_key_view &rhs) const noexcept
		{
			return operator()
			(
				cookie_key_view {
					.name = lhs.name, .domain = lhs.domain, .path = lhs.path
				}, rhs
			);
		}

		[[nodiscard]] bool operator()(const cookie_key_view &lhs, const cookie_key &rhs) const noexcept
		{
			return operator()
			(
				lhs, cookie_key_view {
					.name = rhs.name, .domain = rhs.domain, .path = rhs.path
				}
			);
		}
	};

	struct transparent_string_hash
	{
		using is_transparent = void;

		[[nodiscard]] size_t operator()(std::string_view value) const noexcept {
			return std::hash<std::string_view>{}(value);
		}

		[[nodiscard]] size_t operator()(const std::string &value) const noexcept {
			return operator()(std::string_view(value));
		}
	};

	using entry_list = std::list<entry>;
	using entry_iterator = entry_list::iterator;
	using domain_entries = std::list<entry_iterator>;

	using domain_map = std::unordered_map <
		std::string, domain_entries, transparent_string_hash, std::equal_to<>
	>;
	using expiry_index = std::multimap<
		std::chrono::system_clock::time_point, entry_iterator
	>;
	using expiry_iterator = expiry_index::iterator;

public:
	impl() = default;

	[[nodiscard]] static cookie_key key_for(const entry &item)
	{
		return {
			.name = item.name, .domain = item.domain, .path = item.path
		};
	}

	[[nodiscard]] static cookie_key_view key_view_for(const entry &item) noexcept
	{
		return {
			.name = item.name, .domain = item.domain, .path = item.path
		};
	}

	entry_iterator erase(entry_iterator pos)
	{
		auto next = std::next(pos);
		if( auto expiry = m_expiry_positions.find(std::addressof(*pos));
			expiry != m_expiry_positions.end() )
		{
			m_expiry_index.erase(expiry->second);
			m_expiry_positions.erase(expiry);
		}
		if( auto key = m_key_index.find(key_view_for(*pos));
			key != m_key_index.end() )
			m_key_index.erase(key);

		auto domain = m_domain_index.find(pos->domain);
		auto location = m_domain_positions.find(std::addressof(*pos));

		if( domain != m_domain_index.end() and location != m_domain_positions.end() )
		{
			domain->second.erase(location->second);
			m_domain_positions.erase(location);

			if( domain->second.empty() )
				m_domain_index.erase(domain);
		}
		m_entries.erase(pos);
		return next;
	}

	void remove_expired(std::chrono::system_clock::time_point now)
	{
		while( not m_expiry_index.empty() and m_expiry_index.begin()->first <= now )
		{
			auto pos = m_expiry_index.begin()->second;
			erase(pos);
		}
	}

	void add_expiry(entry_iterator pos)
	{
		if( not pos->expires )
			return ;

		auto expiry = m_expiry_index.emplace(*pos->expires, pos);
		try {
			m_expiry_positions.emplace(std::addressof(*pos), expiry);
		}
		catch(...)
		{
			m_expiry_index.erase(expiry);
			throw;
		}
	}

	void replace(entry_iterator pos, entry item)
	{
		if( auto expiry = m_expiry_positions.find(std::addressof(*pos));
			expiry != m_expiry_positions.end() )
		{
			m_expiry_index.erase(expiry->second);
			m_expiry_positions.erase(expiry);
		}
		*pos = std::move(item);
		try {
			add_expiry(pos);
		}
		catch(...)
		{
			erase(pos);
			throw;
		}
	}

	void insert(entry item)
	{
		m_entries.emplace_back(std::move(item));
		auto inserted = std::prev(m_entries.end());
		auto domain_position = m_domain_index.end();
		bool domain_entry_inserted = false;
		bool position_inserted = false;
		bool expiry_inserted = false;
		try
		{
			auto [domain, created] = m_domain_index.try_emplace(inserted->domain);
			ignore_unused(created);

			domain_position = domain;
			domain->second.emplace_back(inserted);

			domain_entry_inserted = true;
			auto location = std::prev(domain->second.end());

			m_domain_positions.emplace(std::addressof(*inserted), location);
			position_inserted = true;

			m_key_index.emplace(key_for(*inserted), inserted);
			add_expiry(inserted);

			expiry_inserted = inserted->expires.has_value();
		}
		catch(...)
		{
			if( expiry_inserted )
			{
				auto expiry = m_expiry_positions.find(std::addressof(*inserted));
				if( expiry != m_expiry_positions.end() )
				{
					m_expiry_index.erase(expiry->second);
					m_expiry_positions.erase(expiry);
				}
			}
			if( auto key = m_key_index.find(key_view_for(*inserted)); key != m_key_index.end() )
				m_key_index.erase(key);

			if( position_inserted )
				m_domain_positions.erase(std::addressof(*inserted));

			if( domain_entry_inserted )
				domain_position->second.pop_back();

			if( domain_position != m_domain_index.end() and domain_position->second.empty() )
				m_domain_index.erase(domain_position);

			m_entries.erase(inserted);
			throw;
		}
	}

	void collect_domain(std::vector<const entry*> &result, std::string_view domain, bool exact_host) const
	{
		auto entries = m_domain_index.find(domain);
		if( entries == m_domain_index.end() )
			return ;

		for(auto pos : entries->second)
		{
			if( exact_host or not pos->host_only )
				result.emplace_back(std::addressof(*pos));
		}
	}

public:
	uint64_t m_creation_counter = 0;
	entry_list m_entries {};

	std::unordered_map<cookie_key,entry_iterator,
		cookie_key_hash, cookie_key_equal
	> m_key_index {};

	domain_map m_domain_index {};
	std::unordered_map <
		const entry*, domain_entries::iterator
	> m_domain_positions {};

	expiry_index m_expiry_index {};
	std::unordered_map <
		const entry*, expiry_iterator
	> m_expiry_positions {};

	// Every operation may expire entries and update several allocating indexes.
	std::mutex m_mutex {};
};

cookie_jar::cookie_jar() :
	m_impl(new impl())
{

}

cookie_jar::~cookie_jar()
{
	delete m_impl;
}

bool cookie_jar::store(const url &origin, std::string name, const cookie &value)
{
	if( name.empty() )
		return false;

	auto host = canonical_host(origin.host());
	if( host.empty() )
		return false;

	entry item {};
	item.name = std::move(name);
	item.value = value.value().to_string();
	item.domain = host;
	item.path = default_path(origin.path());
	item.secure = boolean_attribute(value, cookie_attribute::secure);
	item.http_only = boolean_attribute(value, cookie_attribute::http_only);

	if( item.secure and strtls::to_lower(origin.protocol()) != "https" )
		return false;

	if( auto domain = attribute(value, cookie_attribute::domain) )
	{
		auto candidate = canonical_host(domain->to_string());
		while( candidate.starts_with('.') )
			candidate.erase(candidate.begin());

		if( candidate.empty() or not domain_match(host, candidate) )
			return false;

		item.domain = std::move(candidate);
		item.host_only = false;
	}
	if( auto path = attribute(value, cookie_attribute::path) )
	{
		if( auto candidate = path->to_string();
			not candidate.empty() and candidate.front() == '/' )
			item.path = std::move(candidate);
	}
	auto now = std::chrono::system_clock::now();
	bool remove = false;
	bool valid_max_age = false;

	if( auto max_age = attribute(value, cookie_attribute::max_age) )
	{
		if( auto seconds = signed_seconds(*max_age) )
		{
			valid_max_age = true;
			remove = *seconds <= 0;

			if( not remove )
				item.expires = now + std::chrono::seconds(*seconds);
		}
	}
	if( not valid_max_age )
	{
		if( auto expires = attribute(value, cookie_attribute::expires) )
		{
			item.expires = parse_http_date(expires->to_string());
			if( not item.expires )
			{
				if( auto seconds = signed_seconds(*expires) )
					item.expires = std::chrono::system_clock::time_point(std::chrono::seconds(*seconds));
			}
			remove = item.expires and *item.expires <= now;
		}
	}
	std::scoped_lock lock(m_impl->m_mutex);
	m_impl->remove_expired(now);

	auto existing = m_impl->m_key_index.find(impl::key_view_for(item));
	if( remove )
	{
		if( existing != m_impl->m_key_index.end() )
			m_impl->erase(existing->second);
		return true;
	}
	if( existing != m_impl->m_key_index.end() )
	{
		item.creation_index = existing->second->creation_index;
		m_impl->replace(existing->second, std::move(item));
	}
	else
	{
		item.creation_index = m_impl->m_creation_counter++;
		m_impl->insert(std::move(item));
	}
	return true;
}

void cookie_jar::store(const url &origin, const std::vector<std::pair<std::string,cookie>> &values)
{
	for(auto &[name,item] : values)
		store(origin, name, item);
}

cookie_values cookie_jar::cookies_for(const url &target) const
{
	auto host = canonical_host(target.host());
	auto path = target.path().empty() ? std::string_view("/") : target.path();

	auto secure = strtls::to_lower(target.protocol()) == "https";
	auto now = std::chrono::system_clock::now();

	std::scoped_lock lock(m_impl->m_mutex);
	m_impl->remove_expired(now);

	std::vector<const entry*> matches {};
	for(std::string_view domain = host; not domain.empty(); )
	{
		m_impl->collect_domain(matches, domain, domain.size() == host.size());
		auto dot = domain.find('.');
		if( dot == std::string_view::npos )
			break;
		domain.remove_prefix(dot + 1);
	}
	std::erase_if(matches, [&](const entry *item) {
		return not path_match(path, item->path) or (item->secure and not secure);
	});
	std::ranges::sort(matches, [](const entry *lhs, const entry *rhs)
	{
		if( lhs->path.size() != rhs->path.size() )
			return lhs->path.size() > rhs->path.size();
		return lhs->creation_index < rhs->creation_index;
	});

	cookie_values result {};
	for(auto *item : matches)
	{
		if( not result.contains(item->name) )
			result[item->name] = item->value;
	}
	return result;
}

std::vector<cookie_jar::entry> cookie_jar::entries() const
{
	std::scoped_lock lock(m_impl->m_mutex);
	m_impl->remove_expired(std::chrono::system_clock::now());
	return {m_impl->m_entries.begin(), m_impl->m_entries.end()};
}

size_t cookie_jar::size() const
{
	std::scoped_lock lock(m_impl->m_mutex);
	m_impl->remove_expired(std::chrono::system_clock::now());
	return m_impl->m_entries.size();
}

void cookie_jar::clear() noexcept
{
	std::scoped_lock lock(m_impl->m_mutex);
	m_impl->m_entries.clear();
	m_impl->m_key_index.clear();
	m_impl->m_domain_index.clear();
	m_impl->m_domain_positions.clear();
	m_impl->m_expiry_index.clear();
	m_impl->m_expiry_positions.clear();
}

} //namespace riwo::http
