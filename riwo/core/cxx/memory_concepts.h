// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_MEMORY_CONCEPTS_H
#define RIWO_CORE_CXX_MEMORY_CONCEPTS_H

#include <memory>

namespace riwo
{

template <typename>
struct is_shared_ptr : std::false_type {};

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template <typename>
struct is_unique_ptr : std::false_type {};

template <typename T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

template <typename>
struct is_weak_ptr : std::false_type {};

template <typename T>
struct is_weak_ptr<std::weak_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_weak_ptr_v = is_weak_ptr<T>::value;

namespace concepts
{

template <typename T>
concept shared_ptr = is_shared_ptr_v<T>;

template <typename T>
concept shared_ptr_p = is_shared_ptr_v<std::remove_cvref_t<T>>;

template <typename T>
concept unique_ptr = is_unique_ptr_v<T>;

template <typename T>
concept unique_ptr_p = is_unique_ptr_v<std::remove_cvref_t<T>>;

template <typename T>
concept weak_ptr = is_weak_ptr_v<T>;

template <typename T>
concept weak_ptr_p = is_weak_ptr_v<std::remove_cvref_t<T>>;

}} //namespace riwo::concepts


#endif //RIWO_CORE_CXX_MEMORY_CONCEPTS_H
