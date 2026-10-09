// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_INI_H
#define RIWO_CORE_DETAIL_INI_H

#include <riwo/core/algorithm/misc.h>
#include <riwo/core/system/app_utls.h>

namespace riwo { namespace detail
{

template <concepts::character CharT>
[[nodiscard]] RIWO_CORE_TAPI std::basic_string<CharT>
ini_replace(const concepts::text_p<CharT> auto &text)
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	std::basic_string<CharT> result;

	if constexpr( concepts::character<text_t> )
		result.assign(1, text);
	else if constexpr( is_std_string_v<text_t,CharT> or is_std_string_view_v<text_t,CharT> )
		result.assign(text.data(), text.size());
	else
		result.assign(text);

	return strtls::replace(std::move(result),
		static_cast<CharT>(' '), static_cast<CharT>('_')
	);
}

} //namespace detail

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
optional<basic_value<CharT>> basic_ini_keys<CharT,Map,MapArgs...>::read
(const concepts::text_p<char_t> auto &key) const noexcept
{
	auto it = m_keys.find(detail::ini_replace<char_t>(key));
	return it == m_keys.end() ? optional<value_t>() : riwo::make_optional(it->second);
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini_keys<CharT,Map,MapArgs...>::write
	(const concepts::text_p<char_t> auto &key, concepts::value_set<char_t> auto &&new_value) noexcept
{
	m_keys[detail::ini_replace<char_t>(key)] = std::forward<decltype(new_value)>(new_value);
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
optional<basic_value<CharT>> basic_ini_keys<CharT,Map,MapArgs...>::operator[](const Text &key) const noexcept
{
	return read(key);
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
basic_value<CharT> &basic_ini_keys<CharT,Map,MapArgs...>::operator[](const Text &key) noexcept
{
	return m_keys[detail::ini_replace<char_t>(key)];
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::begin() noexcept -> iterator
{
	return m_keys.begin();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::cbegin() const noexcept -> const_iterator
{
	return m_keys.cbegin();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::begin() const noexcept -> const_iterator
{
	return m_keys.begin();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::end() noexcept -> iterator
{
	return m_keys.end();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::cend() const noexcept -> const_iterator
{
	return m_keys.cend();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::end() const noexcept -> const_iterator
{
	return m_keys.end();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::rbegin() noexcept -> reverse_iterator
{
	return m_keys.rbegin();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::crbegin() const noexcept -> const_reverse_iterator
{
	return m_keys.crbegin();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::rbegin() const noexcept -> const_reverse_iterator
{
	return m_keys.rbegin();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::rend() noexcept -> reverse_iterator
{
	return m_keys.rend();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::crend() const noexcept -> const_reverse_iterator
{
	return m_keys.crend();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini_keys<CharT,Map,MapArgs...>::rend() const noexcept -> const_reverse_iterator
{
	return m_keys.rend();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini_keys<CharT,Map,MapArgs...>::find(const Text &key) noexcept -> iterator
{
	return m_keys.find(detail::ini_replace<char_t>(key));
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini_keys<CharT,Map,MapArgs...>::find(const Text &key) const noexcept -> const_iterator
{
	return m_keys.find(detail::ini_replace<char_t>(key));
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini_keys<CharT,Map,MapArgs...>::clear() noexcept
{
	m_keys.clear();
}

template <concepts::character CharT, template <typename,typename,typename...> class Map, typename...MapArgs>
size_t basic_ini_keys<CharT,Map,MapArgs...>::size() const noexcept
{
	return m_keys.size();
}

namespace detail
{

class ini_error_category final : public std::error_category
{
	RIWO_DISABLE_COPY_MOVE(ini_error_category)

public:
	constexpr explicit ini_error_category(const char *name, const char *desc) :
		m_name(name), m_desc(desc) {}

	[[nodiscard]] const char *name() const noexcept override {
		return m_name;
	}
	[[nodiscard]] std::string message(int line) const override {
		return std::format("Ini file parsing: syntax error: [line:{}]: {}.", line, m_desc);
	}

private:
	const char *m_name;
	const char *m_desc;
};

[[nodiscard]] RIWO_CORE_API ini_error_category &ini_invalid_group() noexcept;
[[nodiscard]] RIWO_CORE_API ini_error_category &ini_no_group_specified() noexcept;
[[nodiscard]] RIWO_CORE_API ini_error_category &ini_invalid_key_value_line() noexcept;
[[nodiscard]] RIWO_CORE_API ini_error_category &ini_key_is_empty() noexcept;
[[nodiscard]] RIWO_CORE_API ini_error_category &ini_invalid_value() noexcept;

[[nodiscard]] RIWO_CORE_API
std::filesystem::path ini_tmp_file(const std::filesystem::path &file_name);

RIWO_CORE_API void ini_commit_file(const std::filesystem::path &source,
	const std::filesystem::path &destination, error_code &error
) noexcept;

[[nodiscard]] RIWO_CORE_API asio::any_io_executor ini_io_executor() noexcept;
RIWO_CORE_API void ini_commit_io_work(std::function<void()> work);

template <typename Work>
RIWO_CORE_TAPI void ini_commit_io_work(Work &&work) {
	asio::post(ini_io_executor(), std::forward<Work>(work));
}

struct RIWO_CORE_API ini_cancellation_handler
{
	std::weak_ptr<std::atomic_bool> state {};
	void operator()(asio::cancellation_type_t type) const noexcept;
};

class RIWO_CORE_API ini_cancellation_registry
{
public:
	[[nodiscard]] std::shared_ptr<std::atomic_bool> add();
	void cancel() noexcept;

private:
	std::mutex m_mutex {};
	std::vector <
		std::weak_ptr<std::atomic_bool>
	> m_operations {};
};

[[nodiscard]] RIWO_CORE_API error_code ini_stream_error() noexcept;

class RIWO_CORE_API ini_tmp_guard
{
	RIWO_DISABLE_COPY_MOVE(ini_tmp_guard)

public:
	explicit ini_tmp_guard(std::filesystem::path file_name);
	~ini_tmp_guard();

	void release() noexcept;

private:
	std::filesystem::path m_file_name {};
};

} //namespace detail

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
class RIWO_CORE_TAPI basic_ini<CharT,Exec,Map,MapArgs...>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY(impl)
	friend class basic_ini;

public:
	using cancellation_state_t = std::shared_ptr<std::atomic_bool>;

	impl(const auto &exec, const path_t &file_name) :
		m_exec(exec), m_timer(exec) {
		set_file_name(file_name);
	}
	impl(impl&&) = default;
	impl& operator=(impl&&) = default;

	template <typename Exec0>
	explicit impl(basic_ini<char_t,Exec0,Map,MapArgs...>::impl &&other) :
		m_exec(std::move(other.m_exec)),
		m_file_name(std::move(other.m_file_name)),
		m_groups(std::move(other.m_groups)),
		m_timer(std::move(other.m_timer)),
		m_sync_period(other.m_sync_period),
		m_sync_on_delete(other.m_sync_on_delete),
		m_io_mutex(std::move(other.m_io_mutex)),
		m_cancellations(std::move(other.m_cancellations))
	{
		other.m_sync_period = milliseconds(0);
		other.m_sync_on_delete = false;
		other.m_io_mutex = std::make_shared<std::mutex>();
		other.m_cancellations = std::make_shared<detail::ini_cancellation_registry>();
	}

	template <typename Exec0>
	impl &operator=(basic_ini<char_t,Exec0,Map,MapArgs...>::impl &&other)
	{
		m_exec = std::move(other.m_exec);
		m_file_name = std::move(other.m_file_name);
		m_groups = std::move(other.m_groups);
		m_sync_period = other.m_sync_period;
		m_sync_on_delete = other.m_sync_on_delete;
		m_io_mutex = std::move(other.m_io_mutex);
		m_cancellations = std::move(other.m_cancellations);

		other.m_sync_period = milliseconds(0);
		other.m_sync_on_delete = false;
		other.m_io_mutex = std::make_shared<std::mutex>();
		other.m_cancellations = std::make_shared<detail::ini_cancellation_registry>();
		return *this;
	}

public:
	[[nodiscard]] static auto replace(const auto &text) {
		return detail::ini_replace<char_t>(text);
	}

	void set_file_name(const path_t &file_name)
	{
		if( not file_name.empty() )
			m_file_name = *app::absolute_path(file_name).or_else();
	}

public:
	// File work is serialized with synchronous callers and may run on the
	// dedicated INI worker thread.
	[[nodiscard]] data_t load_file(const path_t &file_name, error_code &error,
		const cancellation_state_t &cancellation, bool ignore_missing) const
	{
		std::lock_guard io_lock(*m_io_mutex);
		data_t data;
		error.clear();

		namespace fs = std::filesystem;
		error_code status_error;
		const bool file_exists = fs::exists(file_name, status_error);

		if( status_error )
		{
			error = status_error;
			return data;
		}
		if( not file_exists )
		{
			if( not ignore_missing )
				error = make_system_error_code(std::errc::no_such_file_or_directory);
			return data;
		}
		if( cancelled(cancellation) )
		{
			error = make_error_code(asio::error::operation_aborted);
			return data;
		}
		errno = 0;
		std::basic_ifstream<char_t> file(file_name);

		if( not file.is_open() )
		{
			error = detail::ini_stream_error();
			return data;
		}
		try {
			unit_data_t *curr_group = nullptr;
			string_t buf;
			size_t line = 0;

			while( std::getline(file, buf) )
			{
				line++;
				if( cancelled(cancellation) )
				{
					error = make_error_code(asio::error::operation_aborted);
					data.clear();
					return data;
				}
				buf = strtls::trimmed(buf);
				if( buf.empty() or buf[0] == static_cast<char_t>('#') or
					buf[0] == static_cast<char_t>(';') )
					continue;

				auto list = string_vector_t::from_string(buf, static_cast<char_t>('#'));
				buf = strtls::trimmed(list[0]);

				list = string_vector_t::from_string(buf, static_cast<char_t>(';'));
				buf = strtls::trimmed(list[0]);
				if( buf.empty() )
					continue;

				if( buf.starts_with(static_cast<char_t>('[')) )
					curr_group = &data[parsing_group(buf, line)];
				else
				{
					auto [key, parsed_value] = parsing_key_value(buf, line);
					if( not curr_group )
					{
						system_error::loc_throw (
							std::error_code(static_cast<int>(line), detail::ini_no_group_specified()),
							"riwo::basic_ini"
						);
					}
					(*curr_group)[std::move(key)] = std::move(parsed_value);
				}
			}
			if( file.bad() )
				error = make_system_error_code(std::errc::io_error);

			else if( cancelled(cancellation) )
				error = make_error_code(asio::error::operation_aborted);
		}
		catch(const std::system_error &ex) {
			error = ex.code();
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		if( error )
			data.clear();
		return data;
	}

	void sync_file(const path_t &destination, data_t data, error_code &error,
		const cancellation_state_t &cancellation) const
	{
		std::lock_guard io_lock(*m_io_mutex);
		error.clear();

		if( destination.empty() )
		{
			error = make_system_error_code(std::errc::invalid_argument);
			return ;
		}
		if( cancelled(cancellation) )
		{
			error = make_error_code(asio::error::operation_aborted);
			return ;
		}
		namespace fs = std::filesystem;
		if( const auto parent = destination.parent_path(); not parent.empty() )
		{
			fs::create_directories(parent, error);
			if( error )
				return ;
		}
		auto file_name = detail::ini_tmp_file(destination);
		detail::ini_tmp_guard tmp_guard(file_name);
		errno = 0;

		std::basic_ofstream<char_t> file (
			file_name, std::ios_base::out | std::ios_base::trunc
		);
		if( not file.is_open() )
		{
			error = detail::ini_stream_error();
			return ;
		}
		try {
			for(auto &[group, values] : data)
			{
				if( cancelled(cancellation) )
				{
					error = make_error_code(asio::error::operation_aborted);
					break;
				}
				file << l_str(char_t,"[")
					 << (strtls::is_ascii(group) ? group : to_percent_encoding(group))
					 << l_str(char_t,"]\n");

				for(auto &[key, entry_value] : values)
				{
					if( cancelled(cancellation) )
					{
						error = make_error_code(asio::error::operation_aborted);
						break;
					}
					if( key.empty() or entry_value->empty() )
						continue;

					file << (strtls::is_ascii(key) ? key : to_percent_encoding(key))
						 << l_str(char_t,"=");

					if( entry_value.is_rlnum() )
					{
						if( entry_value->front() == static_cast<char_t>('+') )
							entry_value = entry_value->substr(1);
						file << entry_value.to_string();
					}
					else
					{
						auto str = entry_value.to_string();
						if( str == l_str(char_t,"true") or str == l_str(char_t,"false") )
							file << str;
						else
						{
							file << l_str(char_t,"\"")
								 << (entry_value.is_ascii() ? str : to_percent_encoding(str))
								 << l_str(char_t,"\"");
						}
					}
					file << l_str(char_t,"\n");
				}
				if( error )
					break;
				file << l_str(char_t,"\n");
			}

			if( not error )
			{
				file.flush();
				if( not file )
					error = make_system_error_code(std::errc::io_error);
			}
		}
		catch(const std::system_error &ex) {
			error = ex.code();
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		file.close();
		if( not error and file.fail() )
			error = detail::ini_stream_error();

		if( error )
			return ;

		if( cancelled(cancellation) )
		{
			error = make_error_code(asio::error::operation_aborted);
			return ;
		}
		detail::ini_commit_file(file_name, destination, error);

		if( not error )
			tmp_guard.release();
	}

	[[nodiscard]] static bool cancelled(const cancellation_state_t &state) noexcept {
		return state and state->load(std::memory_order_acquire);
	}

	template <bool IgnoreMissing, concepts::opt_token<error_code> Token>
	auto load_impl(Token &&token)
	{
		const auto file_name = m_file_name;
		const cancellation_state_t no_cancellation;

		if constexpr( is_error_code_token_v<Token> )
		{
			auto adapted_error = adapt_error_code(token);
			auto &error = adapted_error.get();

			auto loaded_data = load_file(file_name, error, no_cancellation, IgnoreMissing);
			if( not error )
				set_data(std::move(loaded_data));
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			error_code error;
			auto loaded_data = load_file(file_name, error, no_cancellation, IgnoreMissing);
			if( error )
				system_error::loc_throw(error, "riwo::basic_ini::load");
			set_data(std::move(loaded_data));
		}
		else
		{
			auto self = this->shared_from_this();
			return initiate_io_void(m_exec,
			[self = std::move(self), file_name]<typename Handler>(Handler &&handler) mutable
			{
				auto cancellation = self->m_cancellations->add();
				auto slot = asio::get_associated_cancellation_slot(handler);
				if( slot.is_connected() )
				{
					slot.template emplace<detail::ini_cancellation_handler>(
						detail::ini_cancellation_handler{cancellation}
					);
				}
				auto work = asio::make_work_guard(handler);
				auto completion_exec = work.get_executor();
				auto allocator = asio::get_associated_allocator(handler);

				detail::ini_commit_io_work([
					self, file_name, cancellation, slot, completion_exec,
					work = std::move(work), allocator,
					completion = std::forward<Handler>(handler)
				]() mutable
				{
					error_code error;
					auto loaded_data = self->load_file(
						file_name, error, cancellation, IgnoreMissing
					);
					asio::dispatch(completion_exec, asio::bind_allocator(allocator, [self,
						cancellation, slot, work = std::move(work), completion = std::move(completion),
						error, loaded_data = std::move(loaded_data)
					]() mutable
					{
						RIWO_UNUSED(work);
						if( slot.is_connected() )
							slot.clear();

						auto result = error;
						if( not result and cancelled(cancellation) )
							result = make_error_code(asio::error::operation_aborted);

						if( not result )
							self->set_data(std::move(loaded_data));
						std::move(completion)(result);
					}));
				});
			},
			std::forward<Token>(token));
		}
	}

	template <concepts::opt_token<error_code> Token>
	auto sync(Token &&token)
	{
		const auto file_name = m_file_name;
		const cancellation_state_t no_cancellation;

		if constexpr( is_error_code_token_v<Token> )
		{
			auto adapted_error = adapt_error_code(token);
			sync_file(file_name, data(), adapted_error.get(), no_cancellation);
		}

		else if constexpr( is_sync_opt_token_v<Token> )
		{
			error_code error;
			sync_file(file_name, data(), error, no_cancellation);
			if( error )
				system_error::loc_throw(error, "riwo::basic_ini::sync");
		}
		else
		{
			auto ini_data = data();
			auto self = this->shared_from_this();

			return initiate_io_void(m_exec,
			[self = std::move(self), file_name, ini_data = std::move(ini_data)]
			<typename Handler>(Handler &&handler) mutable
			{
				auto cancellation = self->m_cancellations->add();
				auto slot = asio::get_associated_cancellation_slot(handler);

				if( slot.is_connected() )
				{
					slot.template emplace<detail::ini_cancellation_handler>(
						detail::ini_cancellation_handler{cancellation}
					);
				}
				auto work = asio::make_work_guard(handler);
				auto completion_exec = work.get_executor();
				auto allocator = asio::get_associated_allocator(handler);

				detail::ini_commit_io_work([self,
					file_name, ini_data = std::move(ini_data), cancellation, slot,
					completion_exec, work = std::move(work), allocator,
					completion = std::forward<Handler>(handler)
				]() mutable
				{
					error_code error;
					self->sync_file(file_name, std::move(ini_data), error, cancellation);

					asio::dispatch(completion_exec, asio::bind_allocator(allocator,
					[slot, work = std::move(work), completion = std::move(completion), error]() mutable
					{
						RIWO_UNUSED(work);
						if( slot.is_connected() )
							slot.clear();
						std::move(completion)(error);
					}));
				});
			},
			std::forward<Token>(token));
		}
	}

	void cancel() {
		m_cancellations->cancel();
	}

public:
	[[nodiscard]] std::pair<string_t,string_t> from_path(std::basic_string_view<char_t> path, const char *func)
	{
		string_vector_t str_list;
		str_list = string_vector_t::from_string(path, static_cast<char_t>('/'));

		if( str_list.size() != 2 )
		{
			runtime_error::loc_throw(std::format (
				"riwo::basic_ini: {}: The path '{}' is invalid.",
				func, strtls::detail::ascii_transition<char>(path)
			));
		}
		return std::make_pair(str_list[0], str_list[1]);
	}

	template <typename Rep, typename Period>
	void set_sync_period(const duration<Rep,Period> &period)
	{
		using namespace std::chrono;
		auto msec = std::chrono::duration_cast<milliseconds>(period);
		if( msec == m_sync_period )
			return ;

		m_timer.cancel();
		m_sync_period = std::move(msec);

		if( msec == 0ms )
			return ;

		dispatch([self = this->shared_from_this()]() -> awaitable<void>
		{
			error_code error;
			while( not error )
			{
				self->m_timer.expires_after(self->m_sync_period);
				using namespace operators;

				co_await self->m_timer.async_wait(use_awaitable | error);
				if( error )
					break;

				detail::ini_commit_io_work (
				[self, file_name = self->m_file_name, data = self->data()]
				{
					error_code sync_error; RIWO_UNUSED(sync_error);
					self->sync_file(file_name, std::move(data), sync_error, {});
				});
			}
			co_return ;
		});
	}

public:
	void set_data(data_t data)
	{
		for(auto &[group, values] : data)
		{
			for(auto &[key, entry_value] : values)
				m_groups[std::move(group)][std::move(key)] = std::move(entry_value);
		}
	}

	[[nodiscard]] data_t data() const
	{
		data_t data;
		for(auto &[group, values] : m_groups)
		{
			for(auto &[key, entry_value] : values)
				data[group][key] = entry_value;
		}
		return data;
	}

private:
	[[nodiscard]] string_t parsing_group(const string_t &str, size_t line) const
	{
		if( str.size() < 3 or not str.ends_with(static_cast<char_t>(']')) )
		{
			system_error::loc_throw (
				std::error_code(static_cast<int>(line), detail::ini_invalid_group()),
				"riwo::basic_ini"
			);
		}
		auto group = from_percent_encoding (
			strtls::trimmed(string_t(str.c_str() + 1, str.size() - 2))
		);
		if( group.empty() )
		{
			system_error::loc_throw (
				std::error_code(static_cast<int>(line), detail::ini_invalid_group()),
				"riwo::basic_ini"
			);
		}
		return detail::ini_replace<char_t>(group);
	}

	[[nodiscard]] std::pair<string_t,value_t> parsing_key_value(const string_t &str, size_t line) const
	{
		if( str.size() < 2 )
		{
			system_error::loc_throw (
				std::error_code(static_cast<int>(line), detail::ini_invalid_key_value_line()),
				"riwo::basic_ini"
			);
		}
		auto pos = str.find(static_cast<char_t>('='));
		if( pos == 0 )
		{
			system_error::loc_throw (
				std::error_code(static_cast<int>(line), detail::ini_key_is_empty()),
				"riwo::basic_ini"
			);
		}
		else if( pos == string_t::npos )
		{
			system_error::loc_throw (
				std::error_code(static_cast<int>(line), detail::ini_invalid_key_value_line()),
				"riwo::basic_ini"
			);
		}
		auto key = from_percent_encoding(strtls::trimmed(str.substr(0,pos)));
		if( key.empty() )
		{
			system_error::loc_throw (
				std::error_code(static_cast<int>(line), detail::ini_key_is_empty()),
				"riwo::basic_ini"
			);
		}
		auto parsed_value = strtls::trimmed(str.substr(pos+1));
		{
			size_t i = 0;
			for(; i<parsed_value.size(); i++)
			{
				if( parsed_value[i] != static_cast<char_t>('=') )
					break;
			}
			if( i > 0 )
				parsed_value = parsed_value.substr(0, i - 1);
		}
		if( parsed_value.size() == 1 )
		{
			if( parsed_value[0] == static_cast<char_t>('=') or
				parsed_value[0] == static_cast<char_t>('\'') or
				parsed_value[0] == static_cast<char_t>('"') )
			{
				system_error::loc_throw (
					std::error_code(static_cast<int>(line), detail::ini_invalid_value()),
					"riwo::basic_ini"
				);
			}
		}
		else if( parsed_value[0] == static_cast<char_t>('\'') or parsed_value[0] == static_cast<char_t>('"') )
		{
			if( parsed_value.back() != parsed_value[0] )
			{
				system_error::loc_throw (
					std::error_code(static_cast<int>(line), detail::ini_invalid_value()),
					"riwo::basic_ini"
				);
			}
			parsed_value = parsed_value.substr(1, parsed_value.size() - 2);
		}
		return std::pair<string_t,value_t>(
			detail::ini_replace<char_t>(key), from_percent_encoding(parsed_value)
		);
	}

public:
	executor_t m_exec {};
	path_t m_file_name {};
	group_map_t m_groups {};

	asio::steady_timer m_timer;
	milliseconds m_sync_period {0};

	bool m_sync_on_delete = false;
	std::shared_ptr<std::mutex> m_io_mutex {
		std::make_shared<std::mutex>()
	};
	std::shared_ptr<detail::ini_cancellation_registry> m_cancellations {
		std::make_shared<detail::ini_cancellation_registry>()
	};
};

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::group_key::group_key
	(const concepts::text_p<char_t> auto &group_name, const concepts::text_p<char_t> auto &key_name) noexcept :
	group(impl::replace(group_name)), key(impl::replace(key_name))
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text<CharT> Text0, concepts::text<CharT> Text1>
basic_ini<CharT,Exec,Map,MapArgs...>::group_key::group_key(const std::pair<Text0,Text1> &pair) noexcept :
	group(impl::replace(pair.first)), key(impl::replace(pair.second))
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text<CharT> Text0, concepts::text<CharT> Text1>
basic_ini<CharT,Exec,Map,MapArgs...>::group_key::group_key(const std::tuple<Text0,Text1> &tuple) noexcept :
	group(impl::replace(std::get<0>(tuple))), key(impl::replace(std::get<1>(tuple)))
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini
(concepts::match_exec_context<executor_t> auto &exec, const path_t &file_name) :
	basic_ini(exec.get_executor(), file_name)
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini(const path_t &file_name)
	requires concepts::match_def_exec<executor_t> :
	basic_ini(io_context(), file_name)
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini
(concepts::match_exec_context<executor_t> auto &exec, data_t data, const path_t &file_name) :
	basic_ini(exec.get_executor(), std::move(data), file_name)
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini
(const concepts::match_exec<executor_t> auto &exec, data_t data, const path_t &file_name)
{
	m_impl = std::make_shared<impl>(exec, file_name);
	set_data(std::move(data));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini(data_t data, const path_t &file_name)
	requires concepts::match_def_exec<executor_t> :
	basic_ini(io_context(), std::move(data), file_name)
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::~basic_ini()
{
	if( not sync_on_delete() )
		return ;

	error_code error; RIWO_UNUSED(error);
	sync(error);
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini(basic_ini &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_shared<impl>(m_impl->m_exec, path_t{});
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
basic_ini<CharT,Exec,Map,MapArgs...> &basic_ini<CharT,Exec,Map,MapArgs...>::operator=(basic_ini &&other) noexcept
{
	if( this == &other )
		return *this;

	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_shared<impl>(m_impl->m_exec, path_t{});
	return *this;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <typename Exec0>
basic_ini<CharT,Exec,Map,MapArgs...>::basic_ini(basic_ini<char_t,Exec0,map_temp,MapArgs...> &&other)
	requires concepts::match_exec<Exec0,executor_t> :
	m_impl(std::make_shared<impl>(std::move(*other.m_impl)))
{

}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <typename Exec0>
basic_ini<CharT,Exec,Map,MapArgs...>&
basic_ini<CharT,Exec,Map,MapArgs...>::operator=(basic_ini<char_t,Exec0,map_temp,MapArgs...> &&other)
	requires concepts::match_exec<Exec0,executor_t>
{
	if( this != &other )
		*m_impl = std::move(*other.m_impl);
	return *this;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
optional<basic_value<CharT>> basic_ini<CharT,Exec,Map,MapArgs...>::read
(const group_key &gk) const noexcept
{
	auto it = m_impl->m_groups.find(gk.group);
	return it == m_impl->m_groups.end() ?
		optional<value_t>() : it->second.read(std::move(gk.key));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
optional<basic_value<CharT>> basic_ini<CharT,Exec,Map,MapArgs...>::read
(const concepts::string_p<char_t> auto &path) const noexcept
{
	return read(m_impl->from_path(path, "read"));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini<CharT,Exec,Map,MapArgs...>::write
(group_key gk, concepts::value_set<char_t> auto &&new_value) noexcept
{
	m_impl->m_groups[std::move(gk.group)].write (
		std::move(gk.key), std::forward<decltype(new_value)>(new_value)
	);
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini<CharT,Exec,Map,MapArgs...>::write
(const concepts::string_p<char_t> auto &path, concepts::value_set<char_t> auto &&new_value) noexcept
{
	auto pair = m_impl->from_path(path, "write");
	write(std::move(pair), std::forward<decltype(new_value)>(new_value));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini<CharT,Exec,Map,MapArgs...>::group(const Text &group) const -> const ini_keys_t&
{
	auto name = impl::replace(group);
	auto it = m_impl->m_groups.find(name);

	if( it == m_impl->m_groups.end() )
	{
		runtime_error::loc_throw(std::format (
			"basic_ini: group: The group '{}' is not exists.",
			strtls::detail::ascii_transition<char>(name)
		));
	}
	return it->second;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini<CharT,Exec,Map,MapArgs...>::group(const Text &group) -> ini_keys_t&
{
	auto name = impl::replace(group);
	auto it = m_impl->m_groups.find(name);

	if( it == m_impl->m_groups.end() )
	{
		runtime_error::loc_throw(std::format (
			"basic_ini: group: The group '{}' is not exists.",
			strtls::detail::ascii_transition<char>(name)
		));
	}
	return it->second;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini<CharT,Exec,Map,MapArgs...>::operator[](const Text &group) const -> const ini_keys_t&
{
	return this->group(group);
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
basic_ini<CharT,Exec,Map,MapArgs...>::ini_keys_t&
basic_ini<CharT,Exec,Map,MapArgs...>::operator[](const Text &group) noexcept
{
	return m_impl->m_groups[impl::replace(group)];
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::operator[](const group_key &gk) const -> value_t
{
	return (*this)[gk.group][gk.key];
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::operator[](group_key gk) noexcept -> value_t&
{
	return (*this)[std::move(gk.group)][std::move(gk.key)];
}

#if RIWO_CPLUSPLUS >= 202100L

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Group, concepts::text_p<CharT> Key>
auto basic_ini<CharT,Exec,Map,MapArgs...>::operator[](const Group &group, const Key &key) const -> value_t
{
	return (*this)[group][key];
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Group, concepts::text_p<CharT> Key>
auto basic_ini<CharT,Exec,Map,MapArgs...>::operator[](Group &&group, Key &&key) noexcept -> value_t&
{
	return (*this)[std::forward<decltype(group)>(group)][std::forward<decltype(key)>(key)];
}

#endif //RIWO_CPLUSPLUS

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::begin() noexcept -> iterator
{
	return m_impl->m_groups.begin();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::cbegin() const noexcept -> const_iterator
{
	return m_impl->m_groups.cbegin();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::begin() const noexcept -> const_iterator
{
	return m_impl->m_groups.begin();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::end() noexcept -> iterator
{
	return m_impl->m_groups.end();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::cend() const noexcept -> const_iterator
{
	return m_impl->m_groups.cend();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::end() const noexcept -> const_iterator
{
	return m_impl->m_groups.end();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::rbegin() noexcept -> reverse_iterator
{
	return m_impl->m_groups.rbegin();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::crbegin() const noexcept -> const_reverse_iterator
{
	return m_impl->m_groups.crbegin();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::rbegin() const noexcept -> const_reverse_iterator
{
	return m_impl->m_groups.rbegin();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::rend() noexcept -> reverse_iterator
{
	return m_impl->m_groups.rend();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::crend() const noexcept -> const_reverse_iterator
{
	return m_impl->m_groups.crend();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::rend() const noexcept -> const_reverse_iterator
{
	return m_impl->m_groups.rend();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::dis_detached_opt_token<error_code> Token>
auto basic_ini<CharT,Exec,Map,MapArgs...>::load(const path_t &file_name, Token &&token)
{
	if( not file_name.empty() )
		m_impl->set_file_name(file_name);
	return load(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::dis_detached_opt_token<error_code> Token>
auto basic_ini<CharT,Exec,Map,MapArgs...>::load_or(const path_t &file_name, Token &&token)
{
	if( not file_name.empty() )
		m_impl->set_file_name(file_name);
	return load_or(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::dis_detached_opt_token<error_code> Token>
auto basic_ini<CharT,Exec,Map,MapArgs...>::load(Token &&token)
{
	return m_impl->template load_impl<false>(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::dis_detached_opt_token<error_code> Token>
auto basic_ini<CharT,Exec,Map,MapArgs...>::load_or(Token &&token)
{
	return m_impl->template load_impl<true>(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::dis_detached_opt_token<error_code> Token>
auto basic_ini<CharT,Exec,Map,MapArgs...>::sync(const path_t &file_name, Token &&token)
{
	m_impl->set_file_name(file_name);
	return sync(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::dis_detached_opt_token<error_code> Token>
auto basic_ini<CharT,Exec,Map,MapArgs...>::sync(Token &&token)
{
	return m_impl->sync(std::forward<Token>(token));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <typename Rep, typename Period>
void basic_ini<CharT,Exec,Map,MapArgs...>::set_sync_period(const duration<Rep,Period> &period)
{
	m_impl->set_sync_period(period);
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini<CharT,Exec,Map,MapArgs...>::set_sync_on_delete(bool enable) noexcept
{
	m_impl->m_sync_on_delete = enable;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
milliseconds basic_ini<CharT,Exec,Map,MapArgs...>::sync_period() const noexcept
{
	return m_impl->m_sync_period;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
bool basic_ini<CharT,Exec,Map,MapArgs...>::sync_on_delete() const noexcept
{
	return m_impl->m_sync_on_delete;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini<CharT,Exec,Map,MapArgs...>::cancel()
{
	m_impl->cancel();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini<CharT,Exec,Map,MapArgs...>::find(const Text &group) noexcept -> iterator
{
	return m_impl->m_groups.find(impl::replace(group));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
template <concepts::text_p<CharT> Text>
auto basic_ini<CharT,Exec,Map,MapArgs...>::find(const Text &group) const noexcept -> const_iterator
{
	return m_impl->m_groups.find(impl::replace(group));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini<CharT,Exec,Map,MapArgs...>::clear() noexcept
{
	m_impl->m_groups.clear();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
size_t basic_ini<CharT,Exec,Map,MapArgs...>::size() const noexcept
{
	return m_impl->m_groups.size();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
void basic_ini<CharT,Exec,Map,MapArgs...>::set_data(data_t data)
{
	m_impl->set_data(std::move(data));
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::data() const -> data_t
{
	return m_impl->data();
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::file_name() const noexcept -> path_t
{
	return m_impl->m_file_name;
}

template <concepts::character CharT, concepts::exec Exec,
		  template<typename,typename,typename...> class Map, typename...MapArgs>
auto basic_ini<CharT,Exec,Map,MapArgs...>::get_executor() noexcept -> executor_t
{
	return m_impl->m_exec;
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_INI_H
