#pragma once

#include <draxul/highlight.h>
#include <draxul/log.h>
#include <unordered_map>

namespace draxul
{

// Shared attribute-to-ID cache used by terminal hosts.
//
// Encapsulates the "get or create highlight attr ID, compact on threshold"
// pattern that was previously duplicated across subsystems. Callers provide
// the live-ID set and grid/alt-screen remap glue; this class owns the map,
// counter, and compaction policy.
class AttributeCache
{
public:
    static constexpr uint16_t kCompactionThreshold = 60000;

    // Returns 0 (the sentinel for "no highlight") when the attr is fully
    // default.  Otherwise returns an existing ID or allocates a new one,
    // registering the attr in `highlights`.
    //
    // When the internal counter reaches kCompactionThreshold, returns true
    // in `needs_compact` so the caller can trigger a full compaction pass
    // (which requires grid/alt-screen scanning that only the caller knows
    // how to do).
    uint16_t get_or_insert(const HlAttr& attr, HighlightTable& highlights, bool& needs_compact)
    {
        if (!attr.has_fg && !attr.has_bg && !attr.has_sp
            && !attr.bold && !attr.italic && !attr.underline
            && !attr.undercurl && !attr.strikethrough && !attr.reverse)
            return 0;

        auto it = cache_.find(attr);
        if (it != cache_.end())
            return it->second;

        if (next_id_ >= kCompactionThreshold)
        {
            needs_compact = true;
            return 0; // Caller must compact and retry.
        }

        const uint16_t id = next_id_++;
        cache_.try_emplace(attr, id);
        highlights.set(id, attr);
        return id;
    }

    // Convenience overload for the common "get or insert, compact if needed,
    // then retry" pattern.  The caller supplies a `do_compact` callable that
    // performs the full compaction pass (grid scan, alt-screen scan, etc.)
    // and is invoked at most once.
    template <typename CompactFn>
    uint16_t get_or_insert(const HlAttr& attr, HighlightTable& highlights, CompactFn&& do_compact)
    {
        bool needs_compact = false;
        uint16_t id = get_or_insert(attr, highlights, needs_compact);
        if (!needs_compact)
            return id;

        do_compact();

        // After compaction the attr may already be in the cache (it was live).
        auto it = cache_.find(attr);
        if (it != cache_.end())
            return it->second;

        // Allocate fresh.
        id = next_id_++;
        cache_.try_emplace(attr, id);
        highlights.set(id, attr);
        return id;
    }

    // Performs compaction given a set of live (old_id -> attr) pairs collected
    // by the caller from all sources (grid, alt-screen, scrollback, etc.).
    //
    // Returns a remap table (old_id -> new_id) that the caller must apply to
    // all grid cells, alt-screen cells, scrollback cells, and any other
    // ID-bearing storage.
    std::unordered_map<uint16_t, uint16_t> compact(
        const std::unordered_map<uint16_t, HlAttr>& active_attrs,
        HighlightTable& highlights)
    {
        std::unordered_map<uint16_t, uint16_t> remap;
        remap.reserve(active_attrs.size());

        uint16_t next_id = 1;
        cache_.clear();
        for (const auto& [old_id, attr] : active_attrs)
        {
            const uint16_t new_id = next_id;
            ++next_id;
            remap.try_emplace(old_id, new_id);
            cache_.try_emplace(attr, new_id);
            highlights.set(new_id, attr);
        }

        next_id_ = next_id;

        DRAXUL_LOG_DEBUG(LogCategory::App,
            "AttributeCache::compact: %zu active attrs, next_id now %u",
            active_attrs.size(), static_cast<unsigned>(next_id_));

        return remap;
    }

    void clear()
    {
        cache_.clear();
        next_id_ = 1;
    }

    size_t size() const
    {
        return cache_.size();
    }

    uint16_t next_id() const
    {
        return next_id_;
    }

private:
    std::unordered_map<HlAttr, uint16_t, HlAttrHash> cache_;
    uint16_t next_id_ = 1;
};

} // namespace draxul
