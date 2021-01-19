// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_CCTEST_TEST_SWISS_HASH_TABLE_SHARED_TESTS_H_
#define V8_TEST_CCTEST_TEST_SWISS_HASH_TABLE_SHARED_TESTS_H_

#include <algorithm>

#include "test/cctest/test-swiss-hash-table-infra.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

extern const char kRuntimeTestFileName[];
extern const char kCSATestFileName[];

// FIXME: solve linking problem.
// static const int kInitialCapacity = 4;

template <typename TestRunner, char const* kTestFileName>
struct SharedSwissTableTests {
  STATIC_ASSERT((std::is_same<TestRunner, RuntimeTestRunner>::value) ||
                (std::is_same<TestRunner, CSATestRunner>::value));

  SharedSwissTableTests() {
    CHECK(kTestFileName == kRuntimeTestFileName ||
          kTestFileName == kCSATestFileName);
  }

  using TS = TestSequence<TestRunner>;

  MEMBER_TEST(Allocation) {
    TS::WithAllInterestingInitialCapacities([](TS& s) {
      // The test runner does the allocation automatically.
      s.CheckCounts(s.initial_capacity, 0, 0);
      s.VerifyHeap();
    });
  }

  MEMBER_TEST(SimpleAdd) {
    TS::WithInitialCapacity(4, [](TS& s) {
      Handle<String> key1 = s.isolate->factory()->InternalizeUtf8String("foo");
      Handle<String> value1 =
          s.isolate->factory()->InternalizeUtf8String("bar");
      PropertyDetails details1 =
          PropertyDetails(PropertyKind::kData, PropertyAttributes::DONT_DELETE,
                          PropertyCellType::kNoCell);

      s.CheckCounts(4, 0, 0);
      s.CheckKeyAbsent(key1);

      s.Add(key1, value1, details1);
      s.CheckDataAtKey(key1, TS::NoIndex, value1, details1);
      s.CheckCounts(4, 1, 0);

      Handle<Symbol> key2 = s.isolate->factory()->NewSymbol();
      Handle<Smi> value2 = handle(Smi::FromInt(123), s.isolate);
      PropertyDetails details2 =
          PropertyDetails(PropertyKind::kData, PropertyAttributes::DONT_DELETE,
                          PropertyCellType::kNoCell);

      s.CheckKeyAbsent(key2);
      s.Add(key2, value2, details2);
      s.CheckDataAtKey(key2, TS::NoIndex, value2, details2);
      s.CheckCounts(4, 2, 0);
    });
  }

  MEMBER_TEST(SimpleUpdate) {
    TS::WithInitialCapacity(4, [](TS& s) {
      Handle<String> key1 = s.isolate->factory()->InternalizeUtf8String("foo");
      Handle<String> value1 =
          s.isolate->factory()->InternalizeUtf8String("bar");
      PropertyDetails details1 =
          PropertyDetails(PropertyKind::kData, PropertyAttributes::DONT_DELETE,
                          PropertyCellType::kNoCell);

      s.Add(key1, value1, details1);

      Handle<Symbol> key2 = s.isolate->factory()->NewSymbol();
      Handle<Smi> value2 = handle(Smi::FromInt(123), s.isolate);
      PropertyDetails details2 =
          PropertyDetails(PropertyKind::kData, PropertyAttributes::DONT_DELETE,
                          PropertyCellType::kNoCell);

      s.Add(key2, value2, details2);

      // Until here same operations as in Test "Add"

      Handle<Smi> value1_updated = handle(Smi::FromInt(456), s.isolate);
      Handle<String> value2_updated =
          s.isolate->factory()->InternalizeUtf8String("updated");
      PropertyDetails details1_updated = details2;
      PropertyDetails details2_updated = details1;

      s.UpdateByKey(key1, value1_updated, details1_updated);
      s.CheckDataAtKey(key1, TS::NoIndex, value1_updated, details1_updated);
      s.CheckDataAtKey(key2, TS::NoIndex, value2, details2);

      s.UpdateByKey(key2, value2_updated, details2_updated);
      s.CheckDataAtKey(key1, TS::NoIndex, value1_updated, details1_updated);
      s.CheckDataAtKey(key2, TS::NoIndex, value2_updated, details2_updated);
      s.CheckCounts(4, 2, 0);
    });
  }

