// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "include/v8-container.h"
#include "include/v8-external.h"
#include "include/v8-function.h"
#include "test/cctest/cctest.h"

namespace {

int32_t g_cross_context_int = 0;

bool g_expect_interceptor_call = false;

void NamedGetter(v8::Local<v8::Name> property,
                 const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (property->Equals(context, v8_str("cross_context_int")).FromJust())
    info.GetReturnValue().Set(g_cross_context_int);
}

void NamedSetter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                 const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!property->Equals(context, v8_str("cross_context_int")).FromJust())
    return;
  if (value->IsInt32()) {
    g_cross_context_int = value->ToInt32(context).ToLocalChecked()->Value();
  }
  info.GetReturnValue().Set(value);
}

void NamedQuery(v8::Local<v8::Name> property,
                const v8::PropertyCallbackInfo<v8::Integer>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!property->Equals(context, v8_str("cross_context_int")).FromJust())
    return;
  info.GetReturnValue().Set(v8::DontDelete);
}

void NamedDeleter(v8::Local<v8::Name> property,
                  const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!property->Equals(context, v8_str("cross_context_int")).FromJust())
    return;
  info.GetReturnValue().Set(false);
}

void NamedEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> names = v8::Array::New(isolate, 1);
  names->Set(context, 0, v8_str("cross_context_int")).FromJust();
  info.GetReturnValue().Set(names);
}

void IndexedGetter(uint32_t index,
                   const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(g_expect_interceptor_call);
  if (index == 7) info.GetReturnValue().Set(g_cross_context_int);
}

void IndexedSetter(uint32_t index, v8::Local<v8::Value> value,
                   const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (index != 7) return;
  if (value->IsInt32()) {
    g_cross_context_int = value->ToInt32(context).ToLocalChecked()->Value();
  }
  info.GetReturnValue().Set(value);
}

void IndexedQuery(uint32_t index,
                  const v8::PropertyCallbackInfo<v8::Integer>& info) {
  CHECK(g_expect_interceptor_call);
  if (index == 7) info.GetReturnValue().Set(v8::DontDelete);
}

void IndexedDeleter(uint32_t index,
                    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  CHECK(g_expect_interceptor_call);
  if (index == 7) info.GetReturnValue().Set(false);
}

void IndexedEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info) {
  CHECK(g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> names = v8::Array::New(isolate, 1);
  names->Set(context, 0, v8_str("7")).FromJust();
  info.GetReturnValue().Set(names);
}

void MethodGetter(v8::Local<v8::Name> property,
                  const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  v8::Local<v8::External> data = info.Data().As<v8::External>();
  v8::Local<v8::FunctionTemplate>& function_template =
      *reinterpret_cast<v8::Local<v8::FunctionTemplate>*>(data->Value());

  info.GetReturnValue().Set(
      function_template->GetFunction(context).ToLocalChecked());
}

void MethodCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(8);
}

void NamedGetterThrowsException(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetIsolate()->ThrowException(v8_str("exception"));
}

void NamedSetterThrowsException(
    v8::Local<v8::Name> property, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetIsolate()->ThrowException(v8_str("exception"));
}

void IndexedGetterThrowsException(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetIsolate()->ThrowException(v8_str("exception"));
}

void IndexedSetterThrowsException(
    uint32_t index, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetIsolate()->ThrowException(v8_str("exception"));
}

bool AccessCheck(v8::Local<v8::Context> accessing_context,
                 v8::Local<v8::Object> accessed_object,
                 v8::Local<v8::Value> data) {
  return false;
}

void GetCrossContextInt(v8::Local<v8::String> property,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(!g_expect_interceptor_call);
  info.GetReturnValue().Set(g_cross_context_int);
}

void SetCrossContextInt(v8::Local<v8::String> property,
                        v8::Local<v8::Value> value,
                        const v8::PropertyCallbackInfo<void>& info) {
  CHECK(!g_expect_interceptor_call);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (value->IsInt32()) {
    g_cross_context_int = value->ToInt32(context).ToLocalChecked()->Value();
  }
}

void Return42(v8::Local<v8::String> property,
              const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(42);
}

