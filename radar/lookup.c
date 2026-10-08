/* adsbdb.com lookups: aircraft by Mode S hex, and route by callsign. */

#include "lookup.h"
#include "weather.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE_SIZE 64
#define QUEUE_SIZE 16

typedef enum { KIND_AIRCRAFT, KIND_ROUTE, KIND_METAR } kind_t;

typedef struct {
    bool              used;
    kind_t            kind;
    char              key[12];      /* hex or callsign */
    lookup_state_t    state;
    lookup_aircraft_t aircraft;
    lookup_route_t    route;
    metar_t           metar;
} entry_t;

static char            base[256];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  wake = PTHREAD_COND_INITIALIZER;
static entry_t         cache[CACHE_SIZE];
static int             next_slot;           /* round-robin replacement */
static int             queue[QUEUE_SIZE];   /* cache indexes waiting for the thread */
static int             q_head, q_len;
static bool            running;

/* ---------- parsing ---------- */

static void copy_json(char *dst, size_t size, const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring)
        snprintf(dst, size, "%s", item->valuestring);
    else
        dst[0] = '\0';
}

static const cJSON *response_part(const cJSON *root, const char *name)
{
    const cJSON *resp = cJSON_GetObjectItemCaseSensitive(root, "response");
    const cJSON *part = cJSON_IsObject(resp) ? cJSON_GetObjectItemCaseSensitive(resp, name) : NULL;
    return cJSON_IsObject(part) ? part : NULL;
}

bool lookup_parse_aircraft(const char *json, lookup_aircraft_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    const cJSON *a = root ? response_part(root, "aircraft") : NULL;
    if (a) {
        copy_json(out->reg, sizeof out->reg, a, "registration");
        copy_json(out->type, sizeof out->type, a, "type");
        copy_json(out->icao_type, sizeof out->icao_type, a, "icao_type");
        copy_json(out->manufacturer, sizeof out->manufacturer, a, "manufacturer");
        copy_json(out->owner, sizeof out->owner, a, "registered_owner");
    }
    cJSON_Delete(root);
    return a && (out->reg[0] || out->type[0]);
}

static void copy_airport(const cJSON *ap, char *icao, char *iata, char *city)
{
    copy_json(icao, 8, ap, "icao_code");
    copy_json(iata, 8, ap, "iata_code");
    copy_json(city, 40, ap, "municipality");
    if (!city[0])
        copy_json(city, 40, ap, "name");
}

bool lookup_parse_route(const char *json, lookup_route_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    const cJSON *r = root ? response_part(root, "flightroute") : NULL;
    if (r) {
        const cJSON *airline = cJSON_GetObjectItemCaseSensitive(r, "airline");
        if (cJSON_IsObject(airline))
            copy_json(out->airline, sizeof out->airline, airline, "name");
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(r, "origin");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(r, "destination");
        if (cJSON_IsObject(o))
            copy_airport(o, out->origin_icao, out->origin_iata, out->origin_city);
        if (cJSON_IsObject(d))
            copy_airport(d, out->dest_icao, out->dest_iata, out->dest_city);
    }
    cJSON_Delete(root);
    return r && out->origin_icao[0] && out->dest_icao[0];
}

/* ---------- fetching ---------- */

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