  MEMBER_TEST(SimpleDelete) {
    TS::WithInitialCapacity(4, [](TS& s) {
      Handle<String> key1 = s.isolate->factory()->InternalizeUtf8String("foo");
      Handle<String> value1 =
          s.isolate->factory()->InternalizeUtf8String("bar");
      PropertyDetails details1 =
          PropertyDetails(PropertyKind::kData, PropertyAttributes::DONT_DELETE,
                          PropertyCellType::kNoCell);

      s.Add(key1, value1, details1);

      Handle<Symbol> key2 = s.isolate->factory()->NewSymbol();
      Handle<Smi> value2 = handle(Smi::FromInt(123), s.isolate);
      PropertyDetails details2 =
          PropertyDetails(PropertyKind::kData, PropertyAttributes::DONT_DELETE,
                          PropertyCellType::kNoCell);

      s.Add(key2, value2, details2);

      // Until here same operations as in Test "Add"

      s.DeleteByKey(key1);
      s.CheckKeyAbsent(key1);
      s.CheckDataAtKey(key2, TS::NoIndex, value2, details2);
      s.CheckCounts(4, 1, 1);

      s.DeleteByKey(key2);
      s.CheckKeyAbsent(key1);
      s.CheckKeyAbsent(key2);
      s.CheckCounts(4, 0, 2);
    });
  }

  MEMBER_TEST(AddAtBoundaries) {
    TS::WithAllInterestingInitialCapacities([](TS& s) {
      // Add entries that land in the very first and last buckets of the hash
      // table.
      s.AddAtBoundaries(true);
    });
  }

  MEMBER_TEST(UpdateAtBoundaries) {
    TS::WithAllInterestingInitialCapacities([](TS& s) {
      // Like AddAtBoundaries, then update the values/property details of the
      // entries.
      s.AddAtBoundaries(false);
      s.UpdateAtBoundaries();
    });
  }

  MEMBER_TEST(DeleteAtBoundaries) {
    TS::WithAllInterestingInitialCapacities([](TS& s) {
      // Like AddAtBoundaries, then delete the entries.
      s.AddAtBoundaries(false);
      s.DeleteAtBoundaries(true);
    });
  }

  MEMBER_TEST(OverwritePresentAtBoundaries) {
    TS::WithAllInterestingInitialCapacities([](TS& s) {
      // Like AddAtBoundaries, then add further entries with the same
      // H1 (= targeting same group).
      s.AddAtBoundaries(false);
      s.OverwriteAtBoundaries();

      // The entries added by AddAtBoundaries must also still be there, at their
      // original indices.
      std::vector<int> interesting_indices =
          TS::boundary_indices(s.initial_capacity);
      int count = 0;
      for (int index : interesting_indices) {
        std::string key = std::string("k") + std::to_string(index);
        std::string value = std::string("v") + std::to_string(index);
        PropertyDetails details = TS::distinct_property_details.at(count++);
        s.CheckDataAtKey(key, InternalIndex(index), value, details, index);
      }
    });
  }

  MEMBER_TEST(OverwriteDeletedAtBoundaries) {
    TS::WithAllInterestingInitialCapacities([](TS& s) {
      // Like AddAtBoundaries, then delete those entries and add further entries
      // targeting with the same H1 (= targeting same group).
      s.AddAtBoundaries(false);
      s.DeleteAtBoundaries(false);
      s.OverwriteAtBoundaries();
    });
  }

