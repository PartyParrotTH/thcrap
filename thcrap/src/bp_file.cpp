/**
  * Touhou Community Reliant Automatic Patcher
  * Main DLL
  *
  * ----
  *
  * Breakpoints for contiguous files.
  */

#include "thcrap.h"
#include <map>
#include <algorithm>

#define POST_JSON_SIZE(fr) (fr)->pre_json_size + (fr)->patch_size

int file_rep_init(file_rep_t *fr, const char *file_name)
{
	size_t fn_len;

	if (fr->name) {
		file_rep_clear(fr);
	}
	fn_len = strlen(file_name) + 1;
	fr->name = EnsureUTF8(file_name, fn_len);
	fr->rep_buffer = stack_game_file_resolve(fr->name, &fr->pre_json_size);
	fr->offset = SIZE_MAX;
	fr->hooks = patchhooks_build(fr->name);
	if (fr->hooks) {
		fr->patch = patchhooks_load_diff(fr->hooks, fr->name, &fr->patch_size);
	}
	InitializeCriticalSection(&fr->cs);
	return 1;
}

static int file_rep_hooks_run(file_rep_t *fr)
{
	return patchhooks_run(
		fr->hooks, fr->game_buffer, POST_JSON_SIZE(fr), fr->pre_json_size, fr->name, fr->patch
	);
}

int file_rep_clear(file_rep_t *fr)
{
	if(!fr) {
		return -1;
	}
	SAFE_FREE(fr->rep_buffer);
	fr->game_buffer = nullptr;
	fr->patch = json_decref_safe(fr->patch);
	SAFE_FREE(fr->hooks);
	fr->patch_size = 0;
	fr->pre_json_size = 0;
	fr->offset = SIZE_MAX;
	fr->orig_size = 0;
	fr->object = NULL;
	DeleteCriticalSection(&fr->cs);
	SAFE_FREE(fr->name);
	return 0;
}

/// Thread-local storage
/// --------------------
THREAD_LOCAL(file_rep_t, fr_tls, NULL, file_rep_clear);
THREAD_LOCAL(file_rep_t*, fr_ptr_tls, NULL, NULL);
/// --------------------

/// Replace a file loaded entirely in memory
/// ----------------------------------------
int BP_file_buffer(x86_reg_t *regs, json_t *bp_info)
{
	file_rep_t *fr = fr_tls_get();

	// Parameters
	// ----------
	auto file_buffer = (BYTE**)json_object_get_pointer(bp_info, regs, "file_buffer");
	// ----------
	if(file_buffer) {
		fr->game_buffer = *file_buffer;
	}
	return 1;
}

int BP_file_load(x86_reg_t *regs, json_t *bp_info)
{
	file_rep_t *fr = fr_tls_get();

	// Mandatory parameters
	// --------------------
	auto file_name = (char**)json_object_get_pointer(bp_info, regs, "file_name");
	auto file_size = json_object_get_pointer(bp_info, regs, "file_size");
	BP_file_buffer(regs, bp_info);
	// -----------------

	if(file_name) {
		file_rep_init(fr, *file_name);
	}

	// th08 and th09 use their file size variable as the loop counter for LZSS
	// decompression. Putting anything other than the original file size from
	// the archive there (by writing to that variable) will result in a few
	// bytes of corruption at the end of the decompressed file.
	// Therefore, these games need the POST_JSON_SIZE to be unconditionally
	// written out to registers three separate times.

	// However, we *do* check whether we have a file name. If we don't, we
	// can't possibly have resolved a replacement file that would give us a
	// custom file size.
	// This allows this breakpoint to be placed in front of memory allocation
	// calls that are used for more than just replaceable files, without
	// affecting unrelated memory allocations.
	if(file_size && fr->name) {
		if(!fr->pre_json_size) {
			fr->pre_json_size = *file_size;
		}
		*file_size = POST_JSON_SIZE(fr);
	}

	// Got everything for a full file replacement?
	if(!fr->game_buffer || !fr->rep_buffer || !fr->pre_json_size) {
		return 1;
	}

	// Load-specific parameters
	// ------------------------
	auto file_buffer_addr_copy = json_object_get_pointer(bp_info, regs, "file_buffer_addr_copy");
	size_t stack_clear_size = json_object_get_hex(bp_info, "stack_clear_size");
	size_t eip_jump_dist = json_object_get_hex(bp_info, "eip_jump_dist");
	// ------------------------

	// Let's do it
	memcpy(fr->game_buffer, fr->rep_buffer, fr->pre_json_size);

	file_rep_hooks_run(fr);

	if(eip_jump_dist) {
		regs->retaddr += eip_jump_dist;
	}
	if(file_buffer_addr_copy) {
		*file_buffer_addr_copy = (size_t)fr->game_buffer;
	}
	if(stack_clear_size) {
		regs->esp += stack_clear_size;
	}
	file_rep_clear(fr);
	return 0;
}

