/* proto.h — protocolo entre lord-server y su ayudante de documentos lord-doc (ver docs/DOCUMENTOS.md).
 *
 * lord-doc es un proceso aparte, sin red y encerrado (Landlock + seccomp), que abre los PDF
 * (Poppler), los comprimidos (libarchive) y las fuentes (FreeType). lord-server nunca enlaza esas
 * bibliotecas: un PDF o un .zip malicioso solo puede tirar abajo al ayudante.
 *
 * El canal es un socketpair SOCK_SEQPACKET (fd 3 del ayudante). Cada mensaje es un DocHdr + JSON
 * (UTF-8) y, con SCM_RIGHTS, hasta DOC_MAX_FDS descriptores. Si el JSON no entra en un mensaje
 * (más de DOC_INLINE_MAX) viaja en un memfd sellado y el encabezado lleva DOCF_JSON_FD.
 *
 * Arranque: lord-server manda primero mensajes DQ_INIT_FD (los archivos que el ayudante puede
 * usar: el documento, la carpeta de destino de una extracción, los elementos a comprimir) y
 * después DQ_INIT_GO. Recién ahí el ayudante se encierra, con Landlock armado a partir de esos
 * fds, y empieza a atender. Lo que llega después nunca agrega permisos de archivos.
 *
 * Las respuestas llevan el id del pedido. Una imagen (página, muestra de una fuente) vuelve en un
 * memfd sellado con píxeles ARGB32 premultiplicados (los de cairo y los del motor de LordOS). */
#ifndef LORD_DOC_PROTO_H
#define LORD_DOC_PROTO_H

#include <stdint.h>

#define DOC_INLINE_MAX (48 * 1024)
#define DOC_JSON_MAX   (16 * 1024 * 1024)   /* JSON más grande que se acepta (en memfd) */
#define DOC_MAX_FDS    16
#define DOC_IMG_MAX    8192                 /* lado máximo de una imagen devuelta */
#define DOC_INIT_FDS   256                  /* fds de arranque como máximo */

typedef struct {
    uint32_t op;        /* DQ_* en los pedidos, DR_* en las respuestas */
    uint32_t id;        /* id del pedido (0 en el arranque) */
    uint32_t len;       /* bytes de JSON que siguen en el mensaje (0 si va en memfd o no hay) */
    uint32_t flags;     /* DOCF_* */
} DocHdr;

enum { DOCF_JSON_FD = 1 };                  /* el primer fd es un memfd con el JSON */

/* Pedidos */
enum {
    DQ_INIT_FD = 1,     /* {"role":"ro"|"rw"|"file"|"out"} + 1..16 fds, antes de encerrarse */
    DQ_INIT_GO,         /* {"mode":"pdf"|"arc"|"font"|"make"} */

    DQ_PDF_OPEN = 10,   /* {"slot":0,"password":""} */
    DQ_PDF_RENDER,      /* {"page","width","rot","invert"} → DR_IMG */
    DQ_PDF_SELECT,      /* {"page","x1","y1","x2","y2","style":0|1|2} → {text, rects} */
    DQ_PDF_FIND,        /* {"text","case","whole"} → DR_PART por página, DR_DONE */
    DQ_PDF_OUTLINE,     /* → {items:[{title,page,kids}]} */
    DQ_PDF_PAGEINFO,    /* {"page"} → {links, fields, annots} */
    DQ_PDF_SETFIELD,    /* {"id","value"} */
    DQ_PDF_ANNOT,       /* {"page","type":"highlight"|"note"|"ink",...} */
    DQ_PDF_SAVE,        /* {"slot"} (fd de salida) → DR_DONE */
    DQ_PDF_BLOB,        /* → DR_BLOB (el PDF con los cambios, en un memfd) */
    DQ_PDF_UNDO,        /* {"page"}: quita la última anotación agregada en esa página */
    DQ_PDF_SIZES,       /* → {sizes:[[w,h]...], forms} (open con "quick" solo trae la primera) */
    DQ_PDF_THUMB,       /* (sesión util) fd + {"width","height","invert"} → DR_IMG de la página 1 */
    DQ_WARM,            /* (sesión util) prepara fontconfig y Poppler, para que la Vista rápida no espere */

    DQ_ARC_LIST = 30,   /* {"slot"} → {entries:[{p,s,m,d,l}], truncated, format} */
    DQ_ARC_EXTRACT,     /* {"slot","dir","only":[...]} → DR_PROG..., DR_DONE */
    DQ_ARC_CREATE,      /* {"out","items":[{"slot","name"}],"format":"zip"} → DR_PROG..., DR_DONE */

    DQ_FONT_INFO = 50,  /* fd + {"index"} → {family, style, ...} */
    DQ_FONT_RENDER,     /* fd + {"index","lines":[{text,size}],"width","fg","bg"} → DR_IMG */
    DQ_FONT_GRID,       /* fd + {"index","cps":[...],"cell","cols","fg","bg"} → DR_IMG + {present} */

    DQ_MAKE_TEXT = 70,  /* {"text","title","mono","size","paper"} → DR_BLOB (PDF) */
    DQ_MAKE_IMAGE,      /* fd + {"paper","fit"} → DR_BLOB (PDF) */
    DQ_IMG_DECODE,      /* fd + {"maxw","maxh"} → DR_IMG */
    DQ_IMG_CONVERT,     /* fd entrada + fd salida + {"format":"png"|"jpeg"|"pdf","quality"} → DR_DONE */

    DQ_SELFTEST = 90,   /* prueba del encierro: intenta lo prohibido y dice qué pasó (tests/doc_test.c) */
};

/* Respuestas */
enum {
    DR_DONE = 1,        /* terminó: JSON con el resultado */
    DR_ERR,             /* terminó con error: {"error":"código","detail":"…"} */
    DR_PROG,            /* progreso (no termina) */
    DR_PART,            /* resultado parcial (no termina) */
    DR_IMG,             /* terminó: {"w","h"} + memfd ARGB32 (w*h*4) */
    DR_BLOB,            /* terminó: {"size"} + memfd con los bytes */
};

#endif
