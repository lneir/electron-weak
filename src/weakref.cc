/*
 * Copyright (c) 2011, Ben Noordhuis <info@bnoordhuis.nl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include "nan.h"
#include <delay_load_hook.h>

using namespace v8;
using namespace Nan;

namespace {


class proxy_container {
public:
  v8::Persistent<Object> target;
  v8::Persistent<Object> emitter;
  v8::Persistent<Object> proxy;

  // we need a reference to our weak object.
  // modifying nan to give us access to the persistent obj is the only way.
  // if we save another persistent reference and not make it weak, it simply won't ever be gc'ed
  //Nan::WeakCallbackInfo<Object> *cbinfo;
};


Nan::Persistent<ObjectTemplate> proxyClass;

Callback *globalCallback;


bool IsDead(Handle<Object> proxy) {
  assert(proxy->InternalFieldCount() == 1);
  proxy_container *cont = reinterpret_cast<proxy_container*>(
    Nan::GetInternalFieldPointer(proxy, 0)
  );
  return cont == NULL || cont->target.IsEmpty();
}


Handle<Object> Unwrap(Handle<Object> proxy) {
  assert(!IsDead(proxy));
  proxy_container *cont = reinterpret_cast<proxy_container*>(
    Nan::GetInternalFieldPointer(proxy, 0)
  );
  Local<Object> _target = Nan::New<Object>(cont->target);
  return _target;
}

Handle<Object> GetEmitter(Handle<Object> proxy) {
  proxy_container *cont = reinterpret_cast<proxy_container*>(
    Nan::GetInternalFieldPointer(proxy, 0)
  );
  assert(cont != NULL);
  Local<Object> _emitter = Nan::New<Object>(cont->emitter);
  return _emitter;
}




#define UNWRAP                            \
  Handle<Object> obj;                     \
  const bool dead = IsDead(info.This());  \
  if (!dead) obj = Unwrap(info.This());   \


NAN_PROPERTY_GETTER(WeakNamedPropertyGetter) {
  UNWRAP
  info.GetReturnValue().Set(dead ? Local<Value>() : obj->Get(property));
}


NAN_PROPERTY_SETTER(WeakNamedPropertySetter) {
  UNWRAP
  if (!dead) obj->Set(property, value);
  info.GetReturnValue().Set(value);
}


NAN_PROPERTY_QUERY(WeakNamedPropertyQuery) {
  info.GetReturnValue().Set(Nan::New<Integer>(None));
}


NAN_PROPERTY_DELETER(WeakNamedPropertyDeleter) {
  UNWRAP
  info.GetReturnValue().Set(Nan::New<Boolean>(!dead && obj->Delete(property)));
}


NAN_INDEX_GETTER(WeakIndexedPropertyGetter) {
  UNWRAP
  info.GetReturnValue().Set(dead ? Local<Value>() : obj->Get(index));
}


NAN_INDEX_SETTER(WeakIndexedPropertySetter) {
  UNWRAP
  if (!dead) obj->Set(index, value);
  info.GetReturnValue().Set(value);
}


NAN_INDEX_QUERY(WeakIndexedPropertyQuery) {
  info.GetReturnValue().Set(Nan::New<Integer>(None));
}


NAN_INDEX_DELETER(WeakIndexedPropertyDeleter) {
  UNWRAP
  info.GetReturnValue().Set(Nan::New<Boolean>(!dead && obj->Delete(index)));
}


/**
 * Only one "enumerator" function needs to be defined. This function is used for
 * both the property and indexed enumerator functions.
 */

NAN_PROPERTY_ENUMERATOR(WeakPropertyEnumerator) {
  UNWRAP
  info.GetReturnValue().Set(dead ? Nan::New<Array>(0) : obj->GetPropertyNames());
}

/**
 * Weakref callback function. Invokes the "global" callback function,
 * which emits the _CB event on the per-object EventEmitter.
 */

void TargetCallback(const v8::WeakCallbackData<Object, proxy_container>& data) {
  Nan::HandleScope scope;

  proxy_container *cont = data.GetParameter();

  // invoke global callback function
  Local<Value> argv[] = {
    Nan::New<Object>(cont->target),
    Nan::New<Object>(cont->emitter)
  };
  // Invoke callback directly, not via NanCallback->Call() which uses
  // node::MakeCallback() which calls into process._tickCallback()
  // too. Those other callbacks are not safe to run from here.
  v8::Local<v8::Function> globalCallbackDirect = globalCallback->GetFunction();
  globalCallbackDirect->Call(Nan::GetCurrentContext()->Global(), 2, argv);

  // clean everything up
  Local<Object> proxy = Nan::New<Object>(cont->proxy);
  Nan::SetInternalFieldPointer(proxy, 0, NULL);
  cont->proxy.Reset();
  cont->emitter.Reset();
  cont->target.Reset();
  delete cont;
}

