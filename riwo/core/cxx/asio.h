// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_ASIO_H
#define RIWO_CORE_CXX_ASIO_H

#include <riwo/core/cxx/configs.h>

#if RIWO_USING_BOOST_ASIO

# include <boost/asio.hpp>
# include <boost/asio/version.hpp>

# if RIWO_OPENSSL_SUPPORT
#  include <boost/asio/ssl.hpp>
# endif //RIWO_OPENSSL_SUPPORT

namespace asio = boost::asio;

#else //RIWO_USING_BOOST_ASIO

# include <asio.hpp>
# include <asio/version.hpp>

# if RIWO_OPENSSL_SUPPORT
#  include <asio/ssl.hpp>
# endif //RIWO_OPENSSL_SUPPORT

#endif //RIWO_USING_BOOST_ASIO

#if RIWO_USING_BOOST_ASIO
# if BOOST_ASIO_VERSION < 103600
#  define RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT 1
# else //BOOST_ASIO_VERSION
#  define RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT 0
# endif //BOOST_ASIO_VERSION

#elif ASIO_VERSION < 103600
# define RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT 1
#else
# define RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT 0
#endif


#endif //RIWO_CORE_CXX_ASIO_H
