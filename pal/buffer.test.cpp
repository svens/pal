#include <pal/buffer>
#include <pal/test>


namespace {


TEMPLATE_TEST_CASE("buffer", "",
	pal::mutable_buffer,
	pal::const_buffer)
{
	char data_1[1], data_2[2];
	TestType empty, buffer_1 = pal::buffer(data_1), buffer_2 = pal::buffer(data_2);

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


TEST_CASE("buffer")
{
	SECTION("void *, size_t")
	{
		size_t size = sizeof(size_t);
		void *data = &size;
		auto buffer = pal::buffer(data, size);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data);
		CHECK(buffer.size() == size);
	}

	SECTION("const void *, size_t")
	{
		size_t size = sizeof(size_t);
		const void *data = &size;
		auto buffer = pal::buffer(data, size);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data);
		CHECK(buffer.size() == size);
	}

	/*
	SECTION("int *, 0")
	{
		int data[2] = { 1, 2, };
		auto buffer = pal::buffer(data, 0);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == nullptr);
		CHECK(buffer.size() == 0);
	}

	SECTION("const int *, 0")
	{
		const int data[2] = { 1, 2, };
		auto buffer = pal::buffer(data, 0);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == nullptr);
		CHECK(buffer.size() == 0);
	}
	*/

	SECTION("T (&)[Size]")
	{
		int data[2] = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data);
		CHECK(buffer.size() == sizeof(data));
	}

	SECTION("const T (&)[Size]")
	{
		const int data[2] = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data);
		CHECK(buffer.size() == sizeof(data));
	}

	SECTION("std::array<T, Size>")
	{
		std::array<int, 2> data = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == sizeof(int) * data.size());
	}

	SECTION("std::array<const T, Size>")
	{
		std::array<const int, 2> data = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == sizeof(int) * data.size());
	}

	SECTION("const std::array<T, Size>")
	{
		const std::array<int, 2> data = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == sizeof(int) * data.size());
	}

	SECTION("std::vector<T>")
	{
		std::vector<int> data = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == sizeof(int) * data.size());
	}

	SECTION("const std::vector<T>")
	{
		const std::vector<int> data = { 1, 2, };
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == sizeof(int) * data.size());
	}

	SECTION("std::string")
	{
		std::string data = "123";
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == data.size());
	}

	SECTION("const std::string")
	{
		const std::string data = "123";
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == data.size());
	}

	SECTION("std::string_view")
	{
		char buf[] = "123";
		std::string_view data{buf};
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == data.size());
	}

	SECTION("std::wstring")
	{
		std::wstring data = L"123";
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == data.size() * sizeof(wchar_t));
	}

	SECTION("const std::wstring")
	{
		const std::wstring data = L"123";
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == data.size() * sizeof(wchar_t));
	}

	SECTION("std::wstring_view")
	{
		wchar_t buf[] = L"123";
		std::wstring_view data{buf};
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == data.size() * sizeof(wchar_t));
	}

	SECTION("mutable_buffer")
	{
		char buf[] = "123";
		const auto b1 = pal::buffer(buf);
		auto b2 = pal::buffer(b1);
		CHECK(std::is_same_v<decltype(b2), pal::mutable_buffer>);
		CHECK(b2.data() == &buf[0]);
		CHECK(b2.size() == sizeof(buf));
	}

	SECTION("mutable_buffer, size_t")
	{
		char buf[] = "123";
		const auto b1 = pal::buffer(buf);
		auto b2 = pal::buffer(b1, 1);
		CHECK(std::is_same_v<decltype(b2), pal::mutable_buffer>);
		CHECK(b2.data() == &buf[0]);
		CHECK(b2.size() == 1);
	}

	SECTION("const_buffer")
	{
		const char buf[] = "123";
		auto b1 = pal::buffer(buf);
		auto b2 = pal::buffer(b1);
		CHECK(std::is_same_v<decltype(b2), pal::const_buffer>);
		CHECK(b2.data() == &buf[0]);
		CHECK(b2.size() == sizeof(buf));
	}

	SECTION("const_buffer, size_t")
	{
		const char buf[] = "123";
		auto b1 = pal::buffer(buf);
		auto b2 = pal::buffer(b1, 1);
		CHECK(std::is_same_v<decltype(b2), pal::const_buffer>);
		CHECK(b2.data() == &buf[0]);
		CHECK(b2.size() == 1);
	}

	SECTION("T (&)[Size], size_t")
	{
		int data[2] = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data);
		CHECK(buffer.size() == 3);
	}

	SECTION("const T (&)[Size], size_t")
	{
		const int data[2] = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data);
		CHECK(buffer.size() == 3);
	}

	SECTION("std::array<T, Size>, size_t")
	{
		std::array<int, 2> data = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 3);
	}

	SECTION("std::array<const T, Size>, size_t")
	{
		std::array<const int, 2> data = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 3);
	}

	SECTION("const std::array<T, Size>, size_t")
	{
		const std::array<int, 2> data = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 3);
	}

	SECTION("std::vector<T>, size_t")
	{
		std::vector<int> data = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 3);
	}

	SECTION("const std::vector<T>, size_t")
	{
		const std::vector<int> data = { 1, 2, };
		auto buffer = pal::buffer(data, 3);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 3);
	}

	SECTION("std::string, size_t")
	{
		std::string data = "123";
		auto buffer = pal::buffer(data, 2);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 2);
	}

	SECTION("const std::string, size_t")
	{
		const std::string data = "123";
		auto buffer = pal::buffer(data, 2);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 2);
	}

	SECTION("std::string_view, size_t")
	{
		char buf[] = "123";
		std::string_view data{buf};
		auto buffer = pal::buffer(data, 2);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 2);
	}

	SECTION("std::wstring, size_t")
	{
		std::wstring data = L"123";
		auto buffer = pal::buffer(data, 2);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 2);
	}

	SECTION("const std::wstring, size_t")
	{
		const std::wstring data = L"123";
		auto buffer = pal::buffer(data, 2);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 2);
	}

	SECTION("std::wstring_view, size_t")
	{
		wchar_t buf[] = L"123";
		std::wstring_view data{buf};
		auto buffer = pal::buffer(data, 2);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == data.data());
		CHECK(buffer.size() == 2);
	}

	SECTION("empty std::vector<T>")
	{
		std::vector<int> data;
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::mutable_buffer>);
		CHECK(buffer.data() == nullptr);
		CHECK(buffer.size() == 0);
	}

	SECTION("empty const std::vector<T>")
	{
		const std::vector<int> data;
		auto buffer = pal::buffer(data);
		CHECK(std::is_same_v<decltype(buffer), pal::const_buffer>);
		CHECK(buffer.data() == nullptr);
		CHECK(buffer.size() == 0);
	}
}


} // namespace
