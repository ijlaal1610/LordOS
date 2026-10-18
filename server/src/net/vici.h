/* vici.h — cliente propio del protocolo VICI de strongSwan (ver vici.c). */
#ifndef LORD_NET_VICI_H
#define LORD_NET_VICI_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *p; size_t len, cap; int oom; } ViciMsg;
typedef void (*ViciFn)(const char *path, const char *key, const char *val, void *ud);
typedef struct { int success; char errmsg[256]; } ViciResult;

int  vici_connect(const char *path);
void vici_begin(ViciMsg *m, const char *name);
void vici_end(ViciMsg *m);
void vici_kv(ViciMsg *m, const char *key, const char *val);
void vici_kv_bin(ViciMsg *m, const char *key, const void *val, size_t n);
void vici_list_begin(ViciMsg *m, const char *name);
void vici_item(ViciMsg *m, const char *val);
void vici_list_end(ViciMsg *m);
void vici_free(ViciMsg *m);
void vici_walk(const uint8_t *m, size_t n, ViciFn fn, void *ud);
/* Pide cmd; si event, se registra antes y llama a on_event por cada aviso hasta la respuesta. */
int  vici_call(int fd, const char *cmd, const ViciMsg *req, const char *event, ViciFn on_event, ViciFn on_reply, void *ud, int timeout_ms);
int  vici_simple(int fd, const char *cmd, const ViciMsg *req, ViciResult *res, int timeout_ms);
void vici_result_cb(const char *path, const char *key, const char *val, void *ud);

#endif
