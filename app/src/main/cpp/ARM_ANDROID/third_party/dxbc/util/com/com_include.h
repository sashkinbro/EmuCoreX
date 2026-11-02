#pragma once

// Trimmed for the vendored Android build.
//
// DXVK's original header pulls <windows.h> and <unknwn.h>. The DXBC compiler keeps only a
// handful of Win32-flavoured types on its non-Windows paths (HRESULT in util_string.h, the
// COM-shaped helpers that are compiled out), so this provides those types instead.
#include <cstdint>
#include <cstddef>

#if !defined(_WIN32)

using HRESULT = int32_t;
using ULONG   = unsigned long;
using DWORD   = unsigned long;
using LONG    = int32_t;
using UINT    = unsigned int;
using BOOL    = int;

#ifndef S_OK
#define S_OK  static_cast<HRESULT>(0)
#endif
#ifndef S_FALSE
#define S_FALSE static_cast<HRESULT>(1)
#endif
#ifndef E_FAIL
#define E_FAIL static_cast<HRESULT>(0x80004005u)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL static_cast<HRESULT>(0x80004001u)
#endif

struct GUID {
  uint32_t Data1 = 0;
  uint16_t Data2 = 0;
  uint16_t Data3 = 0;
  uint8_t  Data4[8] = {};
};

inline bool operator==(const GUID& a, const GUID& b) {
  const auto* pa = reinterpret_cast<const uint8_t*>(&a);
  const auto* pb = reinterpret_cast<const uint8_t*>(&b);
  for (size_t i = 0; i < sizeof(GUID); i++) {
    if (pa[i] != pb[i]) return false;
  }
  return true;
}

inline bool operator!=(const GUID& a, const GUID& b) { return !(a == b); }

using WCHAR = wchar_t;
using TCHAR = char;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;

using REFGUID = const GUID&;
using REFIID  = const GUID&;
using IID     = GUID;

struct IUnknown {
  virtual ~IUnknown() = default;
  virtual HRESULT QueryInterface(const GUID&, void**) { return E_NOTIMPL; }
  virtual ULONG AddRef() { return 0; }
  virtual ULONG Release() { return 0; }
};

#endif