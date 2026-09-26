// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_TYPE_TRAITS_H
#define RIWO_CORE_CXX_TYPE_TRAITS_H

#include <riwo/core/cxx/configs.h>
#include <riwo/core/cxx/string_concepts.h>
#include <riwo/core/cxx/asio.h>

namespace riwo
{

using size_t  = std::size_t;

template<typename Rep, typename Period>
using duration = std::chrono::duration<Rep, Period>;

using nanoseconds  = std::chrono::nanoseconds ;
using microseconds = std::chrono::microseconds;
using milliseconds = std::chrono::milliseconds;

using seconds = std::chrono::seconds;
using minutes = std::chrono::minutes;
using hours   = std::chrono::hours  ;

using days   = std::chrono::days  ;
using weeks  = std::chrono::weeks ;
using months = std::chrono::months;
using years  = std::chrono::years ;

template<typename Clock, typename Duration>
using time_point = std::chrono::time_point<Clock, Duration>;

#if RIWO_USING_BOOST_ASIO

using error_code = boost::system::error_code;
using error_category_t = boost::system::error_category;
using boost::system::system_category;

#else //RIWO_USING_BOOST_ASIO

using error_code = std::error_code;
using error_category_t = std::error_category;
using std::system_category;

#endif //RIWO_USING_BOOST_ASIO

[[nodiscard]] inline error_code make_system_error_code(std::errc value) noexcept {
	return { std::make_error_code(value) };
}
namespace errc = asio::error;

template <size_t>
struct byte_type {};

template <> struct byte_type<1> { using unsigned_t = uint8_t ; using signed_t = int8_t ; };
template <> struct byte_type<2> { using unsigned_t = uint16_t; using signed_t = int16_t; };
template <> struct byte_type<4> { using unsigned_t = uint32_t; using signed_t = int32_t; };
template <> struct byte_type<8> { using unsigned_t = uint64_t; using signed_t = int64_t; };

template <size_t N> using byte_unsigned_t = byte_type<N>::unsigned_t;
template <size_t N> using byte_signed_t   = byte_type<N>::signed_t  ;

template <typename T>
struct sizeof_type : byte_type<sizeof(T)> {
	static constexpr size_t bytes = sizeof(T);
};

using uintptr_t = sizeof_type<void*>::unsigned_t;
using intptr_t  = sizeof_type<void*>::signed_t;

} //namespace riwo


#endif //RIWO_CORE_CXX_TYPE_TRAITS_H
