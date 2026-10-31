// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_BODY_FILE_HANDLE_H
#define RIWO_EXP_HTTP_BODY_FILE_HANDLE_H

#include <riwo/experimental/http/global.h>
#include <fstream>

namespace riwo::experimental::http { namespace concepts
{

template <typename Stream>
concept input_file_stream =
	std::same_as<std::remove_cvref_t<Stream>,std::ifstream> or
	std::same_as<std::remove_cvref_t<Stream>,std::fstream>;

template <typename Stream>
concept output_file_stream =
	std::same_as<std::remove_cvref_t<Stream>,std::ofstream> or
	std::same_as<std::remove_cvref_t<Stream>,std::fstream>;

} //namespace concepts

template <typename Stream>
requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
class RIWO_EXP_HTTP_TAPI borrowed_file_ref
{
	RIWO_DISABLE_COPY(borrowed_file_ref)

public:
	explicit borrowed_file_ref(Stream &stream) noexcept;
	borrowed_file_ref(borrowed_file_ref &&other) noexcept;
	borrowed_file_ref &operator=(borrowed_file_ref &&other) noexcept;

	[[nodiscard]] Stream *get() const noexcept;
	[[nodiscard]] Stream *operator*() const noexcept;

private:
	Stream *m_stream = nullptr;
};

template <typename Stream>
[[nodiscard]] RIWO_EXP_HTTP_TAPI
borrowed_file_ref<Stream> borrowed_file(Stream &stream) noexcept requires (
	concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>
);

class RIWO_EXP_HTTP_API owned_c_file
{
	RIWO_DISABLE_COPY(owned_c_file)

public:
	explicit owned_c_file(std::FILE *file = nullptr) noexcept;
	~owned_c_file();

	owned_c_file(owned_c_file &&other) noexcept;
	owned_c_file &operator=(owned_c_file &&other) noexcept;

	[[nodiscard]] std::FILE *get() const noexcept;
	[[nodiscard]] std::FILE *release() noexcept;

	[[nodiscard]] std::FILE *operator*() const noexcept;

private:
	std::FILE *m_file = nullptr;
};

class RIWO_EXP_HTTP_API borrowed_c_file
{
	RIWO_DISABLE_COPY(borrowed_c_file)

public:
	explicit borrowed_c_file(std::FILE *file = nullptr) noexcept;

	borrowed_c_file(borrowed_c_file &&other) noexcept;
	borrowed_c_file &operator=(borrowed_c_file &&other) noexcept;

	[[nodiscard]] std::FILE *get() const noexcept;
	[[nodiscard]] std::FILE *operator*() const noexcept;

private:
	std::FILE *m_file = nullptr;
};

[[nodiscard]] RIWO_EXP_HTTP_API
owned_c_file owned_file(std::FILE *file) noexcept;

[[nodiscard]] RIWO_EXP_HTTP_API
borrowed_c_file borrowed_file(std::FILE *file) noexcept;

} //namespace riwo::experimental::http

#include <riwo/experimental/http/body/detail/file.h>

#endif //RIWO_EXP_HTTP_BODY_FILE_HANDLE_H
