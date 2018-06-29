// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compilation-dependencies.h"

#include "src/handles-inl.h"
#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

// TODO
// Move helpers to DependentCode.
// Move parts of DependOnFoo to FooDependency constructor.
// Shouldn't need descriptor for FieldTypeDependency.

namespace {

DependentCode* GetDependentCode(Handle<Object> object) {
  if (object->IsMap()) {
    return Handle<Map>::cast(object)->dependent_code();
  } else if (object->IsPropertyCell()) {
    return Handle<PropertyCell>::cast(object)->dependent_code();
  } else if (object->IsAllocationSite()) {
    return Handle<AllocationSite>::cast(object)->dependent_code();
  }
  UNREACHABLE();
}

void SetDependentCode(Handle<Object> object, Handle<DependentCode> dep) {
  if (object->IsMap()) {
    Handle<Map>::cast(object)->set_dependent_code(*dep);
  } else if (object->IsPropertyCell()) {
    Handle<PropertyCell>::cast(object)->set_dependent_code(*dep);
  } else if (object->IsAllocationSite()) {
    Handle<AllocationSite>::cast(object)->set_dependent_code(*dep);
  } else {
    UNREACHABLE();
  }
}

// Always returns true. bool instead of void for convenient use.
bool InstallDependency(Isolate* isolate, Handle<WeakCell> source,
                       Handle<HeapObject> target,
                       DependentCode::DependencyGroup group) {
  // XXX DCHECK_NOT_NULL(isolate->handle_scope_data()->canonical_scope);
  Handle<DependentCode> old_deps(GetDependentCode(target), isolate);
  Handle<DependentCode> new_deps =
      DependentCode::InsertWeakCode(old_deps, group, source);
  // Update the list head if necessary.
  if (!new_deps.equals(old_deps)) SetDependentCode(target, new_deps);
  return true;
}

}  // namespace

class CompilationDependencies::Dependency : public ZoneObject {
 public:
  virtual bool Install(Isolate* isolate, Handle<WeakCell> code) = 0;

 private:
  virtual bool IsValid() const = 0;
};

class InitialMapDependency final : public CompilationDependencies::Dependency {
 public:
  InitialMapDependency(Handle<JSFunction> function, Handle<Map> initial_map)
      : function_(function), initial_map_(initial_map) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    DCHECK(function_->has_initial_map());
    Handle<Map> initial_map(function_->initial_map(), function_->GetIsolate());
    return initial_map.equals(initial_map_);
  }

  bool Install(Isolate* isolate, Handle<WeakCell> code) override {
    return IsValid() &&
           InstallDependency(isolate, code, initial_map_,
                             DependentCode::kInitialMapChangedGroup);
  }

 private:
  Handle<JSFunction> function_;
  Handle<Map> initial_map_;
};

class StableMapDependency final : public CompilationDependencies::Dependency {
 public:
  StableMapDependency(Handle<Map> map) : map_(map) { DCHECK(IsValid()); }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return map_->is_stable();
  }

  bool Install(Isolate* isolate, Handle<WeakCell> code) override {
    return IsValid() && InstallDependency(isolate, code, map_,
                                          DependentCode::kPrototypeCheckGroup);
  }

 private:
  Handle<Map> map_;
};

class TransitionDependency final : public CompilationDependencies::Dependency {
 public:
  TransitionDependency(Handle<Map> map) : map_(map) { DCHECK(IsValid()); }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return !map_->is_deprecated();
  }

  bool Install(Isolate* isolate, Handle<WeakCell> code) override {
    return IsValid() && InstallDependency(isolate, code, map_,
                                          DependentCode::kTransitionGroup);
  }

 private:
  Handle<Map> map_;
};

class PretenureModeDependency final
    : public CompilationDependencies::Dependency {
 public:
  PretenureModeDependency(Handle<AllocationSite> site, PretenureFlag mode)
      : site_(site), mode_(mode) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return mode_ == site_->GetPretenureMode();
  }

  bool Install(Isolate* isolate, Handle<WeakCell> code) override {
    return IsValid() && InstallDependency(
                            isolate, code, site_,
                            DependentCode::kAllocationSiteTenuringChangedGroup);
  }

 private:
  Handle<AllocationSite> site_;
  PretenureFlag mode_;
};

class FieldTypeDependency final : public CompilationDependencies::Dependency {
 public:
  FieldTypeDependency(Handle<Map> owner, int descriptor, Handle<FieldType> type)
      : owner_(owner), descriptor_(descriptor), type_(type) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    DCHECK_EQ(*owner_, owner_->FindFieldOwner(nullptr /* XXX */, descriptor_));
    return *type_ == owner_->instance_descriptors()->GetFieldType(descriptor_);
  }

  bool Install(Isolate* isolate, Handle<WeakCell> code) override {
    return IsValid() && InstallDependency(isolate, code, owner_,
                                          DependentCode::kFieldOwnerGroup);
  }

 private:
  Handle<Map> owner_;
  int descriptor_;
  Handle<FieldType> type_;
};

