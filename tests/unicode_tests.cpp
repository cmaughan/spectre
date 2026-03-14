#include "support/test_support.h"

#include <spectre/unicode.h>

using namespace spectre;
using namespace spectre::tests;

void run_unicode_tests()
{
    run_test("unicode helper matches current nvim-like cluster widths", []() {
        expect_eq(cluster_cell_width("e\xCC\x81"), 1, "combining mark cluster is single width");
        expect_eq(cluster_cell_width("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), 2, "emoji with skin tone is double width");
        expect_eq(cluster_cell_width("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"), 2, "flag sequence is double width");
        expect_eq(cluster_cell_width("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6"), 2, "family zwj sequence is double width");
        expect_eq(cluster_cell_width("1\xEF\xB8\x8F\xE2\x83\xA3"), 1, "keycap sequence remains single width");
        expect_eq(cluster_cell_width("\xE2\x9D\xA4"), 1, "text-presentation heart is single width");
        expect_eq(cluster_cell_width("\xE2\x9D\xA4\xEF\xB8\x8F"), 2, "emoji-presentation heart is double width");
        expect_eq(cluster_cell_width("\xE2\x98\x80"), 1, "text-presentation sun is single width");
        expect_eq(cluster_cell_width("\xE2\x98\x80\xEF\xB8\x8F"), 2, "emoji-presentation sun is double width");
        expect_eq(cluster_cell_width("\xE7\x95\x8C"), 2, "cjk ideograph is double width");
        expect_eq(cluster_cell_width("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"), 1, "indic conjunct remains single width");
        expect_eq(cluster_cell_width("\xF3\xB0\x92\xB2"), 1, "lazy nerd-font icon remains single width");
        expect_eq(cluster_cell_width("\xEE\x98\xA0"), 1, "devicon pua icon remains single width");
    });
}
