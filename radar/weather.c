/* Current conditions and a short forecast from Open-Meteo (open-meteo.com),
   free for non-commercial use with attribution (CC BY 4.0). */

#include "weather.h"

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char            url[512];
static int             period_s;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static weather_t       latest;

const char *weather_text(int code)
{
    switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "";
    }
}

static double num(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

static double num_at(const cJSON *obj, const char *key, int i)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    const cJSON *v = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, i) : NULL;
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

/* Weather Icons codepoints (Private Use Area, U+F0xx) */
enum {
    WI_DAY_SUNNY = 0xf00d, WI_NIGHT_CLEAR = 0xf02e,
    WI_DAY_SUNNY_OVERCAST = 0xf00c, WI_NIGHT_PARTLY_CLOUDY = 0xf081,
    WI_DAY_CLOUDY = 0xf002, WI_NIGHT_CLOUDY = 0xf086, WI_CLOUDY = 0xf013,
    WI_DAY_FOG = 0xf003, WI_NIGHT_FOG = 0xf04a,
    WI_DAY_SPRINKLE = 0xf00b, WI_NIGHT_SPRINKLE = 0xf02b,
    WI_DAY_SLEET = 0xf0b2, WI_NIGHT_SLEET = 0xf0b4,
    WI_DAY_RAIN = 0xf008, WI_NIGHT_RAIN = 0xf028,
    WI_DAY_RAIN_MIX = 0xf006, WI_NIGHT_RAIN_MIX = 0xf026,
    WI_DAY_SNOW = 0xf00a, WI_NIGHT_SNOW = 0xf02a, WI_SNOWFLAKE = 0xf076,
    WI_DAY_SHOWERS = 0xf009, WI_NIGHT_SHOWERS = 0xf029,
    WI_DAY_THUNDER = 0xf010, WI_NIGHT_THUNDER = 0xf02d,
    WI_DAY_STORM_SHOWERS = 0xf00e, WI_NIGHT_STORM_SHOWERS = 0xf02c,
    WI_NA = 0xf07b,
};

const char *weather_icon(int code, bool day)
{
    int cp;
#define DN(d, n) cp = day ? (d) : (n); break
    switch (code) {
    case 0:                     DN(WI_DAY_SUNNY, WI_NIGHT_CLEAR);
    case 1:                     DN(WI_DAY_SUNNY_OVERCAST, WI_NIGHT_PARTLY_CLOUDY);
    case 2:                     DN(WI_DAY_CLOUDY, WI_NIGHT_CLOUDY);
    case 3:                     DN(WI_CLOUDY, WI_CLOUDY);
    case 45: case 48:           DN(WI_DAY_FOG, WI_NIGHT_FOG);
    case 51: case 53: case 55:  DN(WI_DAY_SPRINKLE, WI_NIGHT_SPRINKLE);
    case 56: case 57:           DN(WI_DAY_SLEET, WI_NIGHT_SLEET);
    case 61: case 63: case 65:  DN(WI_DAY_RAIN, WI_NIGHT_RAIN);
    case 66: case 67:           DN(WI_DAY_RAIN_MIX, WI_NIGHT_RAIN_MIX);
    case 71: case 73: case 75:
    case 85: case 86:           DN(WI_DAY_SNOW, WI_NIGHT_SNOW);
    case 77:                    DN(WI_SNOWFLAKE, WI_SNOWFLAKE);
    case 80: case 81: case 82:  DN(WI_DAY_SHOWERS, WI_NIGHT_SHOWERS);
    case 95:                    DN(WI_DAY_THUNDER, WI_NIGHT_THUNDER);
    case 96: case 99:           DN(WI_DAY_STORM_SHOWERS, WI_NIGHT_STORM_SHOWERS);
    default:                    DN(WI_NA, WI_NA);
    }
#undef DN
    /* Three-byte UTF-8; one buffer per call site is plenty here */
    static char buf[8][4];
    static int next;
    char *s = buf[next++ % 8];
    s[0] = (char)(0xE0 | (cp >> 12));
    s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    s[2] = (char)(0x80 | (cp & 0x3F));
    s[3] = '\0';
    return s;
}

bool weather_parse(const char *json, weather_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (cJSON_IsObject(cur)) {
        out->temp_c = num(cur, "temperature_2m");
        out->humidity = (int)lround(num(cur, "relative_humidity_2m"));
        out->code = (int)lround(num(cur, "weather_code"));
        out->wind_kt = num(cur, "wind_speed_10m");
        out->gust_kt = num(cur, "wind_gusts_10m");
        out->wind_dir = (int)lround(num(cur, "wind_direction_10m"));
        out->pressure_hpa = num(cur, "pressure_msl");
        double day = num(cur, "is_day");
        out->is_day = isnan(day) ? -1 : day != 0;
        out->valid = !isnan(out->temp_c);
    }

    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const cJSON *times = cJSON_IsObject(daily) ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
    if (cJSON_IsArray(times)) {
        static const char *names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        int n = cJSON_GetArraySize(times);
        for (int i = 0; i < n && i < WEATHER_DAYS; i++) {
            const cJSON *t = cJSON_GetArrayItem(times, i);
            int y, m, d;
            if (!cJSON_IsString(t) || sscanf(t->valuestring, "%d-%d-%d", &y, &m, &d) != 3)
                break;
            struct tm tm = { .tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_hour = 12 };
            timegm(&tm);                                /* fills in tm_wday */
            snprintf(out->day[i], sizeof out->day[i], "%s", names[tm.tm_wday]);
            out->day_code[i] = (int)lround(num_at(daily, "weather_code", i));
            out->day_max[i] = num_at(daily, "temperature_2m_max", i);
            out->day_min[i] = num_at(daily, "temperature_2m_min", i);
            double pop = num_at(daily, "precipitation_probability_max", i);
            out->day_pop[i] = isnan(pop) ? -1 : (int)lround(pop);
            out->n_days = i + 1;
        }
    }
    cJSON_Delete(root);
    return out->valid;
}

