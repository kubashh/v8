// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_H_
#define V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_H_

#include "include/v8config.h"
#include "src/common/globals.h"
#include "src/sandbox/external-entity-table.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

class Isolate;
class Counters;

/**
 * An intermediate table class that abstracts garbage collection mechanism
 * for external pointer tables.
 *
 * Table memory management:
 * ------------------------
 * For the purpose of memory management, the table is partitioned into Segments
 * (for example 64kb memory chunks) that are grouped together in "Spaces". All
 * segments in a space share a freelist, and so entry allocation and garbage
 * collection happen on the level of spaces. The garbage collection algorithm
 * then works as follows:
 *  - One bit of every entry is reserved for the marking bit.
 *  - Every store to an entry automatically sets the marking bit when ORing
 *    with the tag. This avoids the need for write barriers.
 *  - Every load of an entry automatically removes the marking bit when ANDing
 *    with the inverted tag.
 *  - When the GC marking visitor finds a live object with an external pointer,
 *    it marks the corresponding entry as alive through Mark(), which sets the
 *    marking bit using an atomic CAS operation.
 *  - When marking is finished, SweepAndCompact() iterates over a Space once
 *    while the mutator is stopped and builds a freelist from all dead entries
 *    while also removing the marking bit from any live entry.
 *
 * Table compaction:
 * -----------------
 * The table's spaces are to some degree self-compacting: since the freelists
 * are sorted in ascending order (see SweepAndCompact()), segments at the start
 * of the table will usually be fairly well utilized, while later segments
 * might become completely free, in which case they will be deallocated.
 * However, as a single live entry may keep an entire segment alive, the
 * following simple algorithm is used to compact a space if that is deemed
 * necessary:
 *  - At the start of the GC marking phase, determine if a space needs to be
 *    compacted. This decisiont is mostly based on the absolute and relative
 *    size of the freelist.
 *  - If compaction is needed, this algorithm determines by how many segments
 *    it would like to shrink the space (N). It will then attempts to move all
 *    live entries out of these segments so that they can be deallocated
 *    afterwards during sweeping.
 *  - The algorithm then simply selects the last N segments for evacuation, and
 *    it "marks" them for evacuation simply by remembering the start of the
 *    first selected segment. Everything after this threshold value then
 *    becomes the evacuation area. In this way, it becomes very cheap to test
 *    if an entry or segment should be evacuated: only a single integer
 *    comparison against the threshold is required. It also establishes a
 *    simple compaction invariant: compaction always moves an entry at or above
 *    the threshold to a new position before the threshold.
 *  - During marking, whenever a live entry inside the evacuation area is
 *    found, a new "evacuation entry" is allocated from the freelist (which is
 *    assumed to have enough free slots) and the address of the handle in the
 *    object owning the table entry is written into it.
 *  - During sweeping, these evacuation entries are resolved: the content of
 *    the old entry is copied into the new entry and the handle in the object
 *    is updated to point to the new entry.
 *
 * When compacting, it is expected that the evacuation area contains few live
 * entries and that the freelist will be able to serve all evacuation entry
 * allocations. In that case, compaction is essentially free (very little
 * marking overhead, no memory overhead). However, it can happen that the
 * application allocates a large number of table entries during marking, in
 * which case we might end up allocating new entries inside the evacuation area
 * or even allocate entire new segments for the space that's being compacted.
 * If that situation is detected, compaction is aborted during marking.
 *
 * This algorithm assumes that table entries (except for the null entry) are
 * never shared between multiple objects. Otherwise, the following could
 * happen: object A initially has handle H1 and is scanned during incremental
 * marking. Next, object B with handle H2 is scanned and marked for
 * evacuation. Afterwards, object A copies the handle H2 from object B.
 * During sweeping, only object B's handle will be updated to point to the
 * new entry while object A's handle is now dangling. If shared entries ever
 * become necessary, setting external pointer handles would have to be
 * guarded by write barriers to avoid this scenario.
 */
