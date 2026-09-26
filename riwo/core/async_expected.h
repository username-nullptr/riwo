// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ASYNC_EXPECTED_H
#define RIWO_CORE_ASYNC_EXPECTED_H

#include <riwo/core/execution.h>

namespace riwo
{

template <typename>
struct is_array_buffer : std::false_type {};

template <concepts::trivially_copyable T, size_t N>
requires (not std::is_const_v<T> and not std::is_volatile_v<T>)
struct is_array_buffer<std::array<T,N>> : std::true_type {};

template <typename T>
constexpr bool is_array_buffer_v = is_array_buffer<T>::value;

template <typename>
struct is_vector_buffer : std::false_type {};

template <concepts::trivially_copyable T>
requires requires(std::vector<T> &buffer) {
	{ buffer.data() } -> std::same_as<T*>;
}
struct is_vector_buffer<std::vector<T>> : std::true_type {};

template <typename T>
constexpr bool is_vector_buffer_v = is_vector_buffer<T>::value;

template <typename>
struct is_string_buffer : std::false_type {};

template <concepts::character CharT, typename Traits, typename Alloc>
struct is_string_buffer<std::basic_string<CharT,Traits,Alloc>> : std::true_type {};

template <typename T>
constexpr bool is_string_buffer_v = is_string_buffer<T>::value;

template <typename T>
struct is_buffer : std::disjunction <
	is_array_buffer<T>, is_vector_buffer<T>, is_string_buffer<T>
> {};

template <typename T>
constexpr bool is_buffer_v = is_buffer<T>::value;

template <typename Buffer, typename Source>
[[nodiscard]] RIWO_CORE_TAPI Buffer copy_buffer_data(Source &&source) requires (
	is_buffer_v<Buffer> and is_buffer_v<std::remove_cvref_t<Source>> and
	not is_array_buffer_v<Buffer>
);

template <typename Value>
[[nodiscard]] RIWO_CORE_TAPI
Value expected_value_or_throw(sys_expected<Value> expected);

template <typename Value, typename Error>
[[nodiscard]] RIWO_CORE_TAPI Value expected_value_or_error(sys_expected<Value> expected, Error &error)
	noexcept(std::is_nothrow_move_constructible_v<Value>)
	requires is_error_code_token_v<Error&>;

template <typename T>
[[nodiscard]] RIWO_CORE_TAPI auto capture_async_argument(T &&argument);

template <typename T>
[[nodiscard]] RIWO_CORE_TAPI
decltype(auto) unwrap_async_argument(T &argument) noexcept;

[[nodiscard]] RIWO_CORE_VAPI error_code exception_error (
	const std::exception_ptr &exception
) noexcept;

template <concepts::exec Exec, typename Handler, typename...Args>
RIWO_CORE_TAPI void post_completion (
	const Exec &exec, Handler &&handler, Args&&...args
);

template <typename Value, concepts::exec Exec, typename Factory, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_expected (
	const Exec &exec, Factory factory, Token &&token
);

template <typename Value, concepts::exec Exec, typename Factory, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_preserved_expected (
	const Exec &exec, Factory factory, Token &&token
);

template <concepts::exec Exec, typename Factory, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_expected_void (
	const Exec &exec, Factory factory, Token &&token
);

template <typename Value, concepts::exec Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_io (
	const Exec &exec, Initiator initiation, Token &&token
);

template <concepts::exec Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_io_void (
	const Exec &exec, Initiator initiation, Token &&token
);

namespace concepts
{

template <typename T>
concept array_buffer = is_array_buffer_v<T>;

template <typename T>
concept vector_buffer = is_vector_buffer_v<T>;

template <typename T>
concept string_buffer = is_string_buffer_v<T>;

template <typename T>
concept buffer = is_buffer_v<T>;

}} //namespace riwo::concepts

#include <riwo/core/detail/async_expected.h>


#endif //RIWO_CORE_ASYNC_EXPECTED_H
