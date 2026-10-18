/* proto.h — protocolo local entre lord-server (la sesión), lord-sessiond (que le presta el
 * monitor) y el puente de red del escritorio remoto.
 *
 * Socket unix de la sesión: $XDG_RUNTIME_DIR/lordos/remote.sock (lo crea lord-server ya como el
 * usuario, 0600; root también entra). Cada mensaje: u32 largo (de lo que sigue, little endian),
 * u8 tipo y los datos. Ver docs/REMOTO.md y docs/SESIONES.md. */
#ifndef LORD_REMOTE_PROTO_H
#define LORD_REMOTE_PROTO_H

#define RP_MAX_MSG (40u * 1024 * 1024)   /* un rectángulo de 2560×1600 entra de sobra */

/* sesión → puente */
enum {
    RP_SIZE = 1,        /* u16 ancho, u16 alto: tamaño de la pantalla (al conectar y si cambia) */
    RP_RECT = 2,        /* u16 x, y, w, h + w*h píxeles u32 (0xAARRGGBB en memoria little endian) */
    RP_END = 3,         /* fin del cuadro: el puente contesta RP_READY cuando lo entregó */
    RP_CURSOR = 4,      /* i16 x, y, u8 modo (0 flecha, 1 imagen, 2 oculto) */
    RP_SHAPE = 5,       /* u16 w, h, hx, hy + w*h píxeles u32: imagen del cursor de una app */
    RP_CLIP = 6,        /* texto UTF-8: el portapapeles de la sesión cambió */
    RP_SESSION = 9,     /* JSON: pedido de la sesión al monitor ({"a":"lock"} o {"a":"switch"}) */
    RP_GREETER = 20,    /* JSON: pedido de la pantalla de ingreso a lord-sessiond (solo en modo greeter) */
};

/* puente → sesión */
enum {
    RP_READY = 10,      /* listo para el próximo cuadro (control de flujo) */
    RP_PTR = 11,        /* i16 x, y (absolutos, en píxeles de la sesión) */
    RP_BTN = 12,        /* u8 botón (0 izq, 1 medio, 2 der), u8 apretado */
    RP_WHEEL = 13,      /* i8 pasos (+ arriba) */
    RP_KEY = 14,        /* u16 código evdev, u8 valor (1 apretada, 0 suelta, 2 repite) */
    RP_SETCLIP = 15,    /* texto UTF-8 que el usuario copió en su navegador */
    RP_HELLO = 16,      /* "ip usuario": quién se conectó (aviso en el monitor) */
    RP_RESIZE = 17,     /* u16 ancho, u16 alto: tamaño pedido (si la sesión está en el monitor, no vale) */
    RP_SEAT = 18,       /* esta conexión es lord-sessiond, que maneja el monitor: no recibe la pantalla,
                         * solo los pedidos (RP_SESSION, RP_GREETER); solo root */
    RP_DISPLAY = 19,    /* + fds (SCM_RIGHTS): la placa de video (ya master) y los dispositivos de
                         * entrada. La sesión pasa a mostrarse en el monitor (con GPU si hay). Solo root */
    RP_UNDISPLAY = 22,  /* la sesión deja el monitor (vuelve a dibujarse en memoria). Solo root */
    RP_GREETER_IN = 21, /* JSON: respuesta o aviso de lord-sessiond a la pantalla de ingreso */
    RP_INPUT_ADD = 23,  /* + fds: teclados, mouses o touchpads enchufados después (solo a la sesión del
                         * monitor; los desenchufados los cierra la sesión al recibir ENODEV). Solo root */
    RP_KEYRING = 24,    /* texto: la contraseña recién verificada en la pantalla de ingreso; la sesión abre
                         * con ella su llavero (lord-vault, docs/CONTRASENAS.md) y la borra. Solo root */
    RP_SCROLL = 30,     /* u8 fase (1 empieza, 2 sigue, 3 termina), i32 dx, dy en 1/256 de píxel: trackpad del navegador */
    RP_PINCH = 31,      /* u8 fase, i32 escala ×65536 (relativa al comienzo), i32 giro en grados ×256 */
};

#endif
