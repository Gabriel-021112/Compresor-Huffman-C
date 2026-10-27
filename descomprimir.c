/*
 * descomprimir.c - Descompresor Huffman (formato propio "HUFF" version 1)
 *
 * Compilar:  gcc -O2 -Wall -Wextra descomprimir.c -o descomprimir
 * Uso:       ./descomprimir <archivo.huf> [archivo_salida]
 *            (si no se da salida, se crea <nombre>_recuperado<ext>,
 *             para no pisar el archivo original)
 *
 * Lee archivos creados por ./comprimir. Formato (detalle en comprimir.c):
 *
 *   [0:4]   "HUFF"             [4]     version
 *   [5:13]  tamano original    [13:17] CRC32 del original
 *   [17:19] bits del arbol     [19:19+T] arbol en preorden     [19+T:] datos
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#define MAGIC          "HUFF"
#define VERSION        1
#define TAM_CABECERA   19
#define MAX_NODOS      511          /* 256 hojas + 255 nodos internos */
#define MAX_PROFUNDIDAD 256

static void morir(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

/* ------------------------------- CRC32 ------------------------------- */
static uint32_t tabla_crc[256];

static void iniciar_crc(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        tabla_crc[i] = c;
    }
}

static uint32_t crc32(const uint8_t *datos, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = tabla_crc[(c ^ datos[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint64_t leer_be(const uint8_t *src, int n_bytes) {
    uint64_t v = 0;
    for (int i = 0; i < n_bytes; i++) v = (v << 8) | src[i];
    return v;
}

/* ------------------------ reconstruccion del arbol ------------------------ */
typedef struct {
    int izq, der;           /* indices en nodos[], -1 si es hoja */
    int simbolo;            /* 0..255 en hojas, -1 en nodos internos */
} Nodo;

static Nodo nodos[MAX_NODOS];
static int  n_nodos = 0;

typedef struct {
    const uint8_t *buf;
    uint64_t n_bits, pos;
} BitR;

static int br_bit(BitR *r) {            /* devuelve 0, 1 o -1 si se acabaron los bits */
    if (r->pos >= r->n_bits) return -1;
    int bit = (r->buf[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
    r->pos++;
    return bit;
}

/* Lee un nodo en preorden. Devuelve su indice, o -1 si el arbol esta corrupto. */
static int leer_nodo(BitR *r, int prof) {
    if (prof > MAX_PROFUNDIDAD || n_nodos >= MAX_NODOS) return -1;
    int bit = br_bit(r);
    if (bit < 0) return -1;

    int idx = n_nodos++;
    if (bit == 1) {                         /* hoja: 8 bits de simbolo */
        int simbolo = 0;
        for (int i = 0; i < 8; i++) {
            int b = br_bit(r);
            if (b < 0) return -1;
            simbolo = (simbolo << 1) | b;
        }
        nodos[idx] = (Nodo){ -1, -1, simbolo };
        return idx;
    }
    int izq = leer_nodo(r, prof + 1);       /* nodo interno */
    if (izq < 0) return -1;
    int der = leer_nodo(r, prof + 1);
    if (der < 0) return -1;
    nodos[idx] = (Nodo){ izq, der, -1 };
    return idx;
}

static int es_hoja(int i) { return nodos[i].izq < 0 && nodos[i].der < 0; }

/* ---------------------------- nombre de salida ---------------------------- */
static void nombre_salida_defecto(const char *ruta, char *dst, size_t cap) {
    char base[4096];
    if (strlen(ruta) >= sizeof base) morir("ruta demasiado larga");
    strcpy(base, ruta);
    size_t n = strlen(base);
    if (n >= 4 && strcasecmp(base + n - 4, ".huf") == 0) base[n - 4] = '\0';

    char *barra = strrchr(base, '/');
    char *punto = strrchr(base, '.');
    if (punto && (!barra || punto > barra + 1)) {
        char ext[256];
        snprintf(ext, sizeof ext, "%s", punto);
        *punto = '\0';
        snprintf(dst, cap, "%s_recuperado%s", base, ext);
    } else {
        snprintf(dst, cap, "%s_recuperado", base);
    }
}

/* --------------------------------- main ---------------------------------- */
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Uso: %s <archivo.huf> [archivo_salida]\n", argv[0]);
        return 1;
    }
    const char *ruta_entrada = argv[1];
    char ruta_defecto[4200];
    const char *ruta_salida;
    if (argc == 3) {
        ruta_salida = argv[2];
    } else {
        nombre_salida_defecto(ruta_entrada, ruta_defecto, sizeof ruta_defecto);
        ruta_salida = ruta_defecto;
    }

    /* ---- leer el .huf completo ---- */
    FILE *f = fopen(ruta_entrada, "rb");
    if (!f) { perror(ruta_entrada); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) morir("no se pudo medir el archivo");
    long medida = ftell(f);
    if (medida < 0) morir("no se pudo medir el archivo");
    rewind(f);
    size_t tam_archivo = (size_t)medida;
    uint8_t *contenido = malloc(tam_archivo ? tam_archivo : 1);
    if (!contenido) morir("sin memoria");
    if (fread(contenido, 1, tam_archivo, f) != tam_archivo) morir("error al leer el archivo");
    fclose(f);

    /* ---- cabecera ---- */
    if (tam_archivo < TAM_CABECERA) morir("el archivo es demasiado corto para ser un .huf");
    if (memcmp(contenido, MAGIC, 4) != 0) morir("firma incorrecta: no es un archivo HUFF");
    if (contenido[4] != VERSION) {
        char msg[80];
        snprintf(msg, sizeof msg, "version %d no soportada (se esperaba %d)", contenido[4], VERSION);
        free(contenido);
        morir(msg);
    }
    uint64_t tam_original = leer_be(contenido + 5, 8);
    uint32_t crc_esperado = (uint32_t)leer_be(contenido + 13, 4);
    uint64_t bits_arbol   = leer_be(contenido + 17, 2);

    size_t bytes_arbol = (size_t)((bits_arbol + 7) / 8);
    size_t ini_datos = TAM_CABECERA + bytes_arbol;
    if (tam_archivo < ini_datos) morir("archivo corrupto: falta el arbol");
    const uint8_t *cuerpo = contenido + ini_datos;
    size_t tam_cuerpo = tam_archivo - ini_datos;

    /* ---- reconstruir el arbol ---- */
    int raiz = -1;
    if (bits_arbol > 0) {
        BitR r = { contenido + TAM_CABECERA, bits_arbol, 0 };
        raiz = leer_nodo(&r, 0);
        if (raiz < 0) morir("arbol corrupto");
        if (r.pos != bits_arbol) morir("arbol corrupto: sobran bits");
    }

    /* ---- decodificar ---- */
    uint8_t *datos = NULL;
    size_t n_salida = 0;
    if (tam_original > 0) {
        if (raiz < 0) morir("archivo corrupto: hay datos pero no hay arbol");

        if (es_hoja(raiz)) {                 /* un solo simbolo distinto */
            datos = malloc((size_t)tam_original);
            if (!datos) morir("sin memoria para el archivo descomprimido");
            memset(datos, nodos[raiz].simbolo, (size_t)tam_original);
            n_salida = (size_t)tam_original;
        } else {
            if (tam_original / 8 > tam_cuerpo)       /* cada simbolo usa al menos 1 bit */
                morir("archivo corrupto: los datos terminan antes de tiempo");
            datos = malloc((size_t)tam_original);
            if (!datos) morir("sin memoria para el archivo descomprimido");

            int nodo = raiz;
            for (size_t i = 0; i < tam_cuerpo && n_salida < tam_original; i++) {
                for (int b = 7; b >= 0; b--) {
                    nodo = ((cuerpo[i] >> b) & 1) ? nodos[nodo].der : nodos[nodo].izq;
                    if (es_hoja(nodo)) {
                        datos[n_salida++] = (uint8_t)nodos[nodo].simbolo;
                        nodo = raiz;
                        if (n_salida == tam_original) break;   /* lo que sobra es relleno */
                    }
                }
            }
            if (n_salida != tam_original)
                morir("archivo corrupto: los datos terminan antes de tiempo");
        }
    }

    /* ---- verificar integridad y escribir ---- */
    iniciar_crc();
    if (crc32(datos, n_salida) != crc_esperado)
        morir("CRC32 no coincide: el archivo esta danado");

    FILE *out = fopen(ruta_salida, "wb");
    if (!out) { perror(ruta_salida); return 1; }
    if (n_salida > 0 && fwrite(datos, 1, n_salida, out) != n_salida)
        morir("error al escribir el archivo de salida");
    if (fclose(out) != 0) morir("error al cerrar el archivo de salida");

    printf("Archivo descomprimido: %s (%zu bytes, CRC32 correcto)\n", ruta_salida, n_salida);
    free(datos); free(contenido);
    return 0;
}
