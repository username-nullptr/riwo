// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] std::string_view same_site_text(http::cookie_same_site value) noexcept
{
	switch(value)
	{
	case http::cookie_same_site::strict     : return "Strict"     ;
	case http::cookie_same_site::lax        : return "Lax"        ;
	case http::cookie_same_site::none       : return "None"       ;
	case http::cookie_same_site::unspecified: return "Unspecified";
	}
	return "Unknown";
}

void print_cookie(const http::cookie_entry &cookie)
{
	std::cout << "  " << cookie.name << '=' << cookie.value.to_string()
		<< "; Domain=" << cookie.domain
		<< "; Path=" << cookie.path
		<< "; SameSite=" << same_site_text(cookie.same_site)
		<< (cookie.host_only ? "; HostOnly" : "")
		<< (cookie.secure ? "; Secure" : "")
		<< (cookie.http_only ? "; HttpOnly" : "")
		<< (cookie.persistent ? "; Persistent" : "; Session")
		<< '\n';
}

} //namespace

int main(int argc, const char *argv[])
try
{
	const std::string base = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080";

	http::client client;
	auto reply = client.get(base + "/login");

	auto body = reply.read_all();
	const auto store = client.cookie_store();

	std::cout << "Final HTTP status after redirect: "
		<< reply.status().code() << '\n';

	std::cout << "Server verification: "
		<< http::example::body_text(body);

	std::cout << "Stored cookies: " << store->size() << '\n';

	for(const auto &cookie : store->entries())
		print_cookie(cookie);
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "Cookie example failed: " << exception.what() << '\n';
	return 1;
}
