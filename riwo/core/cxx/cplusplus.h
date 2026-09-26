// SPDX-FileCopyrightText: 2024 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_CPLUSPLUS_H
#define RIWO_CORE_CXX_CPLUSPLUS_H

#ifdef _MSC_VER
# define RIWO_CPLUSPLUS  _MSVC_LANG
#else
# define RIWO_CPLUSPLUS  __cplusplus
#endif //_MSC_VER

#if RIWO_CPLUSPLUS < 202002L
# error "riwo requires at least C++20"
#elif RIWO_CPLUSPLUS < 202307L
# define RIWO_STD_CXX 20
#elif RIWO_CPLUSPLUS < 202600L
# define RIWO_STD_CXX 23
#else /* experimental */
# define RIWO_STD_CXX 2b
#endif //RIWO_CPLUSPLUS

#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__arm64__) || defined(__aarch64__)
# define RIWO_OS_64BIT
#else
# define RIWO_OS_32BIT
#endif // 32bit & 64bit

#define RIWO_UNUSED(x)  (void)(x)

#define RIWO_SHARP_IMPL(a)  #a
#define RIWO_SHARP(a)  RIWO_SHARP_IMPL(a)

#define RIWO_CAT_IMPL(a,b)  a##b
#define RIWO_CAT(a,b)  RIWO_CAT_IMPL(a,b)

#define RIWO_AUTO_XX_NAME(_prefix)  RIWO_CAT(_prefix,__LINE__)

#define RIWO_DISABLE_COPY(_class) \
	explicit _class(const _class&) = delete; \
	void operator=(const _class&) = delete; \
	void operator=(const _class&) volatile = delete;

#define RIWO_DISABLE_MOVE(_class) \
	explicit _class(_class&&) = delete; \
	void operator=(_class&&) = delete; \
	void operator=(_class&&) volatile = delete;

#define RIWO_DISABLE_COPY_MOVE(_class) \
	RIWO_DISABLE_COPY(_class) RIWO_DISABLE_MOVE(_class)

#ifndef RIWO_CRTP_VIRTUAL
# define RIWO_CRTP_VIRTUAL
#endif //RIWO_CRTP_VIRTUAL

#ifndef RIWO_CRTP_OVERRIDE
# define RIWO_CRTP_OVERRIDE
#endif //RIWO_CRTP_OVERRIDE

#ifndef ASIO_HAS_CHRONO
# define ASIO_HAS_CHRONO
#endif //ASIO_HAS_CHRONO

#ifndef SPDLOG_USE_STD_FORMAT
# define SPDLOG_USE_STD_FORMAT
#endif //SPDLOG_USE_STD_FORMAT

#ifdef _MSC_VER
# pragma warning(disable: 4251)
# pragma warning(disable: 4819)
#endif //_MSC_VER


#endif //RIWO_CORE_CXX_CPLUSPLUS_H
