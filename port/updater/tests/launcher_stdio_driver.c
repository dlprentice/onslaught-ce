/* SPDX-License-Identifier: GPL-3.0-only */
/* Native Windows regression check for the real GUI launcher. No game, network or player profile is used.
 * Compile with the Windows toolchain, then run: launcher_stdio_driver.exe PATH\onslaught-ce.exe
 * Every child is bounded and its inherited output is checked with and without a parent console. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;
static SECURITY_ATTRIBUTES inherit = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

static int read_text(HANDLE h, char *text, DWORD size)
{
	DWORD got = 0, total = 0;
	while (total + 1 < size && ReadFile(h, text + total, size - total - 1, &got, NULL) && got)
		total += got;
	text[total] = 0;
	return (int)total;
}

static void check(const char *exe, const char *label, int file_output, int error_case, int console_error)
{
	HANDLE out_read = INVALID_HANDLE_VALUE, out_write = INVALID_HANDLE_VALUE;
	HANDLE err_read = INVALID_HANDLE_VALUE, err_write = INVALID_HANDLE_VALUE, input = INVALID_HANDLE_VALUE;
	HANDLE terminal = INVALID_HANDLE_VALUE;
	char temp[MAX_PATH], output_file[MAX_PATH] = "", command[32768], out[4096], err[4096];
	DWORD code = ~0u, wait;
	STARTUPINFOA startup = {0};
	PROCESS_INFORMATION child = {0};
	int ok = 0;
	if (!GetTempPathA(sizeof temp, temp)) goto done;
	if (file_output)
	{
		if (!GetTempFileNameA(temp, "ceo", 0, output_file)) goto done;
		out_write = CreateFileA(output_file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&inherit, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, NULL);
		if (out_write == INVALID_HANDLE_VALUE) goto done;
	}
	else if (!CreatePipe(&out_read, &out_write, &inherit, 0) || !SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0))
		goto done;
	if (!CreatePipe(&err_read, &err_write, &inherit, 0) || !SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0))
		goto done;
	input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, NULL);
	if (input == INVALID_HANDLE_VALUE) goto done;
	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = input;
	startup.hStdOutput = out_write;
	if (console_error)
	{
		terminal = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&inherit, OPEN_EXISTING, 0, NULL);
		if (terminal == INVALID_HANDLE_VALUE) goto done;
	}
	startup.hStdError = console_error ? terminal : err_write;
	if (snprintf(command, sizeof command, "\"%s\" %s", exe,
		error_case ? "--no-update-check --update-only" : "--version") >= (int)sizeof command) goto done;
	if (!CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &child)) goto done;
	CloseHandle(out_write); out_write = INVALID_HANDLE_VALUE;
	CloseHandle(err_write); err_write = INVALID_HANDLE_VALUE;
	wait = WaitForSingleObject(child.hProcess, 60000);
	if (wait != WAIT_OBJECT_0)
	{
		/* This handle identifies only the child created above, never another process. */
		TerminateProcess(child.hProcess, 124);
		WaitForSingleObject(child.hProcess, 10000);
		goto done;
	}
	if (!GetExitCodeProcess(child.hProcess, &code)) goto done;
	if (file_output)
		out_read = CreateFileA(output_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (out_read == INVALID_HANDLE_VALUE) goto done;
	read_text(out_read, out, sizeof out);
	read_text(err_read, err, sizeof err);
	if (error_case)
		ok = code == 1 && !*out && strstr(err, "could not prepare the settings folder") != NULL;
	else
		ok = code == 0 && !strncmp(out, "Onslaught CE v", 14) && strstr(out, ", build ") && !*err;
	if (!ok) printf("FAIL %s: exit=%lu stdout=[%s] stderr=[%s]\n", label, code, out, err);
done:
	if (!ok) { failures++; printf("FAIL %s (Windows error %lu)\n", label, GetLastError()); }
	else printf("PASS %s\n", label);
	if (child.hThread) CloseHandle(child.hThread);
	if (child.hProcess) CloseHandle(child.hProcess);
	if (out_read != INVALID_HANDLE_VALUE) CloseHandle(out_read);
	if (out_write != INVALID_HANDLE_VALUE) CloseHandle(out_write);
	if (err_read != INVALID_HANDLE_VALUE) CloseHandle(err_read);
	if (err_write != INVALID_HANDLE_VALUE) CloseHandle(err_write);
	if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
	if (terminal != INVALID_HANDLE_VALUE) CloseHandle(terminal);
	if (*output_file) DeleteFileA(output_file);
}

int main(int argc, char **argv)
{
	char exe[MAX_PATH], temp[MAX_PATH], blocked[MAX_PATH];
	DWORD length;
	if (argc != 2) return 2;
	length = GetFullPathNameA(argv[1], sizeof exe, exe, NULL);
	if (!length || length >= sizeof exe || !GetTempPathA(sizeof temp, temp) ||
		!GetTempFileNameA(temp, "ceu", 0, blocked)) return 2;
	/* An existing regular file is an impossible user directory: tests the launcher's early stderr error safely. */
	if (!SetEnvironmentVariableA("BEA_USER_DIR", blocked)) return 2;
	FreeConsole();
	check(exe, "detached pipes version", 0, 0, 0);
	check(exe, "detached file version", 1, 0, 0);
	check(exe, "detached stderr", 0, 1, 0);
	if (!AllocConsole()) { failures++; printf("FAIL allocate isolated test console\n"); }
	else
	{
		check(exe, "console pipes version", 0, 0, 0);
		check(exe, "console file version", 1, 0, 0);
		check(exe, "console mixed stdout pipe stderr console", 0, 0, 1);
		check(exe, "console stderr", 0, 1, 0);
		FreeConsole();
	}
	DeleteFileA(blocked);
	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
