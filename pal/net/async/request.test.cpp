#include <pal/net/async/request>
#include <pal/net/test>


namespace {


TEST_CASE("net/async/request")
{
	SECTION("build")
	{
		// useless tests but draws attention if request size increases
		constexpr size_t max_request_size = 168;
		static_assert(sizeof(pal::net::async::request) <= max_request_size);

		static_assert(sizeof(pal::net::async::message) >= sizeof(uintptr_t));

		// also useless tests, ensure pal::net::async::request plays
		// nice with std::variant
		static_assert(std::variant_size_v<pal::net::async::request> == 8);
		static_assert(std::is_same_v<
			pal::net::async::no_request,
			std::variant_alternative_t<0, pal::net::async::request>
		>);
	}
}


TEMPLATE_TEST_CASE("net/async/request", "",
	pal::net::async::message,
	pal::net::async::send_to,
	pal::net::async::receive_from,
	pal::net::async::send,
	pal::net::async::receive,
	pal::net::async::connect,
	pal::net::async::accept)
{
	pal::net::async::request request;

	CHECK_FALSE(request.error());
	CHECK(std::get_if<TestType>(&request) == nullptr);
	CHECK(std::get_if<pal::net::async::no_request>(&request) != nullptr);

	auto &data = request.emplace<TestType>();
	CHECK(std::is_same_v<TestType, std::remove_cvref_t<decltype(data)>>);

	// XXX not required by std::variant
	// Currently it is true for all supported platforms. Might be useful
	// in some situations: from address of data get address of owner
	// request
	CHECK(static_cast<void *>(&request) == static_cast<void *>(&data));

	SECTION("get_if")
	{
		CHECK(std::get_if<TestType>(&request) != nullptr);
		CHECK(std::get_if<pal::net::async::no_request>(&request) == nullptr);
	}

	SECTION("get")
	{
		CHECK(&std::get<TestType>(request) == &data);
		CHECK_THROWS_AS(std::get<pal::net::async::no_request>(request), std::bad_variant_access);
	}

	SECTION("holds_alternative")
	{
		CHECK(std::holds_alternative<TestType>(request));
		CHECK_FALSE(std::holds_alternative<pal::net::async::no_request>(request));
	}

	SECTION("observers")
	{
		CHECK(request.index() != 0);
		CHECK_FALSE(request.valueless_by_exception());
	}

	SECTION("visit")
	{
		auto handle = [](auto &&arg)
		{
			return std::is_same_v<TestType, std::remove_cvref_t<decltype(arg)>>;
		};
		CHECK(std::visit(handle, request));

		request.emplace<pal::net::async::no_request>();
		CHECK_FALSE(std::visit(handle, request));
	}
}


} // namespace