void CheckCanRunScriptInContext(v8::Isolate* isolate,
                                v8::Local<v8::Context> context) {
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);

  g_expect_interceptor_call = false;
  g_cross_context_int = 0;

  // Running script in this context should work.
  CompileRunChecked(isolate, "this.foo = 42; this[23] = true;");
  ExpectInt32("this.all_can_read", 42);
  CompileRunChecked(isolate, "this.cross_context_int = 23");
  CHECK_EQ(g_cross_context_int, 23);
  ExpectInt32("this.cross_context_int", 23);
}

void CheckCrossContextAccess(v8::Isolate* isolate,
                             v8::Local<v8::Context> accessing_context,
                             v8::Local<v8::Object> accessed_object) {
  v8::HandleScope handle_scope(isolate);
  accessing_context->Global()
      ->Set(accessing_context, v8_str("other"), accessed_object)
      .FromJust();
  v8::Context::Scope context_scope(accessing_context);

  g_expect_interceptor_call = true;
  g_cross_context_int = 23;

  {
    v8::TryCatch try_catch(isolate);
    CHECK(CompileRun(accessing_context, "this.other.foo").IsEmpty());
  }
  {
    v8::TryCatch try_catch(isolate);
    CHECK(CompileRun(accessing_context, "this.other[23]").IsEmpty());
  }

  // AllCanRead properties are also inaccessible.
  {
    v8::TryCatch try_catch(isolate);
    CHECK(CompileRun(accessing_context, "this.other.all_can_read").IsEmpty());
  }

  // Intercepted properties are accessible, however.
  ExpectInt32("this.other.cross_context_int", 23);
  CompileRunChecked(isolate, "this.other.cross_context_int = 42");
  ExpectInt32("this.other[7]", 42);
  ExpectString("JSON.stringify(Object.getOwnPropertyNames(this.other))",
               "[\"7\",\"cross_context_int\"]");
}

void CheckCrossContextAccessWithException(
    v8::Isolate* isolate, v8::Local<v8::Context> accessing_context,
    v8::Local<v8::Object> accessed_object) {
  v8::HandleScope handle_scope(isolate);
  accessing_context->Global()
      ->Set(accessing_context, v8_str("other"), accessed_object)
      .FromJust();
  v8::Context::Scope context_scope(accessing_context);

  {
    v8::TryCatch try_catch(isolate);
    CompileRun("this.other.should_throw");
    CHECK(try_catch.HasCaught());
    CHECK(try_catch.Exception()->IsString());
    CHECK(v8_str("exception")
              ->Equals(accessing_context, try_catch.Exception())
              .FromJust());
  }

  {
    v8::TryCatch try_catch(isolate);
    CompileRun("this.other.should_throw = 8");
    CHECK(try_catch.HasCaught());
    CHECK(try_catch.Exception()->IsString());
    CHECK(v8_str("exception")
              ->Equals(accessing_context, try_catch.Exception())
              .FromJust());
  }

  {
    v8::TryCatch try_catch(isolate);
    CompileRun("this.other[42]");
    CHECK(try_catch.HasCaught());
    CHECK(try_catch.Exception()->IsString());
    CHECK(v8_str("exception")
              ->Equals(accessing_context, try_catch.Exception())
              .FromJust());
  }

  {
    v8::TryCatch try_catch(isolate);
    CompileRun("this.other[42] = 8");
    CHECK(try_catch.HasCaught());
    CHECK(try_catch.Exception()->IsString());
    CHECK(v8_str("exception")
              ->Equals(accessing_context, try_catch.Exception())
              .FromJust());
  }
}

void Ctor(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(info.IsConstructCall());
}

}  // namespace

