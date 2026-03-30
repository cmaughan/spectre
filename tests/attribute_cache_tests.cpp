#include "support/test_support.h"

#include <draxul/attribute_cache.h>
#include <draxul/grid.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// AttributeCache unit tests (work item #08)
// ---------------------------------------------------------------------------

namespace
{

HlAttr make_attr(uint32_t fg)
{
    HlAttr attr;
    attr.fg = Color(fg);
    attr.has_fg = true;
    return attr;
}

} // namespace

TEST_CASE("get_or_insert returns 0 for default attr", "[attribute_cache]")
{
    AttributeCache cache;
    HighlightTable highlights;
    bool needs_compact = false;

    HlAttr empty;
    REQUIRE(cache.get_or_insert(empty, highlights, needs_compact) == 0);
    REQUIRE_FALSE(needs_compact);
    REQUIRE(cache.size() == 0);
}

TEST_CASE("get_or_insert returns same ID for identical attrs", "[attribute_cache]")
{
    AttributeCache cache;
    HighlightTable highlights;
    bool nc = false;

    auto attr = make_attr(0xFF0000);
    uint16_t id1 = cache.get_or_insert(attr, highlights, nc);
    uint16_t id2 = cache.get_or_insert(attr, highlights, nc);

    REQUIRE(id1 == id2);
    REQUIRE(id1 != 0);
    REQUIRE(cache.size() == 1);
}

TEST_CASE("get_or_insert returns different IDs for different attrs", "[attribute_cache]")
{
    AttributeCache cache;
    HighlightTable highlights;
    bool nc = false;

    uint16_t id1 = cache.get_or_insert(make_attr(0xFF0000), highlights, nc);
    uint16_t id2 = cache.get_or_insert(make_attr(0x00FF00), highlights, nc);
    uint16_t id3 = cache.get_or_insert(make_attr(0x0000FF), highlights, nc);

    REQUIRE(id1 != id2);
    REQUIRE(id2 != id3);
    REQUIRE(id1 != id3);
}

TEST_CASE("compact remaps IDs and preserves active attrs", "[attribute_cache]")
{
    ScopedLogCapture cap;
    AttributeCache cache;
    HighlightTable highlights;
    bool nc = false;

    auto red = make_attr(0xFF0000);
    auto green = make_attr(0x00FF00);
    auto blue = make_attr(0x0000FF);

    uint16_t id_red = cache.get_or_insert(red, highlights, nc);
    uint16_t id_green = cache.get_or_insert(green, highlights, nc);
    cache.get_or_insert(blue, highlights, nc); // blue will not be "active"

    // Only red and green are "active" (appear in cells)
    std::unordered_map<uint16_t, HlAttr> active;
    active[id_red] = red;
    active[id_green] = green;

    auto remap = cache.compact(active, highlights);

    // Remap should have entries for the active IDs
    REQUIRE(remap.count(id_red) == 1);
    REQUIRE(remap.count(id_green) == 1);

    // After compaction, next_id should be 3 (1 + 2 active attrs)
    REQUIRE(cache.next_id() == 3);

    // Cache should only contain the 2 active attrs
    REQUIRE(cache.size() == 2);

    // Inserting red again should return the remapped ID
    uint16_t id_red2 = cache.get_or_insert(red, highlights, nc);
    REQUIRE(id_red2 == remap[id_red]);
}

TEST_CASE("compact with empty active set clears cache", "[attribute_cache]")
{
    ScopedLogCapture cap;
    AttributeCache cache;
    HighlightTable highlights;
    bool nc = false;

    cache.get_or_insert(make_attr(0xFF0000), highlights, nc);
    cache.get_or_insert(make_attr(0x00FF00), highlights, nc);
    REQUIRE(cache.size() == 2);

    std::unordered_map<uint16_t, HlAttr> empty_active;
    cache.compact(empty_active, highlights);

    REQUIRE(cache.size() == 0);
    REQUIRE(cache.next_id() == 1);
}

TEST_CASE("compaction threshold triggers needs_compact", "[attribute_cache]")
{
    ScopedLogCapture cap;
    AttributeCache cache;
    HighlightTable highlights;

    // Fill up to near threshold
    for (uint16_t i = 1; i < AttributeCache::kCompactionThreshold; ++i)
    {
        bool nc = false;
        auto attr = make_attr(i);
        cache.get_or_insert(attr, highlights, nc);
        if (nc)
        {
            // Should not trigger before reaching threshold
            REQUIRE(i + 1 >= AttributeCache::kCompactionThreshold);
            break;
        }
    }

    // The next insert should signal needs_compact
    bool needs_compact = false;
    auto extra = make_attr(0xDEADBE);
    uint16_t id = cache.get_or_insert(extra, highlights, needs_compact);
    REQUIRE(needs_compact);
    REQUIRE(id == 0); // Returns 0 to signal "retry after compact"
}

TEST_CASE("clear resets cache to initial state", "[attribute_cache]")
{
    AttributeCache cache;
    HighlightTable highlights;
    bool nc = false;

    cache.get_or_insert(make_attr(0xFF0000), highlights, nc);
    cache.get_or_insert(make_attr(0x00FF00), highlights, nc);
    REQUIRE(cache.size() == 2);

    cache.clear();
    REQUIRE(cache.size() == 0);
    REQUIRE(cache.next_id() == 1);
}