typedef struct {
    char  *data;
    size_t len;
} buffer_t;

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    buffer_t *buf = userdata;
    size_t n = size * nmemb;
    char *grown = realloc(buf->data, buf->len + n + 1);
    if (!grown)
        return 0;
    buf->data = grown;
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return n;
}

static void *weather_thread(void *arg)
{
    (void)arg;
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "radar-display/0.3");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    for (;;) {
        buffer_t buf = { 0 };
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        weather_t w;
        bool ok = rc == CURLE_OK && status == 200 && buf.data && weather_parse(buf.data, &w);
        if (ok) {
            w.fetched = time(NULL);
            pthread_mutex_lock(&lock);
            latest = w;
            pthread_mutex_unlock(&lock);
        } else {
            fprintf(stderr, "weather: fetch failed (%s, HTTP %ld)\n",
                    rc == CURLE_OK ? "ok" : curl_easy_strerror(rc), status);
        }
        free(buf.data);
        sleep(ok ? (unsigned)period_s : 120);      /* retry sooner after a failure */
    }
    return NULL;
}

void weather_start(const char *weather_url, int every_s)
{
    snprintf(url, sizeof url, "%s", weather_url ? weather_url : "");
    period_s = every_s > 60 ? every_s : 900;
    if (!url[0])
        return;
    pthread_t tid;
    if (pthread_create(&tid, NULL, weather_thread, NULL) == 0)
        pthread_detach(tid);
}

void weather_set(const weather_t *w)
{
    pthread_mutex_lock(&lock);
    latest = *w;
    pthread_mutex_unlock(&lock);
}

bool weather_get(weather_t *out)
{
    pthread_mutex_lock(&lock);
    *out = latest;
    pthread_mutex_unlock(&lock);
    return out->valid;
}

/* ---------- METAR ---------- */

static char      metar_url[600];
static metar_t   metar_latest;

bool metar_parse(const char *json, metar_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    const cJSON *first = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
    if (cJSON_IsObject(first)) {
        const cJSON *raw = cJSON_GetObjectItemCaseSensitive(first, "rawOb");
        const cJSON *cat = cJSON_GetObjectItemCaseSensitive(first, "fltCat");
        if (cJSON_IsString(raw) && raw->valuestring) {
            const char *r = raw->valuestring;
            if (!strncmp(r, "METAR ", 6))
                r += 6;
            snprintf(out->raw, sizeof out->raw, "%s", r);
            out->valid = true;
        }
        if (cJSON_IsString(cat) && cat->valuestring)
            snprintf(out->category, sizeof out->category, "%s", cat->valuestring);
    }
    cJSON_Delete(root);
    return out->valid;
}

static void *metar_thread(void *arg)
{
    (void)arg;
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, metar_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "radar-display/0.4");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    for (;;) {
        buffer_t buf = { 0 };
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        metar_t m;
        bool ok = curl_easy_perform(curl) == CURLE_OK && buf.data && metar_parse(buf.data, &m);
        if (ok) {
            m.fetched = time(NULL);
            metar_set(&m);
        }
        free(buf.data);
        sleep(ok ? 600 : 120);
    }
    return NULL;
}

static char metar_tmpl[512] = "https://aviationweather.gov/api/data/metar?ids={station}&format=json";

void metar_set_source(const char *url_template)
{
    if (url_template && url_template[0])
        snprintf(metar_tmpl, sizeof metar_tmpl, "%s", url_template);
}

void metar_make_url(char *out, size_t size, const char *station)
{
    const char *p = strstr(metar_tmpl, "{station}");
    if (p)
        snprintf(out, size, "%.*s%s%s", (int)(p - metar_tmpl), metar_tmpl, station, p + 9);
    else
        snprintf(out, size, "%s", metar_tmpl);
}

void metar_start(const char *station)
{
    if (!station || !station[0])
        return;
    metar_make_url(metar_url, sizeof metar_url, station);
    pthread_t tid;
    if (pthread_create(&tid, NULL, metar_thread, NULL) == 0)
        pthread_detach(tid);
}

void metar_set(const metar_t *m)
{
    pthread_mutex_lock(&lock);
    metar_latest = *m;
    pthread_mutex_unlock(&lock);
}

bool metar_get(metar_t *out)
{
    pthread_mutex_lock(&lock);
    *out = metar_latest;
    pthread_mutex_unlock(&lock);
    return out->valid;
}
