// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_CTRL_PAYLOAD_H
#define RIWO_WEBSOCKET_CTRL_PAYLOAD_H

#include <riwo/websocket/global.h>

namespace riwo::websocket
{

class RIWO_WEBSOCKET_API ctrl_payload
{
public:
	using storage_type = std::vector<std::byte>;
	using value_type = storage_type::value_type;
	ctrl_payload() = default;

	explicit ctrl_payload(storage_type payload) noexcept;
	explicit ctrl_payload(std::string_view payload);

	explicit ctrl_payload(const char *payload);
	explicit ctrl_payload(const const_buffer &payload);

	explicit ctrl_payload(std::span<const std::byte> payload);

public:
	ctrl_payload &assign(std::string_view payload);
	ctrl_payload &assign(const char *payload);

	ctrl_payload &assign(const const_buffer &payload);
	ctrl_payload &assign(std::span<const std::byte> payload);

	ctrl_payload &operator=(std::string_view payload);
	ctrl_payload &operator=(const char *payload);

	void clear() noexcept;
	void resize(size_t size);

public:
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

	[[nodiscard]] std::byte *data() noexcept;
	[[nodiscard]] const std::byte *data() const noexcept;

	[[nodiscard]] std::string_view text() const noexcept;
	[[nodiscard]] std::span<std::byte> bytes() noexcept;
	[[nodiscard]] std::span<const std::byte> bytes() const noexcept;

	[[nodiscard]] mutable_buffer as_mutable_buffer() noexcept;
	[[nodiscard]] const_buffer as_const_buffer() const noexcept;

	[[nodiscard]] storage_type &storage() noexcept;
	[[nodiscard]] const storage_type &storage() const noexcept;

private:
	storage_type m_storage {};
};

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_CTRL_PAYLOAD_H
