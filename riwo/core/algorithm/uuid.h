// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_UUID_H
#define RIWO_CORE_ALGORITHM_UUID_H

#include <riwo/core/global.h>

namespace riwo { namespace uuid_version
{

enum enumeration : uint8_t {
	none = 0, v4 = 4, v5, v6, v7
};

} //namespace uuid_version

using uuid_version_enum = uuid_version::enumeration;
using uuid_data_t = std::array<std::byte,16>;

template <concepts::character CharT>
class RIWO_CORE_TAPI basic_uuid
{
public:
	using char_t = CharT;
	using string_t = std::basic_string<char_t>;
	using string_view_t = std::basic_string_view<char_t>;
	using data_t = uuid_data_t;

public:
	basic_uuid() = default;
	basic_uuid(const data_t &data);
	basic_uuid(string_view_t text);

	basic_uuid(const basic_uuid &other) = default;
	basic_uuid &operator=(const basic_uuid &other) = default;

	template <concepts::character CharT0>
	basic_uuid(const basic_uuid<CharT0> &other);

	template <concepts::character CharT0>
	basic_uuid &operator=(const basic_uuid<CharT0> &other);

	basic_uuid &operator=(const data_t &data);
	basic_uuid &operator=(string_view_t text);

	[[nodiscard]] bool operator==(const basic_uuid &other) const noexcept;
	[[nodiscard]] std::strong_ordering operator<=>(const basic_uuid &other) const noexcept;

public:
	template <uint8_t Version = uuid_version::v4>
	[[nodiscard]] static basic_uuid generate() requires (
		Version == 4 or Version == 6 or Version == 7
	);
	template <concepts::character CharT0>
	[[nodiscard]] static basic_uuid generate_v5 (
		const basic_uuid<CharT0> &ns_uuid, string_view_t name
	);
	[[nodiscard]] static basic_uuid generate_v5 (
		const data_t &ns_uuid, string_view_t name
	);
	[[nodiscard]] static basic_uuid generate(uint8_t version);

public:
	[[nodiscard]] string_t to_string(bool parcel = false) const;
	[[nodiscard]] operator string_t() const { return to_string(); }

	[[nodiscard]] optional<data_t> data() const noexcept;
	[[nodiscard]] optional<data_t> operator*() const noexcept;

	[[nodiscard]] uuid_version_enum version() const noexcept;
	[[nodiscard]] bool is_valid() const noexcept;
	[[nodiscard]] bool is_nil() const noexcept;

private:
	optional<data_t> m_data {};
};

using uuid = basic_uuid<char>;
using wuuid = basic_uuid<wchar_t>;

} //namespace riwo
#include <riwo/core/algorithm/detail/uuid.h>


#endif //RIWO_CORE_ALGORITHM_UUID_H
