// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILATION_DEPENDENCIES_H_
#define V8_COMPILATION_DEPENDENCIES_H_

#include "src/handles.h"
#include "src/objects.h"
#include "src/objects/map.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

// Collects dependencies for this compilation, e.g. assumptions about
// stable maps, constant globals, etc.
class V8_EXPORT_PRIVATE CompilationDependencies {
 public:
  CompilationDependencies(Isolate* isolate, Zone* zone);

  V8_WARN_UNUSED_RESULT bool Commit(Handle<Code> code);
  void Rollback();
  void Abort();
  bool HasAborted() const;

  // Return the initial map of {function} and record the assumption that it
  // stays the intial map.
  Handle<Map> DependOnInitialMap(Handle<JSFunction> function);

  // Record the assumption that {map} stays stable.
  void DependOnStableMap(Handle<Map> map);

  // Record the assumption that {target_map} can be transitioned to, i.e., that
  // it does not become deprecated.
  void DependOnTransition(Handle<Map> target_map);

  // Return the pretenure mode of {site} and record the assumption that it does
  // not change.
  PretenureFlag DependOnPretenureMode(Handle<AllocationSite> site);

  // Record the assumption that the field type of a field does not change. The
  // field is identified by the argument(s).
  void DependOnFieldType(Handle<Map> map, int descriptor);
  void DependOnFieldType(const LookupIterator* it);

  // Record the assumption that neither {cell}'s {CellType} changes, nor the
  // {IsReadOnly()} flag of {cell}'s {PropertyDetails}.
  void DependOnGlobalProperty(Handle<PropertyCell> cell);

  // Record the assumption that the protector remains valid.
  void DependOnProtector(Handle<PropertyCell> cell);

  void DependOnElementsKind(Handle<AllocationSite> site);

  // Depend on the stability of (the maps of) all prototypes of every class in
  // {receiver_type} up to (and including) the {holder}.
  void DependOnStablePrototypeChains(
      Handle<Context> native_context,
      std::vector<Handle<Map>> const& receiver_maps, Handle<JSObject> holder);

  // Like DependOnElementsKind but also applies to all nested allocation sites.
  void DependOnElementsKinds(Handle<AllocationSite> site);

  class Dependency;  // XXX

 private:
  Isolate* isolate_;
  Zone* zone_;
  Handle<Foreign> object_wrapper_;
  bool aborted_;
  ZoneVector<Handle<HeapObject> >* groups_[DependentCode::kGroupCount];

  std::list<Dependency*> dependencies_;

  bool IsEmpty() const;
  void Insert(DependentCode::DependencyGroup group, Handle<HeapObject> handle);
  void DependOnStablePrototypeChain(Handle<Map> map,
                                    MaybeHandle<JSReceiver> last_prototype);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILATION_DEPENDENCIES_H_
