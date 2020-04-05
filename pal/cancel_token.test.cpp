#include <pal/cancel_token>
#include <pal/test>


namespace {


TEST_CASE("cancel_token")
{
	SECTION("request_cancel")
	{
		pal::cancel_source src;
		auto tok = src.cancel_token();
		CHECK(tok.cancel_requested() == false);
		src.request_cancel();
		CHECK(tok.cancel_requested() == true);
	}

	SECTION("multiple tokens")
	{
		pal::cancel_source src;
		auto tok1 = src.cancel_token();
		auto tok2 = src.cancel_token();
		src.request_cancel();
		CHECK(tok1.cancel_requested() == true);
		CHECK(tok2.cancel_requested() == true);
	}

	SECTION("copy token")
	{
		pal::cancel_source src;
		auto tok1 = src.cancel_token();
		auto tok2 = tok1;
		src.request_cancel();
		CHECK(tok1.cancel_requested() == true);
		CHECK(tok2.cancel_requested() == true);
	}
}


} // namespace
