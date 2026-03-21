#include "support/test_support.h"

#include <draxul/mpack_codec.h>

using namespace draxul;
using namespace draxul::tests;

namespace
{

const MpackValue::MapStorage& as_map(const MpackValue& value)
{
    return value.as_map();
}

} // namespace

void run_rpc_codec_tests()
{
    run_test("mpack codec round-trips nested values", []() {
        MpackValue original = NvimRpc::make_map({
            { NvimRpc::make_str("ok"), NvimRpc::make_bool(true) },
            { NvimRpc::make_str("count"), NvimRpc::make_int(42) },
            { NvimRpc::make_str("items"), NvimRpc::make_array({
                                              NvimRpc::make_nil(),
                                              NvimRpc::make_str("hello"),
                                              NvimRpc::make_uint(7),
                                          }) },
        });

        std::vector<char> encoded;
        expect(encode_mpack_value(original, encoded), "value encodes successfully");

        MpackValue decoded;
        size_t consumed = 0;
        expect(decode_mpack_value(
                   { reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() }, decoded, &consumed),
            "value decodes successfully");
        expect_eq(consumed, encoded.size(), "decoder reports full consumption");
        expect_eq(decoded.type(), MpackValue::Map, "decoded value keeps the map type");
        expect_eq(static_cast<int>(as_map(decoded).size()), 3, "decoded map keeps all entries");
        expect_eq(as_map(decoded)[0].first.as_str(), std::string("ok"), "first map key survives");
        expect_eq(as_map(decoded)[0].second.as_bool(), true, "bool payload survives");
        expect_eq(as_map(decoded)[1].second.as_int(), static_cast<int64_t>(42), "int payload survives");
        expect_eq(as_map(decoded)[2].second.as_array()[1].as_str(), std::string("hello"), "nested array survives");
    });

    run_test("rpc request encoding produces the expected msgpack array shape", []() {
        std::vector<char> encoded;
        expect(encode_rpc_request(99, "nvim_ui_attach", {
                                                            NvimRpc::make_int(80),
                                                            NvimRpc::make_int(24),
                                                        },
                   encoded),
            "request encodes successfully");

        MpackValue decoded;
        expect(decode_mpack_value({ reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() }, decoded),
            "request decodes successfully");
        expect_eq(decoded.type(), MpackValue::Array, "request frame is an array");
        expect_eq(static_cast<int>(decoded.as_array().size()), 4, "request frame has 4 fields");
        expect_eq(decoded.as_array()[0].as_int(), static_cast<int64_t>(0), "request frame type is 0");
        expect_eq(decoded.as_array()[1].as_int(), static_cast<int64_t>(99), "request msgid is preserved");
        expect_eq(decoded.as_array()[2].as_str(), std::string("nvim_ui_attach"), "request method survives");
        expect_eq(decoded.as_array()[3].as_array()[0].as_int(), static_cast<int64_t>(80), "request params survive");
    });

    run_test("mpack decoder decodes ext values as MpackValue::Ext", []() {
        // fixext1: 0xd4 <type_byte> <data_byte>
        const uint8_t raw[] = { 0xd4, 0x05, 0x2a };
        MpackValue decoded;
        expect(decode_mpack_value({ raw, sizeof(raw) }, decoded), "ext value decodes successfully");
        expect_eq(decoded.type(), MpackValue::Ext, "ext value has Ext type");
        expect_eq((int)decoded.as_ext().type, 5, "ext type byte is preserved");
        expect_eq(decoded.as_ext().data, static_cast<int64_t>(42), "ext data is preserved");
    });

    run_test("mpack decoder rejects truncated frames", []() {
        std::vector<char> encoded;
        expect(encode_mpack_value(NvimRpc::make_array({
                                      NvimRpc::make_str("abc"),
                                      NvimRpc::make_int(5),
                                  }),
                   encoded),
            "fixture encodes successfully");
        encoded.pop_back();

        MpackValue decoded;
        expect(!decode_mpack_value({ reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() }, decoded),
            "truncated frame is rejected");
    });
}
