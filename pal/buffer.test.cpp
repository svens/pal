#include <pal/buffer>
#include <pal/test>


namespace {


TEMPLATE_TEST_CASE("buffer", "",
	pal::mutable_buffer,
	pal::const_buffer)
{
	char data_1[1], data_2[2];
	TestType
		empty,
		buffer_1(data_1, sizeof(data_1)),
		buffer_2(data_2, sizeof(data_2));

	SECTION("ctor")
	{
		CHECK(empty.data() == nullptr);
		CHECK(empty.size() == 0);
	}

	SECTION("ctor(data, size)")
	{
		CHECK(buffer_1.data() == data_1);
		CHECK(buffer_1.size() == sizeof(data_1));
	}

	SECTION("ctor(that)")
	{
		TestType a(buffer_1);
		CHECK(a.data() == data_1);
		CHECK(a.size() == sizeof(data_1));
	}

	if constexpr (std::is_same_v<TestType, pal::mutable_buffer>)
	{
		SECTION("ctor(mutable_buffer)")
		{
			pal::const_buffer a(buffer_1);
			CHECK(a.data() == data_1);
			CHECK(a.size() == sizeof(data_1));
		}
	}

	SECTION("+=")
	{
		TestType buffer(buffer_2);
		buffer += 1;
		CHECK(buffer.data() == &data_2[1]);
		CHECK(buffer.size() == sizeof(data_2) - 1);
	}

	SECTION("+= overflow")
	{
		TestType buffer(buffer_2);
		buffer += 2 * sizeof(data_2);
		CHECK(buffer.data() == &data_2[0] + 2);
		CHECK(buffer.size() == 0);
	}

	SECTION("buffer_sequence_{begin,end}(1)")
	{
		CHECK(pal::buffer_sequence_begin(empty) == &empty);
		CHECK(pal::buffer_sequence_end(empty) == &empty + 1);
	}

	SECTION("buffer_sequence_{begin,end}(N)")
	{
		TestType buffers[] = { empty, buffer_1, buffer_2 };
		CHECK(pal::buffer_sequence_begin(buffers) == &buffers[0]);
		CHECK(pal::buffer_sequence_end(buffers) == &buffers[3]);

		TestType cbuffers[] = { empty, buffer_1, buffer_2 };
		CHECK(pal::buffer_sequence_begin(cbuffers) == &cbuffers[0]);
		CHECK(pal::buffer_sequence_end(cbuffers) == &cbuffers[3]);
	}

	SECTION("buffer_sequence_size")
	{
		CHECK(pal::buffer_size(empty) == empty.size());
		CHECK(pal::buffer_size(buffer_1) == buffer_1.size());
		CHECK(pal::buffer_size(buffer_2) == buffer_2.size());

		TestType buffers[] = { empty, buffer_1, buffer_2 };
		CHECK(pal::buffer_size(buffers) == 3);
	}
}


} // namespace
