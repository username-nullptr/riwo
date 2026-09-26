// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_DETAIL_SETTINGS_H
#define RIWO_UTILS_DETAIL_SETTINGS_H

#include <riwo/core/shared_mutex.h>

namespace riwo::utils
{

class RIWO_UTILS_API settings::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(std::string name) :
		m_name(std::move(name)) {}

	[[nodiscard]] static error_code claim_file (
		const settings *owner, const path_t &file_name
	) noexcept;

	[[nodiscard]] std::shared_ptr<ini_t> snapshot_ini (
		const path_t &file_name, bool replace_file_name, bool copy_data
	);
	void adopt_ini(ini_t &&source, bool merge_data);

	using io_handler_t = asio::any_completion_handler<void(error_code)>;
	using expected_handler_t = asio::any_completion_handler<void(error_code,sys_expected<>)>;

	[[nodiscard]] sys_expected<> load_sync(settings *owner, const path_t &file_name,
		bool replace_file_name, bool ignore_missing, error_code error
	);
	[[nodiscard]] sys_expected<> sync_sync(settings *owner, const path_t &file_name,
		bool replace_file_name, error_code error
	);

	void start_load(settings *owner, path_t file_name, bool replace_file_name,
		bool ignore_missing, error_code prepare_error, io_handler_t handler
	);
	void start_load(settings *owner, path_t file_name, bool replace_file_name,
		bool ignore_missing, error_code prepare_error, expected_handler_t handler
	);
	void start_sync(settings *owner, path_t file_name, bool replace_file_name,
		error_code prepare_error, io_handler_t handler
	);
	void start_sync(settings *owner, path_t file_name, bool replace_file_name,
		error_code prepare_error, expected_handler_t handler
	);

public:
	template <bool IgnoreMissing, typename Token>
	auto load(settings *owner, const path_t &file_name, bool replace_file_name, Token &&token)
	{
		error_code prepare_error;
		if( replace_file_name )
			prepare_error = claim_file(owner, file_name);

		if constexpr( is_error_code_token_v<Token> )
		{
			auto result = load_sync(owner, file_name, replace_file_name,
				IgnoreMissing, prepare_error
			);
			token = result ? error_code{} : result.error();
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			return load_sync(owner, file_name, replace_file_name,
				IgnoreMissing, prepare_error
			);
		}
		else
		{
			using token_t = std::remove_cvref_t<Token>;
			using unbound_t = token_unbound_t<token_t>;
			token_t completion_token(std::forward<Token>(token));

			if constexpr( is_use_future_v<unbound_t> or
				is_use_awaitable_v<unbound_t> or is_deferred_v<unbound_t> )
			{
				// Keep the transport error empty and return the operation result as
				// an expected value so futures and awaitables never translate it to an exception.
				return asio::async_initiate<token_t,void(error_code,sys_expected<>)>(
				[this, owner, file_name, replace_file_name, prepare_error](auto handler) mutable
				{
					this->start_load(owner, file_name, replace_file_name,
						IgnoreMissing, prepare_error, expected_handler_t(std::move(handler))
					);
				},
				completion_token);
			}
			else
			{
				return asio::async_initiate<token_t,void(error_code)>(
				[this, owner, file_name, replace_file_name, prepare_error](auto handler) mutable
				{
					this->start_load(owner, file_name, replace_file_name,
						IgnoreMissing, prepare_error, io_handler_t(std::move(handler))
					);
				},
				completion_token);
			}
		}
	}

