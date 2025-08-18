#include "logging.h"
#include "app_profile.h"
#include "perf_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void basename_noext(const char* in, char* out, size_t outsz) {
	const char* base = strrchr(in, '/');
	base = base ? base + 1 : in;
	strncpy(out, base, outsz-1);
	out[outsz-1] = 0;
	char* dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

static void load_profile_conf(const char* path) {
	XenoPerfConf tmp;
	xeno_perf_conf_load(path, &tmp);
	if (tmp.sync_mode == XENO_SYNC_AGGRESSIVE) setenv("EXYNOSTOOLS_SYNC", "aggressive", 1);
	else if (tmp.sync_mode == XENO_SYNC_BALANCED) setenv("EXYNOSTOOLS_SYNC", "balanced", 1);
	else setenv("EXYNOSTOOLS_SYNC", "safe", 1);
}

void xeno_app_profile_apply(void) {
	const char* forced = getenv("EXYNOSTOOLS_APP_PROFILE");
	if (forced && *forced) { load_profile_conf(forced); return; }
	char exe[512]={0};
	if (readlink("/proc/self/exe", exe, sizeof(exe)-1) > 0) {
		char base[256]; basename_noext(exe, base, sizeof(base));
		char try1[512]; snprintf(try1, sizeof(try1), "/etc/exynostools/profiles/%s.conf", base);
		load_profile_conf(try1);
	}
}