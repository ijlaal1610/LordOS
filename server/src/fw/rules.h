/* rules.h — reglas de red por app: el dueño del equipo decide qué apps suyas usan la red
 * (como el permiso "acceso a la red" de macOS o Android). Ver docs/FIREWALL.md, "Reglas".
 *
 * Esta parte es solo la tienda de reglas y el que decide: no toca el kernel ni la red. La usan
 * después el daemon (con los ganchos de cgroup), el Monitor (columna "Regla") y Ajustes (listas
 * "Apps confiables" y "Apps restringidas").
 *
 * Orden de decisión (fw_decide), de más fuerte a más débil:
 *   1. restricciones de la app (FW_R_*): niegan siempre;
 *   2. reglas explícitas: gana la de destino más específico; con la misma especificidad,
 *      denegar le gana a permitir;
 *   3. app confiable: permitir sin anotar ni preguntar;
 *   4. modo: silencioso = permitir y anotar, preguntar = pendiente, apagado = permitir.
 *
 * Búsquedas: tabla hash de apps; por app, tabla hash de dominios exactos, tabla de sufijos para
 * los comodines (se recorren las etiquetas del nombre de derecha a izquierda), un trie binario
 * de prefijos por familia para las direcciones y redes. Todo es O(1) u O(largo de la clave). */
#ifndef LORD_FW_RULES_H
#define LORD_FW_RULES_H

#include <stddef.h>
#include <stdint.h>

#define FW_APP_MAX   128        /* bytes de un id de app, con el 0 final */
#define FW_NAME_MAX  254        /* nombre de dominio en texto (253 + 0) */
#define FW_MAX_RULES 65536      /* tope de reglas por tienda (también al cargar) */
#define FW_MAX_APPS  4096       /* tope de apps con estado */
#define FW_FILE_MAX  (4u << 20) /* tope del archivo JSON */

typedef enum { FW_OUT = 0, FW_IN = 1 } FwDir;
typedef enum { FW_PROTO_ANY = 0, FW_PROTO_TCP = 1, FW_PROTO_UDP = 2 } FwProto;
typedef enum { FW_ALLOW = 0, FW_DENY = 1 } FwAction;
/* para siempre (se guarda) / hasta que la app se cierre / una sola vez (no se guardan) */
typedef enum { FW_LIFE_FOREVER = 0, FW_LIFE_UNTIL_QUIT = 1, FW_LIFE_ONCE = 2 } FwLife;
typedef enum { FW_MODE_SILENT = 0, FW_MODE_ASK = 1, FW_MODE_OFF = 2 } FwMode;

typedef enum {
    FW_DST_ANY = 0,
    FW_DST_DOMAIN,      /* exacto, o "*.sufijo" (wildcard = 1): el sufijo y todo lo de abajo */
    FW_DST_CIDR,        /* una dirección es un /32 o /128 */
    FW_DST_LAN,         /* ver fw_addr_is_lan */
    FW_DST_INTERNET,    /* todo lo que no es LAN */
} FwDstKind;

/* Restricciones de una app de la lista "Apps restringidas". Las de red las aplica fw_decide;
 * las demás las consulta quien maneja ese recurso (broker, cámara, portal...) con fw_blocks. */
enum {
    FW_R_NO_INTERNET      = 1u << 0,
    FW_R_NO_LAN           = 1u << 1,
    FW_R_NO_NETWORK       = 1u << 2,
    FW_R_NO_INCOMING      = 1u << 3,
    FW_R_NO_CAMERA        = 1u << 4,
    FW_R_NO_MIC           = 1u << 5,
    FW_R_NO_SCREEN        = 1u << 6,
    FW_R_NO_USB           = 1u << 7,
    FW_R_NO_BLUETOOTH     = 1u << 8,
    FW_R_NO_FILES         = 1u << 9,    /* nada fuera del selector de archivos */
    FW_R_NO_NOTIFICATIONS = 1u << 10,
    FW_R_ALL              = (1u << 11) - 1,
};

typedef struct {
    uint32_t id;                    /* lo pone la tienda (fw_rule_add); 0 = sin asignar */
    char     app[FW_APP_MAX];       /* id canónico (ver fw_app_canon) */
    FwDir    dir;
    FwDstKind kind;
    int      wildcard;              /* FW_DST_DOMAIN: 1 si era "*.sufijo" */
    char     domain[FW_NAME_MAX];   /* FW_DST_DOMAIN: minúsculas, sin "*." ni punto final */
    int      fam;                   /* FW_DST_CIDR: 4 o 6 */
    uint8_t  addr[16];              /* FW_DST_CIDR: red (bits fuera del prefijo en 0) */
    int      prefix;                /* FW_DST_CIDR: 0..32 o 0..128 */
    uint16_t port_lo, port_hi;      /* rango; 0..65535 (o 0..0) = cualquier puerto */
    FwProto  proto;
    FwAction action;
    FwLife   life;
    long long when;                 /* segundos Unix de la decisión (informativo) */
} FwRule;

/* Una conexión a decidir. */
typedef struct {
    const char *app;                /* cualquier forma aceptada por fw_app_canon */
    FwDir    dir;
    FwProto  proto;                 /* FW_PROTO_TCP o FW_PROTO_UDP */
    int      fam;                   /* 4, 6, o 0 si no se sabe la dirección */
    uint8_t  addr[16];              /* dirección remota (IPv4 en los 4 primeros bytes) */
    uint16_t port;                  /* puerto remoto (salida) o local (entrada) */
    const char *const *hosts;       /* nombres de esa dirección para esta app (dnsmap_lookup) */
    int      nhosts;
} FwQuery;

