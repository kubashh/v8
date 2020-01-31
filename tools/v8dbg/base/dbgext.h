// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_WIN_V8DBG_DBGEXT_H_
#define TOOLS_WIN_V8DBG_DBGEXT_H_

#if !defined(UNICODE) || !defined(_UNICODE)
#error Unicode not defined
#endif

#include <DbgEng.h>
#include <DbgModel.h>
#include <Windows.h>
#include <crtdbg.h>
#include <wrl/client.h>

#include <string>

namespace WRL = Microsoft::WRL;

// Globals for use throughout the extension. (Populated on load).
extern WRL::ComPtr<IDataModelManager> sp_data_model_manager;
extern WRL::ComPtr<IDebugHost> sp_debug_host;
extern WRL::ComPtr<IDebugControl5> sp_debug_control;
extern WRL::ComPtr<IDebugHostMemory2> sp_debug_host_memory;
extern WRL::ComPtr<IDebugHostSymbols> sp_debug_host_symbols;
extern WRL::ComPtr<IDebugHostExtensibility> sp_debug_host_extensibility;

// To be implemented by the custom extension code. (Called on load).
HRESULT CreateExtension();
void DestroyExtension();

#endif
