// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_DETAIL_UUID_H
#define RIWO_CORE_ALGORITHM_DETAIL_UUID_H

namespace riwo::detail
{

[[nodiscard]] RIWO_CORE_API uuid_data_t uuid_generate(uint8_t version);
[[nodiscard]] RIWO_CORE_API uuid_data_t uuid_generate_v5(const uuid_data_t &ns_uuid, std::string_view name);

inline bool uuid_append_utf8(std::string &output, uint32_t code_point)
{
	if( code_point <= 0x7F )
		output.push_back(static_cast<char>(code_point));

	else if( code_point <= 0x7FF )
	{
		output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else if( code_point >= 0xD800 and code_point <= 0xDFFF )
		return false;

	else if( code_point <= 0xFFFF )
	{
		output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else if( code_point <= 0x10FFFF )
	{
		output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else
		return false;
	return true;
}

template <concepts::character CharT>
[[nodiscard]] optional<std::string> uuid_name_to_utf8(std::basic_string_view<CharT> name)
{
	std::string output;
	output.reserve(name.size());

	if constexpr( is_char_v<CharT> or is_char8_v<CharT> )
	{
		for(auto ch : name)
			output.push_back(static_cast<char>(static_cast<unsigned char>(ch)));
	}
	else if constexpr( sizeof(CharT) == 2 )
	{
		for(size_t index=0; index<name.size(); index++)
		{
			uint32_t code_point = static_cast<uint16_t>(name[index]);
			if( code_point >= 0xD800 and code_point <= 0xDBFF )
			{
				if( index + 1 >= name.size() )
					return {};

				auto low = static_cast<uint32_t>(static_cast<uint16_t>(name[++index]));
				if( low < 0xDC00 or low > 0xDFFF )
					return {};

				code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
			}
			else if( code_point >= 0xDC00 and code_point <= 0xDFFF )
				return {};

			if( not uuid_append_utf8(output, code_point) )
				return {};
		}
	}
	else
	{
		static_assert(sizeof(CharT) == 4);
		for(auto ch : name)
		{
			using unsigned_char_t = std::make_unsigned_t<CharT>;
			auto code_point = static_cast<uint32_t>(static_cast<unsigned_char_t>(ch));

			if( not uuid_append_utf8(output, code_point) )
				return {};
		}
	}
	return output;
}

template <concepts::character CharT>
[[nodiscard]] int uuid_hex_value(CharT ch) noexcept
{
	if( ch >= static_cast<CharT>('0') and ch <= static_cast<CharT>('9') )
		return ch - static_cast<CharT>('0');

	if( ch >= static_cast<CharT>('a') and ch <= static_cast<CharT>('f') )
		return ch - static_cast<CharT>('a') + 10;

	if( ch >= static_cast<CharT>('A') and ch <= static_cast<CharT>('F') )
		return ch - static_cast<CharT>('A') + 10;
	return -1;
}

} //namespace riwo::detail

namespace riwo
{

template <concepts::character CharT>
basic_uuid<CharT>::basic_uuid(const data_t &data) :
	m_data(data)
{

}

template <concepts::character CharT>
basic_uuid<CharT>::basic_uuid(string_view_t text)
{
	operator=(text);
}

template <concepts::character CharT>
template <concepts::character CharT0>
basic_uuid<CharT>::basic_uuid(const basic_uuid<CharT0> &other)
{
	operator=(other);
}

template <concepts::character CharT>
template <concepts::character CharT0>
basic_uuid<CharT> &basic_uuid<CharT>::operator=(const basic_uuid<CharT0> &other)
{
	m_data.reset();
	auto uuid_data = other.data();
	if( uuid_data )
		m_data.emplace(*uuid_data);
	return *this;
}

template <concepts::character CharT>
basic_uuid<CharT> &basic_uuid<CharT>::operator=(const data_t &data)
{
	m_data.emplace(data);
	return *this;
}

template <concepts::character CharT>
basic_uuid<CharT> &basic_uuid<CharT>::operator=(string_view_t text)
{
	m_data.reset();
	if( text.size() == 38 )
	{
		if( text.front() != static_cast<char_t>('{') or
			text.back() != static_cast<char_t>('}') )
			return *this;

		text = text.substr(1,36);
	}
	else if( text.size() != 36 )
		return *this;

	data_t data {};
	size_t byte_index = 0;
	int high_nibble = -1;

	for(size_t index=0; index<text.size(); index++)
	{
		if( index == 8 or index == 13 or index == 18 or index == 23 )
		{
			if( text[index] != static_cast<char_t>('-') )
				return *this;
			continue;
		}
		auto hex_value = detail::uuid_hex_value(text[index]);
		if( hex_value < 0 )
			return *this;

		if( high_nibble < 0 )
			high_nibble = hex_value;
		else
		{
			data[byte_index++] = static_cast<std::byte>((high_nibble << 4) | hex_value);
			high_nibble = -1;
		}
	}
	if( byte_index != data.size() or high_nibble >= 0 )
		return *this;

	m_data.emplace(data);
	return *this;
}

template <concepts::character CharT>
bool basic_uuid<CharT>::operator==(const basic_uuid &other) const noexcept
{
	if( is_valid() != other.is_valid() )
		return false;

	if( not is_valid() )
		return true;

	return *m_data == *other.m_data;
}

template <concepts::character CharT>
std::strong_ordering basic_uuid<CharT>::operator<=>(const basic_uuid &other) const noexcept
{
	if( m_data and other.m_data )
		return *m_data <=> *other.m_data;

	if( m_data )
		return std::strong_ordering::greater;

	if( other.m_data )
		return std::strong_ordering::less;

	return std::strong_ordering::equal;
}

template <concepts::character CharT>
template <uint8_t Version>
basic_uuid<CharT> basic_uuid<CharT>::generate()
	requires (Version == 4 or Version == 6 or Version == 7)
{
	return detail::uuid_generate(Version);
}

template <concepts::character CharT>
template <concepts::character CharT0>
basic_uuid<CharT> basic_uuid<CharT>::generate_v5(const basic_uuid<CharT0> &ns_uuid, string_view_t name)
{
	auto ns_data = ns_uuid.data();
	if( not ns_data )
		return {};
	return generate_v5(*ns_data, name);
}

template <concepts::character CharT>
basic_uuid<CharT> basic_uuid<CharT>::generate_v5(const data_t &ns_uuid, string_view_t name)
{
	auto name_data = detail::uuid_name_to_utf8(name);
	if( not name_data )
		return {};
	return detail::uuid_generate_v5(ns_uuid, *name_data);
}

template <concepts::character CharT>
basic_uuid<CharT> basic_uuid<CharT>::generate(uint8_t version)
{
	switch( version )
	{
	case uuid_version::v4: return generate<4>();
	case uuid_version::v6: return generate<6>();
	case uuid_version::v7: return generate<7>();
	default: break;
	}
	return {};
}

template <concepts::character CharT>
basic_uuid<CharT>::string_t basic_uuid<CharT>::to_string(bool parcel) const
{
	if( not m_data )
		return {};

	string_t result;

	result.reserve(parcel ? 38 : 36);
	if( parcel )
		result.push_back(static_cast<char_t>('{'));

	for(size_t index=0; index<m_data->size(); index++)
	{
		constexpr char digits[] = "0123456789ABCDEF";
		if( index == 4 or index == 6 or index == 8 or index == 10 )
			result.push_back(static_cast<char_t>('-'));

		auto byte_value = std::to_integer<uint8_t>((*m_data)[index]);
		result.push_back(static_cast<char_t>(digits[byte_value >> 4]));
		result.push_back(static_cast<char_t>(digits[byte_value & 0x0F]));
	}
	if( parcel )
		result.push_back(static_cast<char_t>('}'));
	return result;
}

template <concepts::character CharT>
optional<typename basic_uuid<CharT>::data_t> basic_uuid<CharT>::data() const noexcept
{
	return m_data;
}

template <concepts::character CharT>
optional<typename basic_uuid<CharT>::data_t> basic_uuid<CharT>::operator*() const noexcept
{
	return data();
}

template <concepts::character CharT>
uuid_version_enum basic_uuid<CharT>::version() const noexcept
{
	if( not m_data )
		return uuid_version::none;

	auto variant = std::to_integer<uint8_t>((*m_data)[8]);
	if( (variant & 0xC0) != 0x80 )
		return uuid_version::none;

	switch( std::to_integer<uint8_t>((*m_data)[6]) >> 4 )
	{
	case uuid_version::v4: return uuid_version::v4;
	case uuid_version::v5: return uuid_version::v5;
	case uuid_version::v6: return uuid_version::v6;
	case uuid_version::v7: return uuid_version::v7;
	default: break;
	}
	return uuid_version::none;
}

template <concepts::character CharT>
bool basic_uuid<CharT>::is_valid() const noexcept
{
	return m_data.has_value();
}

template <concepts::character CharT>
bool basic_uuid<CharT>::is_nil() const noexcept
{
	if( not m_data )
		return false;

	return std::ranges::all_of(*m_data, [](const auto &byte_value) {
		return byte_value == std::byte {0};
	});
}

} //namespace riwo

template <riwo::concepts::character CharT>
struct std::formatter<riwo::basic_uuid<CharT>, CharT>
{
	auto format(const riwo::basic_uuid<CharT> &uuid, auto &context) const {
		return m_formatter.format(uuid.to_string(), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<std::basic_string<CharT>, CharT> m_formatter;
};


#endif //RIWO_CORE_ALGORITHM_DETAIL_UUID_H
