// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_EXCEPTION_H
#define RIWO_CORE_CXX_DETAIL_EXCEPTION_H

#include <riwo/core/cxx/formatter.h>

namespace riwo
{

inline std::string with_location(std::string_view msg, std::source_location loc)
{
	return std::format("{} | source: [{}:{}] ({})",
		msg, loc.file_name(), loc.line(), loc.function_name()
	);
}

template <typename Arg0, typename...Args>
runtime_error::runtime_error(std::format_string<Arg0, Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::runtime_error(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

inline void runtime_error::loc_throw(std::string_view msg, std::source_location loc)
{
	throw runtime_error(with_location(msg, loc));
}

template <typename Arg0, typename...Args>
invalid_argument::invalid_argument(std::format_string<Arg0, Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::invalid_argument(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

inline void invalid_argument::loc_throw(std::string_view msg, std::source_location loc)
{
	throw invalid_argument(with_location(msg, loc));
}

template <typename Arg0, typename...Args>
logic_error::logic_error(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::logic_error(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

inline void logic_error::loc_throw(std::string_view msg, std::source_location loc)
{
	throw logic_error(with_location(msg, loc));
}

template <typename Arg0, typename...Args>
length_error::length_error(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::length_error(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

inline void length_error::loc_throw(std::string_view msg, std::source_location loc)
{
	throw length_error(with_location(msg, loc));
}

template <typename Arg0, typename...Args>
out_of_range::out_of_range(std::format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::out_of_range(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

inline void out_of_range::loc_throw(std::string_view msg, std::source_location loc)
{
	throw out_of_range(with_location(msg, loc));
}

template <typename Arg0, typename...Args>
system_error::system_error(std::error_code ec, std::format_string<Arg0, Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::system_error(ec, std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

template <typename Arg0, typename...Args>
system_error::system_error(int v, const std::error_category &ecat, std::format_string<Arg0, Args...> fmt, Arg0 &&arg0, Args&&...args) :
	std::system_error(v, std::move(ecat), std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

inline void system_error::loc_throw(const std::error_code &ec, std::source_location loc)
{
	if( ec )
	{
		throw system_error(ec, std::format("source: [{}:{}] ({}) |",
			loc.file_name(), loc.line(), loc.function_name()
		));
	}
}

inline void system_error::loc_throw(const std::error_code &ec, std::string_view msg, std::source_location loc)
{
	if( ec )
	{
		throw system_error(ec, std::format("source: [{}:{}] ({}) | {}",
			loc.file_name(), loc.line(), loc.function_name(), msg
		));
	}
}

} //namespace riwo

namespace std
{

template <>
struct formatter<std::exception, char>
{
	auto format(const std::exception &ex, auto &context) const
	{
		return m_formatter.format(ex.what(), context);
	}

	constexpr auto parse(auto &context) noexcept
	{
		return m_formatter.parse(context);
	}

private:
	formatter<const char*> m_formatter;
};

template <>
struct formatter<riwo::system_error, char> : riwo::no_parse_formatter<char>
{
	auto format(const riwo::system_error &ex, auto &context) const
	{
		return format_to(context.out(), "{} ({})", ex.what(), ex.code().value());
	}
};

} //namespace std


#endif //RIWO_CORE_CXX_DETAIL_EXCEPTION_H