class GlobalPropertyDependency final
    : public CompilationDependencies::Dependency {
 public:
  GlobalPropertyDependency(Handle<PropertyCell> cell, PropertyCellType type,
                           bool read_only)
      : cell_(cell), type_(type), read_only_(read_only) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return type_ == cell_->property_details().cell_type() &&
           read_only_ == cell_->property_details().IsReadOnly();
  }

  bool Install(Isolate* isolate, Handle<WeakCell> code) override {
    return IsValid() &&
           InstallDependency(isolate, code, cell_,
                             DependentCode::kPropertyCellChangedGroup);
  }

 private:
  Handle<PropertyCell> cell_;
  PropertyCellType type_;
  bool read_only_;
};

CompilationDependencies::CompilationDependencies(Isolate* isolate, Zone* zone)
    : isolate_(isolate),
      zone_(zone),
      object_wrapper_(Handle<Foreign>::null()),
      aborted_(false) {
  std::fill_n(groups_, DependentCode::kGroupCount, nullptr);
}

void CompilationDependencies::Abort() { aborted_ = true; }

bool CompilationDependencies::HasAborted() const { return aborted_; }

bool CompilationDependencies::IsEmpty() const {
  for (int i = 0; i < DependentCode::kGroupCount; i++) {
    if (groups_[i]) return false;
  }
  return true;
}

Handle<Map> CompilationDependencies::DependOnInitialMap(
    Handle<JSFunction> function) {
  Handle<Map> map(function->initial_map(), function->GetIsolate());
  dependencies_.push_front(new (zone_) InitialMapDependency(function, map));
  return map;
}

void CompilationDependencies::DependOnStableMap(Handle<Map> map) {
  if (map->CanTransition()) {
    dependencies_.push_front(new (zone_) StableMapDependency(map));
  } else {
    DCHECK(map->is_stable());
  }
}

void CompilationDependencies::DependOnTransition(Handle<Map> target_map) {
  if (target_map->CanBeDeprecated()) {
    dependencies_.push_front(new (zone_) TransitionDependency(target_map));
  } else {
    DCHECK(!target_map->is_deprecated());
  }
}

PretenureFlag CompilationDependencies::DependOnPretenureMode(
    Handle<AllocationSite> site) {
  PretenureFlag mode = site->GetPretenureMode();
  dependencies_.push_front(new (zone_) PretenureModeDependency(site, mode));
  return mode;
}

void CompilationDependencies::DependOnFieldType(Handle<Map> map,
                                                int descriptor) {
  Handle<Map> owner(map->FindFieldOwner(isolate_, descriptor), isolate_);
  Handle<FieldType> type(
      owner->instance_descriptors()->GetFieldType(descriptor), isolate_);
  DCHECK(type.equals(
      handle(map->instance_descriptors()->GetFieldType(descriptor), isolate_)));
  dependencies_.push_front(new (zone_)
                               FieldTypeDependency(owner, descriptor, type));
}

void CompilationDependencies::DependOnFieldType(const LookupIterator* it) {
  Handle<Map> owner = it->GetFieldOwnerMap();
  int descriptor = it->GetFieldDescriptorIndex();
  Handle<FieldType> type = it->GetFieldType();
  DCHECK(type.equals(
      handle(it->GetHolder<Map>()->map()->instance_descriptors()->GetFieldType(
                 descriptor),
             isolate_)));
  dependencies_.push_front(new (zone_)
                               FieldTypeDependency(owner, descriptor, type));
}

void CompilationDependencies::DependOnGlobalProperty(
    Handle<PropertyCell> cell) {
  PropertyCellType type = cell->property_details().cell_type();
  bool read_only = cell->property_details().IsReadOnly();
  dependencies_.push_front(new (zone_)
                               GlobalPropertyDependency(cell, type, read_only));
}

void CompilationDependencies::Insert(DependentCode::DependencyGroup group,
                                     Handle<HeapObject> object) {
  if (groups_[group] == nullptr) {
    groups_[group] = new (zone_->New(sizeof(ZoneVector<Handle<HeapObject>>)))
        ZoneVector<Handle<HeapObject>>(zone_);
  }
  groups_[group]->push_back(object);

  if (object_wrapper_.is_null()) {
    // Allocate the wrapper if necessary.
    object_wrapper_ =
        isolate_->factory()->NewForeign(reinterpret_cast<Address>(this));
  }

  // Get the old dependent code list.
  Handle<DependentCode> old_dependent_code =
      Handle<DependentCode>(GetDependentCode(object), isolate_);
  Handle<DependentCode> new_dependent_code =
      DependentCode::InsertCompilationDependencies(old_dependent_code, group,
                                                   object_wrapper_);

  // Set the new dependent code list if the head of the list changed.
  if (!new_dependent_code.is_identical_to(old_dependent_code)) {
    SetDependentCode(object, new_dependent_code);
  }
}