  MEMBER_TEST(Empty) {
    TS::WithInitialCapacities({0}, [](TS& s) {
      std::string dummy_key = "dummy";
      s.CheckDataAtKey(dummy_key, InternalIndex::NotFound());
    });

    TS::WithInitialCapacities({0}, [](TS& s) {
      std::string key = "key_for_empty";
      std::string value = "value_for_empty";
      PropertyDetails d = PropertyDetails::Empty();

      s.Add(key, value, d);
      s.CheckDataAtKey(key, base::Optional<InternalIndex>(), value, d);
      // should be kInitialCapacity
      s.CheckCounts(4, 1, 0);
    });

    TS::WithInitialCapacity(0, [](TS& s) {
      // (comment causing linebreak)
      s.CheckEnumerationOrder({});
    });

    if (TS::IsRuntimeTest()) {
      TS::WithInitialCapacity(0, [](TS& s) {
        Isolate* isolate = s.isolate;
        s.RuntimeOnlyOperation([isolate](Handle<SwissNameDictionary> d) {
          d->RehashInplace(isolate);
          return d;
        });
        s.CheckCounts(0, 0, 0);
        s.VerifyHeap();
      });
    }
  }

  MEMBER_TEST(Resize) {
    // We test that hash tables get resized/rehashed correctly: For all x
    // between 0 and |max_exponent| (defined below) we do the following, all
    // operating on a single table: We add 2^x entries to the table and then
    // delete every second of those new entries. We then check that the final
    // map contains the entries we expect.

    TS::WithInitialCapacity(0, [](TS& s) {
      // Should be at least 8 so that we capture the transition from 8 bit to 16
      // bit meta table entries:
      int max_exponent = 10;

      int details_size = static_cast<int>(TS::distinct_property_details.size());
      int added = 0;
      int deleted = 0;
      int offset = 0;
      for (int exponent = 0; exponent <= max_exponent; ++exponent) {
        int count = 1 << exponent;
        for (int i = 0; i < count; ++i) {
          std::string key = std::string("key") + std::to_string(offset + i);
          std::string value = std::string("value") + std::to_string(offset + i);

          PropertyDetails details =
              TS::distinct_property_details[(offset + i) % details_size];
          s.Add(key, value, details);
          ++added;
        }
        for (int i = 0; i < count; i += 2) {
          if (offset + i == 0) {
            continue;
          }
          std::string key = std::string("key") + std::to_string(offset + i);
          s.DeleteByKey(key);
          ++deleted;
        }

        s.CheckCounts(TS::NoInt, added - deleted, TS::NoInt);
        offset += count;
      }

      // Some sany checks on the test itself:
      DCHECK_EQ((1 << (max_exponent + 1)) - 1, offset);
      DCHECK_EQ(offset, added);
      DCHECK_EQ(offset / 2, deleted);

      for (int i = 0; i < offset; i += 2) {
        std::string key = std::string("key") + std::to_string(i);
        std::string value = std::string("value") + std::to_string(i);

        PropertyDetails details =
            TS::distinct_property_details[i % details_size];

        s.CheckDataAtKey(key, base::Optional<InternalIndex>(), value, details);
      }
      s.VerifyHeap();
    });
  }

  MEMBER_TEST(AtFullCapacity) {
    // We test that for those capacities that should allow utilizing the full
    // capacitiy before resizing  do indeed allow this. We trust
    // MaxUsableCapacity to tell us which capacities that are (e.g., 4 and 8),
    // because we test that function separately.

    std::vector<int> capacities_allowing_full_utilization;
    for (int c = SwissNameDictionary::kInitialCapacity;
         c <= static_cast<int>(SwissNameDictionary::kGroupWidth); c *= 2) {
      if (SwissNameDictionary::MaxUsableCapacity(c) == c) {
        capacities_allowing_full_utilization.push_back(c);
      }
    }
    DCHECK_IMPLIES(SwissNameDictionary::kGroupWidth == 16,
                   capacities_allowing_full_utilization.size() > 0);

    TS::WithInitialCapacities(capacities_allowing_full_utilization, [](TS& s) {
      for (int i = 0; i < s.initial_capacity; ++i) {
        std::string key = std::string("key") + std::to_string(i);
        s.Add(key);
      }

      s.CheckCounts(s.initial_capacity, s.initial_capacity, 0);
      for (int i = 0; i < s.initial_capacity; ++i) {
        std::string key = std::string("key") + std::to_string(i);
        s.CheckHasKey(key);
      }

      // Must make sure that the first |SwissNameDictionary::kGroupWidth|
      // entries of the ctrl table contain a kEmpty, so that an unsuccessful
      // search terminates. Therefore, search for a fake key whose H1 is 0,
      // making us start from ctrl table bucket 0.
      s.CheckKeyAbsent("non_existing_key", 0);
    });
  }