int BP_file_name(x86_reg_t *regs, json_t *bp_info)
{
	return BP_file_load(regs, bp_info);
}

int BP_file_size(x86_reg_t *regs, json_t *bp_info)
{
	return BP_file_load(regs, bp_info);
}

// Cool function name.
int DumpDatFile(const char *dir, const file_rep_t *fr)
{
	if(!fr || !fr->game_buffer || !fr->name) {
		return -1;
	}
	{
		size_t fn_len = strlen(dir) + 1 + strlen(fr->name) + 1;
		VLA(char, fn, fn_len);

		sprintf(fn, "%s/%s", dir, fr->name);

		if(!PathFileExists(fn)) {
			file_write(fn, fr->game_buffer, fr->pre_json_size);
		}
		VLA_FREE(fn);
	}
	return 0;
}

// DumpDatFile, fragmented loading style.
int DumpDatFragmentedFile(const char *dir, file_rep_t *fr, HANDLE hFile, fragmented_read_file_hook_t post_read)
{
	if (!fr || !hFile || hFile == INVALID_HANDLE_VALUE || !fr->name || fr->offset != SIZE_MAX) {
		return -1;
	}
	{
		size_t fn_len = strlen(dir) + 1 + strlen(fr->name) + 1;
		VLA(char, fn, fn_len);

		sprintf(fn, "%s/%s", dir, fr->name);

		if (!PathFileExists(fn)) {
			// Read the file
			DWORD nbOfBytesRead;
			BYTE* buffer = (BYTE*)malloc(fr->orig_size);
			ReadFile(hFile, buffer, fr->orig_size, &nbOfBytesRead, NULL);
			SetFilePointer(hFile, -(LONG)nbOfBytesRead, NULL, FILE_CURRENT);

			if (post_read) {
				fr->offset = SetFilePointer(hFile, 0, NULL, FILE_CURRENT); // Needed by nsml
				post_read(fr, buffer, fr->orig_size);
				fr->offset = SIZE_MAX;
			}
			file_write(fn, buffer, fr->orig_size);
		}
		VLA_FREE(fn);
	}
	return 0;
}

int BP_file_loaded(x86_reg_t *regs, json_t *bp_info)
{
	file_rep_t *fr = fr_tls_get();
	const char *dat_dump;

	// Other breakpoints
	// -----------------
	BP_file_buffer(regs, bp_info);
	// -----------------

	if(!fr->game_buffer) {
		return 1;
	}
	dat_dump = runconfig_dat_dump_get();
	if(dat_dump) {
		DumpDatFile(dat_dump, fr);
	}

	file_rep_hooks_run(fr);
	file_rep_clear(fr);
	return 1;
}

/// Replace a file loaded by fragments
/// ----------------------------------

// For these files, we need to know the full file size beforehand.
// So we need a breakpoint in the file header.
std::map<std::string, file_rep_t> files_list;
std::map<const void*, file_rep_t*> file_object_to_rep_list;
static CRITICAL_SECTION cs;

file_rep_t *file_rep_get(const char *filename)
{
	EnterCriticalSection(&cs);
	auto it = files_list.find(filename);
	if (it != files_list.end()) {
		LeaveCriticalSection(&cs);
		return &it->second;
	}
	else {
		LeaveCriticalSection(&cs);
		return nullptr;
	}
}

