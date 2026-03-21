#include "support/test_support.h"

#include <draxul/nvim_rpc.h>

using namespace draxul;
using namespace draxul::tests;

// Static assertions pin each variant alternative index to its expected C++ type.
// If the Storage declaration order is ever changed, these will fail at compile time.
static_assert(std::is_same_v<std::variant_alternative_t<0, MpackValue::Storage>, std::monostate>,
    "MpackValue::Storage index 0 must be std::monostate (Nil)");
static_assert(std::is_same_v<std::variant_alternative_t<1, MpackValue::Storage>, bool>,
    "MpackValue::Storage index 1 must be bool");
static_assert(std::is_same_v<std::variant_alternative_t<2, MpackValue::Storage>, int64_t>,
    "MpackValue::Storage index 2 must be int64_t");
static_assert(std::is_same_v<std::variant_alternative_t<3, MpackValue::Storage>, uint64_t>,
    "MpackValue::Storage index 3 must be uint64_t");
static_assert(std::is_same_v<std::variant_alternative_t<4, MpackValue::Storage>, double>,
    "MpackValue::Storage index 4 must be double");
static_assert(std::is_same_v<std::variant_alternative_t<5, MpackValue::Storage>, std::string>,
    "MpackValue::Storage index 5 must be std::string");
static_assert(std::is_same_v<std::variant_alternative_t<6, MpackValue::Storage>, MpackValue::ArrayStorage>,
    "MpackValue::Storage index 6 must be ArrayStorage");
static_assert(std::is_same_v<std::variant_alternative_t<7, MpackValue::Storage>, MpackValue::MapStorage>,
    "MpackValue::Storage index 7 must be MapStorage");
static_assert(std::is_same_v<std::variant_alternative_t<8, MpackValue::Storage>, MpackValue::ExtValue>,
    "MpackValue::Storage index 8 must be ExtValue");

void run_mpackvalue_variant_stability_tests()
{
    run_test("MpackValue default-constructed type() is Nil", []() {
        MpackValue val;
        expect_eq(val.type(), MpackValue::Nil, "default MpackValue reports Nil type");
    });

    run_test("MpackValue nil type() is Nil", []() {
        MpackValue val = NvimRpc::make_nil();
        expect_eq(val.type(), MpackValue::Nil, "nil MpackValue reports Nil type");
    });

    run_test("MpackValue bool type() is Bool", []() {
        MpackValue val_true = NvimRpc::make_bool(true);
        expect_eq(val_true.type(), MpackValue::Bool, "bool(true) MpackValue reports Bool type");

        MpackValue val_false = NvimRpc::make_bool(false);
        expect_eq(val_false.type(), MpackValue::Bool, "bool(false) MpackValue reports Bool type");
    });

    run_test("MpackValue int type() is Int", []() {
        MpackValue val = NvimRpc::make_int(42);
        expect_eq(val.type(), MpackValue::Int, "int MpackValue reports Int type");

        MpackValue val_neg = NvimRpc::make_int(-1);
        expect_eq(val_neg.type(), MpackValue::Int, "negative int MpackValue reports Int type");
    });

    run_test("MpackValue uint type() is UInt", []() {
        MpackValue val = NvimRpc::make_uint(99);
        expect_eq(val.type(), MpackValue::UInt, "uint MpackValue reports UInt type");
    });

    run_test("MpackValue float type() is Float", []() {
        MpackValue val;
        val.storage = 3.14;
        expect_eq(val.type(), MpackValue::Float, "double MpackValue reports Float type");
    });

    run_test("MpackValue string type() is String", []() {
        MpackValue val = NvimRpc::make_str("hello");
        expect_eq(val.type(), MpackValue::String, "string MpackValue reports String type");
    });

    run_test("MpackValue array type() is Array", []() {
        MpackValue val = NvimRpc::make_array({ NvimRpc::make_int(1), NvimRpc::make_int(2) });
        expect_eq(val.type(), MpackValue::Array, "array MpackValue reports Array type");
    });

    run_test("MpackValue map type() is Map", []() {
        MpackValue val = NvimRpc::make_map({ { NvimRpc::make_str("key"), NvimRpc::make_int(1) } });
        expect_eq(val.type(), MpackValue::Map, "map MpackValue reports Map type");
    });

    run_test("MpackValue ext type() is Ext", []() {
        MpackValue val;
        val.storage = MpackValue::ExtValue{ 5, 42 };
        expect_eq(val.type(), MpackValue::Ext, "ext MpackValue reports Ext type");
    });

    run_test("MpackValue type() does not conflate Bool with Int", []() {
        MpackValue bool_val = NvimRpc::make_bool(true);
        MpackValue int_val = NvimRpc::make_int(1);
        expect(bool_val.type() != int_val.type(), "Bool and Int types must be distinct");
    });

    run_test("MpackValue type() does not conflate Int with UInt", []() {
        MpackValue int_val = NvimRpc::make_int(1);
        MpackValue uint_val = NvimRpc::make_uint(1);
        expect(int_val.type() != uint_val.type(), "Int and UInt types must be distinct");
    });
}
