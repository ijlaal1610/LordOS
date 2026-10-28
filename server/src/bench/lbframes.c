/* lbframes.c — see lbframes.h. */
#include "lbframes.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define MAXF 8192                       /* about two minutes at 60 Hz; later frames are not kept */
#define MAXI 16384                      /* two minutes of the synthetic pointer's events */
#define BATCH 64                        /* events waiting for one frame (8 ms each: half a second) */

static atomic_int on;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static double work[MAXF];
static uint64_t shown[MAXF];
static int nwork, nshown;
static uint32_t period;
static double lat[MAXI];                /* ms */
static int nlat;
static uint64_t applied[BATCH], inframe[BATCH];     /* events applied, not yet in a frame; in the frame being shown */
static int napplied, ninframe;

void lbf_frame(double work_ms) {
    if (!atomic_load_explicit(&on, memory_order_relaxed)) return;
    pthread_mutex_lock(&mu);
    if (nwork < MAXF && work_ms >= 0) work[nwork++] = work_ms;
    pthread_mutex_unlock(&mu);
}

void lbf_presented(uint64_t ns, uint32_t period_ns) {
    if (!atomic_load_explicit(&on, memory_order_relaxed)) return;
    pthread_mutex_lock(&mu);
    if (nshown < MAXF && (!nshown || ns > shown[nshown - 1])) shown[nshown++] = ns;
    if (period_ns) period = period_ns;
    for (int i = 0; i < ninframe; i++)
        if (nlat < MAXI && ns >= inframe[i]) lat[nlat++] = (double)(ns - inframe[i]) / 1e6;
    ninframe = 0;
    pthread_mutex_unlock(&mu);
}

void lbf_input(uint64_t ev_ns) {
    if (!atomic_load_explicit(&on, memory_order_relaxed)) return;
    pthread_mutex_lock(&mu);
    if (napplied == BATCH) { memmove(applied, applied + 1, sizeof applied - sizeof *applied); napplied--; }
    applied[napplied++] = ev_ns;
    pthread_mutex_unlock(&mu);
}

void lbf_built(int main_drawn) {
    if (!main_drawn || !atomic_load_explicit(&on, memory_order_relaxed)) return;
    pthread_mutex_lock(&mu);
    /* a frame built while the previous one is still waiting for its flip never happens on the main display
     * (main.c waits for the flip); if it did, both groups end at this frame's presentation */
    for (int i = 0; i < napplied && ninframe < BATCH; i++) inframe[ninframe++] = applied[i];
    napplied = 0;
    pthread_mutex_unlock(&mu);
}

void lbf_begin(void) {
    pthread_mutex_lock(&mu);
    nwork = nshown = nlat = napplied = ninframe = 0;
    period = 0;
    atomic_store(&on, 1);
    pthread_mutex_unlock(&mu);
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

/* the p-th percentile of sorted v[0..n) (nearest rank) */
static double pct(const double *v, int n, double p) {
    if (n <= 0) return 0;
    int k = (int)(p / 100.0 * n + 0.999999) - 1;
    return v[k < 0 ? 0 : k >= n ? n - 1 : k];
}

int lbf_end(LbFrameStats *st) {
    memset(st, 0, sizeof *st);
    static double w[MAXF], l[MAXI];
    pthread_mutex_lock(&mu);
    atomic_store(&on, 0);
    int nw = nwork, ns = nshown, nl = nlat;
    memcpy(w, work, (size_t)nw * sizeof *w);
    memcpy(l, lat, (size_t)nl * sizeof *l);
    double budget = (period ? period : 16666667u) / 1e6;
    st->budget_ms = budget;
    st->frames = nw;
    st->shown = ns;
    int missed = 0;
    double gap = 0;
    for (int i = 1; i < ns; i++) {
        double d = (double)(shown[i] - shown[i - 1]) / 1e6;
        if (d > budget * 1.5) missed++;
        if (d > gap) gap = d;
    }
    if (ns >= 2) {
        st->span_s = (double)(shown[ns - 1] - shown[0]) / 1e9;
        st->fps = st->span_s > 0 ? (ns - 1) / st->span_s : 0;
        st->missed_pct = 100.0 * missed / (ns - 1);
        st->gap_max = gap;
    }
    pthread_mutex_unlock(&mu);
    qsort(w, (size_t)nw, sizeof *w, cmp_d);
    st->work_p50 = pct(w, nw, 50);
    st->work_p99 = pct(w, nw, 99);
    st->work_max = nw ? w[nw - 1] : 0;
    qsort(l, (size_t)nl, sizeof *l, cmp_d);
    st->inputs = nl;
    st->in_p50 = pct(l, nl, 50);
    st->in_p99 = pct(l, nl, 99);
    return ns >= 2 ? 0 : -1;
}