void file_rep_set_object(file_rep_t *fr, void *object)
{
	EnterCriticalSection(&cs);
	if (fr->object) {
		file_object_to_rep_list.erase(fr->object);
	}
	fr->object = object;
	if (object) {
		file_object_to_rep_list[object] = fr;
	}
	LeaveCriticalSection(&cs);
}

file_rep_t *file_rep_get_by_object(const void *object)
{
	if (!object) {
		return nullptr;
	}

	EnterCriticalSection(&cs);
	auto it = file_object_to_rep_list.find(object);
	if (it != file_object_to_rep_list.end()) {
		LeaveCriticalSection(&cs);
		return it->second;
	}
	else {
		LeaveCriticalSection(&cs);
		return nullptr;
	}
}

int BP_file_header(x86_reg_t *regs, json_t *bp_info)
{
	// Parameters
	// ----------
	const char *filename = (const char*)json_object_get_immediate(bp_info, regs, "file_name");
	size_t *size = json_object_get_pointer(bp_info, regs, "file_size");
	// ----------

	if (!filename || !size)
		return 1;

	file_rep_t *fr = file_rep_get(filename);
	if (fr == nullptr) {
		fr = &files_list[filename];
		memset(fr, 0, sizeof(file_rep_t));
		file_rep_clear(fr);
	}
	file_rep_init(fr, filename);
	if (fr->rep_buffer && fr->patch_size) {
		fr->rep_buffer = realloc(fr->rep_buffer, POST_JSON_SIZE(fr));
	}

	const char* dat_dump = runconfig_dat_dump_get();
	if (fr->rep_buffer != NULL || fr->patch != NULL || fr->hooks != NULL || dat_dump) {
		fr->orig_size = *size;
		*size = MAX(*size, fr->pre_json_size) + fr->patch_size;
	}
	else {
		file_rep_clear(fr);
	}

	return 1;
}

int BP_fragmented_open_file(x86_reg_t *regs, json_t *bp_info)
{
	file_rep_t *fr = *fr_ptr_tls_get();

	// Parameters
	// ----------
	const char *file_name = (const char*)json_object_get_immediate(bp_info, regs, "file_name");
	void *file_object = (void*)json_object_get_immediate(bp_info, regs, "file_object");
	size_t *file_size = json_object_get_pointer(bp_info, regs, "file_size");
	// ----------

	if (file_name) {
		if (files_list.empty() == false) {
			// We got a files list in the file header
			fr = file_rep_get(file_name);
			*fr_ptr_tls_get() = fr;
		}
		else {
			// We didn't have a header, load the file now.
			fr = (file_rep_t*)malloc(sizeof(*fr));
			memset(fr, 0, sizeof(file_rep_t));
			file_rep_clear(fr);
			file_rep_init(fr, file_name);
			if (fr->rep_buffer && fr->patch_size) {
				fr->rep_buffer = realloc(fr->rep_buffer, POST_JSON_SIZE(fr));
			}
			*fr_ptr_tls_get() = fr;
		}
	}

	if (fr && file_object) {
		file_rep_set_object(fr, file_object);
	}

	if (fr && file_size) {
		fr->orig_size = *file_size;
		*file_size = MAX(*file_size, fr->pre_json_size) + fr->patch_size;
	}

	return 1;
}

