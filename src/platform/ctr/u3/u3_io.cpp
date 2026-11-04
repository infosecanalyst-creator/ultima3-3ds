// u3_io.cpp - background SD card writer (see u3_io.h)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "u3_io.h"
#include "u3_ui.h"
#include <3ds.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

struct Job { std::string path; std::vector<uint8_t> data; bool append; };

static LightLock lk;
static LightEvent ev_work;      // oneshot: work was queued
static LightEvent ev_idle;      // sticky: nothing outstanding
static std::deque<Job> jobs;
static Job cur;                 // job being written right now
static bool cur_active = false;
static uint32_t busy_since = 0;
static bool started = false;
static Thread th = 0;

static bool same_path(const std::string &a, const char *b) { return strcasecmp(a.c_str(), b) == 0; }

// ------------------------------------------------------------------ writer thread
static FILE *open_retry(const char *path, const char *mode) {
	// The real 3DS can refuse a file another thread has open; try a few times.
	for (int i = 0; i < 5; i++) {
		FILE *f = fopen(path, mode);
		if (f) return f;
		svcSleepThread(40 * 1000000LL);
	}
	return 0;
}

static void write_job(const Job &j) {
	if (j.append) {
		FILE *f = open_retry(j.path.c_str(), "ab");
		if (!f) return;
		fwrite(j.data.data(), 1, j.data.size(), f);
		fclose(f);
		return;
	}
	// Rewrite in place ("r+b", truncate only if it shrank): much cheaper than "wb" on the 3DS card.
	FILE *f = fopen(j.path.c_str(), "r+b");
	long old = -1;
	if (f) { fseek(f, 0, SEEK_END); old = ftell(f); fseek(f, 0, SEEK_SET); }
	else f = open_retry(j.path.c_str(), "wb");
	if (!f) return;
	if (!j.data.empty()) fwrite(j.data.data(), 1, j.data.size(), f);
	fflush(f);
	if (old > (long)j.data.size()) ftruncate(fileno(f), (off_t)j.data.size());
	fclose(f);
}

static void writer_main(void *) {
	for (;;) {
		LightLock_Lock(&lk);
		if (jobs.empty()) {
			cur_active = false;
			busy_since = 0;
			LightEvent_Signal(&ev_idle);
			LightLock_Unlock(&lk);
			LightEvent_WaitTimeout(&ev_work, 1000000000LL);
			continue;
		}
		cur = jobs.front();
		jobs.pop_front();
		cur_active = true;
		LightLock_Unlock(&lk);
		write_job(cur);   // cur is only changed by this thread, so readers can copy it under the lock
		LightLock_Lock(&lk);
		cur_active = false;
		cur.data.clear(); cur.path.clear();
		LightLock_Unlock(&lk);
	}
}

void u3w_init() {
	if (started) return;
	LightLock_Init(&lk);
	LightEvent_Init(&ev_work, RESET_ONESHOT);
	LightEvent_Init(&ev_idle, RESET_STICKY);
	LightEvent_Signal(&ev_idle);
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	// On the game's own core (-2), one priority step above it. On the system core it never ran on hardware.
	th = threadCreate(writer_main, 0, 32 * 1024, prio > 0x18 ? prio - 1 : prio, -2, true);
	started = th != 0;
}

static void enqueue(Job &j) {
	if (!started) u3w_init();
	if (!started) { write_job(j); return; }   // no thread: write directly rather than lose it
	LightLock_Lock(&lk);
	bool merged = false;
	if (!j.append)
		for (size_t i = 0; i < jobs.size(); i++)
			if (!jobs[i].append && same_path(jobs[i].path, j.path.c_str())) { jobs[i].data.swap(j.data); merged = true; break; }
	if (!merged) jobs.push_back(j);
	if (!busy_since) busy_since = u3_ticks_ms();
	LightEvent_Clear(&ev_idle);
	LightLock_Unlock(&lk);
	LightEvent_Signal(&ev_work);
}

void u3w_put(const char *path, const void *data, size_t n) {
	Job j; j.path = path; j.append = false;
	j.data.assign((const uint8_t *)data, (const uint8_t *)data + n);
	enqueue(j);
}

void u3w_append(const char *path, const char *text) {
	Job j; j.path = path; j.append = true;
	j.data.assign((const uint8_t *)text, (const uint8_t *)text + strlen(text));
	enqueue(j);
}

uint32_t u3w_busy_ms() {
	if (!started) return 0;
	LightLock_Lock(&lk);
	uint32_t b = busy_since;
	LightLock_Unlock(&lk);
	return b ? u3_ticks_ms() - b : 0;
}

void u3w_flush(int timeout_ms) {
	if (!started) return;
	LightEvent_WaitTimeout(&ev_idle, (s64)timeout_ms * 1000000LL);
}

// The newest queued (or in-progress) contents for a file, if any.
static bool pending(const char *path, std::vector<uint8_t> *out, size_t *size) {
	if (!started) return false;
	bool found = false;
	LightLock_Lock(&lk);
	for (int i = (int)jobs.size() - 1; i >= 0; i--)
		if (!jobs[i].append && same_path(jobs[i].path, path)) {
			if (out) *out = jobs[i].data;
			if (size) *size = jobs[i].data.size();
			found = true; break;
		}
	if (!found && cur_active && !cur.append && same_path(cur.path, path)) {
		if (out) *out = cur.data;
		if (size) *size = cur.data.size();
		found = true;
	}
	LightLock_Unlock(&lk);
	return found;
}

