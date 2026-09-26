// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_LOG_H
#define RIWO_UTILS_LOG_H

#include <riwo/utils/global.h>
#include <source_location>

namespace riwo::utils
{

#define riwo_utils_clog(Level, name, ...)    riwo::utils::logger::instance(name).write<Level>(riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)
#define riwo_utils_clog_trace(name, ...)     riwo::utils::logger::instance(name).trace       (riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)
#define riwo_utils_clog_debug(name, ...)     riwo::utils::logger::instance(name).debug       (riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)
#define riwo_utils_clog_info(name, ...)      riwo::utils::logger::instance(name).info        (riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)
#define riwo_utils_clog_warning(name, ...)   riwo::utils::logger::instance(name).warning     (riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)
#define riwo_utils_clog_error(name, ...)     riwo::utils::logger::instance(name).error       (riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)
#define riwo_utils_clog_critical(name, ...)  riwo::utils::logger::instance(name).critical    (riwo::utils::logger::source_loc(std::source_location::current().file_name(), std::source_location::current().function_name(), std::source_location::current().line()), __VA_ARGS__)

#define riwo_utils_log(Level, ...)    riwo_utils_clog         (Level, "default", __VA_ARGS__)
#define riwo_utils_log_trace(...)     riwo_utils_clog_trace   (       "default", __VA_ARGS__)
#define riwo_utils_log_debug(...)     riwo_utils_clog_debug   (       "default", __VA_ARGS__)
#define riwo_utils_log_info(...)      riwo_utils_clog_info    (       "default", __VA_ARGS__)
#define riwo_utils_log_warning(...)   riwo_utils_clog_warning (       "default", __VA_ARGS__)
#define riwo_utils_log_error(...)     riwo_utils_clog_error   (       "default", __VA_ARGS__)
#define riwo_utils_log_critical(...)  riwo_utils_clog_critical(       "default", __VA_ARGS__)

class RIWO_UTILS_API logger
{
	RIWO_DISABLE_COPY_MOVE(logger)
	explicit logger(std::string name);
	~logger();

public:
	[[nodiscard]] static std::vector<std::string> names() noexcept;
	static logger &instance(std::string_view name, bool create = true);
	static logger &instance(); // "default"

	enum class level_t {
		off, critical, error, warning, info, debug, trace
	};
	enum class time_mode_t {
		utc, local, utc_tz, local_tz
	};
	struct config_t
	{
		std::filesystem::path path {};
		time_mode_t time_mode = time_mode_t::local;
		 bool line_break = false;

		 struct {
			level_t console = level_t::info;
			level_t daily = level_t::info;
		 } level;

		 struct {
			size_t warning  = 32 * 1024 * 1024;
			size_t error    = 16 * 1024 * 1024;
			size_t critical =  8 * 1024 * 1024;
		 } max_file_size;

		 struct {
			size_t warning  = 16;
			size_t error    =  8;
			size_t critical =  4;
		 } max_file_count;
	};
	logger &set_config(config_t conf);
	[[nodiscard]] config_t config() const noexcept;

	logger &flush();
	[[nodiscard]] std::string_view name() const noexcept;

public:
	struct RIWO_UTILS_API source_loc
	{
		source_loc(const char *source_file, const char *source_func, int source_line);
		const char *file = nullptr;
		const char *func = nullptr;
		int line = 0;
	};

	template <typename...Args>
	using fmt_str_t = std::format_string<Args...>;

public:
	template <level_t Lv, typename Arg0, typename...Args>
	logger &write(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <level_t Lv, typename T>
	logger &write(const source_loc &loc, T &&msg);

	template <typename Arg0, typename...Args>
	logger &write(level_t lv, const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger &write(level_t lv, const source_loc &loc, T &&msg);

public:
	template <typename Arg0, typename...Args>
	logger &trace(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger &trace(const source_loc &loc, T &&msg);

public:
	template <typename Arg0, typename...Args>
	logger &debug(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger & debug(const source_loc &loc, T &&msg);

public:
	template <typename Arg0, typename...Args>
	logger &info(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger &info(const source_loc &loc, T &&msg);

public:
	template <typename Arg0, typename...Args>
	logger &warning(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger &warning(const source_loc &loc, T &&msg);

public:
	template <typename Arg0, typename...Args>
	logger &error(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger &error(const source_loc &loc, T &&msg);

public:
	template <typename Arg0, typename...Args>
	logger &critical(const source_loc &loc, fmt_str_t<Arg0,Args...> msg, Arg0 &&arg0, Args&&...args);

	template <typename T>
	logger &critical(const source_loc &loc, T &&msg);

private:
	[[nodiscard]] bool _enabled(level_t lv) const noexcept;
	void _log(level_t lv, const source_loc &loc, std::string_view msg) const;

	template <level_t Lv>
	static consteval void check_level();
	static void check_level(level_t lv);

private:
	class impl;
	impl *m_impl = nullptr;
};

} //namespace riwo::utils
#include <riwo/utils/detail/logger.h>


#endif //RIWO_UTILS_LOG_H
