/* Background fetch threads: one per source, each polling its URL and
   publishing snapshots for the UI thread to pick up. */

#include "feed.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_BACKOFF_S 60

typedef struct {
    char  *data;
    size_t len;
} buffer_t;

typedef struct {
    source_config_t cfg;
    int             index;
    pthread_mutex_t lock;
    feed_snapshot_t shared;         /* protected by lock */
    feed_snapshot_t work;           /* fetch thread only */
} source_t;

static source_t sources[MAX_SOURCES];
static int      n_sources;

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    buffer_t *buf = userdata;
    size_t n = size * nmemb;
    char *grown = realloc(buf->data, buf->len + n + 1);
    if (!grown)
        return 0;               /* makes curl abort the transfer */
    buf->data = grown;
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return n;
}

/* One fetch + parse. Returns 0 on success. */
static int fetch_once(source_t *s, CURL *curl)
{
    buffer_t buf = { 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    int result = -1;
    if (rc != CURLE_OK) {
        fprintf(stderr, "feed %s: %s\n", s->cfg.name, curl_easy_strerror(rc));
    } else if (status != 200) {
        fprintf(stderr, "feed %s: HTTP %ld\n", s->cfg.name, status);
    } else if (!buf.data || feed_parse(buf.data, s->cfg.array_key, &s->work) != 0) {
        fprintf(stderr, "feed %s: could not parse response (array key '%s')\n",
                s->cfg.name, s->cfg.array_key);
    } else {
        for (int i = 0; i < s->work.count; i++)
            s->work.ac[i].source = (uint8_t)s->index;
        result = 0;
    }

    free(buf.data);
    return result;
}

static void *feed_thread(void *arg)
{
    source_t *s = arg;
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "feed %s: curl_easy_init failed\n", s->cfg.name);
        return NULL;
    }
    curl_easy_setopt(curl, CURLOPT_URL, s->cfg.url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "radar-display/0.2");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   /* allow gzip */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    int delay = s->cfg.poll_s;
    for (;;) {
        if (fetch_once(s, curl) == 0) {
            pthread_mutex_lock(&s->lock);
            memcpy(s->shared.ac, s->work.ac, sizeof(aircraft_t) * (size_t)s->work.count);
            s->shared.count = s->work.count;
            s->shared.gen++;
            s->shared.last_ok = time(NULL);
            pthread_mutex_unlock(&s->lock);
            delay = s->cfg.poll_s;
        } else {
            /* Back off on errors so we never hammer a public API. */
            delay = delay * 2 > MAX_BACKOFF_S ? MAX_BACKOFF_S : delay * 2;
        }
        sleep((unsigned)delay);
    }
    return NULL;
}

int feed_start(const radar_config_t *cfg)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    n_sources = cfg->n_sources;
    for (int i = 0; i < n_sources; i++) {
        source_t *s = &sources[i];
        s->cfg = cfg->sources[i];
        s->index = i;
        pthread_mutex_init(&s->lock, NULL);

        pthread_t tid;
        if (pthread_create(&tid, NULL, feed_thread, s) != 0) {
            perror("pthread_create");
            return -1;
        }
        pthread_detach(tid);
    }
    return 0;
}

bool feed_get(int src, feed_snapshot_t *out, uint32_t last_gen)
{
    if (src < 0 || src >= n_sources)
        return false;
    source_t *s = &sources[src];

    bool updated = false;
    pthread_mutex_lock(&s->lock);
    if (s->shared.gen != last_gen) {
        memcpy(out->ac, s->shared.ac, sizeof(aircraft_t) * (size_t)s->shared.count);
        out->count = s->shared.count;
        out->gen = s->shared.gen;
        out->last_ok = s->shared.last_ok;
        updated = true;
    }
    pthread_mutex_unlock(&s->lock);
    return updated;
}

time_t feed_last_ok(int src)
{
    if (src < 0 || src >= n_sources)
        return 0;
    source_t *s = &sources[src];
    pthread_mutex_lock(&s->lock);
    time_t t = s->shared.last_ok;
    pthread_mutex_unlock(&s->lock);
    return t;
}
