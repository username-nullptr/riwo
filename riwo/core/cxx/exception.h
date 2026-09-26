// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_EXCEPTION_H
#define RIWO_CORE_CXX_EXCEPTION_H

#include <riwo/core/cxx/attributes.h>
#include <source_location>
#include <exception>
#include <format>

namespace riwo
{

[[nodiscard]] RIWO_CORE_VAPI std::string with_location (
	std::string_view msg = {}, std::source_location loc = std::source_location::current()
);

class RIWO_CORE_VAPI runtime_error : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
	~runtime_error() noexcept override = default;

	template <typename Arg0, typename...Args>
	runtime_error(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

	[[noreturn]] static void loc_throw(std::string_view msg,
		std::source_location loc = std::source_location::current()
	);
};

class RIWO_CORE_VAPI invalid_argument : public std::invalid_argument
{
public:
	using std::invalid_argument::invalid_argument;
	~invalid_argument() noexcept override = default;

	template <typename Arg0, typename...Args>
	invalid_argument(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

	[[noreturn]] static void loc_throw(std::string_view msg,
		std::source_location loc = std::source_location::current()
	);
};

class RIWO_CORE_VAPI logic_error : public std::logic_error
{
public:
	using std::logic_error::logic_error;
	~logic_error() noexcept override = default;

	template <typename Arg0, typename...Args>
	logic_error(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

	[[noreturn]] static void loc_throw(std::string_view msg,
		std::source_location loc = std::source_location::current()
	);
};

class RIWO_CORE_VAPI length_error : public std::length_error
{
public:
	using std::length_error::length_error;
	~length_error() noexcept override = default;

	template <typename Arg0, typename...Args>
	length_error(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

	[[noreturn]] static void loc_throw(std::string_view msg,
		std::source_location loc = std::source_location::current()
	);
};

class RIWO_CORE_VAPI out_of_range : public std::out_of_range
{
public:
	using std::out_of_range::out_of_range;
	~out_of_range() noexcept override = default;

	template <typename Arg0, typename...Args>
	out_of_range(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

	[[noreturn]] static void loc_throw(std::string_view msg,
		std::source_location loc = std::source_location::current()
	);
};

class RIWO_CORE_VAPI system_error : public std::system_error
{
public:
	using std::system_error::system_error;
	~system_error() noexcept override = default;

	template <typename Arg0, typename...Args>
	system_error(std::error_code ec, std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

	template <typename Arg0, typename...Args>
	system_error(int v, const std::error_category &ecat, std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args);

public:
	static void loc_throw(const std::error_code &ec,
		std::source_location loc = std::source_location::current()
	);
	static void loc_throw(const std::error_code &ec, std::string_view msg,
		std::source_location loc = std::source_location::current()
	);
};

} //namespace riwo
#include <riwo/core/cxx/detail/exception.h>


#endif //RIWO_CORE_CXX_EXCEPTION_H
