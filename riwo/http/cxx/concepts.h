// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CXX_CONCEPTS_H
#define RIWO_HTTP_CXX_CONCEPTS_H

#include <riwo/core/global.h>

namespace riwo::http
{

template <typename>
struct is_stream : std::false_type {};

template <concepts::exec Exec>
struct is_stream<asio::basic_stream_socket<asio::ip::tcp,Exec>> : std::true_type {};

#if RIWO_OPENSSL_SUPPORT
template <concepts::exec Exec>
struct is_stream<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>> : std::true_type {};
#endif //RIWO_OPENSSL_SUPPORT

template <typename Stream>
constexpr bool is_stream_v = is_stream<Stream>::value;

template <typename Stream>
struct is_any_exec_stream
{
	static constexpr bool value = is_stream_v<Stream> and
		std::is_same_v<typename Stream::executor_type, asio::any_io_executor>;
};

template <typename Stream>
constexpr bool is_any_exec_stream_v = is_any_exec_stream<Stream>::value;

namespace concepts
{

template <typename Stream>
concept stream = is_stream_v<Stream>;

template <typename Stream>
concept stream_p = is_stream_v<std::remove_cvref_t<Stream>>;

template <typename Stream>
concept any_exec_stream = is_any_exec_stream_v<Stream>;

template <typename Stream>
concept any_exec_stream_p = is_any_exec_stream_v<std::remove_cvref_t<Stream>>;

template <typename Func, typename Token>
concept progress_handler =
	riwo::concepts::callable<Func,size_t,size_t> and
	[]() consteval -> bool
	{
		using token_t = token_unbound_t<Token>;
		using return_t = decltype(std::declval<Func>()(0,0));

		if constexpr( (is_use_awaitable_v<token_t> or is_deferred_v<token_t>) and
			is_awaitable_v<return_t> )
		{
			using co_return_t = return_t::value_type;
			return std::is_same_v<co_return_t, bool> or
				   std::is_same_v<co_return_t, void>;
		}
		else
		{
			return std::is_same_v<return_t, bool> or
				   std::is_same_v<return_t, void>;
		}
		return false;
	}();

template <typename Func, typename Token>
concept progress_callback =
	progress_handler<Func,Token> and
	riwo::concepts::tf_opt_token<Token,error_code,size_t>;

} //namespace concepts

namespace core_concepts = riwo::concepts;

} //namespace riwo::http


#endif //RIWO_HTTP_CXX_CONCEPTS_H
