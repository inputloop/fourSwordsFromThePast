// Writes zelda3_crash.txt when the game crashes on Windows, so crashes can be
// diagnosed without a console or debugger.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <time.h>

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info) {
  FILE *f = fopen("zelda3_crash.txt", "a");
  if (f) {
    void *addr = info->ExceptionRecord->ExceptionAddress;
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)addr, &module);
    if (module)
      GetModuleFileNameA(module, name, sizeof(name));
    time_t t = time(NULL);
    fprintf(f, "Crash at %s", ctime(&t));
    fprintf(f, "  exception 0x%08lx at %p (%s + 0x%llx)\n", (unsigned long)info->ExceptionRecord->ExceptionCode,
            addr, name, (unsigned long long)((char *)addr - (char *)module));
    fclose(f);
  }
  MessageBoxA(NULL, "The game crashed. Details were written to zelda3_crash.txt - please send that file along with what you were doing.",
              "The Legend of Zelda: A Link to the Past", MB_OK | MB_ICONERROR);
  return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() {
  SetUnhandledExceptionFilter(&CrashFilter);
}
#else
void InstallCrashHandler() {}
#endif
