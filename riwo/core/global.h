// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_GLOBAL_H
#define RIWO_CORE_GLOBAL_H

#include <riwo/core/utils.h>
#include <riwo/core/cxx/memory_concepts.h>
#include <riwo/core/cxx/sys_expected.h>
#include <riwo/core/cxx/cplusplus.h>
#include <riwo/core/cxx/operators.h>
#include <riwo/core/cxx/configs.h>

namespace riwo
{

[[nodiscard]] RIWO_CORE_API const char *version_string();

[[nodiscard]] RIWO_CORE_API const char *text_code();

RIWO_CORE_API std::thread::id this_thread_id();

[[noreturn]] RIWO_CORE_API void forced_termination();

template<typename Rep, typename Period>
[[nodiscard]] RIWO_CORE_TAPI decltype(auto) get_associated_redirect_time (
	concepts::any_async_tf_opt_token auto &&token, const duration<Rep,Period> &def_time
);

[[nodiscard]] RIWO_CORE_TAPI decltype(auto) get_associated_redirect_time (
	concepts::any_async_tf_opt_token auto &&token
);

[[nodiscard]] constexpr decltype(auto) unbound_redirect_time (
	concepts::any_async_tf_opt_token auto &&token
);

namespace operators
{

template <concepts::any_async_tf_opt_token Token>
RIWO_CORE_TAPI [[nodiscard]] auto operator|(Token &&token, error_code &error)
	requires (not is_redirect_error_v<std::remove_cvref_t<Token>>);

template <concepts::any_async_tf_opt_token Token>
RIWO_CORE_TAPI [[nodiscard]] auto operator|(Token &&token, const asio::cancellation_slot &slot)
	requires (not is_cancellation_slot_binder_v<std::remove_cvref_t<Token>>);

template <typename Rep, typename Period>
RIWO_CORE_TAPI [[nodiscard]] auto operator| (
	concepts::any_async_opt_token auto &&token, const duration<Rep,Period> &d
);

template <typename Clock, typename Duration>
RIWO_CORE_TAPI [[nodiscard]] auto operator| (
	concepts::any_async_opt_token auto &&token, const time_point<Clock,Duration> &tp
);

}} //namespace riwo
#include <riwo/core/detail/global.h>


#endif //RIWO_CORE_GLOBAL_H
