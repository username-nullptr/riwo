// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/ini.h>
#include <atomic>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#else
# include <unistd.h>
#endif

namespace riwo::detail
{

static ini_error_category

g_invalid_group {
	"invalid_group", "Invalid group"
},
g_no_group_specified {
	"no_group_specified", "No group specified"
},
g_invalid_key_value_line {
	"invalid_key_value_line", "Invalid key-value line"
},
g_key_is_empty {
	"invalid_key_value_line", "Invalid key-value line: Key is empty"
},
g_invalid_value {
	"invalid_key_value_line", "Invalid key-value line: Invalid value"
};

ini_error_category &ini_invalid_group() noexcept
{
	return g_invalid_group;
}

ini_error_category &ini_no_group_specified() noexcept
{
	return g_no_group_specified;
}

ini_error_category &ini_invalid_key_value_line() noexcept
{
	return g_invalid_key_value_line;
}

ini_error_category &ini_key_is_empty() noexcept
{
	return g_key_is_empty;
}

ini_error_category &ini_invalid_value() noexcept
{
	return g_invalid_value;
}

using path_t = std::filesystem::path;

path_t ini_tmp_file(const path_t &file_name)
{
	static std::atomic_uint64_t sequence {0};
	const auto unique = sequence.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
	auto result = file_name;
	result += L".riwo.tmp." + std::to_wstring(GetCurrentProcessId()) +
		L"." + std::to_wstring(unique);
#else //_WIN32
	auto result = file_name;
	result += ".riwo.tmp." + std::to_string(getpid()) +
		"." + std::to_string(unique);
#endif //_WIN32
	return result;
}

void ini_commit_file(const path_t &source, const path_t &destination,
	error_code &error) noexcept
{
	error.clear();
#ifdef _WIN32
	if( not MoveFileExW(source.c_str(), destination.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) )
	{
		error = error_code(std::error_code (
			static_cast<int>(GetLastError()), std::system_category()
		));
	}
#else //_WIN32
	std::filesystem::rename(source, destination, error);
#endif //_WIN32
}

asio::any_io_executor ini_io_executor() noexcept
{
	// epoll cannot provide asynchronous regular-file I/O, while Asio's native
	// file services are limited to IOCP and optional io_uring builds. Keep one
	// bounded worker so INI semantics remain identical on every platform.
	static asio::thread_pool pool(1);
	return pool.get_executor();
}

void ini_commit_io_work(std::function<void()> work)
{
	asio::post(ini_io_executor(), std::move(work));
}

void ini_cancellation_handler::operator()(asio::cancellation_type_t type) const noexcept
{
	if( (type & asio::cancellation_type::terminal) == asio::cancellation_type::none )
		return ;

	if( auto active = state.lock() )
		active->store(true, std::memory_order_release);
}

std::shared_ptr<std::atomic_bool> ini_cancellation_registry::add()
{
	auto state = std::make_shared<std::atomic_bool>(false);
	std::lock_guard lock(m_mutex);

	std::erase_if(m_operations, [](const auto &operation) {
		return operation.expired();
	});
	m_operations.emplace_back(state);
	return state;
}

void ini_cancellation_registry::cancel() noexcept
{
	std::lock_guard lock(m_mutex);
	for(auto &operation : m_operations)
	{
		if( auto active = operation.lock() )
			active->store(true, std::memory_order_release);
	}
	m_operations.clear();
}

error_code ini_stream_error() noexcept
{
	if( errno != 0 )
		return error_code(std::error_code(errno, std::generic_category()));
	return make_system_error_code(std::errc::io_error);
}

ini_tmp_guard::ini_tmp_guard(std::filesystem::path file_name) :
	m_file_name(std::move(file_name))
{

}

ini_tmp_guard::~ini_tmp_guard()
{
	if( not m_file_name.empty() )
	{
		error_code ignored; RIWO_UNUSED(ignored);
		std::filesystem::remove(m_file_name, ignored);
	}
}

void ini_tmp_guard::release() noexcept
{
	m_file_name.clear();
}

} //namespace riwo::detail
