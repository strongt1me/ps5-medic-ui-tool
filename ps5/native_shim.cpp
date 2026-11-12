// SPDX-License-Identifier: GPL-3.0-or-later
// Platzhalter fuer die native App (title/build_native_ui.sh): die App ist ohne Ausnahmen gebaut, der Unwinder (libunwind) wird nicht gelinkt.
// libc++/libc++abi verweisen trotzdem auf seine Einstiegspunkte (Persoenlichkeitsfunktion, Exception-Speicher); sie werden nie erreicht,
// weil nichts wirft. Kommt doch einer an, bricht die App ab statt zu raten.
#include <cstdint>

extern "C" {
[[noreturn]] static void native_unreachable() { __builtin_trap(); }
void _Unwind_RaiseException(void*) { native_unreachable(); }
void _Unwind_Resume(void*) { native_unreachable(); }
void _Unwind_DeleteException(void*) { native_unreachable(); }
std::uintptr_t _Unwind_GetLanguageSpecificData(void*) { native_unreachable(); }
std::uintptr_t _Unwind_GetRegionStart(void*) { native_unreachable(); }
std::uintptr_t _Unwind_GetIP(void*) { native_unreachable(); }
std::uintptr_t _Unwind_GetIPInfo(void*, int*) { native_unreachable(); }
std::uintptr_t _Unwind_GetDataRelBase(void*) { native_unreachable(); }
std::uintptr_t _Unwind_GetTextRelBase(void*) { native_unreachable(); }
void _Unwind_SetIP(void*, std::uintptr_t) { native_unreachable(); }
void _Unwind_SetGR(void*, int, std::uintptr_t) { native_unreachable(); }
std::uintptr_t _Unwind_GetGR(void*, int) { native_unreachable(); }
int _Unwind_Backtrace(void*, void*) { native_unreachable(); }
// libc++abi ruft __assert fuer seine eigenen Pruefungen (FreeBSD-Signatur)
void __assert(const char*, const char*, int, const char*) { native_unreachable(); }
}