TEST(AccessCheckWithInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->SetAccessCheckCallbackAndHandler(
      AccessCheck,
      v8::NamedPropertyHandlerConfiguration(
          NamedGetter, NamedSetter, NamedQuery, NamedDeleter, NamedEnumerator),
      v8::IndexedPropertyHandlerConfiguration(IndexedGetter, IndexedSetter,
                                              IndexedQuery, IndexedDeleter,
                                              IndexedEnumerator));
  global_template->SetNativeDataProperty(
      v8_str("cross_context_int"), GetCrossContextInt, SetCrossContextInt);
  global_template->SetNativeDataProperty(v8_str("all_can_read"), Return42,
                                         nullptr, v8::Local<v8::Value>(),
                                         v8::None, v8::ALL_CAN_READ);

  v8::Local<v8::Context> context0 =
      v8::Context::New(isolate, nullptr, global_template);
  CheckCanRunScriptInContext(isolate, context0);

  // Create another context.
  v8::Local<v8::Context> context1 =
      v8::Context::New(isolate, nullptr, global_template);
  CheckCrossContextAccess(isolate, context1, context0->Global());
}

TEST(CallFunctionWithRemoteContextReceiver) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> global_template =
      v8::FunctionTemplate::New(isolate);

  v8::Local<v8::Signature> signature =
      v8::Signature::New(isolate, global_template);
  v8::Local<v8::FunctionTemplate> function_template = v8::FunctionTemplate::New(
      isolate, MethodCallback, v8::External::New(isolate, &function_template),
      signature);

  global_template->InstanceTemplate()->SetAccessCheckCallbackAndHandler(
      AccessCheck, v8::NamedPropertyHandlerConfiguration(
                       MethodGetter, nullptr, nullptr, nullptr, nullptr,
                       v8::External::New(isolate, &function_template)),
      v8::IndexedPropertyHandlerConfiguration());

  v8::Local<v8::Object> accessed_object =
      v8::Context::NewRemoteContext(isolate,
                                    global_template->InstanceTemplate())
          .ToLocalChecked();
  v8::Local<v8::Context> accessing_context =
      v8::Context::New(isolate, nullptr, global_template->InstanceTemplate());

  v8::HandleScope handle_scope(isolate);
  accessing_context->Global()
      ->Set(accessing_context, v8_str("other"), accessed_object)
      .FromJust();
  v8::Context::Scope context_scope(accessing_context);

  {
    v8::TryCatch try_catch(isolate);
    ExpectInt32("this.other.method()", 8);
    CHECK(!try_catch.HasCaught());
  }
}

TEST(AccessCheckWithExceptionThrowingInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  isolate->SetFailedAccessCheckCallbackFunction([](v8::Local<v8::Object> target,
                                                   v8::AccessType type,
                                                   v8::Local<v8::Value> data) {
    UNREACHABLE();  // This should never be called.
  });

  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->SetAccessCheckCallbackAndHandler(
      AccessCheck, v8::NamedPropertyHandlerConfiguration(
                       NamedGetterThrowsException, NamedSetterThrowsException),
      v8::IndexedPropertyHandlerConfiguration(IndexedGetterThrowsException,
                                              IndexedSetterThrowsException));

  // Create two contexts.
  v8::Local<v8::Context> context0 =
      v8::Context::New(isolate, nullptr, global_template);
  v8::Local<v8::Context> context1 =
      v8::Context::New(isolate, nullptr, global_template);

  CheckCrossContextAccessWithException(isolate, context1, context0->Global());
}

