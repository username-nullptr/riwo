// SPDX-FileCopyrightText: 2024 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_ATTRIBUTES_H
#define RIWO_CORE_CXX_ATTRIBUTES_H

#include <riwo/core/cxx/configs.h>

#ifdef _MSC_VER

# pragma execution_character_set("utf-8")

# define RIWO_DECL_EXPORT  __declspec(dllexport)
# define RIWO_DECL_IMPORT  __declspec(dllimport)
# define RIWO_DECL_HIDDEN

# define RIWO_CXX_ATTR_USED    __declspec(used)
# define RIWO_CXX_ATTR_UNUSED  __declspec(unused)

# define RIWO_CXX_ATTR_WEAK              __declspec(weak)
# define RIWO_CXX_ATTR_WEAKREF(_symbol)  __declspec(weakref(_symbol))

#define RIWO_CXX_ATTR_NOVTABLE  __declspec(novtable)

#elif defined(__GNUC__)

# if defined(__MINGW32__) || defined(__MINGW32__)
#  define RIWO_DECL_EXPORT  __declspec(dllexport)
#  define RIWO_DECL_IMPORT  __declspec(dllimport)
# else
#  define RIWO_DECL_EXPORT  __attribute__((visibility("default")))
#  define RIWO_DECL_IMPORT
# endif //__MINGW

# define RIWO_DECL_HIDDEN  __attribute__((visibility("hidden")))

# define RIWO_CXX_ATTR_USED    __attribute__((used))
# define RIWO_CXX_ATTR_UNUSED  __attribute__((unused))

# define RIWO_CXX_ATTR_WEAK              __attribute__((weak))
# define RIWO_CXX_ATTR_WEAKREF(_symbol)  __attribute__((weakref(_symbol)))

# define RIWO_GNU_ATTR_INIT  __attribute__((constructor))
# define RIWO_GNU_ATTR_EXIT  __attribute__((destructor))

#define RIWO_CXX_ATTR_NOVTABLE  __attribute__(novtable)

#else // other compiler

# define RIWO_DECL_EXPORT
# define RIWO_DECL_IMPORT
# define RIWO_DECL_HIDDEN

# define RIWO_CXX_ATTR_USED
# define RIWO_CXX_ATTR_UNUSED

# define RIWO_CXX_ATTR_WEAK
# define RIWO_CXX_ATTR_WEAKREF(_symbol)

#define RIWO_CXX_ATTR_NOVTABLE

#endif //_MSC_VER

#if RIWO_BUILD_STATIC
# define RIWO_CORE_API
#elif defined(riwo_core_EXPORTS)
# define RIWO_CORE_API  RIWO_DECL_EXPORT
#else //riwo_core_EXPORTS
# define RIWO_CORE_API  RIWO_DECL_IMPORT
#endif //riwo_core_EXPORTS

#define RIWO_CORE_VAPI
#define RIWO_CORE_TAPI

#define C_VIRTUAL_FUNC             RIWO_CXX_ATTR_WEAK
#define C_VIRTUAL_SYMBOL(_symbol)  RIWO_CXX_ATTR_WEAKREF(_symbol)


#endif //RIWO_CORE_CXX_ATTRIBUTES_H
