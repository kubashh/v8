// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load('sort-base.js');

function SetupPolymorphic() {
  CreatePackedSmiArray();

  array_to_sort.push(0.1);
  AssertPackedDoubleElements();

  array_to_sort.push("some string");
  AssertPackedObjectElements();

  array_to_sort[kArraySize + 42] = 42;
  AssertHoleyObjectElements();
}

benchy('Base', Sort, SetupPolymorphic);
benchy('MultipleCompareFns', CreateSortFn([cmp_smaller, cmp_greater]),
       SetupPolymorphic);
