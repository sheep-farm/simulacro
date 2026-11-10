// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: opt-in bridge to an external process (Python, R, awk, ...).
//
// The process is started with `/bin/sh -c command` only when `enable=1`.
// On each step the value of socket 0 is sent as a decimal line on stdin;
// the plugin reads ONE line from stdout and parses the first number. If the process
// does not respond within `timeout_ms`, it is terminated and the output stays at 0; the
// next step tries to restart it. Disabled (default), it opens no process at all
// and returns 0 — so goldens and runs without the bridge do not change.
//
// There is no embedded Python/R: the process is external and separate, with no sandbox. The
// command is arbitrary; execution is the user's responsibility. The command and parameters
// go into the save/load blob; the process itself is restarted after load/F5.
//
// Build: make plugins | Usage: Add menu -> I/O -> External process
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct Proc {
    double enable = 0, timeout_ms = 1000;
    std::string command = "cat";
    std::string allowed; // empty = no allowlist
    pid_t pid = -1;
    int in = -1, out = -1;
    std::string rx;
};

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

void closes(Proc* d) {
    if (!d) return;
    if (d->in >= 0) { close(d->in); d->in = -1; }
    if (d->out >= 0) { close(d->out); d->out = -1; }
    if (d->pid > 0) {
        kill(d->pid, SIGKILL);
        while (waitpid(d->pid, nullptr, 0) < 0 && errno == EINTR) {}
        d->pid = -1;
    }
    d->rx.clear();
}

bool begin(Proc* d) {
    if (!d) return false;
    if (d->pid > 0 && d->in >= 0 && d->out >= 0) return true;
    closes(d);
    int pin[2], pout[2];
    if (pipe(pin) != 0) return false;
    if (pipe(pout) != 0) { close(pin[0]); close(pin[1]); return false; }
    const pid_t pid = fork();
    if (pid < 0) {
        close(pin[0]); close(pin[1]);
        close(pout[0]); close(pout[1]);
        return false;
    }
    if (pid == 0) {
        dup2(pin[0], STDIN_FILENO);
        dup2(pout[1], STDOUT_FILENO);
        const int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) { dup2(nullfd, STDERR_FILENO); close(nullfd); }
        close(pin[0]); close(pin[1]);
        close(pout[0]); close(pout[1]);
        execl("/bin/sh", "sh", "-c", d->command.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    d->pid = pid;
    d->in = pin[1];
    d->out = pout[0];
    const int fl1 = fcntl(d->in, F_GETFL, 0);
    if (fl1 >= 0) fcntl(d->in, F_SETFL, fl1 | O_NONBLOCK);
    const int fl2 = fcntl(d->out, F_GETFL, 0);
    if (fl2 >= 0) fcntl(d->out, F_SETFL, fl2 | O_NONBLOCK);
    d->rx.clear();
    return true;
}

long remainingMs(std::chrono::steady_clock::time_point end) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= end) return 0;
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count() + 1;
}

bool allowed(const Proc* d) {
    if (!d || d->allowed.empty()) return true;
    const std::string& c = d->command;
    size_t i = 0;
    while (i <= d->allowed.size()) {
        size_t j = d->allowed.find_first_of(";," , i);
        std::string p = d->allowed.substr(i, j == std::string::npos ? std::string::npos : j - i);
        const size_t a = p.find_first_not_of(" \t");
        const size_t b = p.find_last_not_of(" \t");
        if (a != std::string::npos) {
            p = p.substr(a, b - a + 1);
            if (!p.empty() && (c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 &&
                                          std::isspace((unsigned char)c[p.size()]))))
                return true;
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return false;
}

bool writeLine(Proc* d, double x) {
    if (!d || d->in < 0) return false;
    char b[64];
    std::snprintf(b, sizeof b, "%.17g\n", x);
    const char* p = b;
    size_t n = std::strlen(b);
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds((long)clampD(d->timeout_ms, 1, 60000));
    while (n > 0) {
        const ssize_t w = write(d->in, p, n);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const long ms = remainingMs(end);
            if (ms <= 0) return false;
            struct pollfd pfd{d->in, POLLOUT, 0};
            const int r = poll(&pfd, 1, (int)ms);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0 || (pfd.revents & (POLLERR | POLLHUP))) return false;
            continue;
        }
        return false;
    }
    return true;
}

bool readLine(Proc* d, std::string& line) {
    if (!d || d->out < 0) return false;
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds((long)clampD(d->timeout_ms, 1, 60000));
    while (true) {
        const size_t pos = d->rx.find('\n');
        if (pos != std::string::npos) {
            line = d->rx.substr(0, pos);
            d->rx.erase(0, pos + 1);
            return true;
        }
        const long ms = remainingMs(end);
        if (ms <= 0) return false;
        struct pollfd pfd{d->out, POLLIN, 0};
        const int r = poll(&pfd, 1, (int)ms);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return false;
        if (r == 0) return false;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            // there may still be bytes in the buffer; try to read before giving up
            if (!(pfd.revents & POLLIN)) return false;
        }
        if (pfd.revents & POLLIN) {
            char buf[4096];
            const ssize_t n = read(d->out, buf, sizeof buf);
            if (n > 0) { d->rx.append(buf, (size_t)n); continue; }
            if (n == 0) return false;
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return false;
        }
    }
}

void* newValue() { return new (std::nothrow) Proc(); }
void dispose(void* h) { closes((Proc*)h); delete (Proc*)h; }

