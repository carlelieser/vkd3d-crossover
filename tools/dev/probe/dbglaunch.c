/* Minimal Win32 debugger: launches a process (DEBUG_ONLY_THIS_PROCESS) and logs debug strings, module loads,
 * and exceptions with module+offset. On a second-chance exception (or a non-continuable first chance from a
 * known-fatal code), prints registers and scans the stack for return addresses. Usage: dbglaunch.exe <exe> [args] */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAXMOD 512
static struct { DWORD64 base, size; char name[260]; } mods[MAXMOD];
static int nmods;
static HANDLE proc;
static FILE* out;

static void addmod(LPVOID base, HANDLE file) {
	if (nmods >= MAXMOD) return;
	char path[MAX_PATH] = "?";
	if (file) GetFinalPathNameByHandleA(file, path, sizeof(path), 0);
	IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; SIZE_T rd;
	DWORD64 size = 0;
	if (ReadProcessMemory(proc, base, &dos, sizeof(dos), &rd) &&
			ReadProcessMemory(proc, (char*)base + dos.e_lfanew, &nt, sizeof(nt), &rd))
		size = nt.OptionalHeader.SizeOfImage;
	const char* slash = strrchr(path, '\\');
	mods[nmods].base = (DWORD64)base; mods[nmods].size = size;
	snprintf(mods[nmods].name, sizeof(mods[nmods].name), "%s", slash ? slash + 1 : path);
	nmods++;
}

static const char* where(DWORD64 a, char* buf, size_t n) {
	for (int i = 0; i < nmods; i++)
		if (a >= mods[i].base && a < mods[i].base + mods[i].size) { snprintf(buf, n, "%s+0x%llx", mods[i].name, a - mods[i].base); return buf; }
	snprintf(buf, n, "0x%llx", a); return buf;
}

static void dump_thread(DWORD tid) {
	HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
	CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_FULL;
	if (!th || !GetThreadContext(th, &c)) { fprintf(out, "  (no context)\n"); return; }
	char b[300];
	fprintf(out, "  rip=%s rsp=%llx rbp=%llx\n", where(c.Rip, b, sizeof(b)), c.Rsp, c.Rbp);
	fprintf(out, "  rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx\n", c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9);
	static DWORD64 stack[8192]; SIZE_T rd = 0;
	ReadProcessMemory(proc, (LPCVOID)c.Rsp, stack, sizeof(stack), &rd);
	int shown = 0;
	for (SIZE_T i = 0; i < rd / 8 && shown < 60; i++) {
		where(stack[i], b, sizeof(b));
		if (b[0] != '0') { fprintf(out, "  [rsp+%04llx] %s\n", (unsigned long long)(i * 8), b); shown++; }
	}
	CloseHandle(th);
}

int main(int argc, char** argv) {
	if (argc < 2) return 1;
	out = fopen("dbglaunch.log", "w");
	char cmd[4096] = ""; for (int i = 1; i < argc; i++) { strcat(cmd, "\""); strcat(cmd, argv[i]); strcat(cmd, "\" "); }
	STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
	if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) { fprintf(out, "CreateProcess failed %lu\n", GetLastError()); return 1; }
	proc = pi.hProcess;
	DEBUG_EVENT ev; int nexc = 0;
	for (;;) {
		if (!WaitForDebugEvent(&ev, INFINITE)) break;
		DWORD cont = DBG_CONTINUE; char b[300];
		switch (ev.dwDebugEventCode) {
		case CREATE_PROCESS_DEBUG_EVENT: addmod(ev.u.CreateProcessInfo.lpBaseOfImage, ev.u.CreateProcessInfo.hFile); break;
		case LOAD_DLL_DEBUG_EVENT: addmod(ev.u.LoadDll.lpBaseOfDll, ev.u.LoadDll.hFile); break;
		case OUTPUT_DEBUG_STRING_EVENT: {
			char s[2048]; SIZE_T rd = 0; DWORD len = ev.u.DebugString.nDebugStringLength; if (len > sizeof(s) - 1) len = sizeof(s) - 1;
			if (ev.u.DebugString.fUnicode) { WCHAR w[2048]; ReadProcessMemory(proc, ev.u.DebugString.lpDebugStringData, w, len * 2, &rd); w[rd / 2] = 0; WideCharToMultiByte(CP_UTF8, 0, w, -1, s, sizeof(s), NULL, NULL); }
			else { ReadProcessMemory(proc, ev.u.DebugString.lpDebugStringData, s, len, &rd); s[rd] = 0; }
			fprintf(out, "[%lu] DBG %s%s", ev.dwThreadId, s, (rd && s[strlen(s) - 1] == '\n') ? "" : "\n");
			break; }
		case EXCEPTION_DEBUG_EVENT: {
			EXCEPTION_RECORD* r = &ev.u.Exception.ExceptionRecord;
			BOOL first = ev.u.Exception.dwFirstChance;
			if (r->ExceptionCode == EXCEPTION_BREAKPOINT && nexc == 0) { nexc++; break; } /* initial loader breakpoint */
			if (r->ExceptionCode == 0x406D1388 /* thread name */ || r->ExceptionCode == DBG_PRINTEXCEPTION_C || r->ExceptionCode == 0x4001000A) { cont = DBG_EXCEPTION_NOT_HANDLED; break; }
			nexc++;
			if (nexc < 400 || !first)
				fprintf(out, "[%lu] EXC %s code=%08lx at %s params=%lu [%llx %llx %llx %llx]\n", ev.dwThreadId, first ? "first" : "SECOND",
					r->ExceptionCode, where((DWORD64)r->ExceptionAddress, b, sizeof(b)), r->NumberParameters,
					(unsigned long long)r->ExceptionInformation[0], (unsigned long long)r->ExceptionInformation[1],
					(unsigned long long)r->ExceptionInformation[2], (unsigned long long)r->ExceptionInformation[3]);
			if (!first || r->ExceptionCode == 0xC0000005 || r->ExceptionCode == 0xC0000409 || r->ExceptionCode == 0xC0000374) dump_thread(ev.dwThreadId);
			cont = DBG_EXCEPTION_NOT_HANDLED;
			break; }
		case EXIT_PROCESS_DEBUG_EVENT: fprintf(out, "EXIT code=%lx\n", ev.u.ExitProcess.dwExitCode); fflush(out); return 0;
		}
		fflush(out);
		ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
	}
	return 0;
}