typedef enum { FW_V_ALLOW = 0, FW_V_DENY = 1, FW_V_ASK = 2 } FwVerdict;
typedef enum { FW_BY_RESTRICTED, FW_BY_RULE, FW_BY_TRUSTED, FW_BY_MODE } FwReason;

typedef struct {
    FwVerdict verdict;
    FwReason  reason;
    uint32_t  rule_id;              /* FW_BY_RULE: la regla que decidió (una "once" ya se borró) */
    uint32_t  flag;                 /* FW_BY_RESTRICTED: la restricción que negó */
    int       record;               /* 1: anotarla para que el dueño la revise (modo silencioso) */
} FwDecision;

typedef struct FwStore FwStore;

FwStore *fw_store_new(void);
void     fw_store_free(FwStore *s);

/* Id canónico: "lordos", "desktop:<id>", "lpk:<id>" o una ruta absoluta de ejecutable. Las scopes
 * de cgroup se traducen: app-lordos-lpk-<id>.scope → lpk:<id>, app-lordos-<id>.scope →
 * desktop:<id>. 0 bien, -1 si no es un id válido (vacío, largo, caracteres raros). */
int fw_app_canon(const char *in, char *out, size_t n);

/* Destino en texto ("any", "lan", "internet", "ejemplo.com", "*.ejemplo.com", "10.0.0.0/8",
 * "2001:db8::1") → kind/wildcard/domain/fam/addr/prefix de r. 0 bien, -1 inválido. */
int fw_parse_dest(const char *s, FwRule *r);
/* Nombre de dominio normalizado en out (FW_NAME_MAX): minúsculas, sin punto final, etiquetas de
 * 1 a 63 de [a-z0-9-_], hasta 253; la última no puede ser solo números. 0 bien, -1 inválido. */
int fw_norm_domain(const char *in, char *out);
/* El inverso de fw_parse_dest, para guardar y mostrar. */
int fw_format_dest(const FwRule *r, char *out, size_t n);

/* LAN: 0/8, 10/8, 100.64/10 (CGNAT), 127/8, 169.254/16, 172.16/12, 192.168/16, 224/4, 255.255.255.255;
 * ::, ::1, fc00::/7, fe80::/10, ff00::/8; ::ffff:a.b.c.d como su IPv4. */
int fw_addr_is_lan(int fam, const uint8_t *addr);

/* Agrega una regla (la valida y copia). Si ya hay una con el mismo app/dir/destino/puertos/
 * protocolo, la reemplaza y conserva su id. *id_out (opcional) recibe el id. 0 bien, -1 inválida
 * o sin lugar. */
int  fw_rule_add(FwStore *s, const FwRule *r, uint32_t *id_out);
int  fw_rule_remove(FwStore *s, uint32_t id);
const FwRule *fw_rule_get(const FwStore *s, uint32_t id);   /* NULL si no existe */
/* Copia las reglas (de una app, o de todas con app NULL) en orden de id; devuelve el total. */
int  fw_rule_list(const FwStore *s, const char *app, FwRule *out, int max);
size_t fw_rule_count(const FwStore *s);

void fw_app_quit(FwStore *s, const char *app);              /* borra sus reglas "hasta cerrar" */
void fw_app_forget(FwStore *s, const char *app);            /* todo lo de la app (desinstalar) */

/* Listas. Una app está en una sola: confiar la saca de restringidas y al revés. */
int      fw_set_trusted(FwStore *s, const char *app, int on);
int      fw_is_trusted(const FwStore *s, const char *app);
int      fw_set_restricted(FwStore *s, const char *app, uint32_t flags);  /* 0 = sacarla */
uint32_t fw_restricted(const FwStore *s, const char *app);
int      fw_blocks(const FwStore *s, const char *app, uint32_t flag);     /* ¿restringida en flag? */
/* Recorre las apps con estado; cb devuelve != 0 para cortar. */
void     fw_app_each(const FwStore *s, int (*cb)(void *u, const char *app, int trusted, uint32_t flags), void *u);

void   fw_set_mode(FwStore *s, FwMode m);
FwMode fw_mode(const FwStore *s);

/* Decide una conexión. Si gana una regla "once", la borra. */
FwDecision fw_decide(FwStore *s, const FwQuery *q);

/* Nombres de las restricciones en el JSON ("no-internet"...). */
const char *fw_flag_name(uint32_t flag);
uint32_t    fw_flag_from_name(const char *name);            /* 0 si no existe */

/* JSON compacto (solo las reglas "forever"). fw_to_json devuelve memoria de malloc. */
char *fw_to_json(const FwStore *s, size_t *len);
/* Reemplaza el contenido de s con el del JSON. Si algo no valida, s queda igual y err dice qué. */
int   fw_from_json(FwStore *s, const char *text, size_t len, char *err, size_t errlen);
/* Archivo por usuario (~/.config/lordos/firewall.json): escritura atómica con modo 0600. */
int   fw_save(const FwStore *s, const char *path);
int   fw_load(FwStore *s, const char *path, char *err, size_t errlen);

#endif