  MEMBER_TEST(EnumerationOrder) {
    TS::WithInitialCapacities({4, 8, 16, 256}, [](TS& s) {
      int i = 0;
      std::vector<std::string> expected_keys;
      for (; i < SwissNameDictionary::MaxUsableCapacity(s.initial_capacity);
           ++i) {
        std::string key = std::string("key") + std::to_string(i);
        expected_keys.push_back(key);
        s.Add(key);
      }
      s.CheckEnumerationOrder(expected_keys);

      if (i >= 3) {
        std::string last_key = "key" + std::to_string(i - 1);
        s.DeleteByKey("key0");
        s.DeleteByKey("key1");
        s.DeleteByKey(last_key);

        expected_keys.erase(
            std::remove(expected_keys.begin(), expected_keys.end(), "key0"),
            expected_keys.end());
        expected_keys.erase(
            std::remove(expected_keys.begin(), expected_keys.end(), "key1"),
            expected_keys.end());
        expected_keys.erase(
            std::remove(expected_keys.begin(), expected_keys.end(), last_key),
            expected_keys.end());
        DCHECK_EQ(expected_keys.size(), i - 3);
      }

      s.CheckEnumerationOrder(expected_keys);

      for (; i < 2 * SwissNameDictionary::MaxUsableCapacity(s.initial_capacity);
           ++i) {
        std::string key = std::string("key") + std::to_string(i);
        expected_keys.push_back(key);
        s.Add(key);
      }
      s.CheckEnumerationOrder(expected_keys);
    });
  }

  MEMBER_TEST(SameH2) {
    TS::WithInitialCapacity(4, [](TS& s) {
      // Make sure that keys with same H2 don't get mixed up.

      s.Add("first_key", "v1", TS::NoDetails, 0, 42);
      s.Add("second_key", "v2", TS::NoDetails, 128, 42);

      s.CheckDataAtKey("first_key", InternalIndex(0), "v1", TS::NoDetails, 0,
                       42);
      s.CheckDataAtKey("second_key", InternalIndex(1), "v2", TS::NoDetails, 128,
                       42);
    });
  }

  // Check that we can delete a key and add it again.
  MEMBER_TEST(ReAddSameKey) {
    TS::WithInitialCapacity(4, [](TS& s) {
      s.Add("some_key", "some_value", TS::distinct_property_details[0]);
      s.DeleteByKey("some_key");
      s.Add("some_key", "new_value", TS::distinct_property_details[1]);
      s.CheckDataAtKey("some_key", TS::NoIndex, "new_value",
                       TS::distinct_property_details[1]);
    });
  }

  MEMBER_TEST(BeyondInitialGroup) {
    TS::WithInitialCapacity(128, [](TS& s) {
      // Make sure that we continue probing if there is no match in the first
      // group.

      int h1 = 33;     // Arbitrarily chosen.
      int count = 37;  // Will always lead to more than 2 groups being filled.

      for (int i = 0; i < count; ++i) {
        std::string key = std::string("key") + std::to_string(i);
        std::string value = std::string("value") + std::to_string(i);

        s.Add(key, value, TS::NoDetails, h1);
      }

      s.CheckDataAtKey("key36", TS::NoIndex, "value36", TS::NoDetails, h1);

      // Deleting something shouldn't disturb further additions.
      s.DeleteByKey("key14", h1);
      s.DeleteByKey("key15", h1);
      s.DeleteByKey("key16", h1);
      s.DeleteByKey("key17", h1);

      s.Add("key37", "value37", TS::NoDetails, h1);
      s.CheckDataAtKey("key37", TS::NoIndex, "value37", TS::NoDetails, h1);
    });
  }

