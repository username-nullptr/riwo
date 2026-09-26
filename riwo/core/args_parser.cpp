// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "args_parser.h"
#include <unordered_set>
#include <iostream>

namespace riwo::cmdline
{

using arguments = args_parser::arguments;

using parsing_cache = std::unordered_set<std::string>;

struct arg_info
{
	std::string group;
	std::string iden;
};
using args_cache = std::unordered_map<std::string, arg_info>;

class RIWO_DECL_HIDDEN args_parser::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(std::string help_title) :
		m_help_title(std::move(help_title)) {}

	void add(args_cache &cache, std::string_view rule, std::string_view description, std::string_view identification)
	{
		static size_t id_source = 0;
		char buf[128] = "";
		std::snprintf(buf, 128, "%zu", id_source++);

		auto str_vector = string_vector::from_string(rule, ",");
		for(auto &arg : str_vector)
		{
			arg = strtls::trimmed(arg);
			if( not check(arg) or arg == "--" )
				continue;

			else if( arg.size() == 1 )
			{
				if( arg != "-" )
				{
					cache.emplace(arg, arg_info {
						buf, identification.empty()? arg : std::string(identification)
					});
				}
			}
			else
			{
				cache.emplace(arg, arg_info {
					buf, identification.empty()? arg : std::string(identification)
				});
			}
		}
		m_help += "    ";
		m_help += rule;
		m_help += " :\n        ";
		m_help += description;
		m_help += "\n\n";
	}

public:
	[[noreturn]] void print_version() const
	{
		std::cout << "\n" << m_version << "\n\n" << std::flush;
		exit(0);
	}

	[[noreturn]] void print_v() const
	{
		std::cout << "\n" << m_v << "\n\n" << std::flush;
		exit(0);
	}

	[[noreturn]] void print_help() const
	{
		std::cout << "\n";
		if( not m_help_title.empty() )
			std::cout << m_help_title << "\n\n";

		std::cout << m_help;

		if( m_v.empty() )
			std::cout << "    --version:";
		else
			std::cout << "    -v, --version:";
		std::cout << "\n        Viewing version.\n\n";

		if( m_h )
			std::cout << "    -h, --help:";
		else
			std::cout << "    --help:";
		std::cout << "\n        Viewing help.\n\n";

		if( not m_help_ex.empty() )
			std::cout << m_help_ex << "\n\n";

		std::cout << std::flush;
		exit(0);
	}

private:
	static bool check(const std::string &str)
	{
		if( str.empty() )
			return false;

		for(size_t i=1; i<str.size()-1; i++)
		{
			if( str[i] < 33 or str[i] == 61 or str[i] == 92 or str[i] > 126 )
				return false;
		}
		return true;
	}

public:
	args_cache m_group {};
	args_cache m_flag {};

	std::string m_version {};
	std::string m_v {};

	std::string m_help_title {};
	std::string m_help {};
	std::string m_help_ex {};

	bool m_h = false;
};

/*---------------------------------------------------------------------------------------------------------------------------------------------------------*/

args_parser::args_parser(std::string help_title) :
	m_impl(new impl(std::move(help_title)))
{

}

args_parser::~args_parser()
{
	delete m_impl;
}

args_parser &args_parser::set_help_title(std::string text)
{
	m_impl->m_help_title = std::move(text);
	return *this;
}

args_parser &args_parser::add_group(std::string_view rule, std::string_view description, std::string_view identification)
{
	m_impl->add(m_impl->m_group, rule, description, identification);
	return *this;
}

args_parser &args_parser::add_flag(std::string_view rule, std::string_view description, std::string_view identification)
{
	m_impl->add(m_impl->m_flag, rule, description, identification);
	return *this;
}

args_parser &args_parser::set_version(std::string d)
{
	m_impl->m_version = std::move(d);
	return *this;
}

args_parser &args_parser::set_v(std::string d)
{
	m_impl->m_v = std::move(d);
	return *this;
}

args_parser &args_parser::enable_h()
{
	m_impl->m_h = true;
	return *this;
}

args_parser &args_parser::disable_h()
{
	m_impl->m_h = false;
	return *this;
}

args_parser &args_parser::set_help_extension(std::string d)
{
	m_impl->m_help_ex = std::move(d);
	return *this;
}

static void group_check_duplication(parsing_cache &cache, const std::string &group)
{
	if( not cache.emplace(group).second )
	{
		std::cerr << "Too many parameters." << std::endl;
		exit(-1);
	}
}

arguments args_parser::parsing(int argc, const char *argv[], string_vector &other)
{
	return parsing({argv, argv + argc}, other);
}

