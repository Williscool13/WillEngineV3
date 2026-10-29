//
// Created by William on 2026-10-29.
//

#ifndef WILL_ENGINE_CATALOG_TABLE_H
#define WILL_ENGINE_CATALOG_TABLE_H

#include <cassert>
#include <cstdint>
#include <utility>

#include "core/string_id.h"
#include "core/containers/map.h"
#include "core/containers/set.h"
#include "core/containers/virtual_array.h"
#include "core/containers/inline_path.h"
#include "logging/engine_log.h"

namespace Engine
{
/**
 * What is on disk for one asset kind: id -> Entry, plus an optional name -> id index.
 * Written only between frames (Scan and editor file actions); reads may run concurrently with each other.
 * Every Stamp marks the entry seen in a scan generation; Prune drops entries a scan did not see.
 * Names: when several entries share a name, FindByName returns one of them and the name is warned about once.
 * @tparam Entry has `Core::Path source` and `uint64_t contentVersion`
 */
template<typename Id, typename Entry>
class CatalogTable
{
    struct Row
    {
        Id id{};
        StringID name{};
        uint32_t scanGeneration{0};
        Entry entry{};
    };

public:
    /** Never pruned: entries that do not come from a file. */
    static constexpr uint32_t PERMANENT = UINT32_MAX;

    struct Stamped
    {
        Entry& entry;
        bool bExisted;
        uint64_t previousVersion;
    };

    struct KVPair
    {
        const Id& key;
        const Entry& value;
    };

    struct MutKVPair
    {
        const Id& key;
        Entry& value;
    };

    template<typename RowT, typename Pair>
    struct IteratorT
    {
        RowT* row;

        bool operator!=(const IteratorT& other) const { return row != other.row; }

        IteratorT& operator++()
        {
            ++row;
            return *this;
        }

        Pair operator*() const { return {row->id, row->entry}; }
    };

    using Iterator = IteratorT<Row, MutKVPair>;
    using ConstIterator = IteratorT<const Row, KVPair>;

    void Init(Core::VirtualMemoryManager* vm, Core::TlsfAllocator* alloc, Core::AllocTag tag, size_t capacity, const char* kind_)
    {
        kind = kind_;
        rows = Core::VirtualArray<Row>(vm, tag, capacity, kind_);
        idToRow = Core::Map<Id, uint32_t>(alloc, tag);
        nameToId = Core::Map<StringID, Id>(alloc, tag);
        ambiguousNames = Core::Set<StringID>(alloc, tag);
    }

    /**
     * Finds or adds id and marks it seen in generation. The caller fills the entry.
     * @param name 0 to keep the entry out of the name index
     * @param source the file being scanned; warns when another file already claimed id this generation
     */
    Stamped Stamp(Id id, StringID name, uint32_t generation, const Core::Path& source)
    {
        assert(id.id != 0 && "CatalogTable: id 0 is invalid");
        if (const uint32_t* index = idToRow.Find(id)) {
            Row& row = rows[*index];
            if (row.scanGeneration == generation && generation != PERMANENT && row.entry.source != source) {
                LOG_WARN(Asset, "Two {} files share id {:x}: '{}' and '{}'; keeping the last", kind, id.id, row.entry.source.c_str(), source.c_str());
            }
            row.name = name;
            row.scanGeneration = generation;
            bNamesDirty = true;
            return {row.entry, true, row.entry.contentVersion};
        }

        const uint32_t index = count++;
        rows.EnsureCommitted(count);
        Row& row = rows[index];
        row.id = id;
        row.name = name;
        row.scanGeneration = generation;
        idToRow[id] = index;
        bNamesDirty = true;
        return {row.entry, false, 0};
    }

    bool Remove(Id id)
    {
        const uint32_t* found = idToRow.Find(id);
        if (found == nullptr) { return false; }
        const uint32_t index = *found;
        idToRow.Remove(id);
        const uint32_t last = count - 1;
        if (index != last) {
            rows[index] = std::move(rows[last]);
            idToRow[rows[index].id] = index;
        }
        rows[last] = Row{};
        --count;
        bNamesDirty = true;
        return true;
    }

    /**
     * Removes entries not stamped in generation, except those keep(Id, Entry&) holds on to.
     * @return number removed
     */
    template<typename Keep>
    int32_t Prune(uint32_t generation, Keep&& keep)
    {
        int32_t removed = 0;
        for (uint32_t i = count; i > 0;) {
            --i;
            Row& row = rows[i];
            if (row.scanGeneration == generation || row.scanGeneration == PERMANENT) { continue; }
            if (keep(row.id, row.entry)) { continue; }
            LOG_INFO(Asset, "{} '{}' is gone from disk", kind, row.entry.source.c_str());
            Remove(row.id);
            ++removed;
        }
        return removed;
    }

    /** Rebuilds the name index after stamps or removes; warns once for each name that became shared. */
    void RebuildNames()
    {
        if (!bNamesDirty) { return; }
        bNamesDirty = false;
        nameToId.Clear();
        const Core::Set<StringID> previouslyAmbiguous = ambiguousNames;
        ambiguousNames.Clear();
        for (uint32_t i = 0; i < count; ++i) {
            const Row& row = rows[i];
            if (row.name.id == 0) { continue; }
            const Id* existing = nameToId.Find(row.name);
            if (existing == nullptr) {
                nameToId[row.name] = row.id;
                continue;
            }
            if (*existing == row.id) { continue; }
            if (ambiguousNames.Insert(row.name) && !previouslyAmbiguous.Contains(row.name)) {
                LOG_WARN(Asset, "{} name shared by several files (e.g. '{}'); by-name lookups pick one", kind, row.entry.source.c_str());
            }
        }
    }

    const Entry* Find(Id id) const
    {
        const uint32_t* index = idToRow.Find(id);
        return index ? &rows[*index].entry : nullptr;
    }

    Entry* Find(Id id)
    {
        const uint32_t* index = idToRow.Find(id);
        return index ? &rows[*index].entry : nullptr;
    }

    bool Contains(Id id) const { return idToRow.Contains(id); }

    /** @return the entry's id, or Id{} */
    Id FindByName(StringID name) const
    {
        const Id* id = nameToId.Find(name);
        return id ? *id : Id{};
    }

    bool IsNameAmbiguous(StringID name) const { return ambiguousNames.Contains(name); }

    size_t Size() const { return count; }

    bool IsEmpty() const { return count == 0; }

    Iterator begin() { return {rows.Data()}; }
    Iterator end() { return {rows.Data() + count}; }
    ConstIterator begin() const { return {rows.Data()}; }
    ConstIterator end() const { return {rows.Data() + count}; }

private:
    const char* kind{""};
    Core::VirtualArray<Row> rows{};
    uint32_t count{0};
    Core::Map<Id, uint32_t> idToRow{};
    Core::Map<StringID, Id> nameToId{};
    Core::Set<StringID> ambiguousNames{};
    bool bNamesDirty{false};
};
} // Engine

#endif //WILL_ENGINE_CATALOG_TABLE_H