TEST(NewRemoteContext) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->SetAccessCheckCallbackAndHandler(
      AccessCheck,
      v8::NamedPropertyHandlerConfiguration(
          NamedGetter, NamedSetter, NamedQuery, NamedDeleter, NamedEnumerator),
      v8::IndexedPropertyHandlerConfiguration(IndexedGetter, IndexedSetter,
                                              IndexedQuery, IndexedDeleter,
                                              IndexedEnumerator));
  global_template->SetNativeDataProperty(
      v8_str("cross_context_int"), GetCrossContextInt, SetCrossContextInt);
  global_template->SetNativeDataProperty(v8_str("all_can_read"), Return42,
                                         nullptr, v8::Local<v8::Value>(),
                                         v8::None, v8::ALL_CAN_READ);

  v8::Local<v8::Object> global0 =
      v8::Context::NewRemoteContext(isolate, global_template).ToLocalChecked();

  // Create a real context.
  {
    v8::HandleScope other_scope(isolate);
    v8::Local<v8::Context> context1 =
        v8::Context::New(isolate, nullptr, global_template);

    CheckCrossContextAccess(isolate, context1, global0);
  }

  // Create a context using the detached global.
  {
    v8::HandleScope other_scope(isolate);
    v8::Local<v8::Context> context2 =
        v8::Context::New(isolate, nullptr, global_template, global0);

    CheckCanRunScriptInContext(isolate, context2);
  }

  // Turn a regular context into a remote context.
  {
    v8::HandleScope other_scope(isolate);
    v8::Local<v8::Context> context3 =
        v8::Context::New(isolate, nullptr, global_template);

    CheckCanRunScriptInContext(isolate, context3);

    // Turn the global object into a remote context, and try to access it.
    v8::Local<v8::Object> context3_global = context3->Global();
    context3->DetachGlobal();
    v8::Local<v8::Object> global3 =
        v8::Context::NewRemoteContext(isolate, global_template, context3_global)
            .ToLocalChecked();
    v8::Local<v8::Context> context4 =
        v8::Context::New(isolate, nullptr, global_template);

    CheckCrossContextAccess(isolate, context4, global3);

    // Turn it back into a regular context.
    v8::Local<v8::Context> context5 =
        v8::Context::New(isolate, nullptr, global_template, global3);

    CheckCanRunScriptInContext(isolate, context5);
  }
}

TEST(NewRemoteInstance) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> tmpl =
      v8::FunctionTemplate::New(isolate, Ctor);
  v8::Local<v8::ObjectTemplate> instance = tmpl->InstanceTemplate();
  instance->SetAccessCheckCallbackAndHandler(
      AccessCheck,
      v8::NamedPropertyHandlerConfiguration(
          NamedGetter, NamedSetter, NamedQuery, NamedDeleter, NamedEnumerator),
      v8::IndexedPropertyHandlerConfiguration(IndexedGetter, IndexedSetter,
                                              IndexedQuery, IndexedDeleter,
                                              IndexedEnumerator));
  tmpl->SetNativeDataProperty(v8_str("all_can_read"), Return42, nullptr,
                              v8::Local<v8::Value>(), v8::None,
                              v8::ALL_CAN_READ);

  v8::Local<v8::Object> obj = tmpl->NewRemoteInstance().ToLocalChecked();

  v8::Local<v8::Context> context = v8::Context::New(isolate);
  CheckCrossContextAccess(isolate, context, obj);
}

namespace {
bool private_field_failed_access_check_called = false;
bool failed_access_check_should_throw = false;
const char* failed_access_check_message = "failed access check callback";
void PrivateFieldAccessCheckCallback(v8::Local<v8::Object> target,
                                     v8::AccessType type,
                                     v8::Local<v8::Value> data) {
  private_field_failed_access_check_called = true;
  if (failed_access_check_should_throw) {
    v8::Isolate::GetCurrent()->ThrowException(
        v8_str(failed_access_check_message));
  }
}

bool access_check_should_pass = false;
bool private_field_access_check_called = false;
bool PrivateFieldAccessCallback(v8::Local<v8::Context> accessing_context,
                                v8::Local<v8::Object> accessed_object,
                                v8::Local<v8::Value> data) {
  private_field_access_check_called = true;
  return access_check_should_pass;
}

const char* classes = R"(
class A {
  constructor(arg) {
    return arg;
  }
}

class B extends A {
  #b = 1;  // ACCESS_CHECK -> DATA
  constructor(arg) {
    super(arg);
  }
  static setField(obj) {
    obj.#b = 'b';  // KeyedStoreIC
  }
  static getField(obj) {
    return obj.#b;
  }
  static hasField(obj) {
    return #b in obj;
  }
}

class C extends A {
  #c;  // DefineKeyedOwnIC: ACCESS_CHECK -> NOT_FOUND
  constructor(arg) {
    super(arg);
  }
  static setField(obj) {
    obj.#c = 'c';  // KeyedStoreIC
  }
  static getField(obj) {
    return obj.#c;
  }
  static hasField(obj) {
    return #c in obj;
  }
}

let d = 0;
class D extends A {
  get #d() { return d; }
  set #d(val) { d = val;}
  constructor(arg) {
    super(arg);  // KeyedStoreIC for private brand
  }
  static setAccessor(obj) {
    obj.#d = 'd';  // KeyedLoadIC for private brand
  }
  static getAccessor(obj) {
    return obj.#d;  // KeyedLoadIC for private brand
  }
}

class E extends A {
  #e() { return 0; }
  constructor(arg) {
    super(arg);  // KeyedStoreIC for private brand
  }
  static setMethod(obj) {
    obj.#e = 'e';  // KeyedLoadIC for private brand
  }
  static getMethod(obj) {
    return obj.#e;  // KeyedLoadIC for private brand
  }
}
)";
}  // namespace

