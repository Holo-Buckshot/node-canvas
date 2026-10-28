#pragma once

// Per-environment state for the addon.
//
// Every JS environment that loads canvas (the main thread and each
// worker_threads Worker) has its own V8 isolate, and V8 handles from one
// isolate must never be used from another. Previously the class templates
// and the DOMMatrix/parseFont functions lived in `static Nan::Persistent`
// members, so loading canvas in a second environment overwrote handles still
// in use by the first one (and left dangling handles behind when a worker
// exited). All of that state now lives here, one instance per environment.
//
// Each environment runs on its own thread, so the current environment's data
// is found through a thread_local pointer that is set when the addon is
// initialized in that environment and cleared by an environment cleanup hook
// when it shuts down.

#include <memory>
#include <unordered_set>
#include <nan.h>
#include <v8.h>
#include <vector>

struct Closure;

struct InstanceData {
  Nan::Persistent<v8::FunctionTemplate> canvasCtor;
  Nan::Persistent<v8::FunctionTemplate> context2dCtor;
  Nan::Persistent<v8::FunctionTemplate> imageCtor;
  Nan::Persistent<v8::FunctionTemplate> imageDataCtor;
  Nan::Persistent<v8::FunctionTemplate> gradientCtor;
  Nan::Persistent<v8::FunctionTemplate> patternCtor;
  Nan::Persistent<v8::FunctionTemplate> imageBackendCtor;
  Nan::Persistent<v8::FunctionTemplate> pdfBackendCtor;
  Nan::Persistent<v8::FunctionTemplate> svgBackendCtor;

  // Set from JS by CanvasRenderingContext2dInit (lib/context2d.js).
  Nan::Persistent<v8::Function> DOMMatrix;
  Nan::Persistent<v8::Function> parseFont;

  // Owned callback data for prototype accessors (see Util.h).
  struct AccessorData {
    Nan::GetterCallback getter;
    Nan::SetterCallback setter;
    Nan::Persistent<v8::FunctionTemplate> receiverTpl;
  };
  std::vector<std::unique_ptr<AccessorData>> accessors;

  // Async encodes (canvas.toBuffer(callback)) queued on this environment's
  // loop. If the environment shuts down first (e.g. Worker.terminate()), they
  // are cancelled or waited for in the cleanup hook, because Node closes the
  // loop afterwards and the isolate they reference is disposed.
  std::unordered_set<Closure*> pendingAsync;
  uv_loop_t* loop = nullptr;

  ~InstanceData();

  // Data for the environment running on the calling thread. Only valid on a
  // thread with a live JS environment that has loaded the addon.
  static InstanceData* Get();

  // Creates the data for the environment owning `isolate` and registers its
  // cleanup. Called once from the module initializer.
  static InstanceData* Create(v8::Isolate* isolate);
};
