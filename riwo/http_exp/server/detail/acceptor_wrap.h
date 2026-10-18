// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_ACCEPTOR_WRAP_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_ACCEPTOR_WRAP_H

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
basic_acceptor_wrap<Exec>::basic_acceptor_wrap(acceptor_type &&acceptor) :
	acceptor_(std::move(acceptor))
{
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<Exec>::get_executor() noexcept -> executor_type {
	return acceptor_.get_executor();
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<Exec>::acceptor() const noexcept -> const acceptor_type& {
	return acceptor_;
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<Exec>::acceptor() noexcept -> acceptor_type& {
	return acceptor_;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_ACCEPTOR_WRAP_H
