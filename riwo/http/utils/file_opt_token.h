// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_UTILS_FILE_OPT_TOKEN_H
#define RIWO_HTTP_UTILS_FILE_OPT_TOKEN_H

#include <riwo/http/cxx/attributes.h>
#include <riwo/http/cxx/concepts.h>
#include <fstream>

namespace riwo::http
{

struct RIWO_HTTP_VAPI file_range {
	size_t begin = 0, total = 0;
};
using file_ranges = std::vector<file_range>;

namespace concepts
{

template <typename...Args>
concept file_ranges_init_list = requires(Args&&...ranges) {
	file_ranges { std::forward<Args>(ranges)... };
};

} //namespace concepts

namespace io_permission
{
using type = size_t; constexpr type
	read  = 0x01,
	write = 0x02,
	read_write = read | write;
}

template <core_concepts::any_fstream>
struct get_io_permissions;

template <core_concepts::character CharT>
struct get_io_permissions<std::basic_fstream<CharT>> {
	static constexpr auto value = io_permission::read_write;
};

template <core_concepts::character CharT>
struct get_io_permissions<std::basic_ofstream<CharT>> {
	static constexpr auto value = io_permission::write;
};

template <core_concepts::character CharT>
struct get_io_permissions<std::basic_ifstream<CharT>> {
	static constexpr auto value = io_permission::read;
};

template <core_concepts::any_fstream FS>
constexpr auto io_permissions_v = get_io_permissions<FS>::value;

namespace file_optype
{
using type = size_t; constexpr type
	single   = 0x01,
	multiple = 0x02,
	combine  = single | multiple;
}

template <core_concepts::any_fstream_p FS>
struct RIWO_HTTP_TAPI file_opt_token_base
{
	using path_t = std::filesystem::path;
	using fstream_t = std::remove_cvref_t<FS>;
	using pos_t = fstream_t::pos_type;

	static constexpr auto permissions = io_permissions_v<fstream_t>;
	std::string mime_type = "unknown";
	size_t file_size = 0;
};

template <typename, file_optype::type>
struct file_opt_token;

template <>
struct RIWO_HTTP_VAPI file_opt_token<void,file_optype::single> : file_opt_token_base<std::fstream>
{
	using type = void;
	static constexpr auto optype = file_optype::single;

	std::shared_ptr<fstream_t> stream {new fstream_t()};
	path_t file_name {};
	optional<file_range> range {};

	file_opt_token(path_t file_path);
	file_opt_token(path_t file_path, const file_range &byte_range);
	~file_opt_token();

	[[nodiscard]] sys_expected<> init(std::ios_base::openmode mode) noexcept;

	file_opt_token(file_opt_token&&) = default;
	file_opt_token(const file_opt_token&) = default;
	file_opt_token &operator=(file_opt_token&&) = default;
	file_opt_token &operator=(const file_opt_token&) = default;
};

template <core_concepts::any_fstream_p FS>
struct RIWO_HTTP_TAPI file_opt_token<FS&&,file_optype::single> : file_opt_token_base<FS&&>
{
	using type = FS&&;
	static constexpr auto optype = file_optype::single;

	using fstream_t = file_opt_token_base<type>::fstream_t;
	std::shared_ptr<fstream_t> stream {};
	optional<file_range> range {};

	file_opt_token(fstream_t &&file_stream);
	file_opt_token(fstream_t &&file_stream, const file_range &byte_range);
	~file_opt_token();

	[[nodiscard]] sys_expected<> init(std::ios_base::openmode mode) noexcept;

	file_opt_token(file_opt_token&&) = default;
	file_opt_token(const file_opt_token&) = default;
	file_opt_token &operator=(file_opt_token&&) = default;
	file_opt_token &operator=(const file_opt_token&) = default;
};

template <core_concepts::any_fstream_p FS>
struct RIWO_HTTP_TAPI file_opt_token<FS&,file_optype::single> : file_opt_token_base<FS&>
{
	using type = FS&;
	static constexpr auto optype = file_optype::single;

	using fstream_t = file_opt_token_base<type>::fstream_t;
	fstream_t *stream = nullptr;
	optional<file_range> range {};

	file_opt_token(fstream_t &file_stream);
	file_opt_token(fstream_t &file_stream, const file_range &byte_range);
	[[nodiscard]] sys_expected<> init(std::ios_base::openmode mode) noexcept;

