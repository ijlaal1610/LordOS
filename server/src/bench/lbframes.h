/* lbframes.h — LordBench's compositor frame recorder (docs/BENCHMARK.md, "Frame times against the refresh
 * budget"). While a recording is on, lord-server's main loop (main.c) reports the work of every frame it
 * builds (layout, paint and present, ms) and every frame the main display shows (the page flip's time, or
 * the end of the frame without a monitor); LordBench's thread (lbrun.c) starts and stops the recording
 * and gets the statistics. Off, each call is one atomic read. */
#ifndef LORD_BENCH_LBFRAMES_H
#define LORD_BENCH_LBFRAMES_H

#include <stdint.h>

typedef struct {
    int frames;                         /* frames built */
    int shown;                          /* frames shown on the main display */
    double span_s;                      /* from the first shown frame to the last */
    double fps;                         /* shown frames per second */
    double work_p50, work_p99, work_max;/* ms to build a frame */
    double budget_ms;                   /* the main display's refresh period */
    double missed_pct;                  /* intervals between shown frames longer than 1.5 periods, % of them */
    double gap_max;                     /* the longest interval, ms */
    int inputs;                         /* synthetic pointer events shown (bench/lbinput.h) */
    double in_p50, in_p99;              /* ms from such an event to the presentation of the first frame showing it */
} LbFrameStats;

/* main.c */
void lbf_frame(double work_ms);
void lbf_presented(uint64_t ns, uint32_t period_ns);
/* the synthetic pointer: an event (stamped ev_ns) was applied to the scene, before the next frame is built
 * (lbinput.c); then main.c says, after building a frame, whether the main display got it: the events
 * applied so far are in that frame and their latency ends at its presentation */
void lbf_input(uint64_t ev_ns);
void lbf_built(int main_drawn);

/* lbrun.c: begin clears and starts; end stops and fills st (0, or -1 with fewer than 2 shown frames) */
void lbf_begin(void);
int  lbf_end(LbFrameStats *st);

#endif
