// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp.h>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] http::protocol_requirement protocol_requirement(
	std::string_view value)
{
	if( value == "auto" ) return http::protocol_requirement::automatic;
	if( value == "http1" ) return http::protocol_requirement::require_http1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "http2" ) return http::protocol_requirement::require_http2;
#endif
	throw std::invalid_argument(
		"protocol must be auto, http1"
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		" or http2"
#endif
	);
}

[[nodiscard]] std::string_view protocol_name(http::protocol value) noexcept
{
	switch(value)
	{
	case http::protocol::v1_0: return "HTTP/1.0";
	case http::protocol::v1_1: return "HTTP/1.1";
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	case http::protocol::v2: return "HTTP/2";
#endif
	}
	return "unknown";
}

[[nodiscard]] std::string as_string(std::span<const std::byte> bytes)
{
	return {
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size()
	};
}

} //namespace

int main(int argc, const char *argv[])
try
{
	if( argc < 2 )
	{
		std::cerr << "Usage: client_file <upload-file> [download-file] "
			"[base-url] [auto|http1|http2]\n";
		return 2;
	}

	const auto upload_file = std::filesystem::absolute(argv[1]);
	const auto download_file = std::filesystem::absolute(
		argc > 2 ? argv[2] : "riwo-http-exp-downloaded.bin");
	const std::string base = argc > 3 ?
		argv[3] : "http://127.0.0.1:8083";
	const auto requirement = protocol_requirement(
		argc > 4 ? std::string_view(argv[4]) : std::string_view("auto"));

	riwo::io_context_t context;
	http::client_config config;
	config.protocols.requirement = requirement;
	config.pool.idle_timeout = std::chrono::milliseconds(1);
	config.timeout.body_idle = std::chrono::milliseconds(500);
	http::client client(context, config);

	uint64_t upload_total = 0;
	uint64_t upload_done = 0;
	http::request upload_request(http::method::post, base + "/upload");
	upload_request.file(upload_file);
	upload_request.on_upload_progress(
		[&](uint64_t transferred, uint64_t total)
		{
			upload_done = transferred;
			upload_total = total;
		});
	auto uploaded = client.fetch(std::move(upload_request));
	std::cout << "Upload [" << protocol_name(uploaded.selected_protocol())
		<< "] -> " << uploaded.status().code() << ", progress "
		<< upload_done << '/' << upload_total << '\n'
		<< as_string(uploaded.buffered_body());

	uint64_t download_total = 0;
	uint64_t download_done = 0;
	http::request download_request(http::method::get, base + "/download");
	download_request.on_download_progress(
		[&](uint64_t transferred, uint64_t total)
		{
			download_done = transferred;
			download_total = total;
		});
	auto downloaded = client.send(std::move(download_request));
	const auto etag = downloaded.headers().find_first("ETag");
	const auto saved = downloaded.save_file(download_file);
	std::cout << "Download [" << protocol_name(downloaded.selected_protocol())
		<< "] -> " << downloaded.status().code() << ", saved " << saved
		<< " bytes, progress " << download_done << '/' << download_total << '\n'
		<< "Destination: " << download_file << '\n';

	if( etag )
	{
		http::request conditional(http::method::get, base + "/download");
		conditional.headers().add("If-None-Match", *etag);
		auto unchanged = client.fetch(std::move(conditional));
		std::cout << "Conditional GET [" << protocol_name(
			unchanged.selected_protocol()) << "] -> "
			<< unchanged.status().code() << " (ETag " << *etag << ")\n";
	}

	if( std::filesystem::file_size(download_file) != 0 )
	{
		http::request range(http::method::get, base + "/download");
		range.headers().add("Range", "bytes=0-15");
		auto partial = client.send(std::move(range));
		const auto body = partial.read_all();
		std::cout << "Range GET [" << protocol_name(partial.selected_protocol())
			<< "] -> " << partial.status().code() << ", "
			<< body.size() << " bytes\n";
	}
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP file client failed: " << exception.what() << '\n';
	return 1;
}
