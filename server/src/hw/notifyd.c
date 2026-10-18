/* notifyd.c — org.freedesktop.Notifications server (see notifyd.h). */
#include "notifyd.h"
#include "jbuf.h"
#include "../vault/dbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUS_NAME "org.freedesktop.Notifications"
#define OBJ "/org/freedesktop/Notifications"

static DConn conn = { .fd = -1 };
static int up;
static uint32_t next_id = 1;
static NotifydEvent emit;

int notifyd_fd(void) { return up ? conn.fd : -1; }
int notifyd_wants_write(void) { return up && dbus_pending_out(&conn); }

void notifyd_plain(const char *in, char *out, size_t n) {
    size_t o = 0;
    if (!n) return;
    for (const char *p = in; *p && o + 1 < n;) {
        if (*p == '<') {                                    /* a tag: dropped (a <br> becomes a newline) */
            const char *e = strchr(p, '>');
            if (!e) break;
            if (!strncmp(p, "<br", 3) && o + 1 < n) out[o++] = '\n';
            p = e + 1;
            continue;
        }
        if (*p == '&') {
            static const struct { const char *ent; char c; } ents[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
            int done = 0;
            for (size_t i = 0; i < sizeof ents / sizeof ents[0]; i++)
                if (!strncmp(p, ents[i].ent, strlen(ents[i].ent))) { out[o++] = ents[i].c; p += strlen(ents[i].ent); done = 1; break; }
            if (done) continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

static void reply_caps(const DMsg *m) {
    DBuf b;
    dw_init(&b);
    size_t a = dw_arr_begin(&b, 4);
    dw_str(&b, "body");
    dw_str(&b, "body-markup");
    dw_str(&b, "actions");
    dw_arr_end(&b, a);
    dbus_reply(&conn, m, "as", &b);
    dw_free(&b);
}

static void reply_info(const DMsg *m) {
    DBuf b;
    dw_init(&b);
    dw_str(&b, "LordOS");
    dw_str(&b, "LordOS");
    dw_str(&b, "0.7");
    dw_str(&b, "1.2");
    dbus_reply(&conn, m, "ssss", &b);
    dw_free(&b);
}

static void on_notify(const DMsg *m) {
    DRd r;
    dr_init(&r, m);
    const char *app = dr_str(&r, NULL);
    uint32_t replaces = dr_u32(&r);
    const char *icon = dr_str(&r, NULL), *summary = dr_str(&r, NULL), *body = dr_str(&r, NULL);
    (void)icon;
    HBuf j = { 0 };
    char plain[2048];
    hb_printf(&j, "{\"actions\":[");
    size_t end = dr_arr(&r, 4);
    int na = 0;
    while (!r.err && dr_more(&r, end)) {
        const char *key = dr_str(&r, NULL);
        const char *label = dr_more(&r, end) ? dr_str(&r, NULL) : NULL;
        if (r.err || !key || !label || na >= 3) continue;
        notifyd_plain(label, plain, 96);
        hb_printf(&j, "%s[", na++ ? "," : "");
        hb_str(&j, key);
        hb_printf(&j, ",");
        hb_str(&j, plain);
        hb_printf(&j, "]");
    }
    char desktop[160] = "";
    int urgency = 1;
    end = dr_arr(&r, 8);
    while (!r.err && dr_more(&r, end)) {
        dr_struct(&r);
        const char *k = dr_str(&r, NULL), *sig = dr_variant(&r);
        if (r.err || !k || !sig) break;
        if (!strcmp(k, "desktop-entry") && !strcmp(sig, "s")) { const char *v = dr_str(&r, NULL); snprintf(desktop, sizeof desktop, "%s", v ? v : ""); }
        else if (!strcmp(k, "urgency") && !strcmp(sig, "y")) urgency = dr_u8(&r);
        else dr_skip(&r, sig);
    }
    int32_t timeout = dr_i32(&r);
    if (r.err || !app || !summary || !body) {
        free(hb_take(&j));
        dbus_error(&conn, m, "org.freedesktop.DBus.Error.InvalidArgs", "Notify(susssasa{sv}i)");
        return;
    }
    uint32_t id = replaces ? replaces : next_id++;
    if (!next_id) next_id = 1;
    hb_printf(&j, "],\"id\":%u,\"replaces\":%s,\"urgency\":%d,\"timeout\":%d,\"app\":", id, replaces ? "true" : "false", urgency, timeout);
    notifyd_plain(app, plain, 128);
    hb_str(&j, plain);
    hb_printf(&j, ",\"desktop\":");
    hb_str(&j, desktop);
    hb_printf(&j, ",\"title\":");
    notifyd_plain(summary, plain, 256);
    hb_str(&j, plain);
    hb_printf(&j, ",\"body\":");
    notifyd_plain(body, plain, sizeof plain);
    hb_str(&j, plain);
    hb_printf(&j, "}");
    DBuf b;
    dw_init(&b);
    dw_u32(&b, id);
    dbus_reply(&conn, m, "u", &b);
    dw_free(&b);
    char *js = hb_take(&j);
    if (js && emit) emit("notify", js);
    free(js);
}

static void handle(const DMsg *m) {
    if (m->type != DBUS_CALL || !m->member) return;
    if (m->iface && !strcmp(m->iface, "org.freedesktop.DBus.Introspectable")) {
        DBuf b;
        dw_init(&b);
        dw_str(&b, "<node><interface name=\"org.freedesktop.Notifications\">"
                   "<method name=\"Notify\"/><method name=\"CloseNotification\"/><method name=\"GetCapabilities\"/>"
                   "<method name=\"GetServerInformation\"/><signal name=\"NotificationClosed\"/><signal name=\"ActionInvoked\"/>"
                   "</interface></node>");
        dbus_reply(&conn, m, "s", &b);
        dw_free(&b);
        return;
    }
    if (!m->path || strcmp(m->path, OBJ) || (m->iface && strcmp(m->iface, BUS_NAME))) {
        dbus_error(&conn, m, "org.freedesktop.DBus.Error.UnknownObject", "no such object");
        return;
    }
    const char *sig = m->sig ? m->sig : "";
    if (!strcmp(m->member, "GetCapabilities")) reply_caps(m);
    else if (!strcmp(m->member, "GetServerInformation")) reply_info(m);
    else if (!strcmp(m->member, "Notify") && !strcmp(sig, "susssasa{sv}i")) on_notify(m);
    else if (!strcmp(m->member, "CloseNotification") && !strcmp(sig, "u")) {
        DRd r;
        dr_init(&r, m);
        uint32_t id = dr_u32(&r);
        dbus_reply(&conn, m, NULL, NULL);
        char j[48];
        snprintf(j, sizeof j, "{\"id\":%u}", id);
        if (emit) emit("notify-close", j);
        notifyd_closed(id, 3);
    } else dbus_error(&conn, m, "org.freedesktop.DBus.Error.UnknownMethod", "unknown method");
}

int notifyd_start(NotifydEvent fn) {
    emit = fn;
    int fd = dbus_connect_session();
    if (fd < 0 || dbus_open(&conn, fd, 2000)) { if (fd >= 0) dbus_close(&conn); return -1; }
    DBuf b;
    dw_init(&b);
    dw_str(&b, BUS_NAME);
    dw_u32(&b, 4);                          /* DBUS_NAME_FLAG_DO_NOT_QUEUE */
    DHdr h = { .type = DBUS_CALL, .path = "/org/freedesktop/DBus", .iface = "org.freedesktop.DBus", .member = "RequestName",
               .dest = "org.freedesktop.DBus", .sig = "su" };
    uint32_t s = dbus_send(&conn, &h, &b);
    dw_free(&b);
    DMsg m;
    int ok = s && dbus_wait_reply(&conn, s, &m, 2000) == 1;
    uint32_t res = 0;
    if (ok) { DRd r; dr_init(&r, &m); res = dr_u32(&r); }
    if (s) dbus_msg_free(&m);
    if (!ok || (res != 1 && res != 4)) {
        fprintf(stderr, "notifyd: another program already serves %s\n", BUS_NAME);
        dbus_close(&conn);
        return -1;
    }
    up = 1;
    return 0;
}

void notifyd_dispatch(void) {
    if (!up) return;
    DMsg m;
    int r;
    while ((r = dbus_next(&conn, &m)) == 1) { handle(&m); dbus_msg_free(&m); }
    if (r < 0 || dbus_flush(&conn) < 0) { fprintf(stderr, "notifyd: lost the session bus\n"); dbus_close(&conn); up = 0; }
}

void notifyd_closed(uint32_t id, uint32_t reason) {
    if (!up || !id) return;
    if (reason < 1 || reason > 4) reason = 4;
    DBuf b;
    dw_init(&b);
    dw_u32(&b, id);
    dw_u32(&b, reason);
    dbus_signal(&conn, OBJ, BUS_NAME, "NotificationClosed", "uu", &b);
    dw_free(&b);
    dbus_flush(&conn);
}

void notifyd_action(uint32_t id, const char *key) {
    if (!up || !id || !key || strlen(key) > 128) return;
    DBuf b;
    dw_init(&b);
    dw_u32(&b, id);
    dw_str(&b, key);
    dbus_signal(&conn, OBJ, BUS_NAME, "ActionInvoked", "us", &b);
    dw_free(&b);
    notifyd_closed(id, 2);
}
