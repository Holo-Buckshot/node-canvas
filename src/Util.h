#pragma once

#include <nan.h>
#include <string>
#include <v8.h>

#include "InstanceData.h"

namespace canvas_detail {

inline InstanceData::AccessorData* AccessorDataFrom(v8::Local<v8::Value> data) {
  return static_cast<InstanceData::AccessorData*>(data.As<v8::External>()->Value());
}

inline bool CheckReceiver(
	InstanceData::AccessorData* d,
	v8::Local<v8::Object> receiver,
	v8::Local<v8::String> property
	) {
  if (Nan::New(d->receiverTpl)->HasInstance(receiver)) return true;
  // Same wording V8 used for AccessorSignature failures.
  Nan::Utf8String name(property);
  std::string msg = std::string("Method ") + (*name ? *name : "") +
    " called on incompatible receiver";
  Nan::ThrowTypeError(msg.c_str());
  return false;
}

inline NAN_GETTER(GuardedGetter) {
  InstanceData::AccessorData* d = AccessorDataFrom(info.Data());
  if (CheckReceiver(d, info.This(), property)) d->getter(property, info);
}

inline NAN_SETTER(GuardedSetter) {
  InstanceData::AccessorData* d = AccessorDataFrom(info.Data());
  if (CheckReceiver(d, info.This(), property)) d->setter(property, value, info);
}

} // namespace canvas_detail

// Defines a getter/setter on a class prototype. Getters/setters must only run
// on actual instances, i.e. MyClass.prototype.getter1 must not try to unwrap
// the non-existent 'this'. See #803, #847, #885, nodejs/node#15099, ...
//
// This used to rely on v8::AccessorSignature, which V8 removed (Node.js 20+),
// so the receiver is checked explicitly against the class template. The
// callback data is owned by the current environment's InstanceData so it is
// released when that environment (e.g. a worker thread) shuts down.
inline void SetProtoAccessor(
	v8::Local<v8::ObjectTemplate> tpl,
	v8::Local<v8::String> name,
	Nan::GetterCallback getter,
	Nan::SetterCallback setter,
	v8::Local<v8::FunctionTemplate> ctor
	) {
	InstanceData* instance = InstanceData::Get();
	auto data = std::unique_ptr<InstanceData::AccessorData>(new InstanceData::AccessorData());
	data->getter = getter;
	data->setter = setter;
	data->receiverTpl.Reset(ctor);
	v8::Local<v8::Value> external = Nan::New<v8::External>(data.get());
	instance->accessors.push_back(std::move(data));

	Nan::SetAccessor(
		tpl,
		name,
		canvas_detail::GuardedGetter,
		setter ? canvas_detail::GuardedSetter : nullptr,
		external
	);
}

inline bool streq_casein(std::string& str1, std::string& str2) {
  return str1.size() == str2.size() && std::equal(str1.begin(), str1.end(), str2.begin(), [](char& c1, char& c2) {
    return c1 == c2 || std::toupper(c1) == std::toupper(c2);
  });
}