/**
 * `_create(obj, emitter)` JS function.
 */

NAN_METHOD(Create) {
  if (!info[0]->IsObject()) return Nan::ThrowTypeError("Object expected");

  proxy_container *cont = new proxy_container();
  Local<Object> proxy = Nan::New<ObjectTemplate>(proxyClass)->NewInstance();
  Nan::SetInternalFieldPointer(proxy, 0, cont);

  cont->target.Reset(v8::Isolate::GetCurrent(), info[0].As<Object>());
  cont->emitter.Reset(v8::Isolate::GetCurrent(), info[1].As<Object>());
  cont->proxy.Reset(v8::Isolate::GetCurrent(), proxy);

  cont->target.SetWeak(cont, TargetCallback);

  info.GetReturnValue().Set(proxy);
}

/**
 * TODO: Make this better.
 */

bool isWeakRef (Handle<Value> val) {
  return val->IsObject() && val.As<Object>()->InternalFieldCount() == 1;
}

/**
 * `isWeakRef()` JS function.
 */

NAN_METHOD(IsWeakRef) {
  info.GetReturnValue().Set(Nan::New<Boolean>(isWeakRef(info[0])));
}

#define WEAKREF_FIRST_ARG                                    \
  if (!isWeakRef(info[0])) {                                 \
    return Nan::ThrowTypeError("Weakref instance expected");   \
  }                                                          \
  Local<Object> proxy = info[0].As<Object>();

/**
 * `get(weakref)` JS function.
 */

NAN_METHOD(Get) {
  WEAKREF_FIRST_ARG
  if (IsDead(proxy)) info.GetReturnValue().Set(Nan::Undefined());
  else info.GetReturnValue().Set(Unwrap(proxy));
}

/**
 * `isNearDeath(weakref)` JS function.
 */

NAN_METHOD(IsNearDeath) {
  WEAKREF_FIRST_ARG

  proxy_container *cont = reinterpret_cast<proxy_container*>(
    Nan::GetInternalFieldPointer(proxy, 0)
  );
  assert(cont != NULL);

  Handle<Boolean> rtn = Nan::New<Boolean>(cont->target.IsNearDeath());

  info.GetReturnValue().Set(rtn);
}

/**
 * `isDead(weakref)` JS function.
 */

NAN_METHOD(IsDead) {
  WEAKREF_FIRST_ARG
  info.GetReturnValue().Set(Nan::New<Boolean>(IsDead(proxy)));
}

/**
 * `_getEmitter(weakref)` JS function.
 */

NAN_METHOD(GetEmitter) {
  WEAKREF_FIRST_ARG
  info.GetReturnValue().Set(GetEmitter(proxy));
}

/**
 * Sets the global weak callback function.
 */

NAN_METHOD(SetCallback) {
  Local<Function> callbackHandle = info[0].As<Function>();
  globalCallback = new Nan::Callback(callbackHandle);
  info.GetReturnValue().Set(Nan::Undefined());
}

/**
 * Init function.
 */

NAN_MODULE_INIT(Initialize) {
  Handle<ObjectTemplate> tpl = Nan::New<ObjectTemplate>();
  Nan::SetNamedPropertyHandler(
    tpl,
    WeakNamedPropertyGetter,
    WeakNamedPropertySetter,
    WeakNamedPropertyQuery,
    WeakNamedPropertyDeleter,
    WeakPropertyEnumerator);

  Nan::SetIndexedPropertyHandler(
    tpl,
    WeakIndexedPropertyGetter,
    WeakIndexedPropertySetter,
    WeakIndexedPropertyQuery,
    WeakIndexedPropertyDeleter,
    WeakPropertyEnumerator);

  tpl->SetInternalFieldCount(1);
  Nan::Persistent<ObjectTemplate> proxy(tpl);
  proxyClass.Reset(tpl);

  Nan::Set(target, New("get").ToLocalChecked(), New<FunctionTemplate>(Get)->GetFunction());
  Nan::Set(target, New("isWeakRef").ToLocalChecked(), New<FunctionTemplate>(IsWeakRef)->GetFunction());
  Nan::Set(target, New("isNearDeath").ToLocalChecked(), New<FunctionTemplate>(IsNearDeath)->GetFunction());
  Nan::Set(target, New("isDead").ToLocalChecked(), New<FunctionTemplate>(IsDead)->GetFunction());
  Nan::Set(target, New("_create").ToLocalChecked(), New<FunctionTemplate>(Create)->GetFunction());
  Nan::Set(target, New("_getEmitter").ToLocalChecked(), New<FunctionTemplate>(GetEmitter)->GetFunction());
  Nan::Set(target, New("_setCallback").ToLocalChecked(), New<FunctionTemplate>(SetCallback)->GetFunction());
}

} // anonymous namespace

NODE_MODULE(weakref, Initialize);