// ------------------------------------------------------------------ memory-backed FILE for game files
struct MemF { std::string path; std::vector<uint8_t> d; size_t pos; bool writable, dirty; FILE *f; };
static std::map<FILE *, MemF *> memfiles;   // open memory files (emulation thread only)

static ssize_t mf_read(void *c, char *buf, size_t n) {
	MemF *m = (MemF *)c;
	if (m->pos >= m->d.size()) return 0;
	size_t k = m->d.size() - m->pos; if (k > n) k = n;
	memcpy(buf, &m->d[m->pos], k); m->pos += k;
	return (ssize_t)k;
}
static ssize_t mf_write(void *c, const char *buf, size_t n) {
	MemF *m = (MemF *)c;
	if (!m->writable) return -1;
	if (m->pos + n > m->d.size()) m->d.resize(m->pos + n);
	memcpy(&m->d[m->pos], buf, n); m->pos += n; m->dirty = true;
	return (ssize_t)n;
}
template <typename OFF> static int mf_seek(void *c, OFF *off, int whence) {
	MemF *m = (MemF *)c;
	long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)m->pos : (long long)m->d.size();
	long long np = base + (long long)*off;
	if (np < 0) return -1;
	m->pos = (size_t)np; *off = (OFF)np;
	return 0;
}
static int mf_close(void *c) {
	MemF *m = (MemF *)c;
	if (m->writable && m->dirty) u3w_put(m->path.c_str(), m->d.data(), m->d.size());
	if (m->f) memfiles.erase(m->f);
	delete m;
	return 0;
}

static FILE *mem_open(const char *path, std::vector<uint8_t> &data, bool writable, bool dirty, const char *mode) {
	MemF *m = new MemF;
	m->path = path; m->d.swap(data); m->pos = 0; m->writable = writable; m->dirty = dirty; m->f = 0;
	cookie_io_functions_t fn;
	fn.read = mf_read; fn.write = mf_write; fn.seek = mf_seek; fn.close = mf_close;
	FILE *f = fopencookie(m, mode, fn);
	if (!f) { delete m; return 0; }
	setvbuf(f, 0, _IONBF, 0);
	m->f = f;
	memfiles[f] = m;
	return f;
}

static bool is_game_path(const char *path) {
	static const char pre[] = U3_GAME_DIR "/";
	return strncasecmp(path, pre, sizeof pre - 1) == 0;
}

FILE *u3w_fopen(const char *path, const char *mode) {
	if (!is_game_path(path)) return fopen(path, mode);
	bool w = strchr(mode, 'w') || strchr(mode, '+') || strchr(mode, 'a');
	std::vector<uint8_t> data;
	bool has = pending(path, &data, 0);
	if (!w) return has ? mem_open(path, data, false, false, "rb") : fopen(path, mode);
	if (mode[0] == 'w') { data.clear(); return mem_open(path, data, true, true, "wb+"); }
	if (!has) {
		FILE *rf = fopen(path, "rb");
		if (!rf) return 0;
		fseek(rf, 0, SEEK_END); long n = ftell(rf); fseek(rf, 0, SEEK_SET);
		if (n > 2 * 1024 * 1024) { fclose(rf); return fopen(path, mode); }   // too big to hold: use the card
		data.resize(n > 0 ? (size_t)n : 0);
		if (n > 0) fread(&data[0], 1, (size_t)n, rf);
		fclose(rf);
	}
	FILE *f = mem_open(path, data, true, false, "rb+");
	if (f && mode[0] == 'a') fseek(f, 0, SEEK_END);
	return f;
}

int u3w_ftruncate(FILE *f, long len) {
	std::map<FILE *, MemF *>::iterator it = memfiles.find(f);
	if (it == memfiles.end()) return ftruncate(fileno(f), (off_t)len);
	fflush(f);
	it->second->d.resize(len < 0 ? 0 : (size_t)len);
	it->second->dirty = true;
	return 0;
}

int u3w_stat(const char *path, struct stat *st) {
	size_t n = 0;
	if (is_game_path(path) && pending(path, 0, &n)) {
		memset(st, 0, sizeof *st);
		st->st_mode = S_IFREG | 0666;
		st->st_size = (off_t)n;
		st->st_mtime = time(0);
		return 0;
	}
	return stat(path, st);
}

int u3w_unlink(const char *path) {
	bool dropped = false;
	if (started) {
		LightLock_Lock(&lk);
		for (size_t i = 0; i < jobs.size();)
			if (!jobs[i].append && same_path(jobs[i].path, path)) { jobs.erase(jobs.begin() + i); dropped = true; }
			else i++;
		bool writing = cur_active && same_path(cur.path, path);
		LightLock_Unlock(&lk);
		if (writing) u3w_flush(5000);
	}
	int r = unlink(path);
	return (r != 0 && dropped) ? 0 : r;
}

int u3w_rename(const char *from, const char *to) {
	if (is_game_path(from) && pending(from, 0, 0)) u3w_flush(5000);
	return rename(from, to);
}
