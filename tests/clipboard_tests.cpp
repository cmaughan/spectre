#include "support/test_support.h"

#include <draxul/nvim.h>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// These helpers replicate the conversion logic in App::handle_clipboard_set
// and App::handle_rpc_request("clipboard_get") verbatim so the tests exercise
// the same algorithm without requiring a full App/Window instantiation.

namespace
{

// Mirror of App::handle_clipboard_set: join lines array into a single string.
std::string clipboard_lines_to_text(const std::vector<MpackValue>& params)
{
    if (params.size() < 3 || params[1].type() != MpackValue::Array)
        return {};

    const auto& lines = params[1].as_array();
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            text += '\n';
        if (lines[i].type() == MpackValue::String)
            text += lines[i].as_str();
    }
    return text;
}

// Mirror of App::handle_rpc_request("clipboard_get"): split text on '\n' and
// return [lines_array, regtype] in the same MpackValue shape.
MpackValue clipboard_text_to_response(const std::string& text)
{
    std::vector<MpackValue> lines;
    std::string::size_type pos = 0;
    while (pos <= text.size())
    {
        auto nl = text.find('\n', pos);
        if (nl == std::string::npos)
        {
            lines.push_back(NvimRpc::make_str(text.substr(pos)));
            break;
        }
        lines.push_back(NvimRpc::make_str(text.substr(pos, nl - pos)));
        pos = nl + 1;
    }
    if (lines.empty())
        lines.push_back(NvimRpc::make_str(""));

    return NvimRpc::make_array({ NvimRpc::make_array(std::move(lines)), NvimRpc::make_str("v") });
}

// Build a clipboard_set notification params vector [register, lines, regtype].
std::vector<MpackValue> make_clipboard_set_params(const std::string& reg,
    const std::vector<std::string>& lines,
    const std::string& regtype)
{
    std::vector<MpackValue> line_values;
    for (const auto& l : lines)
        line_values.push_back(NvimRpc::make_str(l));

    return {
        NvimRpc::make_str(reg),
        NvimRpc::make_array(std::move(line_values)),
        NvimRpc::make_str(regtype),
    };
}

} // namespace

void run_clipboard_tests()
{
    run_test("clipboard_set notification stores single-line text", []() {
        auto params = make_clipboard_set_params("+", { "hello world" }, "v");
        std::string text = clipboard_lines_to_text(params);
        expect_eq(text, std::string("hello world"), "single line survives clipboard_set");
    });

    run_test("clipboard_get returns single-line text as one-element array", []() {
        MpackValue response = clipboard_text_to_response("hello world");
        expect_eq(response.type(), MpackValue::Array, "response is an array");
        const auto& outer = response.as_array();
        expect_eq(static_cast<int>(outer.size()), 2, "response has two elements");
        expect_eq(outer[0].type(), MpackValue::Array, "first element is the lines array");
        expect_eq(static_cast<int>(outer[0].as_array().size()), 1, "single line produces one element");
        expect_eq(outer[0].as_array()[0].as_str(), std::string("hello world"), "line text survives");
        expect_eq(outer[1].as_str(), std::string("v"), "regtype is v");
    });

    run_test("clipboard round-trip preserves single-line text", []() {
        // clipboard_set joins lines, clipboard_get splits them back.
        auto set_params = make_clipboard_set_params("+", { "hello world" }, "v");
        std::string stored = clipboard_lines_to_text(set_params);
        MpackValue response = clipboard_text_to_response(stored);
        const auto& lines = response.as_array()[0].as_array();
        expect_eq(static_cast<int>(lines.size()), 1, "round-trip preserves line count");
        expect_eq(lines[0].as_str(), std::string("hello world"), "round-trip preserves text");
    });

    run_test("clipboard round-trip preserves multi-line text", []() {
        auto set_params = make_clipboard_set_params("+", { "line one", "line two", "line three" }, "V");
        std::string stored = clipboard_lines_to_text(set_params);
        expect_eq(stored, std::string("line one\nline two\nline three"), "lines are joined with newlines");

        MpackValue response = clipboard_text_to_response(stored);
        const auto& lines = response.as_array()[0].as_array();
        expect_eq(static_cast<int>(lines.size()), 3, "multi-line round-trip preserves line count");
        expect_eq(lines[0].as_str(), std::string("line one"), "first line survives round-trip");
        expect_eq(lines[1].as_str(), std::string("line two"), "second line survives round-trip");
        expect_eq(lines[2].as_str(), std::string("line three"), "third line survives round-trip");
    });

    run_test("clipboard_get on empty text returns one empty-string element", []() {
        MpackValue response = clipboard_text_to_response("");
        const auto& lines = response.as_array()[0].as_array();
        expect_eq(static_cast<int>(lines.size()), 1, "empty text produces one element");
        expect_eq(lines[0].as_str(), std::string(""), "the element is an empty string");
    });

    run_test("clipboard_get before any clipboard_set returns sensible default", []() {
        // Before clipboard_set is ever called the internal text is empty.
        // clipboard_text_to_response("") must return a valid [[""], "v"] shape,
        // not nil or a malformed array, so neovim's paste handler does not crash.
        MpackValue response = clipboard_text_to_response("");
        expect_eq(response.type(), MpackValue::Array, "response is an array even for empty clipboard");
        const auto& outer = response.as_array();
        expect_eq(static_cast<int>(outer.size()), 2, "response has two elements even for empty clipboard");
        expect_eq(outer[0].type(), MpackValue::Array, "lines element is an array");
        expect(!outer[0].as_array().empty(), "lines array is never empty");
    });

    run_test("clipboard_set ignores malformed notifications", []() {
        // Too few params.
        std::string text = clipboard_lines_to_text({});
        expect_eq(text, std::string(""), "too few params yields empty string");

        // Second param is not an array.
        std::vector<MpackValue> bad_params = {
            NvimRpc::make_str("+"),
            NvimRpc::make_str("not-an-array"),
            NvimRpc::make_str("v"),
        };
        text = clipboard_lines_to_text(bad_params);
        expect_eq(text, std::string(""), "non-array lines param yields empty string");
    });
}
