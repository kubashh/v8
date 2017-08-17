// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sstream>
#include <utility>

#include "src/api.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "src/v8.h"

#include "test/cctest/cctest.h"

using namespace v8::base;
using namespace v8::internal;

template <typename T>
static Handle<T> OpenHandle(v8::Local<v8::Value> value) {
  Handle<Object> obj = v8::Utils::OpenHandle(*value);
  return Handle<T>::cast(obj);
}

static inline v8::Local<v8::Value> Run(v8::Local<v8::Script> script) {
  v8::Local<v8::Value> result;
  if (script->Run(v8::Isolate::GetCurrent()->GetCurrentContext())
          .ToLocal(&result)) {
    return result;
  }
  return v8::Local<v8::Value>();
}

template <typename T = Object>
Handle<T> GetGlobal(const char* name) {
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Handle<String> str_name = factory->InternalizeUtf8String(name);

  Handle<Object> value =
      Object::GetProperty(isolate->global_object(), str_name).ToHandleChecked();
  return Handle<T>::cast(value);
}

template <typename T>
static inline Handle<T> Run(v8::Local<v8::Script> script) {
  return OpenHandle<T>(Run(script));
}

template <typename T>
static inline Handle<T> CompileRun(const char* script) {
  return OpenHandle<T>(CompileRun(script));
}

TEST(AddHashCodeToFastObjectWithoutProperties) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set();"
      " var x = {};";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(isolate->heap()->empty_fixed_array(), obj->raw_properties_or_hash());

  CompileRun("set.add(x)");
  CHECK(obj->HasFastProperties());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK(hash->IsSmi());
  CHECK_EQ(hash, obj->raw_properties_or_hash());
}

TEST(AddHashCodeToFastObjectWithInObjectProperties) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set();"
      " var x = { a: 1};";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(isolate->heap()->empty_fixed_array(), obj->raw_properties_or_hash());

  CompileRun("set.add(x)");
  CHECK(obj->HasFastProperties());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK(hash->IsSmi());
  CHECK_EQ(hash, obj->raw_properties_or_hash());
}

TEST(AddHashCodeToFastObjectWithPropertiesArray) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set(); "
      " var x = {}; "
      " x.a = 1; x.b = 2; x.c = 3; x.d = 4; x.e = 5; ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());

  CompileRun("set.add(x)");
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_array()->Hash());
}

TEST(AddHashCodeToSlowObject) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set(); "
      " var x = Object.create(null); ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(!obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsDictionary());

  CompileRun("set.add(x)");
  CHECK(!obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsDictionary());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_dictionary()->Hash());
}

TEST(TransitionFastWithInObjectToFastWithPropertyArray) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set();"
      " var x = { };"
      " x.a = 1; x.b = 2; x.c = 3; x.d = 4;";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(isolate->heap()->empty_fixed_array(), obj->raw_properties_or_hash());

  CompileRun("set.add(x)");
  CHECK(obj->HasFastProperties());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK(hash->IsSmi());
  CHECK_EQ(hash, obj->raw_properties_or_hash());

  CompileRun("x.e = 5;");
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());
  CHECK_EQ(hash, JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(Smi::ToInt(hash), obj->property_array()->Hash());
}

TEST(TransitionFastWithPropertyArray) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set();"
      " var x = { };"
      " x.a = 1; x.b = 2; x.c = 3; x.d = 4; x.e = 5; ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());

  CompileRun("set.add(x)");
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_array()->Hash());

  int length = obj->property_array()->length();
  CompileRun("x.f = 2; x.g = 5; x.h = 2");
  CHECK(obj->property_array()->length() > length);
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());
  CHECK_EQ(hash, JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(Smi::ToInt(hash), obj->property_array()->Hash());
}

TEST(TransitionFastWithPropertyArrayToSlowWithPropertyDictionary) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set();"
      " var x = { };"
      " x.a = 1; x.b = 2; x.c = 3; x.d = 4; x.e = 5; ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());

  CompileRun("set.add(x)");
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_array()->Hash());

  CompileRun("delete x.a;");
  CHECK(!obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsDictionary());
  CHECK_EQ(hash, JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(Smi::ToInt(hash), obj->property_dictionary()->Hash());
}

TEST(TransitionSlowWithPropertyDictionaryToSlowWithPropertyDictionary) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set(); "
      " var x = Object.create(null); ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(!obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsDictionary());

  CompileRun("set.add(x)");
  CHECK(!obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsDictionary());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_dictionary()->Hash());

  int length = obj->property_dictionary()->length();
  CompileRun("for(var i = 0; i < 10; i++) { x['f'+i] = i };");
  CHECK(!obj->HasFastProperties());
  CHECK(obj->property_dictionary()->length() > length);
  CHECK(obj->raw_properties_or_hash()->IsDictionary());
  CHECK_EQ(hash, JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(Smi::ToInt(hash), obj->property_dictionary()->Hash());
}

TEST(TransitionSlowWithPropertyDictionaryToFastWithoutProperties) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set(); "
      " var x = Object.create(null); ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(!obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsDictionary());

  CompileRun("set.add(x)");
  CHECK(!obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsDictionary());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_dictionary()->Hash());

  JSObject::MigrateSlowToFast(obj, 0, "Cctest/test-hashcode");
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsSmi());
  CHECK_EQ(hash, JSReceiver::GetIdentityHash(isolate, *obj));
}

TEST(TransitionSlowWithPropertyDictionaryToFastWithPropertyArray) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  const char* source =
      " var set = new Set(); "
      " var x = Object.create(null); "
      " for(var i = 0; i < 10; i++) { x['f'+i] = i }; ";
  CompileRun(source);

  Handle<JSObject> obj = GetGlobal<JSObject>("x");
  CHECK(!obj->HasFastProperties());
  CHECK_EQ(isolate->heap()->undefined_value(),
           JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK(obj->raw_properties_or_hash()->IsDictionary());

  CompileRun("set.add(x)");
  CHECK(!obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsDictionary());
  Object* hash = JSReceiver::GetIdentityHash(isolate, *obj);
  CHECK_NE(isolate->heap()->undefined_value(), hash);
  CHECK_EQ(Smi::ToInt(hash), obj->property_dictionary()->Hash());

  JSObject::MigrateSlowToFast(obj, 0, "Cctest/test-hashcode");
  CHECK(obj->HasFastProperties());
  CHECK(obj->raw_properties_or_hash()->IsPropertyArray());
  CHECK_EQ(hash, JSReceiver::GetIdentityHash(isolate, *obj));
  CHECK_EQ(Smi::ToInt(hash), obj->property_array()->Hash());
}
