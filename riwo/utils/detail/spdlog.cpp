// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

// Provide the single spdlog implementation used by riwo.utils.  This mirrors
// spdlog's compiled-library sources while keeping the resulting symbols in the
// Riwo utils library.

#ifndef SPDLOG_COMPILED_LIB
# error SPDLOG_COMPILED_LIB must be defined when compiling this file.
#endif //SPDLOG_COMPILED_LIB

#include <spdlog/async.h>
#include <spdlog/cfg/helpers-inl.h>

#include <spdlog/pattern_formatter-inl.h>
#include <spdlog/async_logger-inl.h>
#include <spdlog/logger-inl.h>
#include <spdlog/spdlog-inl.h>
#include <spdlog/common-inl.h>

#include <spdlog/sinks/rotating_file_sink-inl.h>
#include <spdlog/sinks/stdout_color_sinks-inl.h>
#include <spdlog/sinks/basic_file_sink-inl.h>
#include <spdlog/sinks/stdout_sinks-inl.h>
#include <spdlog/sinks/base_sink-inl.h>
#include <spdlog/sinks/sink-inl.h>

#ifdef _WIN32
# include <spdlog/sinks/wincolor_sink-inl.h>
#else //_WIN32
# include <spdlog/sinks/ansicolor_sink-inl.h>
#endif //_WIN32

#include <spdlog/details/periodic_worker-inl.h>
#include <spdlog/details/log_msg_buffer-inl.h>
#include <spdlog/details/file_helper-inl.h>
#include <spdlog/details/thread_pool-inl.h>
#include <spdlog/details/backtracer-inl.h>
#include <spdlog/details/registry-inl.h>
#include <spdlog/details/log_msg-inl.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os-inl.h>

template SPDLOG_API spdlog::logger::logger (
	std::string name, sinks_init_list::iterator begin, sinks_init_list::iterator end
);

template class SPDLOG_API spdlog::sinks::base_sink<std::mutex>;
template class SPDLOG_API spdlog::sinks::base_sink<spdlog::details::null_mutex>;

template class SPDLOG_API spdlog::sinks::basic_file_sink<std::mutex>;
template class SPDLOG_API spdlog::sinks::basic_file_sink<spdlog::details::null_mutex>;

template class SPDLOG_API spdlog::sinks::rotating_file_sink<std::mutex>;
template class SPDLOG_API spdlog::sinks::rotating_file_sink<spdlog::details::null_mutex>;

#ifdef _WIN32
template class SPDLOG_API spdlog::sinks::wincolor_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_sink<spdlog::details::console_nullmutex>;

template class SPDLOG_API spdlog::sinks::wincolor_stdout_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stdout_sink<spdlog::details::console_nullmutex>;

template class SPDLOG_API spdlog::sinks::wincolor_stderr_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stderr_sink<spdlog::details::console_nullmutex>;

#else //_WIN32
template class SPDLOG_API spdlog::sinks::ansicolor_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_sink<spdlog::details::console_nullmutex>;

template class SPDLOG_API spdlog::sinks::ansicolor_stdout_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_stdout_sink<spdlog::details::console_nullmutex>;

template class SPDLOG_API spdlog::sinks::ansicolor_stderr_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::ansicolor_stderr_sink<spdlog::details::console_nullmutex>;
#endif //_WIN32

template class SPDLOG_API spdlog::sinks::stdout_sink_base<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::stdout_sink_base<spdlog::details::console_nullmutex>;

template class SPDLOG_API spdlog::sinks::stdout_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::stdout_sink<spdlog::details::console_nullmutex>;

template class SPDLOG_API spdlog::sinks::stderr_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::stderr_sink<spdlog::details::console_nullmutex>;

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_mt<spdlog::synchronous_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_st<spdlog::synchronous_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_mt<spdlog::synchronous_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_st<spdlog::synchronous_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_mt<spdlog::async_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_st<spdlog::async_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_mt<spdlog::async_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_st<spdlog::async_factory>(const std::string&, spdlog::color_mode);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_logger_mt<spdlog::synchronous_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_logger_st<spdlog::synchronous_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_logger_mt<spdlog::synchronous_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_logger_st<spdlog::synchronous_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_logger_mt<spdlog::async_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_logger_st<spdlog::async_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_logger_mt<spdlog::async_factory>(const std::string&);

template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_logger_st<spdlog::async_factory>(const std::string&);
