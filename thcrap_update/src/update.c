/**
  * Touhou Community Reliant Automatic Patcher
  * Update plugin
  *
  * ----
  *
  * Main updating functionality.
  */

#include <thcrap.h>
#include <MMSystem.h>
#include <WinInet.h>
#include "update.h"

HINTERNET hINet = NULL;

int inet_init()
{
	// DWORD timeout = 500;
	const char *project_name = PROJECT_NAME(); 
	size_t agent_len = strlen(project_name) + strlen(" (--) " ) + 16 + 1;
	VLA(char, agent, agent_len);
	sprintf(
		agent, "%s (%s)", project_name, PROJECT_VERSION_STRING()
	);

	hINet = InternetOpenA(agent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if(!hINet) {
		// No internet access...?
		return 1;
	}
	/*
	InternetSetOption(hINet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(DWORD));
	InternetSetOption(hINet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(DWORD));
	InternetSetOption(hINet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(DWORD));
	*/
	VLA_FREE(agent);
	return 0;
}

void inet_exit()
{
	if(hINet) {
		InternetCloseHandle(hINet);
		hINet = NULL;
	}
}

void ServerNewSession(json_t *servers)
{
	size_t i;
	json_t *server;

	json_array_foreach(servers, i, server) {
		json_object_set_nocheck(server, "time", json_true());
	}
}

size_t ServerGetNumActive(const json_t *servers)
{
	size_t i;
	json_t *server;
	int ret = json_array_size(servers);

	json_array_foreach(servers, i, server) {
		if(json_is_false(json_object_get(server, "time"))) {
			ret--;
		}
	}
	return ret;
}

int ServerDisable(json_t *server)
{
	json_object_set(server, "time", json_false());
	return 0;
}

const int ServerGetFirst(const json_t *servers)
{
	unsigned int last_time = -1;
	size_t i = 0;
	int fastest = -1;
	int tryout = -1;
	json_t *server;

	// Any verification is performed in ServerDownloadFile().
	if(json_array_size(servers) == 0) {
		return 0;
	}

	// Get fastest server from previous data
	json_array_foreach(servers, i, server) {
		const json_t *server_time = json_object_get(server, "time");
		if(json_is_integer(server_time)) {
			json_int_t cur_time = json_integer_value(server_time);
			if(cur_time < last_time) {
				last_time = (unsigned int)cur_time;
				fastest = i;
			}
		} else if(json_is_true(server_time)) {
			tryout = i;
		}
	}
	if(tryout != -1) {
		return tryout;
	} else if(fastest != -1) {
		return fastest;
	} else {
		// Everything down...
		return -1;
	}
}

void* ServerDownloadFileW(json_t *servers, const wchar_t *fn, DWORD *file_size)
{
	HINTERNET hFile = NULL;
	DWORD http_stat;
	DWORD byte_ret = sizeof(DWORD);
	DWORD read_size = 0;
	BOOL ret;
	BYTE *file_buffer = NULL, *p;
	size_t i;
	URL_COMPONENTS uc;

	int servers_left;
	int servers_first;

	if(!fn || !file_size) {
		return NULL;
	}
	servers_first = ServerGetFirst(servers);
	if(servers_first < 0) {
		return NULL;
	}
	// gets decremented in the loop
	servers_left = json_array_size(servers);
	for(i = servers_first; servers_left; i++) {
		DWORD time, time_start;
		json_t *server;
		const char *server_url;
		json_t *server_time;

		// Loop back
		if(i >= json_array_size(servers)) {
			i = 0;
		}

		server = json_array_get(servers, i);
		server_time = json_object_get(server, "time");

		if(json_is_false(server_time)) {
			continue;
		}

		server_url = json_object_get_string(server, "url");
		servers_left--;

		InternetCloseHandle(hFile);

		{
			size_t server_len = strlen(server_url) + 1;
			// * 3 because characters may be URL-encoded
			DWORD url_len = server_len + 1 + (wcslen(fn) * 3) + 1;
			VLA(wchar_t, server_w, server_len);
			VLA(wchar_t, server_host, server_len);
			VLA(wchar_t, url, url_len);

			StringToUTF16(server_w, server_url, server_len);

			InternetCombineUrl(server_w, fn, url, &url_len, 0);

			ZeroMemory(&uc, sizeof(URL_COMPONENTS));
			uc.dwStructSize = sizeof(URL_COMPONENTS);
			uc.lpszHostName = server_host;
			uc.dwHostNameLength = server_len;

			InternetCrackUrl(server_w, 0, 0, &uc);

			log_wprintf(L"%s (%s)... ", fn, uc.lpszHostName);

			time_start = timeGetTime();

			hFile = InternetOpenUrl(hINet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
			VLA_FREE(url);
			VLA_FREE(server_host);
			VLA_FREE(server_w);
		}
		if(!hFile) {
			DWORD inet_ret = GetLastError();
			log_printf("WinInet error %d\n", inet_ret);
			ServerDisable(server);
			continue;
		}

		HttpQueryInfo(hFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &http_stat, &byte_ret, 0);
	
		log_printf("%d", http_stat);

		if(http_stat != 200) {
			// If the file is missing on one server, it's probably missing on all of them.
			// No reason to disable any server.
			log_printf("\n");
			continue;
		}
	
		HttpQueryInfo(hFile, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, file_size, &byte_ret, 0);

		file_buffer = (BYTE*)malloc(*file_size);
		if(!file_buffer) {
			break;
		}
		p = file_buffer;

		read_size = *file_size;
		while(read_size) {
			ret = InternetReadFile(hFile, file_buffer, *file_size, &byte_ret);
			if(ret) {
				read_size -= byte_ret;
				p += byte_ret;
			} else {
				SAFE_FREE(file_buffer);
				log_printf("\nReading error #%d!", GetLastError());
				ServerDisable(server);
				read_size = 0;
				return NULL;
			}
		}
		time = timeGetTime() - time_start;

		log_printf(" (%d b, %d ms)\n", *file_size, time);

		json_object_set_new(server, "time", json_integer(time));
		InternetCloseHandle(hFile);
		return file_buffer;
	}
	return NULL;
}

void* ServerDownloadFileA(json_t *servers, const char *fn, DWORD *file_size)
{
	if(!fn) {
		return NULL;
	}
	{
		void *ret;
		WCHAR_T_DEC(fn);
		fn_w = StringToUTF16_VLA(fn_w, fn, fn_len);
		ret = ServerDownloadFileW(servers, fn_w, file_size);
		VLA_FREE(fn_w);
		return ret;
	}
}

json_t* ServerInit(json_t *patch_js)
{
	json_t *servers = json_object_get(patch_js, "servers");
	json_t *val;
	size_t i;

	if(json_is_object(servers)) {
		// This is only to support old, unfinished downloads where the local patch.js
		// still has <servers> as an object. Thus, we convert it back to an array
		json_t *arr = json_array();
		const char *url;
		json_object_foreach(servers, url, val) {
			json_t *server = json_object();
			json_object_set_new(server, "url", json_string(url));
			json_object_set(server, "time", val);
		}
		servers = arr;
		json_object_set_new(patch_js, "servers", arr);
	}

	json_array_foreach(servers, i, val) {
		json_t *obj = val;
		if(json_is_string(val)) {
			// Convert to object
			obj = json_object();
			json_object_set(obj, "url", val);
			json_array_set_new(servers, i, obj);
		}
		json_object_set(obj, "time", json_true());
	}
	return servers;
}

int PatchFileRequiresUpdate(const json_t *patch_info, const char *fn, json_t *local_val, json_t *remote_val)
{
	// Update if remote and local JSON values don't match
	if(!json_equal(local_val, remote_val)) {
		return 1;
	}
	// Update if local file doesn't exist
	if(!patch_file_exists(patch_info, fn)) {
		return 1;
	}
	return 0;
}

int patch_update(const json_t *patch_info)
{
	const char *main_fn = "patch.js";

	json_t *local_patch_js = NULL;
	json_t *local_servers = NULL;
	json_t *local_files = NULL;

	DWORD remote_patch_js_size;
	BYTE *remote_patch_js_buffer = NULL;

	json_t *remote_patch_js = NULL;
	json_t *remote_servers = NULL;
	json_t *remote_files;
	json_t *remote_val;

	int ret = 0;
	size_t i = 0;
	size_t file_count = 0;
	size_t file_digits = 0;

	const char *key;

	if(!patch_info) {
		return -1;
	}

	// Load local patch.js
	local_patch_js = patch_json_load(patch_info, main_fn, NULL);
	if(!local_patch_js) {
		// No patch.js, no update
		ret = 1;
		goto end_update;
	}

	local_files = json_object_get_create(local_patch_js, "files", json_object());

	if(json_is_false(json_object_get(local_patch_js, "update"))) {
		// Updating deactivated on this patch
		ret = 2;
		goto end_update;
	}
	{
		const char *patch_name = json_object_get_string(local_patch_js, "id");
		if(patch_name) {
			log_printf("Checking for updates of %s...\n", patch_name);
		}
	}

	// Init local servers for bootstrapping
	local_servers = ServerInit(local_patch_js);
	
	remote_patch_js_buffer = ServerDownloadFileA(local_servers, main_fn, &remote_patch_js_size);
	if(!remote_patch_js_buffer) {
		// All servers offline...
		ret = 3;
		goto end_update;
	}

	remote_patch_js = json_loadb_report(remote_patch_js_buffer, remote_patch_js_size, 0, main_fn);
	if(!remote_patch_js) {
		// Remote patch_js is invalid!
		ret = 4;
		goto end_update;
	}

	// Buffer is written to a file at the end of the update - after all, the
	// remote patch.js can include more changes than just new file stamps.

	remote_files = json_object_get(remote_patch_js, "files");
	if(!json_is_object(remote_files)) {
		// No "files" object in the remote patch_js
		ret = 5;
		goto end_update;
	}

	remote_servers = ServerInit(remote_patch_js);
	if(json_object_size(remote_patch_js) == 0) {
		// No remote servers...? OK, continue with the local ones
		remote_servers = local_servers;
	}

	// Yay for doubled loops... just to get the correct number
	json_object_foreach(remote_files, key, remote_val) {
		json_t *local_val = json_object_get(local_files, key);
		if(PatchFileRequiresUpdate(patch_info, key, local_val, remote_val)) {
			file_count++;
		}
	}
	if(!file_count) {
		// Nice, we're up-to-date!
		log_printf("Everything up-to-date.\n", file_count);
		ret = 0;
		goto end_update;
	}

	file_digits = str_num_digits(file_count);
	log_printf("Need to get %d files.\n", file_count);

	i = 0;
	json_object_foreach(remote_files, key, remote_val) {
		void *file_buffer;
		DWORD file_size;
		json_t *local_val;
		
		if(!ServerGetNumActive(remote_servers)) {
			ret = 3;
			break;
		}

		local_val = json_object_get(local_files, key);
		if(!PatchFileRequiresUpdate(patch_info, key, local_val, remote_val)) {
			continue;
		}
		i++;

		log_printf("(%*d/%*d) ", file_digits, i, file_digits, file_count);
		file_buffer = ServerDownloadFileA(remote_servers, key, &file_size);
		if(file_buffer) {
			patch_file_store(patch_info, key, file_buffer, file_size);
			SAFE_FREE(file_buffer);
			
			json_object_set(local_files, key, remote_val);
			patch_json_store(patch_info, main_fn, local_patch_js);
		}
	}
	if(i == file_count) {
		patch_file_store(patch_info, main_fn, remote_patch_js_buffer, remote_patch_js_size);
		log_printf("Update completed.\n");
	}

	// I thought 15 minutes about this, and considered it appropriate
end_update:
	if(ret == 3) {
		log_printf("Can't reach any server at the moment.\nCancelling update...\n");
	}
	SAFE_FREE(remote_patch_js_buffer);
	json_decref(remote_patch_js);
	json_decref(local_patch_js);
	return ret;
}