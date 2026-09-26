#include <riwo/core/cxx/tools.h>

#ifdef __GNUC__
# include <cxxabi.h>
# include <string>
# include <memory>
# include <mutex>
# include <map>
#endif //__GNUC__

namespace riwo
{

#ifdef __GNUC__
namespace
{

std::string demangle_type_name(const char *name)
{
	int status = 0;
	using result_t = std::unique_ptr<char,decltype(&std::free)>;
	result_t result {
		abi::__cxa_demangle(name, nullptr, nullptr, &status), &std::free
	};
	if( status == 0 and result )
		return result.get();
	return name;
}

const char *dynamic_type_name(const std::type_info &type)
{
	static std::mutex mutex;
	static std::map<std::string,std::string,std::less<>> names;

	const std::scoped_lock lock(mutex);
	auto [it, inserted] = names.try_emplace(type.name());

	if( inserted )
		it->second = demangle_type_name(type.name());
	return it->second.c_str();
}

} //namespace
#endif //__GNUC__

const char *type_name(const std::type_info &type)
{
#ifdef __GNUC__
	return dynamic_type_name(type);
#else //__GNUC__
	return type.name();
#endif //__GNUC__
}

} //namespace riwo
