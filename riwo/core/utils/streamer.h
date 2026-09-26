// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_STREAMER_H
#define RIWO_CORE_CXX_STREAMER_H

#include <riwo/core/cxx/type_traits.h>

namespace riwo
{

template <typename>
struct streamer {};

template <typename T>
struct decoder_data
{
	T data {};
	size_t size = 0;

	const T &operator*() const noexcept { return data; }
	T &operator*() noexcept { return data; }

	const T *operator->() const noexcept { return &data; }
	T *operator->() noexcept { return &data; }

	operator const T&() const noexcept { return data; }
	operator T&() noexcept { return data; }
};

#define RIWO_SERIALIZE_FIELDS(...) \
	auto meta_fields() { return std::tie(__VA_ARGS__); } \
	auto meta_fields() const { return std::tie(__VA_ARGS__); }

#define RIWO_META_FIELDS(...) \
	RIWO_FIELD_MAP(RIWO_FIELD_DECL, __VA_ARGS__) \
	RIWO_SERIALIZE_FIELDS(RIWO_FIELD_MAP_COMMA(RIWO_FIELD_NAME,__VA_ARGS__))

} //namespace riwo
#include <riwo/core/utils/detail/streamer.h>
#include <riwo/core/utils/detail/streamer_container.h>
#include <riwo/core/utils/detail/streamer_stateful.h>
#include <riwo/core/utils/detail/streamer_chrono.h>
#include <riwo/core/utils/detail/streamer_custom.h>
#include <riwo/core/utils/detail/streamer_asio.h>


#endif //RIWO_CORE_CXX_STREAMER_H
