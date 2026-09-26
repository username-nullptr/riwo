// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "settings.h"
#include "logger.h"

using namespace std::chrono_literals;
using namespace riwo::operators;

namespace riwo::utils { namespace
{

using path_t = settings::path_t;

void notify_loaded
(settings *owner, const path_t &file_name, error_code error) noexcept
{
	if( error )
	{
		riwo_utils_clog_error("Riwo.Utils",
			"settings: load file '{}' failed: '{}'.",
			file_name.empty() ? owner->file_name() : file_name, error
		);
	}
	RIWO_UNUSED(owner->loaded(error));
}

void notify_synced
(settings *owner, const path_t &file_name, error_code error) noexcept
{
	if( error )
	{
		riwo_utils_clog_error("Riwo.Utils",
			"settings: sync file '{}' failed: '{}'.",
			file_name.empty() ? owner->file_name() : file_name, error
		);
	}
	RIWO_UNUSED(owner->synced(error));
}

} //namespace

settings::settings(std::string name) :
	m_impl(new impl(std::move(name)))
{
	m_impl->m_ini.set_sync_on_delete(true);
	m_impl->m_ini.set_sync_period(5s);
}

settings::~settings()
{
	delete m_impl;
}

namespace {
struct RIWO_DECL_HIDDEN no_deleter {
	void operator()(settings*) const {}
};
} //namespace

using settings_ptr = std::unique_ptr<settings, no_deleter>;

static std::map<std::string, settings_ptr> g_instances;
static shared_mutex g_instances_lock;

settings &settings::instance(std::string_view name, bool create)
{
	std::string _name(name.data(), name.size());
	{
		std::shared_lock locker(g_instances_lock);
		if( auto it = g_instances.find(_name); it != g_instances.end() )
			return *it->second;
	}
	if( create )
	{
		std::unique_lock locker(g_instances_lock);
		if( auto it = g_instances.find(_name); it != g_instances.end() )
			return *it->second;

		settings_ptr object(new settings(_name), no_deleter());
		auto it = g_instances.emplace(std::move(_name), std::move(object)).first;
		return *it->second;
	}

	runtime_error::loc_throw(std::format (
		"riwo::utils::settings::instance: Instance '{}' does not exist.", name
	));
	// return {};
}

settings &settings::instance()
{
	return instance("default");
}

static std::map<std::filesystem::path, const settings*> g_file_paths;
static std::mutex g_file_paths_lock;

error_code settings::impl::claim_file(const settings *owner, const path_t &file_name) noexcept
{
	if( file_name.empty() )
		return {};
	try {
		std::unique_lock locker(g_file_paths_lock);

		if( auto [it, inserted] = g_file_paths.emplace(file_name, owner);
			not inserted and it->second != owner )
			return make_system_error_code(std::errc::device_or_resource_busy);
	}
	catch(...) {
		return exception_error(std::current_exception());
	}
	return {};
}

std::shared_ptr<settings::ini_t> settings::impl::snapshot_ini
(const path_t &file_name, bool replace_file_name, bool copy_data)
{
	std::shared_lock locker(m_ini_lock);
	RIWO_UNUSED(locker);

	const auto path = replace_file_name ?
		file_name : m_ini.file_name();

	if( copy_data )
		return std::make_shared<ini_t>(m_ini.get_executor(), m_ini.data(), path);

	return std::make_shared<ini_t>(m_ini.get_executor(), path);
}

void settings::impl::adopt_ini(ini_t &&source, bool merge_data)
{
	std::unique_lock locker(m_ini_lock);
	RIWO_UNUSED(locker);

	auto source_data = merge_data ?
		source.data() : ini_t::data_t{};

	if( m_ini.file_name() == source.file_name() )
	{
		if( merge_data )
			m_ini.set_data(std::move(source_data));
		return ;
	}
	const auto period = m_ini.sync_period();
	const auto sync_on_delete = m_ini.sync_on_delete();
	auto current_data = m_ini.data();

	m_ini.set_sync_period(0s);
	m_ini.set_sync_on_delete(false);

	source.clear();
	source.set_data(std::move(current_data));

	if( merge_data )
		source.set_data(std::move(source_data));

	source.set_sync_on_delete(sync_on_delete);
	source.set_sync_period(period);
	m_ini = std::move(source);
}

sys_expected<> settings::impl::load_sync
(settings *owner, const path_t &file_name, bool replace_file_name, bool ignore_missing, error_code error)
{
	if( not error )
	{
		try {
			std::unique_lock locker(m_ini_lock);
			RIWO_UNUSED(locker);

			if( ignore_missing )
			{
				if( replace_file_name )
					m_ini.load_or(file_name, error);
				else
					m_ini.load_or(error);
			}
			else
			{
				if( replace_file_name )
					m_ini.load(file_name, error);
				else
					m_ini.load(error);
			}
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
	}
	notify_loaded(owner, file_name, error);
	return error ? sys_expected<>(sys_unexpected(error)) : make_sys_expected();
}

sys_expected<> settings::impl::sync_sync
(settings *owner, const path_t &file_name, bool replace_file_name, error_code error)
{
	if( not error )
	{
		try {
			std::unique_lock locker(m_ini_lock);
			RIWO_UNUSED(locker);

			if( replace_file_name )
				m_ini.sync(file_name, error);
			else
				m_ini.sync(error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
	}
	notify_synced(owner, file_name, error);
	return error ? sys_expected<>(sys_unexpected(error)) : make_sys_expected();
}

void settings::impl::start_load(settings *owner, path_t file_name,
	bool replace_file_name, bool ignore_missing, error_code prepare_error, io_handler_t handler)
{
	start_load_impl(owner, std::move(file_name), replace_file_name,
		ignore_missing, prepare_error, std::move(handler)
	);
}

void settings::impl::start_load(settings *owner, path_t file_name,
	bool replace_file_name, bool ignore_missing, error_code prepare_error, expected_handler_t handler)
{
	start_load_impl(owner, std::move(file_name), replace_file_name,
		ignore_missing, prepare_error, std::move(handler)
	);
}

void settings::impl::start_sync
(settings *owner, path_t file_name, bool replace_file_name, error_code prepare_error, io_handler_t handler)
{
	start_sync_impl(owner, std::move(file_name), replace_file_name,
		prepare_error, std::move(handler)
	);
}

void settings::impl::start_sync
(settings *owner, path_t file_name, bool replace_file_name, error_code prepare_error, expected_handler_t handler)
{
	start_sync_impl(owner, std::move(file_name), replace_file_name,
		prepare_error, std::move(handler)
	);
}

template <typename Handler>
void settings::impl::complete(Handler &&handler, error_code error)
{
	if constexpr( std::is_same_v<std::remove_cvref_t<Handler>,expected_handler_t> )
	{
		auto result = error ?
			sys_expected<>(sys_unexpected(error)) : make_sys_expected();
		std::forward<Handler>(handler)(error_code{}, std::move(result));
	}
	else
		std::forward<Handler>(handler)(error);
}

template <typename Handler>
void settings::impl::post_error
(settings *owner, path_t file_name, error_code error, bool loading, Handler handler)
{
	auto fallback_exec = m_ini.get_executor();
	auto exec = asio::get_associated_executor(handler, fallback_exec);
	auto allocator = asio::get_associated_allocator(handler);

	auto work = std::make_shared<decltype(asio::make_work_guard(handler))>(
		asio::make_work_guard(handler)
	);
	asio::post(fallback_exec, [owner, file_name = std::move(file_name),
		error, loading, exec, allocator, work, handler = std::move(handler)]() mutable
	{
		exec.execute(asio::bind_allocator(allocator,
		[owner, file_name = std::move(file_name), error, loading, work, handler = std::move(handler)]
		() mutable
		{
			RIWO_UNUSED(work);
			if( loading )
				notify_loaded(owner, file_name, error);
			else
				notify_synced(owner, file_name, error);
			complete(std::move(handler), error);
		}));
	});
}

template <typename Handler>
void settings::impl::start_load_impl(settings *owner, path_t file_name,
	bool replace_file_name, bool ignore_missing, error_code prepare_error, Handler handler)
{
	if( prepare_error )
	{
		post_error(owner, std::move(file_name),
			prepare_error, true, std::move(handler)
		);
		return ;
	}
	std::shared_ptr<ini_t> source;
	try {
		source = snapshot_ini(file_name, replace_file_name, false);
	}
	catch(...)
	{
		post_error(owner, std::move(file_name),
			exception_error(std::current_exception()), true, std::move(handler)
		);
		return ;
	}
	auto source_exec = source->get_executor();

	auto exec = asio::get_associated_executor(handler, source_exec);
	auto allocator = asio::get_associated_allocator(handler);

	auto slot = asio::get_associated_cancellation_slot(handler);
	auto work = std::make_shared<decltype(asio::make_work_guard(handler))>(
		asio::make_work_guard(handler)
	);
	auto handler_state = std::make_shared<Handler>(std::move(handler));
	auto completed = std::make_shared<std::atomic_bool>(false);

	auto completion = asio::bind_allocator(allocator,
		asio::bind_executor(source_exec, asio::bind_cancellation_slot(slot, [
			this, owner, source, file_name = std::move(file_name), replace_file_name,
			handler_state, completed, exec, allocator, work]
		(error_code error) mutable
		{
			exec.execute(asio::bind_allocator(allocator, [
				this, owner, source, file_name = std::move(file_name), replace_file_name,
				handler_state, completed, work, error
			]() mutable
			{
				RIWO_UNUSED(work);
				if( completed->exchange(true, std::memory_order_acq_rel) )
					return ;

				if( not error or replace_file_name )
					adopt_ini(std::move(*source), not error);

				notify_loaded(owner, file_name, error);
				complete(std::move(*handler_state), error);
			}));
		}))
	);
	try {
		if( ignore_missing )
			source->load_or(completion);
		else
			source->load(completion);
	}
	catch(...)
	{
		auto error = exception_error(std::current_exception());
		asio::post(source_exec, asio::bind_allocator(allocator,
		[completion = std::move(completion), error]() mutable {
			completion(error);
		}));
	}
}

template <typename Handler>
void settings::impl::start_sync_impl
(settings *owner, path_t file_name, bool replace_file_name, error_code prepare_error, Handler handler)
{
	if( prepare_error )
	{
		post_error(owner, std::move(file_name), prepare_error, false,
			std::move(handler));
		return ;
	}
	std::shared_ptr<ini_t> source;
	try {
		source = snapshot_ini(file_name, replace_file_name, true);
	}
	catch(...)
	{
		post_error(owner, std::move(file_name),
			exception_error(std::current_exception()), false, std::move(handler)
		);
		return ;
	}
	auto source_exec = source->get_executor();

	auto exec = asio::get_associated_executor(handler, source_exec);
	auto allocator = asio::get_associated_allocator(handler);

	auto slot = asio::get_associated_cancellation_slot(handler);
	auto work = std::make_shared<decltype(asio::make_work_guard(handler))>(
		asio::make_work_guard(handler)
	);
	auto handler_state = std::make_shared<Handler>(std::move(handler));
	auto completed = std::make_shared<std::atomic_bool>(false);

	auto completion = asio::bind_allocator(allocator,
		asio::bind_executor(source_exec, asio::bind_cancellation_slot(slot, [
			this, owner, source, file_name = std::move(file_name), replace_file_name,
			handler_state, completed, exec, allocator, work
		](error_code error) mutable
		{
			exec.execute(asio::bind_allocator(allocator, [
				this, owner, source, file_name = std::move(file_name), replace_file_name,
				handler_state, completed, work, error
			]() mutable
			{
				RIWO_UNUSED(work);
				if( completed->exchange(true, std::memory_order_acq_rel) )
					return ;

				if( replace_file_name )
					adopt_ini(std::move(*source), false);

				notify_synced(owner, file_name, error);
				complete(std::move(*handler_state), error);
			}));
		}))
	);
	try {
		source->sync(completion);
	}
	catch(...)
	{
		auto error = exception_error(std::current_exception());
		asio::post(source_exec, asio::bind_allocator(allocator,
		[completion = std::move(completion), error]() mutable {
			completion(error);
		}));
	}
}

std::vector<std::string> settings::names() noexcept
{
	std::vector<std::string> names;
	std::shared_lock locker(g_instances_lock); RIWO_UNUSED(locker);

	names.reserve(g_instances.size());
	for(auto &key : g_instances | std::views::keys)
		names.push_back(key);
	return names;
}

std::filesystem::path settings::file_name() const noexcept
{
	std::shared_lock locker(m_impl->m_ini_lock); RIWO_UNUSED(locker);
	return m_impl->m_ini.file_name();
}

optional<value> settings::get(const group_key_t &gk)
{
	std::shared_lock locker(m_impl->m_ini_lock); RIWO_UNUSED(locker);
	return m_impl->m_ini.read(gk);
}

std::string_view settings::name() const noexcept
{
	return m_impl->m_name;
}

const settings::ini_t &settings::ini() const noexcept
{
	return m_impl->m_ini;
}

settings::ini_t &settings::ini() noexcept
{
	return m_impl->m_ini;
}

} //namespace riwo::utils
