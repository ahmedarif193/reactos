/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
int main(void) {
 HMODULE m; DWORD e; unsigned i, failures=0;
 setvbuf(stdout,NULL,_IONBF,0);
 for(i=0;i<60;i++) {
 SetLastError(0); m=LoadLibraryExW(L"System.Runtime.dll",NULL,i%3==0?DONT_RESOLVE_DLL_REFERENCES:i%3==1?LOAD_WITH_ALTERED_SEARCH_PATH:0); e=GetLastError(); if(!m) failures++;
 printf("MANAGED_LOAD mode=%u module=%p error=%lu\n",i,m,e);
 if(m) { PIMAGE_NT_HEADERS n=(void*)((BYTE*)m+((PIMAGE_DOS_HEADER)m)->e_lfanew); printf("HEAD machine=%x magic=%x ep=%lx mscoree=%p\n",n->FileHeader.Machine,n->OptionalHeader.Magic,n->OptionalHeader.AddressOfEntryPoint,GetModuleHandleW(L"mscoree.dll")); FreeLibrary(m); }
 }
 printf("MANAGED_LOAD_DONE failures=%u\n",failures); return failures?1:0;
}
