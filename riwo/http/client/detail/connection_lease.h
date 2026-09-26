// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_CONNECTION_LEASE_H
#define RIWO_HTTP_CLIENT_DETAIL_CONNECTION_LEASE_H

namespace riwo::http
{

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_connection_lease<Exec>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	impl(connection_ptr conn, std::function<void(connection_ptr)> give_back) :
		m_give_back(std::move(give_back)),
		m_connection(std::move(conn)) {}

	std::function<void(connection_ptr)> m_give_back {};
	connection_ptr m_connection {};
};

template <core_concepts::exec Exec>
basic_connection_lease<Exec>::basic_connection_lease
(connection_ptr conn, std::function<void(connection_ptr)> give_back) :
	m_impl(new impl(std::move(conn), std::move(give_back)))
{

}

template <core_concepts::exec Exec>
basic_connection_lease<Exec>::~basic_connection_lease()
{
	if( m_impl->m_connection )
	{
		auto conn = std::move(m_impl->m_connection);
		if( m_impl->m_give_back )
		{
			try { m_impl->m_give_back({}); }
			catch(...) {}
		}
		ignore_unused(conn->close());
	}
	delete m_impl;
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::get_executor() noexcept -> executor_t
{
	return get().get_executor();
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::get() noexcept -> connection_t&
{
	if( not m_impl->m_connection )
		std::terminate();
	return *m_impl->m_connection;
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::operator*() noexcept -> connection_t&
{
	return get();
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::operator->() noexcept -> connection_t*
{
	return &get();
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::get() const noexcept -> const connection_t&
{
	if( not m_impl->m_connection )
		std::terminate();
	return *m_impl->m_connection;
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::operator*() const noexcept -> const connection_t&
{
	return get();
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::operator->() const noexcept -> const connection_t*
{
	return &get();
}

template <core_concepts::exec Exec>
auto basic_connection_lease<Exec>::take() noexcept -> connection_ptr
{
	if( not m_impl->m_connection )
		return {};

	auto conn = std::move(m_impl->m_connection);
	if( m_impl->m_give_back )
	{
		try {
			m_impl->m_give_back({});
		}
		catch(...) {}
		m_impl->m_give_back = {};
	}
	return conn;
}

template <core_concepts::exec Exec>
void basic_connection_lease<Exec>::release()
{
	if( not m_impl->m_connection )
		return ;

	auto conn = std::move(m_impl->m_connection);
	if( not m_impl->m_give_back )
	{
		ignore_unused(conn->close());
		return ;
	}
	auto give_back = std::move(m_impl->m_give_back);
	try {
		give_back(std::move(conn));
	}
	catch(...)
	{
		if( conn )
			ignore_unused(conn->close());
		throw ;
	}
}

template <core_concepts::exec Exec>
bool basic_connection_lease<Exec>::is_valid() const noexcept
{
	return static_cast<bool>(m_impl->m_connection);
}

template <core_concepts::exec Exec>
basic_connection_lease<Exec>::operator bool() const noexcept
{
	return is_valid();
}

} //namespace riwo::http


#endif //RIWO_HTTP_CLIENT_DETAIL_CONNECTION_LEASE_H