arguments args_parser::parsing(const string_vector &args, string_vector &other)
{
	arguments result;
	parsing_cache cache;
	auto argc = args.size();

	for(size_t i=1; i<argc; i++)
	{
		std::string arg(args[i]);
		if( arg == "-" or arg == "--" )
		{
			std::cerr << "Invalid arguments." << std::endl;
			exit(-1);
		}
		else if( arg == "--version" )
		{
			if( argc > 2 )
			{
				std::cerr << "Too many parameters." << std::endl;
				exit(-1);
			}
			else if( not m_impl->m_version.empty() )
				m_impl->print_version();
			else if( not m_impl->m_v.empty() )
				m_impl->print_v();
			else
			{
				std::cerr << "Invalid arguments." << std::endl;
				exit(-1);
			}
		}
		else if( arg == "-v" )
		{
			if( not m_impl->m_v.empty() )
			{
				if( argc > 2 )
				{
					std::cerr << "Too many parameters." << std::endl;
					exit(-1);
				}
				m_impl->print_v();
			}
		}
		else if( arg == "--help" )
		{
			if( argc > 2 )
			{
				std::cerr << "Too many parameters." << std::endl;
				exit(-1);
			}
			else if( not m_impl->m_help.empty() )
				m_impl->print_help();
			else
			{
				std::cerr << "Invalid arguments." << std::endl;
				exit(-1);
			}
		}
		else if( arg == "-h" )
		{
			if( m_impl->m_h )
			{
				if( argc > 2 )
				{
					std::cerr << "Too many parameters." << std::endl;
					exit(-1);
				}
				m_impl->print_help();
			}
		}
		auto it = m_impl->m_group.find(arg);
		if( it != m_impl->m_group.end() )
		{
			if( i + 1 == argc or
				m_impl->m_group.contains(args[i+1]) or
				m_impl->m_flag.contains(args[i+1]))
			{
				std::cerr << "Invalid arguments." << std::endl;
				exit(-1);
			}
			group_check_duplication(cache, it->second.group);
			result.emplace(it->second.iden, args[i+1]);
			i++;
			continue ;
		}
		it = m_impl->m_flag.find(arg);
		if( it != m_impl->m_flag.end() )
		{
			group_check_duplication(cache, it->second.group);
			result.emplace(it->second.iden, it->second.iden);
			continue ;
		}
		else if( arg.size() <= 2 )
		{
			group_check_duplication(cache, arg);
			other.emplace_back(arg);
			continue;
		}
		auto pos = arg.find('=');
		if( pos != std::string::npos )
		{
			auto it2 = m_impl->m_group.find(arg.substr(0,pos));
			if( it2 == m_impl->m_group.end() )
			{
				group_check_duplication(cache, arg);
				other.emplace_back(arg);
			}
			else
			{
				group_check_duplication(cache, it2->second.group);
				result.emplace(it2->second.iden, arg.substr(pos+1));
			}
			continue;
		}
		if( arg[0] != '-' )
		{
			group_check_duplication(cache, arg);
			other.emplace_back(arg);
			continue;
		}
		bool flag = true;
		size_t j = 1;

		for(; j<arg.size(); j++)
		{
			if( arg[j] < 'A' or (arg[j] > 'Z' and arg[j] < 'a') or arg[j] > 'z' )
			{
				flag = false;
				break;
			}
		}
		if( not flag )
		{
			group_check_duplication(cache, arg);
			other.emplace_back(arg);
			continue;
		}
		for(j=1; j<arg.size()-1; j++)
		{
			auto it2 = m_impl->m_flag.find(std::string("-") + arg[j]);
			if( it2 == m_impl->m_flag.end() )
			{
				group_check_duplication(cache, arg);
				other.emplace_back(arg);
				continue;
			}
			else
			{
				group_check_duplication(cache, it2->second.group);
				result.emplace(it2->second.iden, it2->second.iden);
				continue ;
			}
		}
		auto tmp = std::string("-") + arg[j];

		auto it2 = m_impl->m_flag.find(tmp);
		if( it2 != m_impl->m_flag.end() )
		{
			group_check_duplication(cache, it2->second.group);
			result.emplace(it2->second.iden, it2->second.iden);
			continue ;
		}
		it2 = m_impl->m_group.find(tmp);
		if( it2 != m_impl->m_group.end() )
		{
			if( i + 1 == argc or
				m_impl->m_group.contains(args[i+1]) or
				m_impl->m_flag.contains(args[i+1]))
			{
				std::cerr << "Invalid arguments." << std::endl;
				exit(-1);
			}
			group_check_duplication(cache, it2->second.group);
			result.emplace(it2->second.iden, args[i+1]);
			i++;
			continue ;
		}
		other.emplace_back(arg);
	}
	return result;
}

arguments args_parser::parsing(int argc, const char *argv[])
{
	return parsing({argv, argv + argc});
}

arguments args_parser::parsing(const string_vector &args)
{
	string_vector other;
	auto res = parsing(args, other);
	if( not other.empty() )
	{
		std::cerr << "Invalid arguments." << std::endl;
		exit(-1);
	}
	return res;
}

} //namespace riwo::cmdline

using namespace riwo::cmdline;

bool operator&(const args_parser::arguments &args_hash, const std::string &key)
{
	return args_hash.contains(key);
}

bool operator&(const std::string &key, const args_parser::arguments &args_hash)
{
	return args_hash.contains(key);
}
