// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_TEST_SCOPED_ENVIRONMENT_H
#define RIWO_TEST_SCOPED_ENVIRONMENT_H

#include <riwo/system/environment.h>
#include <initializer_list>
#include <optional>
#include <vector>

namespace riwo::test
{

class scoped_environment
{
	struct entry
	{
		std::string name;
		std::optional<std::string> value;
	};

public:
	explicit scoped_environment(std::initializer_list<std::string_view> names)
	{
		for(const auto name : names)
		{
			std::optional<std::string> snapshot;
			if( auto value = system::environment::get(name); value )
				snapshot = std::move(*value);
			m_entries.push_back({std::string(name), std::move(snapshot)});
		}
	}

	~scoped_environment()
	{
		for(const auto &item : m_entries)
		{
			if( item.value )
				ignore_unused(system::environment::set(item.name, *item.value));
			else
				ignore_unused(system::environment::unset(item.name));
		}
	}

private:
	std::vector<entry> m_entries;
};

} //namespace riwo::test


#endif //RIWO_TEST_SCOPED_ENVIRONMENT_H
