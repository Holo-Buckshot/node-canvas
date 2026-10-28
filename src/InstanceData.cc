#include "InstanceData.h"
#include "closure.h"

#include <mutex>
#include <node.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
thread_local InstanceData* current = nullptr;

// Node dlclose()s an addon when the last environment that loaded it shuts
// down, e.g. when the only worker using canvas exits. cairo, pango, glib and
// fontconfig cannot be safely unloaded: glib's type system can't be torn
// down and pango starts background threads (fontconfig initialization) that
// may still be running, which then crash in unmapped code. Pin this module
// so it, and the libraries it links, stay loaded for the life of the process.
void PinModule() {
  static std::once_flag once;
  std::call_once(once, [] {
#ifdef _WIN32
    HMODULE handle;
    GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
      reinterpret_cast<LPCWSTR>(&PinModule),
      &handle);
#else
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&PinModule), &info) && info.dli_fname) {
      // The extra reference is intentionally never released.
      dlopen(info.dli_fname, RTLD_LAZY | RTLD_NODELETE);
    }
#endif
  });
}

void Cleanup(void* arg) {
  InstanceData* data = static_cast<InstanceData*>(arg);

  // Settle async encodes still in flight: no JS can run any more, so drop
  // their callbacks, cancel the ones that haven't started and wait for the
  // rest. Node closes this loop once the environment is gone, which fails if
  // requests are still active. The Canvas objects they read from stay alive
  // because no more GC runs in this environment.
  for (Closure* c : data->pendingAsync) {
    c->cb.Reset();
    c->orphaned = true;
    if (c->req) uv_cancel(reinterpret_cast<uv_req_t*>(c->req));
  }
  while (!data->pendingAsync.empty()) uv_run(data->loop, UV_RUN_ONCE);

  if (current == data) current = nullptr;
  delete data;
}
} // namespace

InstanceData::~InstanceData() {
  // Release every handle while the isolate is still alive.
  canvasCtor.Reset();
  context2dCtor.Reset();
  imageCtor.Reset();
  imageDataCtor.Reset();
  gradientCtor.Reset();
  patternCtor.Reset();
  imageBackendCtor.Reset();
  pdfBackendCtor.Reset();
  svgBackendCtor.Reset();
  DOMMatrix.Reset();
  parseFont.Reset();
  for (auto& a : accessors) a->receiverTpl.Reset();
}

InstanceData* InstanceData::Get() {
  return current;
}

InstanceData* InstanceData::Create(v8::Isolate* isolate) {
  // The addon can be initialized more than once in the same environment
  // (e.g. process.dlopen after clearing the require cache). Reuse the data;
  // the Initialize functions replace the templates it holds.
  if (current) return current;

  PinModule();
  current = new InstanceData();
  current->loop = node::GetCurrentEventLoop(isolate);
  node::AddEnvironmentCleanupHook(isolate, Cleanup, current);
  return current;
}
