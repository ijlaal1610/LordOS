/* map.h — mapa del mundo del Monitor dibujado en vectores (ver map.c y docs/MONITOR.md).
 *
 * Los contornos de los países (assets/monitor/land.bin, Natural Earth 1:50m) se rellenan con
 * antialias a cualquier zoom, con bordes, arcos desde "este equipo" y un punto por lugar. Todo en
 * un hilo propio: el hilo de la pantalla pide un cuadro (map_request) y después lo retira
 * (map_take) cuando está listo, sin esperar. */
#ifndef LORD_MAP_H
#define LORD_MAP_H

#include <stdint.h>

#define MAP_MAX_W 2048
#define MAP_MAX_H 2048
#define MAP_MAX_DOTS 256

typedef struct { float lon, lat, r; int sel; } MapDot;

typedef struct {
    int w, h;
    double lon0, lat0, ppd;             /* esquina superior izquierda y píxeles por grado */
    uint32_t land, border, accent, home_color, sel_color;   /* 0xRRGGBBAA */
    int has_home;
    double hlon, hlat;
    int ndots;
    MapDot dots[MAP_MAX_DOTS];
} MapReq;

void map_init(const char *assets_dir);
/* Pide un cuadro (reemplaza al pedido anterior si todavía no empezó). Devuelve su número. */
uint64_t map_request(const MapReq *r);
/* El último cuadro terminado, si es más nuevo que *seq: copia (malloc) en *rgba; 1 si hay. */
int map_take(uint64_t *seq, uint8_t **rgba, MapReq *view, double *ms);

#endif
