// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_ASIO_TOOLS_H
#define RIWO_CORE_UTILS_ASIO_TOOLS_H

#include <riwo/core/utils/error_code_adapter.h>
#include <riwo/core/utils/asio_concepts.h>
#include <riwo/core/cxx/attributes.h>
#include <riwo/core/cxx/tools.h>

namespace riwo
{

using mutable_buffer = asio::mutable_buffer;

class RIWO_CORE_VAPI const_buffer : public asio::const_buffer
{
public:
	using asio::const_buffer::const_buffer;
	const_buffer &operator=(const const_buffer&) = default;
	const_buffer(const asio::const_buffer &buf);
	const_buffer(const mutable_buffer &buf);
	const_buffer(const char *buf);
	const_buffer(const std::string &buf);
	const_buffer(std::string_view buf);
	const_buffer &operator=(const mutable_buffer &buf);
};

template <typename...Args>
[[nodiscard]] RIWO_CORE_TAPI auto buffer(Args&&...args)
	requires (sizeof...(Args) > 0);

template <typename Token>
[[nodiscard]] RIWO_CORE_TAPI
decltype(auto) unbound_token(Token &&token);

template <typename Token>
struct token_unbound
{
	using type = std::remove_cvref_t <
		decltype(unbound_token(std::declval<Token>()))
	>;
};

template <typename Token>
using token_unbound_t = token_unbound<Token>::type;

[[nodiscard]] RIWO_CORE_TAPI
decltype(auto) get_executor_helper(concepts::sched auto &&exec);

template <typename Token, typename...Args>
struct is_dis_func_opt_token
{
	static constexpr bool value =
		is_opt_token_v<Token,Args...> and
		not is_function_v<token_unbound_t<Token>>;
};

template <typename Token, typename...Args>
constexpr bool is_dis_func_opt_token_v = is_dis_func_opt_token<Token,Args...>::value;

template <typename Token, typename...Args>
struct is_dis_func_tf_opt_token
{
	static constexpr bool value =
		is_tf_opt_token_v<Token,Args...> and
		not is_function_v<token_unbound_t<Token>>;
};

template <typename Token, typename...Args>
constexpr bool is_dis_func_tf_opt_token_v = is_dis_func_tf_opt_token<Token,Args...>::value;

template <typename Token, typename...Args>
struct is_dis_detached_opt_token
{
	static constexpr bool value =
		is_opt_token_v<Token,Args...> and
		not is_detached_v<token_unbound_t<Token>>;
};

template <typename Token, typename...Args>
constexpr bool is_dis_detached_opt_token_v = is_dis_detached_opt_token<Token,Args...>::value;

template <typename Token, typename...Args>
struct is_dis_detached_tf_opt_token
{
	static constexpr bool value =
		is_tf_opt_token_v<Token,Args...> and
		not is_detached_v<token_unbound_t<Token>>;
};

template <typename Token, typename...Args>
constexpr bool is_dis_detached_tf_opt_token_v = is_dis_detached_tf_opt_token<Token,Args...>::value;

namespace concepts
{

template <typename Token, typename...Args>
concept dis_func_opt_token = is_dis_func_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept dis_func_tf_opt_token = is_dis_func_tf_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept dis_detached_opt_token = is_dis_detached_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept dis_detached_tf_opt_token = is_dis_detached_tf_opt_token_v<Token,Args...>;

}} //namespace riwo::concepts
#include <riwo/core/utils/detail/asio_tools.h>


#endif //RIWO_CORE_UTILS_ASIO_TOOLS_H
