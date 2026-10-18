/* keys.h — claves públicas oficiales de LordOS (Ed25519, en hex). Lo firmado con una de estas
 * claves se muestra como «LordOS»; el catálogo (catalog.json) solo vale firmado por una de ellas.
 *
 * Hoy hay una sola: la clave de DESARROLLO (packaging/keys/lordos-dev.pub). Su clave privada no
 * está en el repositorio: vive en la máquina de desarrollo, en ~/.config/lordos-dev/lordos-dev.key
 * (ver docs/FIRMA.md). Para una versión pública se agrega la clave definitiva y se quita esta.
 * Están compiladas en el binario a propósito: ningún archivo de la carpeta personal puede sumar
 * una clave oficial. */
#ifndef LORD_SEC_KEYS_H
#define LORD_SEC_KEYS_H

static const char *const lordos_official_keys[] = {
    "3deb68366612b82046761673f6d8936a10b87aa4dab52b90d5a7039354b7ddb7",     /* LordOS (desarrollo) */
    NULL,
};

#endif