bool CompilationDependencies::Commit(Handle<Code> code) {
  // if (IsEmpty()) return;

  Handle<WeakCell> cell = Code::WeakCellFor(code);
  AllowDeferredHandleDereference get_wrapper;
  for (int i = 0; i < DependentCode::kGroupCount; i++) {
    ZoneVector<Handle<HeapObject>>* group_objects = groups_[i];
    if (group_objects == nullptr) continue;
    DependentCode::DependencyGroup group =
        static_cast<DependentCode::DependencyGroup>(i);
    for (size_t j = 0; j < group_objects->size(); j++) {
      DependentCode* dependent_code = GetDependentCode(group_objects->at(j));
      DCHECK(!object_wrapper_.is_null());
      dependent_code->UpdateToFinishedCode(group, *object_wrapper_, *cell);
    }
    groups_[i] = nullptr;  // Zone-allocated, no need to delete.
  }

  while (!dependencies_.empty()) {
    Dependency* dep = dependencies_.front();
    dependencies_.pop_front();
    if (!dep->Install(isolate_, cell)) return false;
  }

  return true;
}


void CompilationDependencies::Rollback() {
  if (IsEmpty()) return;

  AllowDeferredHandleDereference get_wrapper;
  // Unregister from all dependent maps if not yet committed.
  for (int i = 0; i < DependentCode::kGroupCount; i++) {
    ZoneVector<Handle<HeapObject>>* group_objects = groups_[i];
    if (group_objects == nullptr) continue;
    DependentCode::DependencyGroup group =
        static_cast<DependentCode::DependencyGroup>(i);
    for (size_t j = 0; j < group_objects->size(); j++) {
      DependentCode* dependent_code = GetDependentCode(group_objects->at(j));
      dependent_code->RemoveCompilationDependencies(group, *object_wrapper_);
    }
    groups_[i] = nullptr;  // Zone-allocated, no need to delete.
  }
}

void CompilationDependencies::DependOnProtector(Handle<PropertyCell> cell) {
  DCHECK_EQ(cell->value(), Smi::FromInt(Isolate::kProtectorValid));
  Insert(DependentCode::kPropertyCellChangedGroup, cell);
}

void CompilationDependencies::DependOnStablePrototypeChain(
    Handle<Map> map, MaybeHandle<JSReceiver> last_prototype) {
  for (PrototypeIterator i(map); !i.IsAtEnd(); i.Advance()) {
    Handle<JSReceiver> const current =
        PrototypeIterator::GetCurrent<JSReceiver>(i);
    DependOnStableMap(handle(current->map(), isolate_));
    Handle<JSReceiver> last;
    if (last_prototype.ToHandle(&last) && last.is_identical_to(current)) {
      break;
    }
  }
}

void CompilationDependencies::DependOnStablePrototypeChains(
    Handle<Context> native_context,
    std::vector<Handle<Map>> const& receiver_maps, Handle<JSObject> holder) {
  // Determine actual holder and perform prototype chain checks.
  for (auto map : receiver_maps) {
    // Perform the implicit ToObject for primitives here.
    // Implemented according to ES6 section 7.3.2 GetV (V, P).
    Handle<JSFunction> constructor;
    if (Map::GetConstructorFunction(map, native_context)
            .ToHandle(&constructor)) {
      map = handle(constructor->initial_map(), isolate_);
    }
    DependOnStablePrototypeChain(map, holder);
  }
}

void CompilationDependencies::DependOnElementsKind(
    Handle<AllocationSite> site) {
  // Do nothing if the object doesn't have any useful element transitions left.
  ElementsKind kind = site->PointsToLiteral()
                          ? site->boilerplate()->GetElementsKind()
                          : site->GetElementsKind();
  if (AllocationSite::ShouldTrack(kind)) {
    Insert(DependentCode::kAllocationSiteTransitionChangedGroup, site);
  }
}

void CompilationDependencies::DependOnElementsKinds(
    Handle<AllocationSite> site) {
  while (true) {
    DependOnElementsKind(site);
    if (!site->nested_site()->IsAllocationSite()) break;
    site = handle(AllocationSite::cast(site->nested_site()), isolate_);
  }
  CHECK_EQ(site->nested_site(), Smi::kZero);
}

}  // namespace internal
}  // namespace v8
