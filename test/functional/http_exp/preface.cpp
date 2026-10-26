// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp/transport/preface.h>

namespace
{

namespace http = riwo::http_exp;

void plaintext_preface_detector()
{
	http::v2_preface_detector detector;
	auto preface = std::string(http::v2_preface);
	auto first = detector.feed(riwo::const_buffer(
		std::string_view(preface).substr(0, 7)));
	RIWO_TEST_CHECK(first == http::preface_result::need_more);
	auto second = detector.feed(riwo::const_buffer(
		std::string_view(preface).substr(7)));
	RIWO_TEST_CHECK(second == http::preface_result::v2);
	RIWO_TEST_CHECK_EQ(detector.buffer(), preface);

	http::v2_preface_detector v1;
	const std::string request = "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n";
	auto result = v1.feed(riwo::const_buffer(request));
	RIWO_TEST_CHECK(result == http::preface_result::v1);
	const auto inspected = request.substr(0,
		http::v2_preface.size());
	RIWO_TEST_CHECK_EQ(v1.buffer(), inspected);
	RIWO_TEST_CHECK_EQ(v1.take_buffer(), inspected);
	RIWO_TEST_CHECK(v1.buffer().empty());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"plaintext preface detector", plaintext_preface_detector}
	});
}
