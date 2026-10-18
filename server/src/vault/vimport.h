/* vimport.h — importar y exportar contraseñas de otros gestores (ver docs/PASSWORDS.md,
 * «Importar y exportar»). Corre adentro de lord-vault (encerrado, sin red): el archivo llega como
 * bytes ya leídos de un fd que abrió lord-server. Todo lo que se arma (textos, adjuntos) va en la
 * arena de secretos (smem.h) y imp_free lo borra.
 *
 * Formatos:
 *   - KeePass/KeePassXC KDBX 4.x (lector propio: Argon2d/Argon2id/AES-KDF, AES-256-CBC o ChaCha20,
 *     bloques HMAC-SHA256, gzip, flujo interno ChaCha20/Salsa20 y el XML de KeePass), con
 *     contraseña y/o archivo llave; y el CSV que exporta KeePassXC.
 *   - Bitwarden JSON (sin cifrar) y CSV.
 *   - Chrome and other Chromium-based browsers, Firefox, and the generic
 *     Title,URL,Username,Password,Notes,OTPAuth CSV of other password managers.
 *   - CSV propio de LordOS (el que escribe exp_csv).
 * Los parsers son robustos ante archivos rotos o maliciosos (pruebas «fuzz-ish» en tests/). */
#ifndef LORD_VAULT_VIMPORT_H
#define LORD_VAULT_VIMPORT_H

#include <stddef.h>
#include <stdint.h>

enum {
    IMP_UNKNOWN = 0,
    IMP_KDBX,              /* KeePass 2.x / KeePassXC, .kdbx (versión 4; 3.1 si se puede) */
    IMP_KEEPASSXC_CSV,     /* "Group","Title","Username","Password","URL","Notes","TOTP","Icon","Last Modified","Created" */
    IMP_BITWARDEN_JSON,    /* {"encrypted":false,"folders":[...],"items":[...]} */
    IMP_BITWARDEN_CSV,     /* folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp */
    IMP_CHROME_CSV,        /* name,url,username,password[,note] */
    IMP_FIREFOX_CSV,       /* "url","username","password","httpRealm","formActionOrigin","guid","timeCreated","timeLastUsed","timePasswordChanged" */
    IMP_APPLE_CSV,         /* Title,URL,Username,Password,Notes,OTPAuth */
    IMP_LORD_CSV,          /* category,title,username,password,url,notes,totp,tags,favorite,fields,urls,kind,created,modified (exp_csv) */
};

/* Tipo de campo extra (coincide con los de la bóveda: vmodel.h) */
enum { IMPF_TEXT = 0, IMPF_HIDDEN, IMPF_URL, IMPF_DATE, IMPF_NUMBER, IMPF_TOTP };

typedef struct { char *key; char *value; int type; } ImpField;          /* IMPF_* */
typedef struct { char *name; uint8_t *data; size_t len; } ImpAttach;

typedef struct {
    char *title, *username, *password, *url, *notes;
    char *otp;              /* URI otpauth:// o el secreto en base32 tal cual venía */
    char *group;            /* ruta de la categoría/carpeta, separada por '/' ("Internet/Bancos"); NULL = sin */
    char *kind;             /* pista del tipo: "login", "card", "identity", "note", "wifi", "ssh"… o NULL */
    char **tags; int ntags;
    char **urls; int nurls; /* URLs adicionales (Bitwarden trae varias) */
    ImpField *fields; int nfields;
    ImpAttach *atts; int natts;
    int64_t created, modified;      /* segundos Unix; 0 = no se sabe */
    int favorite;
} ImpEntry;

typedef struct {
    ImpEntry *v; int n, cap;
    char **groups; int ngroups;     /* carpetas que existían aunque estén vacías (KDBX, Bitwarden) */
    char err[160];                  /* código corto en inglés para la interfaz: "bad-password", "unsupported-cipher"… */
} ImpList;

/* Adivina el formato por el contenido (y el nombre del archivo, opcional). */
int  imp_detect(const uint8_t *data, size_t len, const char *name_hint);
/* Lee el archivo. password/keyfile solo para KDBX (NULL si no hay). 0 bien; -1 con l->err:
 *   "bad-password" (KDBX: la clave compuesta no abre), "corrupt", "truncated",
 *   "unsupported-version", "unsupported-cipher", "unsupported-kdf", "encrypted-export"
 *   (Bitwarden cifrado: pedir el JSON sin cifrar), "empty", "too-big", "memory", "bad-keyfile" (archivo llave XML de KeePass con
 *   Hash que no coincide). */
int  imp_parse(int kind, const uint8_t *data, size_t len, const char *password,
               const uint8_t *keyfile, size_t kflen, ImpList *l);
void imp_free(ImpList *l);           /* borra y libera todo (también l->v) */

/* CSV propio (IMP_LORD_CSV), con encabezado; las entradas como las de ImpList. *out en smem. */
int  exp_csv(const ImpList *l, char **out, size_t *outlen);

/* Utilidades compartidas */
char *imp_strdup(const char *s);                      /* en smem */
ImpEntry *imp_add(ImpList *l);                        /* entrada nueva en cero, o NULL */
/* Fecha ISO 8601 ("2024-03-05T10:20:30Z", con fracción u hora local que se toma como UTC; o solo
 * la fecha) → segundos Unix; 0 si no se entiende. */
int64_t imp_parse_iso(const char *s);

#endif
