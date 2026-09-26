// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_GLOBAL_H
#define RIWO_HTTP_GLOBAL_H

#include <riwo/http/cxx/configs.h>
#include <riwo/http/cxx/container.h>
#include <riwo/http/protocol/model.h>

namespace riwo::http
{

#define RIWO_HTTP_DEFINE_ENUM(_type, _struct, _list, _str_func, ...) \
	struct RIWO_HTTP_API _struct { \
		enum enumeration : _type { _list } value = {}; \
		constexpr _struct(enumeration e) : value(e) {} \
		constexpr _struct() = default; \
		static bool check(enumeration e, bool _throw = true); \
		bool check(bool _throw = true) const { return check(value, _throw); } \
		[[nodiscard]] static const char *_str_func(enumeration e, bool _throw = true); \
		[[nodiscard]] const char *_str_func(bool _throw = true) const { return _str_func(value, _throw); } \
		template <enumeration Enum> [[nodiscard]] static consteval bool is_valid(); \
		template <enumeration Enum> static constexpr bool is_valid_v = is_valid<Enum>(); \
		template <enumeration Enum> [[nodiscard]] static consteval const char *_str_func() \
			requires is_valid_v<Enum>; \
		__VA_ARGS__ \
	}; \
	using _struct##_enum = _struct::enumeration

} //namespace riwo::http


#endif //RIWO_HTTP_GLOBAL_H
