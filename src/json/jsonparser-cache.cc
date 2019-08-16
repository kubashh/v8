// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json/jsonparser-cache.h"

#include "src/common/globals.h"
#include "src/heap/factory.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/jsonparser-cache-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/objects/visitors.h"

namespace v8 {
namespace internal {

// Initial size of each compilation cache table allocated.
static const int kInitialCacheSize = 64;

JsonParserCache::JsonParserCache(Isolate* isolate)
    : isolate_(isolate), object_(isolate), enabled_(true) {
  JsonParserSubCache* subcaches[kSubCacheCount] = {&object_};
  for (int i = 0; i < kSubCacheCount; ++i) {
    subcaches_[i] = subcaches[i];
  }
}

Handle<JsonParserCacheTable> JsonParserSubCache::GetTable(int generation) {
  DCHECK(generation < generations_);
  Handle<JsonParserCacheTable> result;
  if (tables_[generation].IsUndefined(isolate())) {
    result = JsonParserCacheTable::New(isolate(), kInitialCacheSize);
    tables_[generation] = *result;
  } else {
    JsonParserCacheTable table =
        JsonParserCacheTable::cast(tables_[generation]);
    result = Handle<JsonParserCacheTable>(table, isolate());
  }
  return result;
}

void JsonParserSubCache::Age() {
  // Don't directly age single-generation caches.
  if (generations_ == 1) {
    if (!tables_[0].IsUndefined(isolate())) {
      JsonParserCacheTable::cast(tables_[0]).Age();
    }
    return;
  }

  // Age the generations implicitly killing off the oldest.
  for (int i = generations_ - 1; i > 0; i--) {
    tables_[i] = tables_[i - 1];
  }

  // Set the first generation as unborn.
  tables_[0] = ReadOnlyRoots(isolate()).undefined_value();
}

void JsonParserSubCache::Iterate(RootVisitor* v) {
  v->VisitRootPointers(Root::kJsonParserCache, nullptr,
                       FullObjectSlot(&tables_[0]),
                       FullObjectSlot(&tables_[generations_]));
}

void JsonParserSubCache::Clear() {
  MemsetPointer(reinterpret_cast<Address*>(tables_),
                ReadOnlyRoots(isolate()).undefined_value().ptr(), generations_);
}

void JsonParserSubCache::Remove(Handle<Object> object) {
  // Probe the script generation tables. Make sure not to leak handles
  // into the caller's handle scope.
  {
    HandleScope scope(isolate());
    for (int generation = 0; generation < generations(); generation++) {
      Handle<JsonParserCacheTable> table = GetTable(generation);
      table->Remove(*object);
    }
  }
}

JsonParserCacheObject::JsonParserCacheObject(Isolate* isolate)
    : JsonParserSubCache(isolate, 1) {}

// TODO(245): Need to allow identical code from different contexts to
// be cached in the same script generation. Currently the first use
// will be cached, but subsequent code from different source / line
// won't.
MaybeHandle<Object> JsonParserCacheObject::Lookup(
    Handle<String> source, Handle<Context> native_context) {
  MaybeHandle<Object> result;

  // Probe the script generation tables. Make sure not to leak handles
  // into the caller's handle scope.
  {
    HandleScope scope(isolate());
    const int generation = 0;
    DCHECK_EQ(generations(), 1);
    Handle<JsonParserCacheTable> table = GetTable(generation);
    MaybeHandle<Object> probe =
        JsonParserCacheTable::LookupObject(table, source, native_context);
    Handle<Object> object;
    if (probe.ToHandle(&object)) {
      // Break when we've found a suitable shared function info that
      // matches the origin.
      // if (HasOrigin(function_info, name, line_offset, column_offset,
      //               resource_options)) {
      result = scope.CloseAndEscape(object);
      // }
    }
  }

  // Once outside the manacles of the handle scope, we need to recheck
  // to see if we actually found a cached script. If so, we return a
  // handle created in the caller's handle scope.
  //   Handle<SharedFunctionInfo> function_info;
  //   if (result.ToHandle(&function_info)) {
  // #ifdef DEBUG
  //     // Since HasOrigin can allocate, we need to protect the
  //     SharedFunctionInfo
  //     // with handles during the call.
  //     DCHECK(HasOrigin(function_info, name, line_offset, column_offset,
  //                      resource_options));
  // #endif
  //     isolate()->counters()->compilation_cache_hits()->Increment();
  //     LOG(isolate(), CompilationCacheEvent("hit", "script", *function_info));
  //   } else {
  //     isolate()->counters()->compilation_cache_misses()->Increment();
  //   }
  return result;
}

void JsonParserCacheObject::Put(Handle<String> source,
                                Handle<Context> native_context,
                                Handle<Object> object) {
  HandleScope scope(isolate());
  Handle<JsonParserCacheTable> table = GetFirstTable();
  SetFirstTable(
      JsonParserCacheTable::PutObject(table, source, native_context, object));
}

void JsonParserCache::Remove(Handle<Object> object) {
  if (!IsEnabled()) return;

  object_.Remove(object);
}

MaybeHandle<Object> JsonParserCache::LookupObject(
    Handle<String> source, Handle<Context> native_context) {
  if (!IsEnabled()) return MaybeHandle<Object>();

  return object_.Lookup(source, native_context);
}

void JsonParserCache::PutObject(Handle<String> source,
                                Handle<Context> native_context,
                                Handle<Object> object) {
  if (!IsEnabled()) return;
  // LOG(isolate(), CompilationCacheEvent("put", "script", *function_info));
  object_.Put(source, native_context, object);
}

void JsonParserCache::Clear() {
  for (int i = 0; i < kSubCacheCount; i++) {
    subcaches_[i]->Clear();
  }
}

void JsonParserCache::Iterate(RootVisitor* v) {
  for (int i = 0; i < kSubCacheCount; i++) {
    subcaches_[i]->Iterate(v);
  }
}

void JsonParserCache::MarkCompactPrologue() {
  for (int i = 0; i < kSubCacheCount; i++) {
    subcaches_[i]->Age();
  }
}

void JsonParserCache::Enable() { enabled_ = true; }

void JsonParserCache::Disable() {
  enabled_ = false;
  Clear();
}

}  // namespace internal
}  // namespace v8
