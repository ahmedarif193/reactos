@ stdcall CompareStringEx(wstr long wstr long wstr long ptr ptr ptr) kernel32.CompareStringEx
@ stdcall CompareStringW(long long wstr long wstr long) kernel32.CompareStringW
@ stdcall EnumSystemLocalesEx(ptr long long ptr) kernel32.EnumSystemLocalesEx
# FindNLSStringEx not yet implemented in kernel32
@ stdcall FoldStringW(long wstr long ptr long) kernel32.FoldStringW
@ stdcall GetCPInfoExW(long long ptr) kernel32.GetCPInfoExW
@ stdcall GetLocaleInfoEx(wstr long ptr long) kernel32.GetLocaleInfoEx
@ stdcall GetLocaleInfoW(long long ptr long) kernel32.GetLocaleInfoW
@ stdcall GetNLSVersionEx(long wstr ptr) kernel32.GetNLSVersionEx
@ stdcall GetStringTypeW(long wstr long ptr) kernel32.GetStringTypeW
@ stdcall GetSystemDefaultLocaleName(ptr long) kernel32.GetSystemDefaultLocaleName
@ stdcall GetUserDefaultLocaleName(wstr long) kernel32.GetUserDefaultLocaleName
@ stdcall GetUserPreferredUILanguages(long ptr wstr ptr) kernel32.GetUserPreferredUILanguages
@ stdcall IsNLSDefinedString(long long ptr long long) kernel32.IsNLSDefinedString
@ stdcall IsValidLocaleName(wstr) kernel32.IsValidLocaleName
@ stdcall LCMapStringEx(long long wstr long ptr long ptr ptr long) kernel32.LCMapStringEx
@ stdcall LCMapStringW(long long wstr long ptr long) kernel32.LCMapStringW
# ResolveLocaleName not yet implemented in kernel32
