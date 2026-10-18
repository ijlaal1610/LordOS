/* wasm.h — intérprete de WebAssembly propio de LordOS (sin dependencias; ver docs/WASM.md).
 *
 * Soporta WebAssembly 2.0 sin SIMD: el MVP más sign-extension, non-trapping float-to-int,
 * multi-value, bulk memory, reference types y mutable globals. Una sola memoria de 32 bits.
 *
 * Uso:
 *   WasmModule *m = wasm_module_new(bytes, len, &err);          // decodifica, valida y compila
 *   WasmExtern imports[...] = ...;                               // en el orden de wasm_import_*
 *   WasmInstance *inst = wasm_instantiate(m, imports, n, &err);  // corre la función start
 *   WasmExtern e = wasm_instance_export_find(inst, "suma");
 *   WasmVal args[2] = { { .i32 = 2 }, { .i32 = 3 } }, res[1];
 *   if (wasm_call(e.func, args, res, &err)) ...                  // trampa o error del host
 *
 * Objetos con cuenta de referencias: módulo, instancia, función (las de una instancia cuentan
 * en su instancia), memoria, tabla y global. Todo lo que devuelve *_new o wasm_instantiate
 * empieza con una referencia; wasm_extern_ref/unref para los de los exports. Un solo hilo. */
#ifndef LORD_WASM_H
#define LORD_WASM_H

#include <stddef.h>
#include <stdint.h>

enum {
    WASM_I32 = 0x7f, WASM_I64 = 0x7e, WASM_F32 = 0x7d, WASM_F64 = 0x7c,
    WASM_FUNCREF = 0x70, WASM_EXTERNREF = 0x6f,
};
enum { WASM_EXT_FUNC = 0, WASM_EXT_TABLE = 1, WASM_EXT_MEMORY = 2, WASM_EXT_GLOBAL = 3 };

#define WASM_PAGE 65536u
#define WASM_NO_MAX UINT32_MAX

/* Un valor. f32 y f64 guardan sus bits tal cual (NaN incluidos). funcref = WasmFunc * (NULL
 * es ref.null); externref = valor opaco del host (0 es ref.null). */
typedef union {
    int32_t i32; uint32_t u32;
    int64_t i64; uint64_t u64;
    float f32; double f64;
    void *ref;
} WasmVal;

typedef enum {
    WASM_OK = 0,
    WASM_ERR_MALFORMED,                 /* el binario no se puede decodificar */
    WASM_ERR_INVALID,                   /* no valida (tipos, índices...) */
    WASM_ERR_LINK,                      /* imports que no corresponden */
    WASM_ERR_TRAP,                      /* trampa al ejecutar */
    WASM_ERR_EXHAUSTED,                 /* pila de llamadas agotada */
    WASM_ERR_HOST,                      /* una función del host falló (p. ej. excepción de JS) */
    WASM_ERR_NOMEM,
} WasmErrKind;

typedef struct {
    WasmErrKind kind;
    char msg[160];
} WasmError;

