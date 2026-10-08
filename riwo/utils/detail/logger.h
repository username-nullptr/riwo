// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_DETAIL_LOG_H
#define RIWO_UTILS_DETAIL_LOG_H

namespace riwo::utils
{

template <logger::level_t Lv, typename Arg0, typename...Args>
logger &logger::write(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	check_level<Lv>();
	if( not _enabled(Lv) )
		return *this;

	_log(Lv, loc, std::format(std::move(msg), std::forward<Arg0>(arg0), std::forward<Args>(args)...));
	return *this;
}

template <logger::level_t Lv, typename T>
logger &logger::write(const source_loc &loc, T &&msg)
{
	check_level<Lv>();
	if( not _enabled(Lv) )
		return *this;

	if constexpr( concepts::string_p<T,char> )
		_log(Lv, loc, std::string_view(std::forward<T>(msg)));
	else
		_log(Lv, loc, std::format("{}", std::forward<T>(msg)));
	return *this;
}

template <typename Arg0, typename...Args>
logger &logger::write(level_t lv, const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	check_level(lv);
	if( not _enabled(lv) )
		return *this;

	_log(lv, loc, std::format(std::move(msg), std::forward<Arg0>(arg0), std::forward<Args>(args)...));
	return *this;
}

template <typename T>
logger &logger::write(level_t lv, const source_loc &loc, T &&msg)
{
	check_level(lv);
	if( not _enabled(lv) )
		return *this;

	if constexpr( concepts::string_p<T,char> )
		_log(lv, loc, std::string_view(std::forward<T>(msg)));
	else
		_log(lv, loc, std::format("{}", std::forward<T>(msg)));
	return *this;
}

template <typename Arg0, typename...Args>
logger &logger::trace(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	return write<level_t::trace>(std::move(loc), msg, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename T>
logger &logger::trace(const source_loc &loc, T &&msg)
{
	return write<level_t::trace>(std::move(loc), std::forward<T>(msg));
}

template <typename Arg0, typename...Args>
logger &logger::debug(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	return write<level_t::debug>(std::move(loc), msg, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename T>
logger &logger::debug(const source_loc &loc, T &&msg)
{
	return write<level_t::debug>(std::move(loc), std::forward<T>(msg));
}

template <typename Arg0, typename...Args>
logger &logger::info(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	return write<level_t::info>(std::move(loc), msg, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename T>
logger &logger::info(const source_loc &loc, T &&msg)
{
	return write<level_t::info>(std::move(loc), std::forward<T>(msg));
}

template <typename Arg0, typename...Args>
logger &logger::warning(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	return write<level_t::warning>(std::move(loc), msg, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename T>
logger &logger::warning(const source_loc &loc, T &&msg)
{
	return write<level_t::warning>(std::move(loc), std::forward<T>(msg));
}

template <typename Arg0, typename...Args>
logger &logger::error(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	return write<level_t::error>(std::move(loc), msg, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename T>
logger &logger::error(const source_loc &loc, T &&msg)
{
	return write<level_t::error>(std::move(loc), std::forward<T>(msg));
}

template <typename Arg0, typename...Args>
logger &logger::critical(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args)
{
	return write<level_t::critical>(std::move(loc), msg, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename T>
logger &logger::critical(const source_loc &loc, T &&msg)
{
	return write<level_t::critical>(std::move(loc), std::forward<T>(msg));
}

template <logger::level_t Lv>
consteval void logger::check_level()
{
	static_assert (
		Lv == level_t::trace or
		Lv == level_t::debug or
		Lv == level_t::info or
		Lv == level_t::warning or
		Lv == level_t::error or
		Lv == level_t::critical,
		"logger: Code bug: Invalid level."
	);
}

} //namespace riwo::utils


#endif //RIWO_UTILS_DETAIL_LOG_H
