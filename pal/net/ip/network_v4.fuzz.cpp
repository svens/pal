#include <pal/fuzz.hpp>
#include <pal/net/ip/network_v4.hpp>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerInitialize (int *argc, char ***argv)
{
    pal::fuzz::init(argc, argv, pal::net::ip::network_v4::max_string_length);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *data, std::size_t size)
{
    pal::net::ip::network_v4 net;
    (void)net.from_chars(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(data) + size);
    return 0;
}
