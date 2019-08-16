// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_JSON_JSONPARSER_CACHE_H_
#define V8_JSON_JSONPARSER_CACHE_H_

#include "src/objects/jsonparser-cache.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

template <typename T>
class Handle;

class RootVisitor;

// The compilation cache consists of several generational sub-caches which uses
// this class as a base class. A sub-cache contains a compilation cache tables
// for each generation of the sub-cache. Since the same source code string has
// different compiled code for scripts and evals, we use separate sub-caches
// for different compilation modes, to avoid retrieving the wrong result.
class JsonParserSubCache {
 public:
  JsonParserSubCache(Isolate* isolate, int generations)
      : isolate_(isolate), generations_(generations) {
    tables_ = NewArray<Object>(generations);
  }

  ~JsonParserSubCache() { DeleteArray(tables_); }

  // Index for the first generation in the cache.
  static const int kFirstGeneration = 0;

  // Get the compilation cache tables for a specific generation.
  Handle<JsonParserCacheTable> GetTable(int generation);

  // Accessors for first generation.
  Handle<JsonParserCacheTable> GetFirstTable() {
    return GetTable(kFirstGeneration);
  }
  void SetFirstTable(Handle<JsonParserCacheTable> value) {
    DCHECK_LT(kFirstGeneration, generations_);
    tables_[kFirstGeneration] = *value;
  }

  // Age the sub-cache by evicting the oldest generation and creating a new
  // young generation.
  void Age();

  // GC support.
  void Iterate(RootVisitor* v);

  // Clear this sub-cache evicting all its content.
  void Clear();

  // Remove given shared function info from sub-cache.
  void Remove(Handle<Object> object);

  // Number of generations in this sub-cache.
  inline int generations() { return generations_; }

 protected:
  Isolate* isolate() { return isolate_; }

 private:
  Isolate* isolate_;
  int generations_;  // Number of generations.
  Object* tables_;   // Compilation cache tables - one for each generation.

  DISALLOW_IMPLICIT_CONSTRUCTORS(JsonParserSubCache);
};

// Sub-cache for scripts.
class JsonParserCacheObject : public JsonParserSubCache {
 public:
  explicit JsonParserCacheObject(Isolate* isolate);

  MaybeHandle<Object> Lookup(Handle<String> source,
                             Handle<Context> native_context);

  void Put(Handle<String> source, Handle<Context> native_context,
           Handle<Object> object);

 private:
  // bool HasOrigin(Handle<SharedFunctionInfo> function_info,
  //                MaybeHandle<Object> name, int line_offset, int
  //                column_offset, ScriptOriginOptions resource_options);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JsonParserCacheObject);
};

// The compilation cache keeps shared function infos for compiled
// scripts and evals. The shared function infos are looked up using
// the source string as the key. For regular expressions the
// compilation data is cached.
class V8_EXPORT_PRIVATE JsonParserCache {
 public:
  // Finds the script shared function info for a source
  // string. Returns an empty handle if the cache doesn't contain a
  // script for the given source string with the right origin.
  MaybeHandle<Object> LookupObject(Handle<String> source,
                                   Handle<Context> native_context);

  // Associate the (source, kind) pair to the shared function
  // info. This may overwrite an existing mapping.
  void PutObject(Handle<String> source, Handle<Context> native_context,
                 Handle<Object> object);

  // Clear the cache - also used to initialize the cache at startup.
  void Clear();

  // Remove given shared function info from all caches.
  void Remove(Handle<Object> object);

  // GC support.
  void Iterate(RootVisitor* v);

  // Notify the cache that a mark-sweep garbage collection is about to
  // take place. This is used to retire entries from the cache to
  // avoid keeping them alive too long without using them.
  void MarkCompactPrologue();

  // Enable/disable compilation cache. Used by debugger to disable compilation
  // cache during debugging to make sure new scripts are always compiled.
  void Enable();
  void Disable();

 private:
  explicit JsonParserCache(Isolate* isolate);
  ~JsonParserCache() = default;

  base::HashMap* EagerOptimizingSet();

  // The number of sub caches covering the different types to cache.
  static const int kSubCacheCount = 1;

  bool IsEnabled() const { return enabled_; }

  Isolate* isolate() const { return isolate_; }

  Isolate* isolate_;

  JsonParserCacheObject object_;
  JsonParserSubCache* subcaches_[kSubCacheCount];

  // Current enable state of the compilation cache.
  bool enabled_;

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(JsonParserCache);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_JSON_JSONPARSER_CACHE_H_