	file_opt_token(file_opt_token&&) = default;
	file_opt_token(const file_opt_token&) = default;
	file_opt_token &operator=(file_opt_token&&) = default;
	file_opt_token &operator=(const file_opt_token&) = default;
};

template <>
struct RIWO_HTTP_VAPI file_opt_token<void,file_optype::multiple> : file_opt_token_base<std::fstream>
{
	using type = void;
	static constexpr auto optype = file_optype::multiple;

	std::shared_ptr<fstream_t> stream {};
	path_t file_name {};
	file_ranges ranges {};

	file_opt_token(path_t file_path);
	file_opt_token(path_t file_path, const file_range &byte_range);
	file_opt_token(path_t file_path, file_ranges byte_ranges);

	template <concepts::file_ranges_init_list...Args>
	file_opt_token(path_t file_path, Args&&...range_args);

	file_opt_token(file_opt_token<type,file_optype::single> opt);
	~file_opt_token();

	[[nodiscard]] sys_expected<> init(std::ios_base::openmode mode) noexcept;

	file_opt_token(file_opt_token&&) = default;
	file_opt_token(const file_opt_token&) = default;
	file_opt_token &operator=(file_opt_token&&) = default;
	file_opt_token &operator=(const file_opt_token&) = default;
};

template <core_concepts::any_fstream_p FS>
struct RIWO_HTTP_TAPI file_opt_token<FS&&,file_optype::multiple> : file_opt_token_base<FS&&>
{
	using type = FS&&;
	using fstream_t = file_opt_token_base<type>::fstream_t;
	static constexpr auto optype = file_optype::multiple;

	std::shared_ptr<fstream_t> stream {};
	file_ranges ranges {};

	file_opt_token(fstream_t &&file_stream);
	file_opt_token(fstream_t &&file_stream, const file_range &byte_range);
	file_opt_token(fstream_t &&file_stream, file_ranges byte_ranges);

	template <concepts::file_ranges_init_list...Args>
	file_opt_token(fstream_t &&file_stream, Args&&...range_args);

	file_opt_token(file_opt_token<type,file_optype::single> opt);
	~file_opt_token();

	[[nodiscard]] sys_expected<> init(std::ios_base::openmode mode) noexcept;

	file_opt_token(file_opt_token&&) = default;
	file_opt_token(const file_opt_token&) = default;
	file_opt_token &operator=(file_opt_token&&) = default;
	file_opt_token &operator=(const file_opt_token&) = default;
};

template <core_concepts::any_fstream_p FS>
struct RIWO_HTTP_TAPI file_opt_token<FS&,file_optype::multiple> : file_opt_token_base<FS&>
{
	using type = FS&;
	using fstream_t = file_opt_token_base<type>::fstream_t;
	static constexpr auto optype = file_optype::multiple;

	fstream_t *stream = nullptr;
	file_ranges ranges {};

	file_opt_token(fstream_t &file_stream);
	file_opt_token(fstream_t &file_stream, const file_range &byte_range);
	file_opt_token(fstream_t &file_stream, file_ranges byte_ranges);

	template <concepts::file_ranges_init_list...Args>
	file_opt_token(fstream_t &file_stream, Args&&...range_args);

	file_opt_token(file_opt_token<type,file_optype::single> opt);
	[[nodiscard]] sys_expected<> init(std::ios_base::openmode mode) noexcept;

