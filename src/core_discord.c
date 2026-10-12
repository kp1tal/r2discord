/* r2discord - MIT - Copyright 2026 - kp1tal */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <time.h>
#include <r_core.h>
#include "presence.h"
#include "worker.h"

#define R2D_DEFAULT_APPID "1559252980589535242"
#define R2D_DEFAULT_IMAGE "r2"
#define R2D_DEFAULT_IDLE 300

static RCoreHelpMessage help_msg = {
	"Usage:", "discord", " Discord Rich Presence",
	"discord", "", "show connection status and the last presence sent",
	"discord?", "", "show this help",
	"e discord.enabled", "=<bool>", "turn the presence on/off",
	"e discord.showname", "=<bool>", "show file and function names (off by default)",
	"e discord.appid", "=<id>", "Discord application id",
	"e discord.image", "=<key>", "large image asset key",
	"e discord.idle", "=<secs>", "seconds without commands before showing Idle (0 = never)",
	NULL
};

typedef struct {
	R2DWorker *w;
	bool enabled;
	bool showname;
	bool ready; // set once all config vars exist
	// change detection for the hot path
	bool valid;
	R2DMode mode;
	ut32 fileid; // RBinFile id + 1, 0 = no file
	ut64 fcn;
	// cache used only by function_at
	bool seek_cached;
	ut64 seek;
	ut64 seek_fcn;
	ut64 start;
} R2DCtx;

// RConfig callbacks only get the RCore, so the context is global; one r2
// instance per process is the normal case.
static R2DCtx *G = NULL;

static bool cb_enabled(void *user, void *data) {
	(void)user;
	RConfigNode *n = data;
	if (G) {
		G->enabled = n->i_value;
		G->valid = false;
		if (!G->enabled) {
			r2d_worker_clear (G->w);
		}
	}
	return true;
}

static bool cb_showname(void *user, void *data) {
	(void)user;
	RConfigNode *n = data;
	if (G) {
		G->showname = n->i_value;
		G->valid = false;
		G->seek_cached = false;
	}
	return true;
}

static bool cb_appid(void *user, void *data) {
	RCore *core = user;
	RConfigNode *n = data;
	// Discord application ids are numeric snowflakes
	const char *v = n->value;
	size_t len = v? strlen (v): 0;
	if (len < 1 || len >= 32 || strspn (v, "0123456789") != len) {
		return false;
	}
	if (G && G->ready) {
		r2d_worker_set_app (G->w, n->value, r_config_get (core->config, "discord.image"));
	}
	return true;
}

static bool cb_image(void *user, void *data) {
	RCore *core = user;
	RConfigNode *n = data;
	if (G && G->ready) {
		r2d_worker_set_app (G->w, r_config_get (core->config, "discord.appid"), n->value);
	}
	return true;
}

static bool cb_idle(void *user, void *data) {
	(void)user;
	RConfigNode *n = data;
	if (G) {
		r2d_worker_set_idle (G->w, (int)n->i_value);
	}
	return true;
}

static ut64 function_at(RCore *core, R2DCtx *c) {
	if (c->seek_cached && c->seek == core->addr) {
		return c->seek_fcn;
	}
	ut64 addr = UT64_MAX;
	RList *l = r_anal_get_functions_in (core->anal, core->addr);
	RAnalFunction *f = l? r_list_first (l): NULL;
	if (f) {
		addr = f->addr;
	}
	r_list_free (l);
	c->seek = core->addr;
	c->seek_fcn = addr;
	c->seek_cached = true;
	return addr;
}

static void refresh(RCore *core, R2DCtx *c) {
	RBinFile *bf = r_bin_cur (core->bin);
	R2DMode mode = !bf? R2D_MODE_NOFILE
		: r_config_get_b (core->config, "cfg.debug")? R2D_MODE_DEBUG
		: core->vmode? R2D_MODE_VISUAL
		: R2D_MODE_PROMPT;
	ut32 fileid = bf? bf->id + 1: 0;
	ut64 fcn = (c->showname && mode == R2D_MODE_VISUAL)? function_at (core, c): UT64_MAX;
	if (c->valid && mode == c->mode && fileid == c->fileid && fcn == c->fcn) {
		return;
	}
	if (mode != c->mode || fileid != c->fileid) {
		c->seek_cached = false;
	}
	if (!c->start || fileid != c->fileid) {
		c->start = (ut64)time (NULL);
	}
	c->valid = true;
	c->mode = mode;
	c->fileid = fileid;
	c->fcn = fcn;

	RBinInfo *info = bf? r_bin_get_info (core->bin): NULL;
	RAnalFunction *f = fcn != UT64_MAX? r_anal_get_function_at (core->anal, fcn): NULL;
	R2DPresenceInput in = {
		.mode = mode,
		.format = info? info->rclass: NULL,
		.arch = info? info->arch: NULL,
		.bits = info? info->bits: 0,
		.filename = (bf && bf->file)? r_file_basename (bf->file): NULL,
		.function = f? f->name: NULL,
		.showname = c->showname,
	};
	R2DText t;
	r2d_presence_text (&in, &t);
	r2d_worker_update (c->w, &t, c->start);
}