static void *lookup_thread(void *arg)
{
    (void)arg;
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "radar-display/0.3");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    for (;;) {
        pthread_mutex_lock(&lock);
        while (q_len == 0)
            pthread_cond_wait(&wake, &lock);
        int idx = queue[q_head];
        q_head = (q_head + 1) % QUEUE_SIZE;
        q_len--;
        entry_t job = cache[idx];
        pthread_mutex_unlock(&lock);

        char url[600];
        if (job.kind == KIND_METAR)
            metar_make_url(url, sizeof url, job.key);
        else
            snprintf(url, sizeof url, "%s/%s/%s", base,
                     job.kind == KIND_AIRCRAFT ? "aircraft" : "callsign", job.key);
        buffer_t buf = { 0 };
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        CURLcode rc = curl_easy_perform(curl);

        bool found = false;
        if (rc == CURLE_OK && buf.data) {
            if (job.kind == KIND_AIRCRAFT)
                found = lookup_parse_aircraft(buf.data, &job.aircraft);
            else if (job.kind == KIND_ROUTE)
                found = lookup_parse_route(buf.data, &job.route);
            else
                found = metar_parse(buf.data, &job.metar);
        } else if (rc != CURLE_OK) {
            fprintf(stderr, "lookup %s: %s\n", job.key, curl_easy_strerror(rc));
        }
        free(buf.data);

        pthread_mutex_lock(&lock);
        /* Only store it if the slot still holds this request */
        if (cache[idx].used && cache[idx].kind == job.kind && strcmp(cache[idx].key, job.key) == 0) {
            cache[idx].aircraft = job.aircraft;
            cache[idx].route = job.route;
            cache[idx].metar = job.metar;
            cache[idx].metar.fetched = time(NULL);
            cache[idx].state = found ? LOOKUP_FOUND : LOOKUP_NONE;
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

void lookup_start(const char *base_url)
{
    snprintf(base, sizeof base, "%s", base_url ? base_url : "");
    size_t n = strlen(base);
    while (n > 0 && base[n - 1] == '/')
        base[--n] = '\0';

    /* The thread also serves METARs, so it runs even without adsbdb */
    pthread_t tid;
    if (pthread_create(&tid, NULL, lookup_thread, NULL) == 0) {
        pthread_detach(tid);
        running = true;
    } else {
        base[0] = '\0';
    }
}

bool lookup_enabled(void)
{
    return base[0] != '\0';
}

/* Find or create the cache entry; queue a fetch for a new one. */
static lookup_state_t get(kind_t kind, const char *key, entry_t *copy)
{
    if (!running || !key[0] || (kind != KIND_METAR && !base[0]))
        return LOOKUP_NONE;

    pthread_mutex_lock(&lock);
    for (int i = 0; i < CACHE_SIZE; i++) {
        /* METARs go stale: ask again after 15 minutes */
        if (cache[i].used && kind == KIND_METAR && cache[i].kind == KIND_METAR &&
            cache[i].state != LOOKUP_PENDING && strcmp(cache[i].key, key) == 0 &&
            time(NULL) - cache[i].metar.fetched > 900)
            cache[i].used = false;
        if (cache[i].used && cache[i].kind == kind && strcmp(cache[i].key, key) == 0) {
            *copy = cache[i];
            pthread_mutex_unlock(&lock);
            return copy->state;
        }
    }
    if (q_len == QUEUE_SIZE) {                  /* too busy: try again later */
        pthread_mutex_unlock(&lock);
        return LOOKUP_PENDING;
    }
    int idx = next_slot;
    next_slot = (next_slot + 1) % CACHE_SIZE;
    memset(&cache[idx], 0, sizeof cache[idx]);
    cache[idx].used = true;
    cache[idx].kind = kind;
    cache[idx].state = LOOKUP_PENDING;
    snprintf(cache[idx].key, sizeof cache[idx].key, "%s", key);
    queue[(q_head + q_len) % QUEUE_SIZE] = idx;
    q_len++;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    return LOOKUP_PENDING;
}

lookup_state_t lookup_aircraft(const char *hex, lookup_aircraft_t *out)
{
    entry_t e;
    lookup_state_t s = get(KIND_AIRCRAFT, hex, &e);
    if (s == LOOKUP_FOUND && out)
        *out = e.aircraft;
    return s;
}

/* Routes only make sense for airline-style callsigns: three letters then a
   digit (ACA123, WJA456). Registrations used as callsigns (CGSDZ) are skipped. */
static bool airline_callsign(const char *cs)
{
    return strlen(cs) >= 4 && isalpha((unsigned char)cs[0]) && isalpha((unsigned char)cs[1]) &&
           isalpha((unsigned char)cs[2]) && isdigit((unsigned char)cs[3]);
}

lookup_state_t lookup_route(const char *callsign, lookup_route_t *out)
{
    if (!airline_callsign(callsign))
        return LOOKUP_NONE;
    entry_t e;
    lookup_state_t s = get(KIND_ROUTE, callsign, &e);
    if (s == LOOKUP_FOUND && out)
        *out = e.route;
    return s;
}

lookup_state_t lookup_metar(const char *icao, metar_t *out)
{
    entry_t e;
    lookup_state_t s = get(KIND_METAR, icao, &e);
    if (s == LOOKUP_FOUND && out)
        *out = e.metar;
    return s;
}

void lookup_set_route(const char *callsign, const lookup_route_t *r)
{
    entry_t e;
    get(KIND_ROUTE, callsign, &e);              /* make sure there's an entry */
    pthread_mutex_lock(&lock);
    for (int i = 0; i < CACHE_SIZE; i++)
        if (cache[i].used && cache[i].kind == KIND_ROUTE && !strcmp(cache[i].key, callsign)) {
            cache[i].route = *r;
            cache[i].state = LOOKUP_FOUND;
        }
    pthread_mutex_unlock(&lock);
}