	template <typename Token>
	auto sync(settings *owner, const path_t &file_name, bool replace_file_name, Token &&token)
	{
		error_code prepare_error;
		if( replace_file_name )
			prepare_error = claim_file(owner, file_name);

		if constexpr( is_error_code_token_v<Token> )
		{
			auto result = sync_sync(owner, file_name, replace_file_name, prepare_error);
			token = result ? error_code{} : result.error();
		}

		else if constexpr( is_sync_opt_token_v<Token> )
			return sync_sync(owner, file_name, replace_file_name, prepare_error);
		else
		{
			using token_t = std::remove_cvref_t<Token>;
			using unbound_t = token_unbound_t<token_t>;
			token_t completion_token(std::forward<Token>(token));

			if constexpr( is_use_future_v<unbound_t> or
				is_use_awaitable_v<unbound_t> or is_deferred_v<unbound_t> )
			{
				// Keep the transport error empty and return the operation result as
				// an expected value so futures and awaitables never translate it to an exception.
				return asio::async_initiate<token_t,void(error_code,sys_expected<>)>(
				[this, owner, file_name, replace_file_name, prepare_error](auto handler) mutable
				{
					this->start_sync(owner, file_name, replace_file_name,
						prepare_error, expected_handler_t(std::move(handler))
					);
				},
				completion_token);
			}
			else
			{
				return asio::async_initiate<token_t,void(error_code)>(
				[this, owner, file_name, replace_file_name, prepare_error](auto handler) mutable
				{
					this->start_sync(owner, file_name, replace_file_name,
						prepare_error, io_handler_t(std::move(handler))
					);
				},
				completion_token);
			}
		}
	}

private:
	template <typename Handler>
	static void complete(Handler &&handler, error_code error);

	template <typename Handler>
	void post_error(settings *owner,
		path_t file_name, error_code error, bool loading, Handler handler
	);
	template <typename Handler>
	void start_load_impl(settings *owner, path_t file_name, bool replace_file_name,
		bool ignore_missing, error_code prepare_error, Handler handler
	);
	template <typename Handler>
	void start_sync_impl(settings *owner, path_t file_name,
		bool replace_file_name, error_code prepare_error, Handler handler
	);

public:
	ini_t m_ini;
	// Loading and syncing perform file I/O while holding this lock; snapshots
	// can also copy the complete INI tree.
	mutable shared_mutex m_ini_lock;
	std::string m_name;
};

template <concepts::opt_token<error_code> Token>
auto settings::load(const path_t &file_name, Token &&token)
{
	return m_impl->load<false>(this, file_name, true, std::forward<Token>(token));
}

template <concepts::opt_token<error_code> Token>
auto settings::load_or(const path_t &file_name, Token &&token)
{
	return m_impl->load<true>(this, file_name, true, std::forward<Token>(token));
}

template <concepts::opt_token<error_code> Token>
auto settings::load(Token &&token)
{
	return m_impl->load<false>(this, {}, false, std::forward<Token>(token));
}

template <concepts::opt_token<error_code> Token>
auto settings::load_or(Token &&token)
{
	return m_impl->load<true>(this, {}, false, std::forward<Token>(token));
}

template <concepts::opt_token<error_code> Token>
auto settings::sync(const path_t &file_name, Token &&token)
{
	return m_impl->sync(this, file_name, true, std::forward<Token>(token));
}

template <concepts::opt_token<error_code> Token>
auto settings::sync(Token &&token)
{
	return m_impl->sync(this, {}, false, std::forward<Token>(token));
}

optional<value> settings::get(concepts::string_p<char> auto &&path)
{
	std::shared_lock locker(m_impl->m_ini_lock); RIWO_UNUSED(locker);
	return m_impl->m_ini.read(std::forward<decltype(path)>(path));
}

settings &settings::set(const group_key_t &gk, const concepts::value_set<char> auto &value) noexcept
{
	{
		std::unique_lock locker(m_impl->m_ini_lock);
		m_impl->m_ini.write(gk, value);
	}

	changed(gk.group + "/" + gk.key, value);
	return *this;
}

settings &settings::set
(const concepts::string_p<char> auto &path, const concepts::value_set<char> auto &value) noexcept
{
	{
		std::unique_lock locker(m_impl->m_ini_lock);
		m_impl->m_ini.write(path, value);
	}

	changed(path, value);
	return *this;
}

} //namespace riwo::utils


#endif //RIWO_UTILS_DETAIL_SETTINGS_H
