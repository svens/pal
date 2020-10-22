#include <pal/buffer>
#include <pal/test>


namespace {


TEMPLATE_TEST_CASE("buffer", "",
	pal::mutable_buffer,
	pal::const_buffer)
{
	SECTION("ctor")
	{
		TestType buffer;
		CHECK(buffer.data() == nullptr);
		CHECK(buffer.size() == 0);
	}

	SECTION("ctor(data, size)")
	{
		char buf[1];
		TestType buffer(buf, sizeof(buf));
		CHECK(buffer.data() == buf);
		CHECK(buffer.size() == sizeof(buf));
	}

	SECTION("ctor(that)")
	{
		char buf[1];
		TestType a(buf, sizeof(buf));
		TestType b(a);
		CHECK(b.data() == buf);
		CHECK(b.size() == sizeof(buf));
	}

	if constexpr (std::is_same_v<TestType, pal::const_buffer>)
	{
		SECTION("ctor(mutable_buffer)")
		{
			char buf[1];
			pal::mutable_buffer a(buf, sizeof(buf));
			TestType b(a);
			CHECK(b.data() == buf);
			CHECK(b.size() == sizeof(buf));
		}
	}

	SECTION("+=")
	{
		char buf[2];
		TestType buffer(buf, sizeof(buf));
		buffer += 1;
		CHECK(buffer.data() == &buf[1]);
		CHECK(buffer.size() == sizeof(buf) - 1);
	}

	SECTION("+= overflow")
	{
		char buf[2];
		TestType buffer(buf, sizeof(buf));
		buffer += 2*sizeof(buf);
		CHECK(buffer.size() == 0);
	}
}


} // namespace