template <typename Entry, size_t size>
class V8_EXPORT_PRIVATE CompactibleExternalEntityTable
    : public ExternalEntityTable<Entry, size> {
 public:
  CompactibleExternalEntityTable() = default;
  CompactibleExternalEntityTable(const CompactibleExternalEntityTable&) =
      delete;
  CompactibleExternalEntityTable& operator=(
      const CompactibleExternalEntityTable&) = delete;

  static inline bool IsValidHandle(ExternalPointerHandle handle);
  static inline uint32_t HandleToIndex(ExternalPointerHandle handle);
  static inline ExternalPointerHandle IndexToHandle(uint32_t index);

  // The Spaces used by an ExternalPointerTable also contain the state related
  // to compaction.
  struct Space : public ExternalEntityTable<Entry, size>::Space {
   public:
    Space() : start_of_evacuation_area_(kNotCompactingMarker) {}

    // Determine if compaction is needed and if so start the compaction.
    // This is expected to be called at the start of the GC marking phase.
    void StartCompactingIfNeeded();

    // During table compaction, we may record the addresses of fields
    // containing external pointer handles (if they are evacuation candidates).
    // As such, if such a field is invalidated (for example because the host
    // object is converted to another object type), we need to be notified of
    // that. Note that we do not need to care about "re-validated" fields here:
    // if an external pointer field is first converted to different kind of
    // field, then again converted to a external pointer field, then it will be
    // re-initialized, at which point it will obtain a new entry in the
    // external pointer table which cannot be a candidate for evacuation.
    inline void NotifyExternalPointerFieldInvalidated(Address field_address);

   private:
    friend class CompactibleExternalEntityTable<Entry, size>;
    friend class ExternalPointerTable;

    // Routines for compaction. See the comment about table compaction above.
    inline bool IsCompacting();
    inline void StartCompacting(uint32_t start_of_evacuation_area);
    inline void StopCompacting();
    inline void AbortCompacting(uint32_t start_of_evacuation_area);
    inline bool CompactingWasAborted();

    inline bool FieldWasInvalidated(Address field_address) const;
    inline void ClearInvalidatedFields();

    // This value indicates that this space is not currently being compacted. It
    // is set to uint32_t max so that determining whether an entry should be
    // evacuated becomes a single comparison:
    // `bool should_be_evacuated = index >= start_of_evacuation_area`.
    static constexpr uint32_t kNotCompactingMarker =
        std::numeric_limits<uint32_t>::max();

    // This value may be ORed into the start of evacuation area threshold
    // during the GC marking phase to indicate that compaction has been
    // aborted because the freelist grew to short and so evacuation entry
    // allocation is no longer possible. This will prevent any further
    // evacuation attempts as entries will be evacuated if their index is at or
    // above the start of the evacuation area, which is now a huge value.
    static constexpr uint32_t kCompactionAbortedMarker = 0xf0000000;

    // When compacting this space, this field contains the index of the first
    // entry in the evacuation area. The evacuation area then consists of all
    // segments above this threshold, and the goal of compaction is to move all
    // live entries out of these segments so that they can be deallocated after
    // sweeping. The field can have the following values:
    // - kNotCompactingMarker: compaction is not currently running.
    // - A kEntriesPerSegment aligned value within: compaction is running and
    //   all entries after this value should be evacuated.
    // - A value that has kCompactionAbortedMarker in its top bits:
    //   compaction has been aborted during marking. The original start of the
    //   evacuation area is still contained in the lower bits.
    std::atomic<uint32_t> start_of_evacuation_area_;

    // List of external pointer fields that have been invalidated. See
    // NotifyExternalPointerFieldInvalidated. Only used when table compaction
    // is running.
    // We expect very few (usually none at all) fields to be invalidated during
    // a GC, so a std::vector is probably better than a std::set or similar.
    std::vector<Address> invalidated_fields_;

    // Mutex guarding access to the invalidated_fields_ set.
    base::Mutex invalidated_fields_mutex_;
  };

  // Marks the specified entry as alive.
  //
  // If the space to which the entry belongs is currently being compacted, this
  // may also mark the entry for evacuation for which the location of the
  // handle is required. See the comments about the compaction algorithm for
  // more details.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(Space* space, ExternalPointerHandle handle,
                   Address handle_location);

  // Frees unmarked entries and finishes space compaction (if running).
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while the table is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t SweepAndCompact(Space* space, Counters* counters);

 private:
  bool TryResolveEvacuationEntryDuringSweeping(
      uint32_t index, ExternalPointerHandle* handle_location,
      uint32_t start_of_evacuation_area);

  inline void MaybeCreateEvacuationEntry(Space* space, uint32_t index,
                                         Address handle_location);

  // Outcome of external pointer table compaction to use for the
  // ExternalPointerTableCompactionOutcome histogram.
  enum class TableCompactionOutcome {
    // Table compaction was successful.
    kSuccess = 0,
    // Outcome 1, partial success, is no longer supported.
    // Table compaction was aborted because the freelist grew to short.
    kAborted = 2,
  };
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_H_
