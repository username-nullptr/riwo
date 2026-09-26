// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_MIME_TYPE_H
#define RIWO_CORE_MIME_TYPE_H

#include <riwo/core/global.h>
#include <unordered_map>
#include <map>

namespace riwo::mime_type
{

using path_t = std::filesystem::path;

namespace mapping
{

using suffix_type_map = std::unordered_map<std::string, std::string>;
using mime_head_map = std::map<std::string, std::string>;

[[nodiscard]] RIWO_CORE_API suffix_type_map &suffix();
[[nodiscard]] RIWO_CORE_API mime_head_map &signatures();
[[nodiscard]] RIWO_CORE_API mime_head_map &signatures_offset4();

} //namespace mapping

[[nodiscard]] RIWO_CORE_API
std::string get(const path_t &file_name, bool magic_first = false);

[[nodiscard]] RIWO_CORE_API
bool is_text(const path_t &file_name);

[[nodiscard]] RIWO_CORE_API
bool is_binary(const path_t &file_name);

[[nodiscard]] RIWO_CORE_API
std::string text_encoding(const path_t &file_name);

template <typename FS>
[[nodiscard]] RIWO_CORE_TAPI std::string get(FS &stream)
	requires is_fstream_v<FS,char> or is_ifstream_v<FS,char>;

template <typename FS>
[[nodiscard]] RIWO_CORE_TAPI bool is_text(FS &stream)
	requires is_fstream_v<FS,char> or is_ifstream_v<FS,char>;

template <typename FS>
[[nodiscard]] RIWO_CORE_TAPI bool is_binary(FS &stream)
	requires is_fstream_v<FS,char> or is_ifstream_v<FS,char>;

template <typename FS>
[[nodiscard]] RIWO_CORE_TAPI std::string text_encoding(FS &stream)
	requires is_fstream_v<FS,char> or is_ifstream_v<FS,char>;

} //namespace riwo::mime_type
#include <riwo/core/detail/mime_type.h>


#endif //RIWO_CORE_MIME_TYPE_H