int BP_fragmented_read_file(x86_reg_t *regs, json_t *bp_info)
{
	file_rep_t *fr = *fr_ptr_tls_get();

	// Parameters
	// ----------
	const char *file_name   = (const char*)json_object_get_immediate(bp_info, regs, "file_name");
	const void *file_object = (const void*)json_object_get_immediate(bp_info, regs, "file_object");
	// Stack
	// ----------
	HANDLE       hFile                = ((HANDLE*)      regs->esp)[1];
	LPBYTE       lpBuffer             = ((LPBYTE*)      regs->esp)[2];
	DWORD        nNumberOfBytesToRead = ((DWORD*)       regs->esp)[3];
	LPDWORD      lpNumberOfBytesRead  = ((LPDWORD*)     regs->esp)[4];
	LPOVERLAPPED lpOverlapped         = ((LPOVERLAPPED*)regs->esp)[5];
	// ----------

	if (file_name) {
		fr = file_rep_get(file_name);
		*fr_ptr_tls_get() = fr;
	}
	else if (file_object) {
		fr = file_rep_get_by_object(file_object);
		*fr_ptr_tls_get() = fr;
	}
	if (!fr || !fr->name || !fr->orig_size) {
		return 1;
	}

	const char *dat_dump = runconfig_dat_dump_get();
	if (dat_dump) {
		DumpDatFragmentedFile(dat_dump, fr, hFile, (fragmented_read_file_hook_t)json_object_get_immediate(bp_info, regs, "post_read"));
	}

	if (!fr->rep_buffer && !fr->patch && !fr->hooks) {
		return 1;
	}
	if (lpOverlapped) {
		// Overlapped operations are not supported.
		// We'd better leave that file alone rather than ignoring that.
		return 1;
	}

	EnterCriticalSection(&fr->cs);
	bool has_rep = fr->rep_buffer != nullptr;
	if (fr->offset == SIZE_MAX) {
		fr->offset = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);

		// Read the original file if we don't have a replacement one
		if (!fr->rep_buffer) {
			DWORD nbOfBytesRead;
			fr->rep_buffer = malloc(fr->orig_size + fr->patch_size);
			fr->pre_json_size = fr->orig_size;
			ReadFile(hFile, fr->rep_buffer, fr->orig_size, &nbOfBytesRead, NULL);
			SetFilePointer(hFile, fr->offset, NULL, FILE_BEGIN);

			fragmented_read_file_hook_t post_read = (fragmented_read_file_hook_t)json_object_get_immediate(bp_info, regs, "post_read");
			if (post_read) {
				post_read(fr, (BYTE*)fr->rep_buffer, fr->orig_size);
			}
		}
		// Patch the game
		if (patchhooks_run(fr->hooks, fr->rep_buffer, POST_JSON_SIZE(fr), fr->pre_json_size, fr->name, fr->patch)) {
			has_rep = 1;
		}
		fragmented_read_file_hook_t post_patch = (fragmented_read_file_hook_t)json_object_get_immediate(bp_info, regs, "post_patch");
		if (post_patch) {
			post_patch(fr, (BYTE*)fr->rep_buffer, POST_JSON_SIZE(fr));
		}

		// If we didn't change the file in any way, we can free the rep buffer.
		if (!has_rep) {
			SAFE_FREE(fr->rep_buffer);
		}
	}
	if (!fr->rep_buffer) {
		LeaveCriticalSection(&fr->cs);
		return 1;
	}

	log_printf("Patching %s\n", fr->name);
	DWORD offset = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - fr->offset;
	if (offset <= POST_JSON_SIZE(fr)) {
		*lpNumberOfBytesRead = MIN(POST_JSON_SIZE(fr) - offset, nNumberOfBytesToRead);
	}
	else {
		*lpNumberOfBytesRead = 0;
	}

	memcpy(lpBuffer, (BYTE*)fr->rep_buffer + offset, *lpNumberOfBytesRead);
	SetFilePointer(hFile, *lpNumberOfBytesRead, NULL, FILE_CURRENT);

	if (offset + *lpNumberOfBytesRead == POST_JSON_SIZE(fr)) {
		*fr_ptr_tls_get() = nullptr;
	}

	LeaveCriticalSection(&fr->cs);
	regs->eax = 1;
	regs->esp += 5 * sizeof(DWORD);
	return 0;
}

int BP_fragmented_close_file(x86_reg_t *regs, json_t *bp_info)
{
	file_rep_t *fr = *fr_ptr_tls_get();

	// Parameters
	// ----------
	const void *file_object = (const void*)json_object_get_immediate(bp_info, regs, "file_object");
	// ----------

	if (file_object) {
		file_rep_t *fr = file_rep_get_by_object(file_object);
		if (fr) {
			file_rep_set_object(fr, nullptr);
		}
	}

	if (fr && files_list.empty() == false) {
		// If we didn't use a header, we need to free the file_rep we allocated.
		free(fr);
	}

	*fr_ptr_tls_get() = nullptr;
	return 1;
}

extern "C" int file_mod_init()
{
	InitializeCriticalSection(&cs);
	return 0;
}

extern "C" void file_mod_exit()
{
	DeleteCriticalSection(&cs);
}