/* Llena err (para las funciones del host). */
void wasm_err(WasmError *err, WasmErrKind kind, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Tipo de función (internado: dos tipos iguales son el mismo puntero). */
typedef struct WasmFuncType {
    uint32_t nparams, nresults;
    const uint8_t *params, *results;
} WasmFuncType;

typedef struct WasmModule WasmModule;
typedef struct WasmInstance WasmInstance;
typedef struct WasmFunc WasmFunc;
typedef struct WasmMemory WasmMemory;
typedef struct WasmTable WasmTable;
typedef struct WasmGlobal WasmGlobal;

typedef struct {
    int kind;                           /* WASM_EXT_* */
    union { WasmFunc *func; WasmTable *table; WasmMemory *memory; WasmGlobal *global; void *ptr; };
} WasmExtern;

/* Descripción de un import o export. */
typedef struct {
    int kind;
    const WasmFuncType *func;           /* función */
    uint8_t valtype;                    /* global: tipo; tabla: tipo de elemento */
    int mut;                            /* global */
    uint32_t min, max;                  /* tabla y memoria (páginas); max = WASM_NO_MAX si no hay */
} WasmExternType;

const WasmFuncType *wasm_functype(const uint8_t *params, uint32_t np, const uint8_t *results, uint32_t nr);

/* ---- módulos ---- */
WasmModule *wasm_module_new(const uint8_t *bytes, size_t len, WasmError *err);
int  wasm_validate(const uint8_t *bytes, size_t len, WasmError *err);   /* 0 si es válido */
void wasm_module_ref(WasmModule *m);
void wasm_module_unref(WasmModule *m);
uint32_t wasm_module_num_imports(const WasmModule *m);
/* Los nombres terminan en 0 pero pueden tener bytes 0 adentro: len (si no es NULL) da el largo. */
const char *wasm_import_module(const WasmModule *m, uint32_t i, size_t *len);
const char *wasm_import_name(const WasmModule *m, uint32_t i, size_t *len);
WasmExternType wasm_import_type(const WasmModule *m, uint32_t i);
uint32_t wasm_module_num_exports(const WasmModule *m);
const char *wasm_export_name(const WasmModule *m, uint32_t i, size_t *len);
WasmExternType wasm_export_type(const WasmModule *m, uint32_t i);
/* Secciones custom con ese nombre (WebAssembly.Module.customSections): la n-ésima, o NULL. */
const uint8_t *wasm_custom_section(const WasmModule *m, const char *name, uint32_t n, size_t *len);

/* ---- instancias ---- */
/* imports en el orden de wasm_import_*; se valida cada tipo (LINK si no corresponde). Si la
 * función start (o un segmento activo) atrapa, devuelve NULL con kind TRAP. */
WasmInstance *wasm_instantiate(WasmModule *m, const WasmExtern *imports, uint32_t nimports, WasmError *err);
void wasm_instance_ref(WasmInstance *i);
void wasm_instance_unref(WasmInstance *i);
WasmModule *wasm_instance_module(const WasmInstance *i);
WasmExtern wasm_instance_export(const WasmInstance *i, uint32_t idx);      /* sin sumar referencia */
WasmExtern wasm_instance_export_find(const WasmInstance *i, const char *name);  /* kind -1 si no está */
WasmExtern wasm_instance_export_find_len(const WasmInstance *i, const char *name, size_t len);

/* ---- funciones ---- */
/* Función del host: lee args (nparams) y escribe results (nresults). Devuelve 0, o un error
 * (kind TRAP o HOST) que aborta la llamada wasm en curso. */
typedef int (*WasmHostFn)(void *ud, const WasmVal *args, WasmVal *results, WasmError *err);
WasmFunc *wasm_func_new_host(const WasmFuncType *type, WasmHostFn fn, void *ud, void (*free_ud)(void *));
const WasmFuncType *wasm_func_type(const WasmFunc *f);
int  wasm_func_is_host(const WasmFunc *f);
void *wasm_func_host_data(const WasmFunc *f);
void wasm_func_ref(WasmFunc *f);
void wasm_func_unref(WasmFunc *f);
/* Un puntero libre para el host (el binding de JS guarda ahí el objeto función). */
void **wasm_func_slot(WasmFunc *f);
int  wasm_call(WasmFunc *f, const WasmVal *args, WasmVal *results, WasmError *err);

/* ---- memorias (páginas de 64 KiB; los datos no cambian de dirección al crecer) ---- */
WasmMemory *wasm_memory_new(uint32_t min, uint32_t max, WasmError *err);
void     wasm_memory_ref(WasmMemory *m);
void     wasm_memory_unref(WasmMemory *m);
uint8_t *wasm_memory_data(const WasmMemory *m);
size_t   wasm_memory_size(const WasmMemory *m);                 /* en bytes */
uint32_t wasm_memory_max(const WasmMemory *m);                  /* páginas */
int64_t  wasm_memory_grow(WasmMemory *m, uint32_t delta);       /* páginas anteriores, o -1 */
/* Aviso de que creció (desde wasm o el host), p. ej. para desvincular el ArrayBuffer de JS. */
void     wasm_memory_on_grow(WasmMemory *m, void (*fn)(void *ud, WasmMemory *m), void *ud);
/* Punteros libres para el host (el binding de JS guarda el objeto Memory y su ArrayBuffer). */
void   **wasm_memory_slots(WasmMemory *m);                      /* 2 */

/* ---- tablas ---- */
WasmTable *wasm_table_new(uint8_t elemtype, uint32_t min, uint32_t max, WasmVal init, WasmError *err);
void     wasm_table_ref(WasmTable *t);
void     wasm_table_unref(WasmTable *t);
uint8_t  wasm_table_elemtype(const WasmTable *t);
uint32_t wasm_table_size(const WasmTable *t);
uint32_t wasm_table_max(const WasmTable *t);
int      wasm_table_get(const WasmTable *t, uint32_t i, WasmVal *v);   /* 0 bien, -1 fuera de rango */
int      wasm_table_set(WasmTable *t, uint32_t i, WasmVal v);
int64_t  wasm_table_grow(WasmTable *t, uint32_t delta, WasmVal init);
void   **wasm_table_slot(WasmTable *t);

/* ---- globales ---- */
WasmGlobal *wasm_global_new(uint8_t type, int mut, WasmVal v);
void    wasm_global_ref(WasmGlobal *g);
void    wasm_global_unref(WasmGlobal *g);
uint8_t wasm_global_type(const WasmGlobal *g);
int     wasm_global_mut(const WasmGlobal *g);
WasmVal wasm_global_get(const WasmGlobal *g);
void    wasm_global_set(WasmGlobal *g, WasmVal v);
void  **wasm_global_slot(WasmGlobal *g);

void wasm_extern_ref(WasmExtern e);
void wasm_extern_unref(WasmExtern e);

/* Límites (se pueden ajustar antes de la primera llamada). */
extern uint32_t wasm_max_call_depth;    /* marcos de llamada wasm anidados (por defecto 20000) */
extern uint32_t wasm_max_memory_pages;  /* tope por memoria (por defecto 65536 = 4 GiB) */

#endif