static bool is_discord_cmd(const char *input) {
	return r_str_startswith (input, "discord")
		&& (!input[7] || input[7] == '?' || input[7] == ' ');
}

static bool r2d_call(RCorePluginSession *s, const char *input) {
	R2DCtx *c = s->data;
	if (!c) {
		return false;
	}
	RCore *core = s->core;
	if (is_discord_cmd (input)) {
		if (input[7] == '?') {
			r_cons_cmd_help (core->cons, help_msg);
		} else {
			char buf[1024];
			r2d_worker_status (c->w, buf, sizeof buf);
			r_cons_println (core->cons, buf);
		}
		return true;
	}
	if (c->enabled && r_cons_is_interactive (core->cons)) {
		r2d_worker_touch (c->w);
		refresh (core, c);
	}
	return false;
}

static RConfigNode *setdesc(RConfigNode *n, const char *desc) {
	if (n) {
		r_config_node_desc (n, desc);
	}
	return n;
}

static bool r2d_init(RCorePluginSession *s) {
	if (G) {
		return true; // already running for another RCore in this process
	}
	R2DCtx *c = R_NEW0 (R2DCtx);
	if (!c) {
		return false;
	}
	R2DWorkerTiming t = { 4000, 5000, 60000 };
	c->w = r2d_worker_new (&t);
	if (!c->w) {
		free (c);
		return false;
	}
	c->enabled = true;
	c->fcn = UT64_MAX;
	G = c;
	s->data = c;
	r2d_worker_set_app (c->w, R2D_DEFAULT_APPID, R2D_DEFAULT_IMAGE);
	r2d_worker_set_idle (c->w, R2D_DEFAULT_IDLE);

	RConfig *cfg = s->core->config;
	bool was_locked = cfg->lock;
	r_config_lock (cfg, false);
	setdesc (r_config_set_cb (cfg, "discord.enabled", "true", cb_enabled),
		"show radare2 activity as Discord Rich Presence");
	setdesc (r_config_set_cb (cfg, "discord.showname", "false", cb_showname),
		"include file and function names in the presence");
	setdesc (r_config_set_cb (cfg, "discord.appid", R2D_DEFAULT_APPID, cb_appid),
		"Discord application id used for the presence");
	setdesc (r_config_set_cb (cfg, "discord.image", R2D_DEFAULT_IMAGE, cb_image),
		"large image asset key of the Discord application");
	setdesc (r_config_set_i_cb (cfg, "discord.idle", R2D_DEFAULT_IDLE, cb_idle),
		"seconds without commands before showing Idle (0 = never)");
	r_config_lock (cfg, was_locked);
	c->ready = true;
	return true;
}

static bool r2d_fini(RCorePluginSession *s) {
	R2DCtx *c = s->data;
	if (!c) {
		return true;
	}
	if (!r2d_worker_free (c->w)) {
		// The worker thread is still running code in this library. Pin the
		// .so so the dlclose that follows fini does not unmap it.
		Dl_info di;
		if (dladdr ((void *)r2d_fini, &di) && di.dli_fname) {
			dlopen (di.dli_fname, RTLD_NOW | RTLD_NODELETE);
		}
	}
	if (G == c) {
		G = NULL;
		// The setters point into this library; drop the vars before it goes.
		RConfig *cfg = s->core->config;
		bool was = cfg->lock;
		r_config_lock (cfg, false);
		r_config_rm (cfg, "discord.enabled");
		r_config_rm (cfg, "discord.showname");
		r_config_rm (cfg, "discord.appid");
		r_config_rm (cfg, "discord.image");
		r_config_rm (cfg, "discord.idle");
		r_config_lock (cfg, was);
	}
	free (c);
	s->data = NULL;
	return true;
}

RCorePlugin r_core_plugin_discord = {
	.meta = {
		.name = "discord",
		.desc = "Discord Rich Presence for radare2",
		.author = "kp1tal",
		.license = "MIT",
	},
	.init = r2d_init,
	.fini = r2d_fini,
	.call = r2d_call,
};

#ifndef R2_PLUGIN_INCORE
R_API RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_CORE,
	.data = &r_core_plugin_discord,
	.version = R2_VERSION,
	.abiversion = R2_ABIVERSION,
};
#endif
