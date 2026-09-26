// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_INI_H
#define RIWO_CORE_INI_H

#include <riwo/core/async_expected.h>
#include <riwo/core/string_vector.h>
#include <riwo/core/value.h>
#include <map>

namespace riwo
{

template <concepts::character CharT,
		  template<typename,typename,typename...> class Map = std::map,
		  typename...MapArgs>
class RIWO_CORE_TAPI basic_ini_keys
{
	RIWO_DISABLE_COPY_MOVE(basic_ini_keys)

public:
	using char_t = CharT;
	using string_t = std::basic_string<char_t>;
	using value_t = basic_value<char_t>;
	using map_t = Map<string_t,value_t,MapArgs...>;

	template <typename...Args>
	using format_string = value_t::template format_string<Args...>;

public:
	basic_ini_keys() = default;
	virtual ~basic_ini_keys() = default;

public:
	[[nodiscard]] optional<value_t> read (
		const concepts::text_p<char_t> auto &key
	) const noexcept;

	void write (
		const concepts::text_p<char_t> auto &key,
		concepts::value_set<char_t> auto &&new_value
	) noexcept;

public:
	template <concepts::text_p<CharT> Text>
	[[nodiscard]] optional<value_t> operator[](const Text &key) const noexcept;

	template <concepts::text_p<CharT> Text>
	[[nodiscard]] value_t &operator[](const Text &key) noexcept;

public:
	using iterator = map_t::iterator;
	using const_iterator = map_t::const_iterator;
	using reverse_iterator = map_t::reverse_iterator;
	using const_reverse_iterator = map_t::const_reverse_iterator;

public:
	[[nodiscard]] iterator begin() noexcept;
	[[nodiscard]] const_iterator cbegin() const noexcept;
	[[nodiscard]] const_iterator begin() const noexcept;

	[[nodiscard]] iterator end() noexcept;
	[[nodiscard]] const_iterator cend() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

	[[nodiscard]] reverse_iterator rbegin() noexcept;
	[[nodiscard]] const_reverse_iterator crbegin() const noexcept;
	[[nodiscard]] const_reverse_iterator rbegin() const noexcept;

	[[nodiscard]] reverse_iterator rend() noexcept;
	[[nodiscard]] const_reverse_iterator crend() const noexcept;
	[[nodiscard]] const_reverse_iterator rend() const noexcept;

public:
	template <concepts::text_p<CharT> Text>
	[[nodiscard]] iterator find(const Text &key) noexcept;

	template <concepts::text_p<CharT> Text>
	[[nodiscard]] const_iterator find(const Text &key) const noexcept;

	void clear() noexcept;
	[[nodiscard]] size_t size() const noexcept;

protected:
	map_t m_keys;
};
/**
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * File jobs are serialized internally. Calls and completions that access the
 * same INI object's mutable state must still be serialized by the program.
 */
template <concepts::character CharT,
		  concepts::exec Exec = asio::any_io_executor,
		  template<typename,typename,typename...> class Map = std::map,
		  typename...MapArgs>
class RIWO_CORE_TAPI basic_ini
{
	RIWO_DISABLE_COPY(basic_ini)

public:
	using char_t = CharT;
	using executor_type = Exec;
	using executor_t = executor_type;

	template <typename Key, typename Value, typename...Args>
	using map_temp = Map<Key,Value,Args...>;

	using ini_keys_t = basic_ini_keys<char_t,map_temp,MapArgs...>;
	using string_t = std::basic_string<char_t>;

	using group_map_t = map_temp<string_t,ini_keys_t,MapArgs...>;
	using string_vector_t = basic_string_vector<char_t>;

	using path_t = std::filesystem::path;
	using value_t = basic_value<char_t>;

	using unit_data_t = ini_keys_t::map_t;
	using data_t = map_temp<string_t,unit_data_t>;

	struct group_key
	{
		string_t group;
		string_t key;

		group_key (
			const concepts::text_p<char_t> auto &group_name,
			const concepts::text_p<char_t> auto &key_name
		) noexcept;

		template <concepts::text<CharT> Text0, concepts::text<CharT> Text1>
		group_key(const std::pair<Text0,Text1> &pair) noexcept;

		template <concepts::text<CharT> Text0, concepts::text<CharT> Text1>
		group_key(const std::tuple<Text0,Text1> &tuple) noexcept;
	};

public:
	explicit basic_ini (
		concepts::match_exec_context<executor_t> auto &exec,
		const path_t &file_name = {}
	);
	template <typename Exec0>
	explicit basic_ini(const Exec0 &exec, const path_t &file_name = {}) requires
	(not std::same_as<std::remove_cvref_t<Exec0>,basic_ini> and concepts::match_exec<Exec0,executor_t>) {
		m_impl = std::make_shared<impl>(exec, file_name);
	}
	explicit basic_ini(const path_t &file_name = {}) requires
		concepts::match_def_exec<executor_t>;

	explicit basic_ini (
		concepts::match_exec_context<executor_t> auto &exec,
		data_t data, const path_t &file_name = {}
	);
	explicit basic_ini (
		const concepts::match_exec<executor_t> auto &exec,
		data_t data, const path_t &file_name = {}
	);
	explicit basic_ini(data_t data, const path_t &file_name = {}) requires
		concepts::match_def_exec<executor_t>;

