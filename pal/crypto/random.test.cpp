#include <pal/crypto/random>
#include <pal/crypto/test>


namespace {


TEST_CASE("crypto/random")
{
	SECTION("nullptr")
	{
		pal::mutable_buffer buffer;
		pal::crypto::random(buffer);
		CHECK(buffer.size() == 0);
	}

	SECTION("empty")
	{
		int data;
		pal::mutable_buffer buffer{&data, 0};
		pal::crypto::random(buffer);
		CHECK(buffer.size() == 0);
	}

	SECTION("single")
	{
		std::string initial = "test", data = initial;
		pal::crypto::random(pal::buffer(data));
		CHECK(data != initial);
	}

	SECTION("multiple")
	{
		std::string one = "one", two = "two";
		pal::mutable_buffer buffers[] =
		{
			pal::buffer(one),
			pal::buffer(two),
		};
		pal::crypto::random(buffers);
		CHECK(one != "one");
		CHECK(two != "two");
	}
}


} // namespace
