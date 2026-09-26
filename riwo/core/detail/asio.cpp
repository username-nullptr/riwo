// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

// Provide the single Asio implementation used by riwo.core, the other Riwo
// modules, and consumers.
#include <riwo/core/cxx/configs.h>

#if defined(_WIN32) && !defined(_WIN32_WINNT)
# define _WIN32_WINNT 0x0601
#endif

#if RIWO_USING_BOOST_ASIO

# if !RIWO_BUILD_STATIC && !defined(BOOST_ASIO_DYN_LINK)
#  define BOOST_ASIO_DYN_LINK  1
# endif

# include <boost/asio/impl/src.hpp>

# if RIWO_OPENSSL_SUPPORT
#  include <boost/asio/ssl/impl/src.hpp>
# endif

#else //RIWO_USING_BOOST_ASIO

# if !RIWO_BUILD_STATIC && !defined(ASIO_DYN_LINK)
#  define ASIO_DYN_LINK  1
# endif

# include <asio/impl/src.hpp>

# if RIWO_OPENSSL_SUPPORT
#  include <asio/ssl/impl/src.hpp>
# endif

#endif //RIWO_USING_BOOST_ASIO
