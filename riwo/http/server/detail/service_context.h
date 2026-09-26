// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_SERVICE_CONTEXT_H
#define RIWO_HTTP_SERVER_DETAIL_SERVICE_CONTEXT_H

namespace riwo::http
{

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_service_context<Exec>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	impl(connection_ptr conn,
		session_manager &session_manager, std::filesystem::path resource_root) :
		m_session_manager(session_manager),
		m_connection(conn),
		m_response(conn, resource_root),
		m_request(conn, std::move(resource_root)) {}

	impl(connection_ptr conn, parser_t &&parser,
		session_manager &session_manager, std::filesystem::path resource_root) :
		m_session_manager(session_manager),
		m_connection(conn),
		m_response(conn, resource_root),
		m_request(conn, std::move(parser), std::move(resource_root)) {}

public:
	session_manager &m_session_manager;
	connection_ptr m_connection {};

	response_t m_response;
	request_t m_request;

	bool m_connection_handed_over = false;
};

template <core_concepts::exec Exec>
basic_service_context<Exec>::basic_service_context
(connection_ptr conn, session_manager &session_manager, std::filesystem::path resource_root) :
	m_impl(new impl(std::move(conn), session_manager, std::move(resource_root)))
{

}

template <core_concepts::exec Exec>
basic_service_context<Exec>::basic_service_context(connection_ptr conn,
	parser_t &&parser, session_manager &session_manager, std::filesystem::path resource_root) :
	m_impl(new impl(std::move(conn), std::move(parser), session_manager, std::move(resource_root)))
{

}

template <core_concepts::exec Exec>
basic_service_context<Exec>::~basic_service_context()
{
	delete m_impl;
}

template <core_concepts::exec Exec>
auto basic_service_context<Exec>::request() const noexcept -> const request_t&
{
	return m_impl->m_request;
}

template <core_concepts::exec Exec>
auto basic_service_context<Exec>::request() noexcept -> request_t&
{
	return m_impl->m_request;
}

template <core_concepts::exec Exec>
auto basic_service_context<Exec>::response() const noexcept -> const response_t&
{
	return m_impl->m_response;
}

template <core_concepts::exec Exec>
auto basic_service_context<Exec>::response() noexcept -> response_t&
{
	return m_impl->m_response;
}

template <core_concepts::exec Exec>
auto basic_service_context<Exec>::get_executor() noexcept -> executor_t
{
	return request().get_executor();
}

template <core_concepts::exec Exec>
auto basic_service_context<Exec>::hand_over_connection() noexcept -> connection_ptr
{
	m_impl->m_connection_handed_over = true;
	return m_impl->m_connection;
}

template <core_concepts::exec Exec>
bool basic_service_context<Exec>::connection_handed_over() const noexcept
{
	return m_impl->m_connection_handed_over;
}

template <core_concepts::exec Exec>
template <typename Session, typename...Args>
std::shared_ptr<Session> basic_service_context<Exec>::session(Args&&...args) requires
	core_concepts::base_of<Session,session_t> and core_concepts::constructible<Session, Args...>
{
	auto session_cookie = m_impl->m_session_manager.cookie_key();
	auto session_id = request().cookie(session_cookie).or_else()->to_string();

	auto session = m_impl->m_session_manager
		.template get_or_make<Session>(session_id, std::forward<Args>(args)...);

	response().set_cookie(session_cookie, cookie(session->id()));
	return session;
}

template <core_concepts::exec Exec>
template <typename...Args>
session_ptr basic_service_context<Exec>::session(Args&&...args)
	requires core_concepts::constructible<session_t, Args...>
{
	auto session_cookie = m_impl->m_session_manager.cookie_key();
	auto session_id = request().cookie(session_cookie).or_else()->to_string();

	auto session = m_impl->m_session_manager
		.get_or_make(session_id, std::forward<Args>(args)...);

	response().set_cookie(session_cookie, cookie(session->id()));
	return session;
}

template <core_concepts::exec Exec>
template <typename Session>
std::shared_ptr<Session> basic_service_context<Exec>::session() const
	requires core_concepts::base_of<Session,session_t>
{
	auto session_cookie = m_impl->m_session_manager.cookie_key();
	auto session_id = request().cookie(session_cookie).or_else()->to_string();
	return m_impl->m_session_manager.template get<Session>(session_id);
}

template <core_concepts::exec Exec>
template <typename Session>
std::shared_ptr<Session> basic_service_context<Exec>::session_or()
	requires core_concepts::base_of<Session,session_t>
{
	auto session_cookie = m_impl->m_session_manager.cookie_key();
	auto session_id = request().cookie(session_cookie).or_else()->to_string();

	auto session = m_impl->m_session_manager
		.template get_or<Session>(session_id);

	if( session )
		response().set_cookie(session_cookie, cookie(session->id()));
	return session;
}

template <core_concepts::exec Exec>
session_ptr basic_service_context<Exec>::session() const
{
	auto session_cookie = m_impl->m_session_manager.cookie_key();
	auto session_id = request().cookie(session_cookie).or_else()->to_string();
	return m_impl->m_session_manager.get(session_id);
}

template <core_concepts::exec Exec>
session_ptr basic_service_context<Exec>::session_or() noexcept
{
	auto session_cookie = m_impl->m_session_manager.cookie_key();
	auto session_id = request().cookie(session_cookie).or_else()->to_string();
	auto session = m_impl->m_session_manager.get_or(session_id);

	if( session )
		response().set_cookie(session_cookie, cookie(session->id()));
	return session;
}

} //namespace riwo::http


#endif //RIWO_HTTP_SERVER_DETAIL_SERVICE_CONTEXT_H
