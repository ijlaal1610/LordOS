/* par.h — a small pool of threads for the CPU path's pixel loops (rows split between them).
 *
 * par_rows(j0, j1, fn, ctx) calls fn(ctx, a, b) over disjoint pieces of [j0, j1) on the pool and the
 * calling thread, and returns when every piece is done. fn must only write its own rows (or columns)
 * and touch no shared state: it is used for pure pixel work (blur, blits, gradients, the remote
 * desktop's comparison and copies), never for the node tree, caches or anything with side effects.
 * Without par_init (or with one CPU) it runs fn(ctx, j0, j1) on the caller. */
#ifndef LORD_PAR_H
#define LORD_PAR_H

typedef void (*ParFn)(void *ctx, int a, int b);

void par_init(int threads);         /* 0: the CPUs this process may use, at most 8 */
int  par_threads(void);             /* threads that take pieces, the caller included (1: serial) */
void par_rows(int j0, int j1, ParFn fn, void *ctx);

#endif
