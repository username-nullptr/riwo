// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_URL_H
#define RIWO_CORE_URL_H

#include <riwo/core/container.h>

namespace riwo
{

class RIWO_CORE_API url : public mutable_parameters<url>
{
public:
	template <typename...Args>
	using format_string = std::format_string <
		std::type_identity_t<Args>...
	>;

public:
	template <typename Arg0, typename...Args>
	url(format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);
	url(std::string_view url_text);
	url(const std::string &url_text);
	url(const char *url_text);

	url();
	~url() override;

	url(const url &other);
	url &operator=(const url &other);

	url(url &&other) noexcept;
	url &operator=(url &&other) noexcept;

public:
	template <typename Arg0, typename...Args>
	url &emplace(format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);
	url &emplace(std::string_view url_text);

	url &set_address(std::string addr);
	url &set_port(uint16_t port);
	url &set_path(std::string_view path);
	url &set_fragment(std::string_view fragment);
	url &clear_fragment() noexcept;

public:
	[[nodiscard]] std::string_view protocol() const noexcept;
	[[nodiscard]] std::string_view host() const noexcept;
	[[nodiscard]] uint16_t port() const noexcept;
	[[nodiscard]] std::string_view path() const noexcept;

	[[nodiscard]] std::string_view fragment() const noexcept;
	[[nodiscard]] std::string_view encoded_path() const noexcept;
	[[nodiscard]] std::string encoded_query() const;

	[[nodiscard]] bool has_fragment() const noexcept;
	[[nodiscard]] bool has_query() const noexcept;

	[[nodiscard]] bool is_valid() const noexcept;

public:
	[[nodiscard]] std::string to_string() const noexcept;
	[[nodiscard]] explicit operator std::string() const noexcept;

	[[nodiscard]] static url resolve (
		const url &base, std::string_view reference
	);

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo
#include <riwo/core/detail/url.h>


#endif //RIWO_CORE_URL_H
