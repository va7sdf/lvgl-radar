/* Health of the Pi itself, from /proc and /sys. */

#define _DEFAULT_SOURCE
#include "pistats.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

static unsigned long long prev_total, prev_idle, prev_app;

static double read_double(const char *path, double scale)
{
    FILE *f = fopen(path, "r");
    double v = NAN;
    if (f) {
        if (fscanf(f, "%lf", &v) != 1)
            v = NAN;
        fclose(f);
    }
    return isnan(v) ? v : v * scale;
}

static void cpu(pistats_t *s)
{
    unsigned long long u, n, sy, id, io, irq, sirq, st = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return;
    int got = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &sy, &id, &io, &irq, &sirq, &st);
    fclose(f);
    if (got < 7)
        return;
    unsigned long long total = u + n + sy + id + io + irq + sirq + st;
    unsigned long long idle = id + io;

    /* This program: utime + stime are fields 14 and 15 of /proc/self/stat */
    unsigned long long app = 0;
    f = fopen("/proc/self/stat", "r");
    if (f) {
        char buf[1024];
        if (fgets(buf, sizeof buf, f)) {
            char *p = strrchr(buf, ')');            /* skip "pid (comm)" */
            unsigned long long ut, stt;
            if (p && sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &stt) == 2)
                app = ut + stt;
        }
        fclose(f);
    }

    if (prev_total && total > prev_total) {
        double dt = (double)(total - prev_total);
        s->cpu_pct = 100.0 * (1.0 - (double)(idle - prev_idle) / dt);
        /* total counts every core; express the app as % of one core */
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        s->app_pct = 100.0 * (double)(app - prev_app) / dt * (cores > 0 ? cores : 1);
    } else {
        s->cpu_pct = s->app_pct = NAN;
    }
    prev_total = total;
    prev_idle = idle;
    prev_app = app;
}

static void memory(pistats_t *s)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return;
    char key[64];
    long kb;
    while (fscanf(f, "%63s %ld kB\n", key, &kb) == 2) {
        if (!strcmp(key, "MemTotal:"))
            s->mem_total_mb = kb / 1024;
        else if (!strcmp(key, "MemAvailable:"))
            s->mem_avail_mb = kb / 1024;
    }
    fclose(f);
}

static void throttling(pistats_t *s)
{
    /* Raspberry Pi kernels expose this; otherwise ask vcgencmd */
    FILE *f = fopen("/sys/devices/platform/soc/soc:firmware/get_throttled", "r");
    if (f) {
        s->throttle_known = fscanf(f, "%x", &s->throttled) == 1;
        fclose(f);
        if (s->throttle_known)
            return;
    }
    f = popen("vcgencmd get_throttled 2>/dev/null", "r");
    if (f) {
        s->throttle_known = fscanf(f, "throttled=%x", &s->throttled) == 1;
        pclose(f);
    }
}

static void network(pistats_t *s)
{
    struct ifaddrs *ifs;
    s->ip[0] = '\0';
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK))
                continue;
            char addr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, addr, sizeof addr);
            snprintf(s->ip, sizeof s->ip, "%s %s", i->ifa_name, addr);
            break;
        }
        freeifaddrs(ifs);
    }

    /* Wi-Fi signal: /proc/net/wireless "wlan0: 0000   54.  -56.  -256 ..." */
    s->wifi_dbm = 0;
    FILE *f = fopen("/proc/net/wireless", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char name[32];
            double link, level;
            if (sscanf(line, " %31[^:]: %*x %lf %lf", name, &link, &level) == 3) {
                s->wifi_dbm = (int)lround(level);
                break;
            }
        }
        fclose(f);
    }
}

/* The SSID needs an outside tool; ask every 30 s at most */
static void wifi_ssid(pistats_t *s)
{
    static char cached[64];
    static time_t asked;
    time_t now = time(NULL);
    if (now - asked >= 30) {
        asked = now;
        cached[0] = '\0';
        FILE *f = popen("(iwgetid -r || nmcli -t -f active,ssid dev wifi | sed -n 's/^yes://p' "
                        "|| iw dev wlan0 link | sed -n 's/^[[:space:]]*SSID: //p') 2>/dev/null | head -1", "r");
        if (f) {
            if (fgets(cached, sizeof cached, f))
                cached[strcspn(cached, "\r\n")] = '\0';
            pclose(f);
        }
    }
    snprintf(s->ssid, sizeof s->ssid, "%s", cached);
}

void pistats_read(pistats_t *s)
{
    memset(s, 0, sizeof *s);
    s->temp_c = read_double("/sys/class/thermal/thermal_zone0/temp", 0.001);
    cpu(s);
    memory(s);
    throttling(s);
    network(s);
    if (s->wifi_dbm)
        wifi_ssid(s);

    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &s->load1) != 1)
            s->load1 = NAN;
        fclose(f);
    }
    double up = read_double("/proc/uptime", 1.0);
    s->uptime_s = isnan(up) ? 0 : (long)up;

    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0)
        s->disk_free_mb = (long)((unsigned long long)vfs.f_bavail * vfs.f_frsize / (1024 * 1024));

    gethostname(s->host, sizeof s->host - 1);
}

const char *pistats_throttle_text(const pistats_t *s)
{
    if (!s->throttle_known)
        return "unknown";
    unsigned t = s->throttled;
    if (t & 0x1)
        return "UNDER-VOLTAGE now";
    if (t & 0x4)
        return "throttled now";
    if (t & 0x2)
        return "speed capped now";
    if (t & 0x8)
        return "too hot now";
    if (t & 0x10000)
        return "OK (under-voltage earlier)";
    if (t & 0x40000)
        return "OK (throttled earlier)";
    return "OK";
}
