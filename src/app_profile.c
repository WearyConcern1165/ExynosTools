#include "logging.h"
#include "app_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void load_env_file(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[1024];
	XENO_LOGI("app_profile: applying %s", path);
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\n') continue;
		char key[512], val[512];
		if (sscanf(line, "%511[^=]=%511[^\n]", key, val) == 2) {
			setenv(key, val, 1);
		}
	}
	fclose(f);
}

static void basename_noext(const char* in, char* out, size_t outsz) {
	const char* base = strrchr(in, '/');
	base = base ? base + 1 : in;
	strncpy(out, base, outsz-1);
	out[outsz-1] = 0;
	char* dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

void xeno_app_profile_apply(void) {
	const char* forced = getenv("EXYNOSTOOLS_APP_PROFILE");
	if (forced && *forced) {
		load_env_file(forced);
		return;
	}
	char exe[512]={0};
	if (readlink("/proc/self/exe", exe, sizeof(exe)-1) > 0) {
		char base[256]; basename_noext(exe, base, sizeof(base));
		char try1[512]; snprintf(try1, sizeof(try1), "profiles/winlator/%s.env", base);
		load_env_file(try1);
	}
	// fallback to generic profiles
	load_env_file("profiles/winlator/dxvk.env");
	load_env_file("profiles/winlator/vkd3d.env");
}