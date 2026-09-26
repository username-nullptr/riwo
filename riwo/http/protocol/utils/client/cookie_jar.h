// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CLIENT_COOKIE_JAR_H
#define RIWO_HTTP_PROTOCOL_UTILS_CLIENT_COOKIE_JAR_H

#include <riwo/http/protocol/cookie.h>
#include <riwo/core/url.h>

namespace riwo::http
{

class RIWO_HTTP_API cookie_jar
{
	RIWO_DISABLE_COPY_MOVE(cookie_jar)

public:
	struct entry
	{
		std::string name {};
		std::string value {};
		std::string domain {};
		std::string path {};

		optional <
			std::chrono::system_clock::time_point
		> expires {};

		bool secure = false;
		bool http_only = false;
		bool host_only = true;

		uint64_t creation_index = 0;
	};

public:
	cookie_jar();
	~cookie_jar();

	bool store(const url &origin,
		std::string name, const cookie &value
	);
	void store(const url &origin,
		const std::vector<std::pair<std::string,cookie>> &values
	);
	[[nodiscard]] cookie_values cookies_for(const url &target) const;
	[[nodiscard]] std::vector<entry> entries() const;

	[[nodiscard]] size_t size() const;
	void clear() noexcept;

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo::http

#endif //RIWO_HTTP_PROTOCOL_UTILS_CLIENT_COOKIE_JAR_H