TEST(AccessCheckWithPrivateField) {
  i::FLAG_allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  isolate->SetFailedAccessCheckCallbackFunction(
      PrivateFieldAccessCheckCallback);

  v8::HandleScope scope(isolate);

  v8::Local<v8::ObjectTemplate> templ = v8::ObjectTemplate::New(isolate);
  templ->SetAccessCheckCallbackAndHandler(
      PrivateFieldAccessCallback,
      // If any of these are called with a private name a DCHECK should fail.
      v8::NamedPropertyHandlerConfiguration(
          NamedGetter, NamedSetter, NamedQuery, NamedDeleter, NamedEnumerator),
      v8::IndexedPropertyHandlerConfiguration(IndexedGetter, IndexedSetter,
                                              IndexedQuery, IndexedDeleter,
                                              IndexedEnumerator));

  v8::Local<v8::Context> context0 = v8::Context::New(isolate, nullptr, templ);

  {
    v8::Context::Scope context_scope(context0);

    CompileRun(classes);

    auto throws = [&](const char* code, const char* expected = nullptr) {
      private_field_access_check_called = false;
      private_field_failed_access_check_called = false;
      v8::TryCatch try_catch(isolate);
      printf("[THROWS] '%s' %s, %s\n", code,
             access_check_should_pass ? "has access" : "doesn't have access",
             failed_access_check_should_throw ? "callback throws"
                                              : "callback doesn't throw");
      CompileRun(code);
      CHECK(private_field_access_check_called);
      CHECK(try_catch.HasCaught());
      if (expected != nullptr) {
        v8::Local<v8::String> message =
            v8::Exception::CreateMessage(isolate, try_catch.Exception())->Get();
        v8::String::Utf8Value utf8(isolate, message);
        CHECK_EQ(std::string(expected), std::string(*utf8));
      }
      CHECK_EQ(!access_check_should_pass,
               private_field_failed_access_check_called);
    };

    auto pass = [&](const char* code,
                    v8::Local<v8::Value> expected = v8::Local<v8::Value>()) {
      private_field_access_check_called = false;
      private_field_failed_access_check_called = false;
      v8::TryCatch try_catch(isolate);
      printf("[PASS] '%s' %s, %s\n", code,
             access_check_should_pass ? "has access" : "doesn't have access",
             failed_access_check_should_throw ? "callback throws"
                                              : "callback doesn't throw");
      v8::Local<v8::Value> value = CompileRun(code);
      CHECK(private_field_access_check_called);
      CHECK(!try_catch.HasCaught());
      if (!expected.IsEmpty()) {
        if (expected->IsString()) {
          CHECK(value->IsString());
          v8::String::Utf8Value expected_utf8(isolate,
                                              expected.As<v8::String>());
          v8::String::Utf8Value actual_utf8(isolate, value.As<v8::String>());
          CHECK_EQ(std::string(*expected_utf8), std::string(*actual_utf8));
        } else if (expected->IsInt32()) {
          CHECK(value->IsInt32());
          CHECK_EQ(expected.As<v8::Int32>()->Value(),
                   value.As<v8::Int32>()->Value());
        } else {
          CHECK(value->StrictEquals(expected));
        }
      }
      CHECK_EQ(!access_check_should_pass,
               private_field_failed_access_check_called);
      return value;
    };

    std::string failed_message =
        std::string("Uncaught ") + failed_access_check_message;
    const char* failed_message_str = failed_message.c_str();

    {
      v8::Local<v8::Context> context1 =
          v8::Context::New(isolate, nullptr, templ);
      context0->Global()
          ->Set(context0, v8_str("global1"), context1->Global())
          .FromJust();
      access_check_should_pass = false;
      failed_access_check_should_throw = true;
      throws("new B(global1)", failed_message_str);
      throws("new C(global1)", failed_message_str);
      throws("new D(global1)", failed_message_str);
      throws("new E(global1)", failed_message_str);
      throws("B.setField(global1)", failed_message_str);
      throws("C.setField(global1)", failed_message_str);
      throws("B.hasField(global1)", failed_message_str);
      throws("C.hasField(global1)", failed_message_str);
      throws("B.getField(global1)", failed_message_str);
      throws("C.getField(global1)", failed_message_str);
      throws("D.setAccessor(global1)", failed_message_str);
      throws("E.setMethod(global1)", failed_message_str);
      throws("D.getAccessor(global1)", failed_message_str);
      throws("E.getMethod(global1)", failed_message_str);
    }

    {
      v8::Local<v8::Context> context2 =
          v8::Context::New(isolate, nullptr, templ);
      context0->Global()
          ->Set(context0, v8_str("global2"), context2->Global())
          .FromJust();
      access_check_should_pass = false;
      failed_access_check_should_throw = false;
      // The failed access callback is supposed to throw.
      // If it doesn't, behaviors are undefined. The tests
      // here just document the current behavior and make sure
      // that it doesn't crash.
      pass("new B(global2)");
      pass("new C(global2)");
      pass("new D(global2)");
      pass("new E(global2)");
      pass("B.setField(global2)");
      pass("C.setField(global2)");
      pass("B.hasField(global2)", v8::False(isolate));
      pass("C.hasField(global2)", v8::False(isolate));
      pass("B.getField(global2)", v8::Undefined(isolate));
      pass("C.getField(global2)", v8::Undefined(isolate));
      pass("D.setAccessor(global2)");
      throws("E.setMethod(global2)");
      pass("D.getAccessor(global2)", v8_str("d"));
      pass("E.getMethod(global2)()", v8_int(0));
    }

    {
      v8::Local<v8::Context> context3 =
          v8::Context::New(isolate, nullptr, templ);
      context0->Global()
          ->Set(context0, v8_str("global3"), context3->Global())
          .FromJust();
      access_check_should_pass = true;
      failed_access_check_should_throw = true;

      throws("B.setField(global3)");
      throws("C.setField(global3)");
      throws("B.getField(global3)");
      throws("C.getField(global3)");

      pass("B.hasField(global3)", v8::False(isolate));
      pass("C.hasField(global3)", v8::False(isolate));
      throws("D.setAccessor(global3)");
      throws("E.setMethod(global3)");
      throws("D.getAccessor(global3)");
      throws("E.getMethod(global3)");

      pass("new B(global3)");
      pass("new C(global3)");
      pass("new D(global3)");
      pass("new E(global3)");

      pass("B.getField(global3)", v8_int(1));
      pass("B.setField(global3)");
      pass("B.getField(global3)", v8_str("b"));
      pass("B.getField(global3)", v8_str("b"));  // fast case
      pass("B.hasField(global3)", v8::True(isolate));
      pass("B.hasField(global3)", v8::True(isolate));  // fast case
      throws("new B(global3)");

      pass("C.getField(global3)", v8::Undefined(isolate));
      pass("C.setField(global3)");
      pass("C.getField(global3)", v8_str("c"));
      pass("C.getField(global3)", v8_str("c"));  // fast case
      pass("C.hasField(global3)", v8::True(isolate));
      pass("C.hasField(global3)", v8::True(isolate));  // fast case
      throws("new C(global3)");

      CompileRun("d = 0;");
      pass("D.getAccessor(global3)", v8_int(0));
      pass("D.setAccessor(global3)");
      pass("D.getAccessor(global3)", v8_str("d"));
      pass("D.getAccessor(global3)", v8_str("d"));  // fast case
      throws("new D(global3)");

      pass("E.getMethod(global3)()", v8_int(0));
      throws("E.setMethod(global3)");
      pass("E.getMethod(global3)()", v8_int(0));  // fast case
      throws("new E(global3)");

      access_check_should_pass = false;
      throws("new B(global3)", failed_message_str);
      throws("new C(global3)", failed_message_str);
      throws("new D(global3)", failed_message_str);
      throws("new E(global3)", failed_message_str);
      throws("B.setField(global3)", failed_message_str);
      throws("C.setField(global3)", failed_message_str);
      throws("B.getField(global3)", failed_message_str);
      throws("C.getField(global3)", failed_message_str);
      throws("B.hasField(global3)", failed_message_str);
      throws("C.hasField(global3)", failed_message_str);
      throws("D.setAccessor(global3)", failed_message_str);
      throws("E.setMethod(global3)", failed_message_str);
      throws("D.getAccessor(global3)", failed_message_str);
      throws("E.getMethod(global3)", failed_message_str);
    }

    {
      v8::Local<v8::Context> context4 =
          v8::Context::New(isolate, nullptr, templ);
      context0->Global()
          ->Set(context0, v8_str("global4"), context4->Global())
          .FromJust();
      access_check_should_pass = true;
      failed_access_check_should_throw = false;

      throws("B.setField(global4)");
      throws("C.setField(global4)");
      pass("B.hasField(global4)", v8::False(isolate));
      pass("C.hasField(global4)", v8::False(isolate));
      throws("B.getField(global4)");
      throws("C.getField(global4)");
      throws("D.setAccessor(global4)");
      throws("E.setMethod(global4)");
      throws("D.getAccessor(global4)");
      throws("E.getMethod(global4)");

      pass("new B(global4)");
      pass("new C(global4)");
      pass("new D(global4)");
      pass("new E(global4)");

      pass("B.getField(global4)", v8_int(1));
      pass("B.setField(global4)");
      pass("B.getField(global4)", v8_str("b"));
      pass("B.getField(global4)", v8_str("b"));  // fast case
      pass("B.hasField(global4)", v8::True(isolate));
      pass("B.hasField(global4)", v8::True(isolate));  // fast case
      throws("new B(global4)");

      pass("C.getField(global4)", v8::Undefined(isolate));
      pass("C.setField(global4)");
      pass("C.getField(global4)", v8_str("c"));
      pass("C.getField(global4)", v8_str("c"));  // fast case
      pass("C.hasField(global4)", v8::True(isolate));
      pass("C.hasField(global4)", v8::True(isolate));  // fast case
      throws("new C(global4)");

      CompileRun("d = 0;");
      pass("D.getAccessor(global4)", v8_int(0));
      pass("D.setAccessor(global4)");
      pass("D.getAccessor(global4)", v8_str("d"));
      pass("D.getAccessor(global4)", v8_str("d"));  // fast case
      throws("new D(global4)");

      pass("E.getMethod(global4)()", v8_int(0));
      throws("E.setMethod(global4)");
      pass("E.getMethod(global4)()", v8_int(0));  // fast case
      throws("new E(global4)");

      access_check_should_pass = false;
      // The failed access callback is supposed to throw.
      // If it doesn't, behaviors are undefined. The tests
      // here just document the current behavior and make
      // sure that it doesn't crash.
      pass("new B(global4)");
      pass("new C(global4)");
      pass("new D(global4)");
      pass("new E(global4)");
      pass("B.setField(global4)");
      pass("C.setField(global4)");
      pass("B.getField(global4)", v8::Undefined(isolate));
      pass("C.getField(global4)", v8::Undefined(isolate));
      pass("B.hasField(global4)", v8::False(isolate));
      pass("C.hasField(global4)", v8::False(isolate));
      pass("D.setAccessor(global4)");
      throws("E.setMethod(global4)");
      pass("D.getAccessor(global2)", v8_str("d"));
      pass("E.getMethod(global2)()", v8_int(0));
    }
  }
}