  MEMBER_TEST(WrapAround) {
    int width = static_cast<int>(Group::kWidth);
    for (int offset_from_end = 0; offset_from_end < width; ++offset_from_end) {
      TS::WithAllInterestingInitialCapacities([width, offset_from_end](TS& s) {
        int capacity = s.initial_capacity;
        int index = capacity - offset_from_end;
        int filler_entries =
            std::min(width, SwissNameDictionary::MaxUsableCapacity(capacity)) -
            1;

        if (index < 0 ||
            // No wraparound in this case:
            index + filler_entries < capacity)
          return;

        // Starting at bucket |index|,  add a sequence of a |kGroupWitdth| - 1
        // (if table can take that many) dummy entries in a single collision
        // chain.
        for (int f = 0; f < filler_entries; ++f) {
          std::string key = std::string("filler") + std::to_string(f);
          s.Add(key, TS::NoValue, TS::NoDetails, index);
        }

        // ... then add a final key which (unless table too small) will end up
        // in the last bucket belonging to the group started at |index|.
        // Check that we can indeed find it.
        std::string final_key = std::string("final_key");
        s.Add(final_key, TS::NoValue, TS::NoDetails, index);
        s.CheckDataAtKey(final_key,
                         InternalIndex(filler_entries - offset_from_end),
                         TS::NoValue, TS::NoDetails, index);

        // Now delete the dummy entries in between and make sure that this
        // doesn't break anything.
        for (int f = 0; f < filler_entries; ++f) {
          std::string key = std::string("filler") + std::to_string(f);
          s.DeleteByKey(key, index);
        }

        s.CheckDataAtKey(final_key, TS::NoIndex, TS::NoValue, TS::NoDetails,
                         index);
      });
    }
  }

  MEMBER_TEST(RehashInplace) {
    if (TS::IsRuntimeTest()) {
      TS::WithAllInterestingInitialCapacities([](TS& s) {
        Isolate* isolate = s.isolate;

        int count =
            s.initial_capacity == 4 && SwissNameDictionary::kGroupWidth == 8
                ? 3
                : 4;
        s.AddMultiple(count);
        s.RuntimeOnlyOperation([isolate](Handle<SwissNameDictionary> d) {
          d->RehashInplace(isolate);
          return d;
        });

        s.CheckMultiple(count);
        s.VerifyHeap();
      });
    }
  }

  MEMBER_TEST(Shrink) {
    // FIXME: make this a non-runtime test if we decide to implement a CSA
    // version of Shrink.
    if (TS::IsRuntimeTest()) {
      TS::WithInitialCapacity(16, [](TS& s) {
        Isolate* isolate = s.isolate;

        // Will cause a resize:
        int count = 20;

        s.AddMultiple(count);
        // Remove all but 4 of the entries we just added.
        for (int i = 4; i < count; ++i) {
          s.DeleteByKey(std::string("key") + std::to_string(i));
        }

        s.RuntimeOnlyOperation([isolate](Handle<SwissNameDictionary> d) {
          return SwissNameDictionary::Shrink(isolate, d);
        });

        s.CheckMultiple(4, "key", "value", 0, false);

        // right now Shrink doesn't shrink to fit, but only halves the capacity.
        int expected_capacity = SwissNameDictionary::CapacityFor(count) / 2;
        s.CheckCounts(expected_capacity, 4, 0);

        s.CheckEnumerationOrder({"key0", "key1", "key2", "key3"});
        s.VerifyHeap();
      });
    }
  }
};

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8

#endif  // V8_TEST_CCTEST_TEST_SWISS_HASH_TABLE_SHARED_TESTS_H_
