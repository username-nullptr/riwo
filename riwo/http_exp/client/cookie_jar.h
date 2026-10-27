// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_COOKIE_JAR_H
#define RIWO_HTTP_EXP_CLIENT_COOKIE_JAR_H

#include <riwo/http_exp/protocol/cookie.h>
#include <riwo/http_exp/protocol/method.h>

namespace riwo::http_exp
{

enum class cookie_site_context {
	non_browser, same_site, cross_site
};

struct cookie_request_context
{
	cookie_site_context site = cookie_site_context::non_browser;
	method request_method { method::get };

	bool top_level_navigation = false;
	bool http_api = true;
};

struct cookie_entry
{
	using time_point_t = std::chrono::system_clock::time_point;

	std::string name {};
	riwo::value value {};

	std::string domain {};
	std::string path {"/"};

	optional<time_point_t> expires {};
	cookie_same_site same_site = cookie_same_site::unspecified;

	bool secure = false;
	bool http_only = false;

	bool host_only = true;
	bool persistent = false;

	uint64_t creation_index = 0;
	uint64_t last_access_index = 0;

	friend bool operator== (
		const cookie_entry&, const cookie_entry&
	) = default;
};

struct cookie_jar_config
{
	size_t max_cookie_size = 4096;
	size_t max_cookies_per_domain = 50;
	size_t max_cookies = 3000;

	optional<std::chrono::seconds> max_persistent_age {
		std::chrono::hours(24 * 400)
	};
	std::function<bool(std::string_view)> public_suffix {};
};

class RIWO_HTTP_EXP_API cookie_jar
{
	RIWO_DISABLE_COPY_MOVE(cookie_jar)

public:
	explicit cookie_jar(cookie_jar_config config = {});
	~cookie_jar();

	bool store(const url &origin, const response_cookie &value);
	size_t store(const url &origin, const fields &response_headers);

	[[nodiscard]] std::vector<cookie_pair> select (
		const url &target, const cookie_request_context &context = {}
	);
	void clear() noexcept;

	[[nodiscard]] std::vector<cookie_entry> entries() const;
	[[nodiscard]] size_t size() const;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

using cookie_jar_ptr = std::shared_ptr<cookie_jar>;

struct cookie_config
{
	bool enabled = true;
	std::shared_ptr<cookie_jar> jar {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_COOKIE_JAR_H
