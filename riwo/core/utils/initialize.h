// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_INITIALIZE_H
#define RIWO_CORE_UTILS_INITIALIZE_H

#include <riwo/core/cxx/cplusplus.h>
#include <riwo/core/cxx/attributes.h>

#define RIWO_AUTO_FUNC_NAME  RIWO_AUTO_XX_NAME(__riwo_auto_xx_name_)

#define RIWO_DEFAULT_REGISTRATION \
	static void RIWO_AUTO_FUNC_NAME(); \
	namespace { \
		struct RIWO_DECL_HIDDEN RIWO_AUTO_XX_NAME(__riwo_auto_register_) { \
			RIWO_AUTO_XX_NAME(__riwo_auto_register_)() { \
				RIWO_AUTO_FUNC_NAME(); \
			} \
		}; \
	} \
	static const RIWO_AUTO_XX_NAME(__riwo_auto_register_) RIWO_AUTO_XX_NAME(__auto_register_); \
	static void RIWO_AUTO_FUNC_NAME()

#ifdef _MSC_VER
# define RIWO_REGISTRATION RIWO_DEFAULT_REGISTRATION
#else //GNU & Clang ...
# define RIWO_REGISTRATION \
	RIWO_GNU_ATTR_INIT static void RIWO_AUTO_XX_NAME(__riwo_auto_register_)()
#endif //_MSC_VER


#endif //RIWO_CORE_UTILS_INITIALIZE_H