	virtual ~basic_ini();
	basic_ini(basic_ini &&other) noexcept;
	basic_ini &operator=(basic_ini &&other) noexcept;

	template <typename Exec0>
	explicit basic_ini(basic_ini<char_t,Exec0,map_temp,MapArgs...> &&other)
		requires concepts::match_exec<Exec0,executor_t>;

	template <typename Exec0>
	basic_ini &operator=(basic_ini<char_t,Exec0,map_temp,MapArgs...> &&other)
		requires concepts::match_exec<Exec0,executor_t>;

public:
	[[nodiscard]] optional<value_t> read (
		const group_key &gk
	) const noexcept;

	[[nodiscard]] optional<value_t> read (
		const concepts::string_p<char_t> auto &path
	)const noexcept;

public:
	void write (
		group_key gk, concepts::value_set<char_t> auto &&new_value
	) noexcept;

	void write (
		const concepts::string_p<char_t> auto &path,
		concepts::value_set<char_t> auto &&new_value
	) noexcept;

public:
	template <concepts::text_p<CharT> Text>
	[[nodiscard]] const ini_keys_t &group(const Text &group) const;

	template <concepts::text_p<CharT> Text>
	[[nodiscard]] ini_keys_t &group(const Text &group);

	template <concepts::text_p<CharT> Text>
	[[nodiscard]] const ini_keys_t &operator[](const Text &group) const;

	template <concepts::text_p<CharT> Text>
	[[nodiscard]] ini_keys_t &operator[](const Text &group) noexcept;

	[[nodiscard]] value_t operator[](const group_key &gk) const;
	[[nodiscard]] value_t &operator[](group_key gk) noexcept;

#if RIWO_CPLUSPLUS >= 202100L
	template <concepts::text_p<CharT> Group, concepts::text_p<CharT> Key>
	[[nodiscard]] value_t operator[] (
		const Group &group, const Key &key
	) const;

	template <concepts::text_p<CharT> Group, concepts::text_p<CharT> Key>
	[[nodiscard]] value_t &operator[] (
		Group &&group, Key &&key
	) noexcept;
#endif //RIWO_CPLUSPLUS

public:
	using iterator = group_map_t::iterator;
	using const_iterator = group_map_t::const_iterator;
	using reverse_iterator = group_map_t::reverse_iterator;
	using const_reverse_iterator = group_map_t::const_reverse_iterator;

public:
	[[nodiscard]] iterator begin() noexcept;
	[[nodiscard]] const_iterator cbegin() const noexcept;
	[[nodiscard]] const_iterator begin() const noexcept;

	[[nodiscard]] iterator end() noexcept;
	[[nodiscard]] const_iterator cend() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

	[[nodiscard]] reverse_iterator rbegin() noexcept;
	[[nodiscard]] const_reverse_iterator crbegin() const noexcept;
	[[nodiscard]] const_reverse_iterator rbegin() const noexcept;

	[[nodiscard]] reverse_iterator rend() noexcept;
	[[nodiscard]] const_reverse_iterator crend() const noexcept;
	[[nodiscard]] const_reverse_iterator rend() const noexcept;

public:
	template <concepts::dis_detached_opt_token<error_code> Token = use_sync_t>
	auto load(const path_t &file_name, Token &&token = {});

	template <concepts::dis_detached_opt_token<error_code> Token = use_sync_t>
	auto load_or(const path_t &file_name, Token &&token = {});

	template <concepts::dis_detached_opt_token<error_code> Token = use_sync_t>
	auto load(Token &&token = {});

	template <concepts::dis_detached_opt_token<error_code> Token = use_sync_t>
	auto load_or(Token &&token = {});

	template <concepts::dis_detached_opt_token<error_code> Token = use_sync_t>
	auto sync(const path_t &file_name, Token &&token = {});

	template <concepts::dis_detached_opt_token<error_code> Token = use_sync_t>
	auto sync(Token &&token = {});

	template <typename Rep, typename Period>
	void set_sync_period(const duration<Rep,Period> &period = {});
	void set_sync_on_delete(bool enable = true) noexcept;

	[[nodiscard]] milliseconds sync_period() const noexcept;
	[[nodiscard]] bool sync_on_delete() const noexcept;
	void cancel();

public:
	template <concepts::text_p<CharT> Text>
	[[nodiscard]] iterator find(const Text &group) noexcept;

	template <concepts::text_p<CharT> Text>
	[[nodiscard]] const_iterator find(const Text &group) const noexcept;

	void clear() noexcept;
	[[nodiscard]] size_t size() const noexcept;

	void set_data(data_t data);
	[[nodiscard]] data_t data() const;

	[[nodiscard]] path_t file_name() const noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;

protected:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using ini    = basic_ini<char    >;
using u8ini  = basic_ini<char8_t >;
using u16ini = basic_ini<char16_t>;
using u32ini = basic_ini<char32_t>;
using wini   = basic_ini<wchar_t >;

} //namespace riwo
#include <riwo/core/detail/ini.h>


#endif //RIWO_CORE_INI_H
