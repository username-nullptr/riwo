// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "uuid.h"
#include "sha1.h"
#include <random>

namespace riwo::detail { namespace
{

void uuid_fill_random(uuid_data_t &data)
{
	thread_local std::random_device source;
	thread_local std::uniform_int_distribution distribution (
		std::numeric_limits<uint32_t>::min(),
		std::numeric_limits<uint32_t>::max()
	);
	for(size_t offset=0; offset<data.size(); offset+=sizeof(uint32_t))
	{
		auto value = distribution(source);
		data[offset + 0] = static_cast<std::byte>(value >> 24);
		data[offset + 1] = static_cast<std::byte>(value >> 16);
		data[offset + 2] = static_cast<std::byte>(value >> 8);
		data[offset + 3] = static_cast<std::byte>(value);
	}
}

void uuid_apply_version(uuid_data_t &data, uint8_t version)
{
	auto version_octet = std::to_integer<uint8_t>(data[6]);
	data[6] = static_cast<std::byte>((version_octet & 0x0F) | (version << 4));

	auto variant_octet = std::to_integer<uint8_t>(data[8]);
	data[8] = static_cast<std::byte>((variant_octet & 0x3F) | 0x80);
}

uint64_t uuid_v6_timestamp()
{
	constexpr int64_t gregorian_offset = 122192928000000000LL;
	auto unix_nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds> (
		std::chrono::system_clock::now().time_since_epoch()
	).count();

	auto timestamp = unix_nanoseconds / 100 + gregorian_offset;
	if( timestamp < 0 )
		return 0;
	return static_cast<uint64_t>(timestamp) & ((uint64_t {1} << 60) - 1);
}

uint64_t uuid_v7_timestamp()
{
	auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds> (
		std::chrono::system_clock::now().time_since_epoch()
	).count();

	if( timestamp < 0 )
		return 0;
	return static_cast<uint64_t>(timestamp) & ((uint64_t {1} << 48) - 1);
}

int uuid_hex_value(char ch)
{
	if( ch >= '0' and ch <= '9' )
		return ch - '0';
	if( ch >= 'a' and ch <= 'f' )
		return ch - 'a' + 10;
	return ch - 'A' + 10;
}

} //namespace

uuid_data_t uuid_generate(uint8_t version)
{
	uuid_data_t data {};
	uuid_fill_random(data);

	if( version == uuid_version::v6 )
	{
		auto timestamp = uuid_v6_timestamp();
		data[0] = static_cast<std::byte>(timestamp >> 52);
		data[1] = static_cast<std::byte>(timestamp >> 44);
		data[2] = static_cast<std::byte>(timestamp >> 36);
		data[3] = static_cast<std::byte>(timestamp >> 28);
		data[4] = static_cast<std::byte>(timestamp >> 20);
		data[5] = static_cast<std::byte>(timestamp >> 12);
		data[6] = static_cast<std::byte>(timestamp >> 8);
		data[7] = static_cast<std::byte>(timestamp);

		auto node_octet = std::to_integer<uint8_t>(data[10]);
		data[10] = static_cast<std::byte>(node_octet | 0x01);
	}
	else if( version == uuid_version::v7 )
	{
		auto timestamp = uuid_v7_timestamp();
		data[0] = static_cast<std::byte>(timestamp >> 40);
		data[1] = static_cast<std::byte>(timestamp >> 32);
		data[2] = static_cast<std::byte>(timestamp >> 24);
		data[3] = static_cast<std::byte>(timestamp >> 16);
		data[4] = static_cast<std::byte>(timestamp >> 8);
		data[5] = static_cast<std::byte>(timestamp);
	}
	uuid_apply_version(data, version);
	return data;
}

uuid_data_t uuid_generate_v5(const uuid_data_t &ns_uuid, std::string_view name)
{
	sha1 hash;
	hash.append(ns_uuid.data(), ns_uuid.size());

	if( not name.empty() )
		hash.append(name.data(), name.size());

	auto digest = hash.finalize().hex(false);
	uuid_data_t data {};

	for(size_t index=0; index<data.size(); index++)
	{
		auto high = uuid_hex_value(digest[index * 2]);
		auto low = uuid_hex_value(digest[index * 2 + 1]);
		data[index] = static_cast<std::byte>((high << 4) | low);
	}
	uuid_apply_version(data, uuid_version::v5);
	return data;
}

} //namespace riwo::detail