	file_opt_token(file_opt_token&&) = default;
	file_opt_token(const file_opt_token&) = default;
	file_opt_token &operator=(file_opt_token&&) = default;
	file_opt_token &operator=(const file_opt_token&) = default;
};

template <core_concepts::character CharT, typename...Args>
[[nodiscard]] RIWO_HTTP_TAPI auto make_file_opt_token (
	CharT file_name, Args&&...args
) noexcept;

template <typename...Args>
[[nodiscard]] RIWO_HTTP_TAPI auto make_file_opt_token (
	std::filesystem::path file_name, Args&&...args
) noexcept;

template <typename...Args>
[[nodiscard]] RIWO_HTTP_TAPI auto make_file_opt_token (
	core_concepts::any_fstream_p auto &&stream, Args&&...args
) noexcept;

template <typename, core_concepts::character>
struct is_file_opt_token : std::false_type {};

template <typename T, core_concepts::character CharT>
struct is_file_opt_token<file_opt_token<T,file_optype::single>,CharT>
{
	static constexpr bool value =
		std::is_same_v<CharT, typename file_opt_token<T,file_optype::single>::fstream_t::char_type>;
};

template <typename T, core_concepts::character CharT>
struct is_file_opt_token<file_opt_token<T,file_optype::multiple>,CharT>
{
	static constexpr bool value =
		std::is_same_v<CharT, typename file_opt_token<T,file_optype::single>::fstream_t::char_type>;
};

template <typename T, core_concepts::character CharT>
constexpr bool is_file_opt_token_v = is_file_opt_token<T,CharT>::value;

template <typename T>
struct is_any_file_opt_token : std::disjunction <
	is_file_opt_token<T,char>, is_file_opt_token<T,wchar_t>,
	is_file_opt_token<T,char16_t>, is_file_opt_token<T,char32_t>,
	is_file_opt_token<T,char8_t>
> {};

template <typename T>
constexpr bool is_any_file_opt_token_v = is_any_file_opt_token<T>::value;

namespace concepts
{

template <typename T, typename CharT,
	file_optype::type Types = file_optype::combine,
	io_permission::type Perms = io_permission::read_write
>
concept file_opt_token =
	is_file_opt_token_v<std::remove_cvref_t<T>,CharT> and
	!!(std::remove_cvref_t<T>::optype & Types) and
	!!(std::remove_cvref_t<T>::permissions & Perms);

template <typename T,
	file_optype::type Types = file_optype::combine,
	io_permission::type Perms = io_permission::read_write
>
concept any_file_opt_token =
	file_opt_token<T,char,Types,Perms> or
	file_opt_token<T,wchar_t,Types,Perms> or
	file_opt_token<T,char16_t,Types,Perms> or
	file_opt_token<T,char32_t,Types,Perms> or
	file_opt_token<T,char8_t,Types,Perms>;

template <typename T, typename CharT,
	file_optype::type Types = file_optype::combine,
	io_permission::type Perms = io_permission::read_write
>
concept file_opt_token_p =
	core_concepts::any_text_p<T> or
	std::same_as<std::remove_cvref_t<T>,std::filesystem::path> or
	!!(io_permissions_v<std::remove_cvref_t<T>> & Perms) or
	file_opt_token<T,CharT,Types,Perms>;

template <typename T,
	file_optype::type Types = file_optype::combine,
	io_permission::type Perms = io_permission::read_write
>
concept any_file_opt_token_p =
	file_opt_token_p<T,char,Types,Perms> or
	file_opt_token_p<T,wchar_t,Types,Perms> or
	file_opt_token_p<T,char16_t,Types,Perms> or
	file_opt_token_p<T,char32_t,Types,Perms> or
	file_opt_token_p<T,char8_t,Types,Perms>;

} //namespace concepts

namespace operators
{

[[nodiscard]] RIWO_HTTP_VAPI auto operator| (
	std::filesystem::path file_name, const file_range &range
);

[[nodiscard]] RIWO_HTTP_VAPI auto operator| (
	std::filesystem::path file_name, file_ranges ranges
);

[[nodiscard]] RIWO_HTTP_TAPI auto operator| (
	core_concepts::any_fstream_p auto &&stream, const file_range &range
);

[[nodiscard]] RIWO_HTTP_TAPI auto operator| (
	core_concepts::any_fstream_p auto &&stream, file_ranges ranges
);

template <typename T>
[[nodiscard]] RIWO_HTTP_TAPI file_opt_token<T,file_optype::multiple> operator| (
	file_opt_token<T,file_optype::single> opt, const file_range &range
);

template <typename T>
[[nodiscard]] RIWO_HTTP_TAPI file_opt_token<T,file_optype::multiple> operator| (
	file_opt_token<T,file_optype::single> opt, file_ranges ranges
);

template <typename T>
RIWO_HTTP_TAPI file_opt_token<T,file_optype::multiple> &operator| (
	file_opt_token<T,file_optype::multiple> &opt, const file_range &range
);

template <typename T>
RIWO_HTTP_TAPI file_opt_token<T,file_optype::multiple> &operator| (
	file_opt_token<T,file_optype::multiple> &opt, file_ranges ranges
);

template <typename T>
[[nodiscard]] RIWO_HTTP_TAPI file_opt_token<T,file_optype::multiple> &&operator| (
	file_opt_token<T,file_optype::multiple> &&opt, const file_range &range
);

template <typename T>
[[nodiscard]] RIWO_HTTP_TAPI file_opt_token<T,file_optype::multiple> &&operator| (
	file_opt_token<T,file_optype::multiple> &&opt, file_ranges ranges
);

}} //namespace riwo::http::operators
#include <riwo/http/utils/detail/file_opt_token.h>


#endif //RIWO_HTTP_UTILS_FILE_OPT_TOKEN_H