double step(void* h, const NsContext* c) {
    Proc* d = (Proc*)h;
    if (!d) return 0.0;
    if (d->enable <= 0.5) { closes(d); return 0.0; }
    if (!c || c->nin <= 0) return 0.0;
    const double x = sockValue(c, 0);
    if (!std::isfinite(x)) return 0.0;
    if (!allowed(d)) return 0.0;
    if (!begin(d)) return 0.0;
    std::string line;
    if (!writeLine(d, x) || !readLine(d, line)) {
        closes(d);
        return 0.0;
    }
    char* end = nullptr;
    const double y = std::strtod(line.c_str(), &end);
    if (end == line.c_str() || !std::isfinite(y)) {
        closes(d);
        return 0.0;
    }
    return y;
}

const char* const defaultCommand = "cat";

NsParam params[] = {
    {"enable", 0, 1, 0, NS_PARAM_BOOL, 1, nullptr,
     "enable the bridge (runs a shell command; disabled does not open a process)", 0, nullptr},
    {"timeout_ms", 1, 60000, 1000, NS_PARAM_INT, 1, "ms",
     "maximum time to send and receive a line", 0, nullptr},
};
const char* textNames[] = {"command", "allowed"};

void normalize(Proc* d) {
    if (!d) return;
    d->enable = d->enable > 0.5 ? 1.0 : 0.0;
    d->timeout_ms = std::floor(clampD(d->timeout_ms, 1, 60000) + 0.5);
    if (d->command.empty()) d->command = defaultCommand;
}

double getParam(void* h, int i) {
    Proc* d = (Proc*)h;
    if (!d) return 0.0;
    if (i == 0) return d->enable;
    if (i == 1) return d->timeout_ms;
    return 0.0;
}

int setParam(void* h, int i, double v) {
    Proc* d = (Proc*)h;
    if (!d) return 0;
    if (i == 0) {
        const bool newValue = v > 0.5;
        d->enable = newValue ? 1.0 : 0.0;
        if (!newValue) closes(d);
        return 1;
    }
    if (i == 1) { d->timeout_ms = v; normalize(d); return 1; }
    return 0;
}

char* getText(void* h, int i) {
    Proc* d = (Proc*)h;
    if (!d || i < 0 || i > 1) return nullptr;
    const std::string& s = i == 0 ? d->command : d->allowed;
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

int setText(void* h, int i, const char* s) {
    Proc* d = (Proc*)h;
    if (!d || !s || i < 0 || i > 1) return 0;
    if (i == 1) { d->allowed = s; return 1; }
    if (d->command != s) {
        d->command = s;
        if (d->command.empty()) d->command = defaultCommand;
        closes(d);
    }
    return 1;
}

void addField(std::string& s, const char* k, const std::string& v) {
    s += k;
    s += '=';
    s += std::to_string(v.size());
    s += ':';
    s += v;
    s += ';';
}

char* save(void* h) {
    Proc* d = (Proc*)h;
    if (!d) return nullptr;
    std::string s;
    addField(s, "enable", std::to_string((int)d->enable));
    addField(s, "timeout_ms", std::to_string((long)d->timeout_ms));
    addField(s, "command", d->command);
    addField(s, "allowed", d->allowed);
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

bool readField(const char*& p, std::string& k, std::string& v) {
    if (!p || !*p) return false;
    const char* e = p;
    while (*e && *e != '=') ++e;
    if (*e != '=') return false;
    k.assign(p, e - p);
    p = e + 1;
    char* end = nullptr;
    const unsigned long n = std::strtoul(p, &end, 10);
    if (end == p || *end != ':') return false;
    p = end + 1;
    if (std::strlen(p) < n) return false;
    v.assign(p, n);
    p += n;
    if (*p != ';') return false;
    ++p;
    return true;
}

int load(void* h, const char* blob) {
    Proc* d = (Proc*)h;
    if (!d || !blob) return 0;
    double hab = d->enable, timeout = d->timeout_ms;
    std::string command = d->command, allowed = d->allowed;
    const char* p = blob;
    std::string k, v;
    while (readField(p, k, v)) {
        if (k == "enable") hab = std::strtod(v.c_str(), nullptr);
        else if (k == "timeout_ms") timeout = std::strtod(v.c_str(), nullptr);
        else if (k == "command") command = v;
        else if (k == "allowed") allowed = v;
    }
    closes(d);
    d->enable = hab;
    d->timeout_ms = timeout;
    d->command = command.empty() ? defaultCommand : command;
    d->allowed = allowed;
    normalize(d);
    return 1;
}

char* labelIn(void*, int p) {
    if (p != 0) return nullptr;
    char* r = (char*)std::malloc(2);
    if (r) std::memcpy(r, "x", 2);
    return r;
}

char* labelOut(void*, int p) {
    if (p != 0) return nullptr;
    char* r = (char*)std::malloc(2);
    if (r) std::memcpy(r, "y", 2);
    return r;
}

NsNodeType types[] = {
    [] {
        NsNodeType t{};
        t.id = "proc";
        t.label = "External process";
        t.color[0] = 210;
        t.color[1] = 160;
        t.color[2] = 130;
        t.create = newValue;
        t.destroy = dispose;
        t.step = step;
        t.save = save;
        t.load = load;
        t.params = params;
        t.num_params = 2;
        t.getParam = getParam;
        t.setParam = setParam;
        t.text_names = textNames;
        t.num_text = 2;
        t.getText = getText;
        t.setText = setText;
        t.def_in = 1;
        t.portLabel = labelOut;
        t.portLabelIn = labelIn;
        t.capabilities = NS_CAP_INPUT_LABELS;
        return t;
    }(),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("proc", "1.0", 1, types, "I/O");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
