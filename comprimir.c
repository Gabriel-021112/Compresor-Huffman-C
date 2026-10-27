/*
 * comprimir.c - Compresor Huffman (formato propio "HUFF" version 1)
 *
 * Compilar:  gcc -O2 -Wall -Wextra comprimir.c -o comprimir
 * Uso:       ./comprimir <archivo_entrada> [archivo_salida]
 *            (si no se da salida, se crea <archivo_entrada>.huf)
 *
 * ======================= FORMATO DEL ARCHIVO .huf =======================
 *
 *   Offset  Tam   Campo
 *   ------  ----  ---------------------------------------------------------
 *   0       4     Firma ("magic"): "HUFF"
 *   4       1     Version del formato (1)
 *   5       8     Tamano del archivo original en bytes     (uint64, big-endian)
 *   13      4     CRC32 del archivo original               (uint32, big-endian)
 *   17      2     Longitud del arbol serializado, EN BITS  (uint16, big-endian)
 *   19      T     Arbol serializado, T = ceil(bits_arbol / 8) bytes
 *   19+T    ...   Datos codificados con Huffman
 *
 *   * Arbol (recorrido en preorden, bit a bit):
 *         nodo interno -> bit 0, luego subarbol izquierdo y subarbol derecho
 *         hoja         -> bit 1, luego los 8 bits del byte (simbolo)
 *     El ultimo byte del arbol se rellena con ceros.
 *     Un arbol con n hojas ocupa (10n - 1) bits (maximo 2559 bits con 256 hojas).
 *
 *   * Datos: concatenacion de los codigos de cada byte del original
 *     (izquierda = 0, derecha = 1). Sobran de 0 a 7 bits de relleno al final;
 *     el descompresor sabe cuantos simbolos leer por el campo "tamano original".
 *
 *   * Casos especiales:
 *         - archivo vacio: tamano 0, arbol de 0 bits y sin datos.
 *         - un solo simbolo distinto: el arbol es una sola hoja y no hay datos
 *           (el descompresor repite el simbolo "tamano" veces).
 * =========================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAGIC          "HUFF"
#define VERSION        1
#define TAM_CABECERA   19
#define MAX_NODOS      511          /* 256 hojas + 255 nodos internos */

/* ------------------------------ utilidades ------------------------------ */
static void morir(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

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

static void escribir_be(uint8_t *dst, uint64_t valor, int n_bytes) {
    for (int i = n_bytes - 1; i >= 0; i--) {
        dst[i] = (uint8_t)(valor & 0xFF);
        valor >>= 8;
    }
}

/* ------------------------- escritor de bits (BitW) ------------------------ */
typedef struct {
    uint8_t *buf;
    size_t   len, cap;      /* bytes completos escritos / capacidad */
    uint8_t  actual;        /* bits pendientes */
    int      n_actual;      /* cuantos bits hay en "actual" (0..7) */
} BitW;

static void bw_iniciar(BitW *w, size_t cap) {
    w->cap = cap ? cap : 16;
    w->buf = malloc(w->cap);
    if (!w->buf) morir("sin memoria");
    w->len = 0; w->actual = 0; w->n_actual = 0;
}

static void bw_byte(BitW *w, uint8_t b) {
    if (w->len == w->cap) {
        w->cap *= 2;
        w->buf = realloc(w->buf, w->cap);
        if (!w->buf) morir("sin memoria");
    }
    w->buf[w->len++] = b;
}

static void bw_bit(BitW *w, int bit) {
    w->actual = (uint8_t)((w->actual << 1) | (bit & 1));
    if (++w->n_actual == 8) {
        bw_byte(w, w->actual);
        w->actual = 0; w->n_actual = 0;
    }
}

static uint64_t bw_bits_escritos(const BitW *w) {
    return (uint64_t)w->len * 8 + (uint64_t)w->n_actual;
}

static void bw_terminar(BitW *w) {      /* rellena con ceros el ultimo byte */
    if (w->n_actual > 0) {
        bw_byte(w, (uint8_t)(w->actual << (8 - w->n_actual)));
        w->actual = 0; w->n_actual = 0;
    }
}

/* ------------------------------- el arbol -------------------------------- */
typedef struct {
    uint64_t freq;
    int izq, der;           /* indices en nodos[], -1 si es hoja */
    int simbolo;            /* 0..255 en hojas, -1 en nodos internos */
} Nodo;

static Nodo nodos[MAX_NODOS];
static int  n_nodos = 0;

static int heap[MAX_NODOS + 1];     /* min-heap de indices de nodos */
static int n_heap = 0;

/* orden: menor frecuencia primero; empate -> menor indice (resultado determinista) */
static int menor(int a, int b) {
    if (nodos[a].freq != nodos[b].freq) return nodos[a].freq < nodos[b].freq;
    return a < b;
}

static void heap_push(int x) {
    int i = n_heap++;
    heap[i] = x;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!menor(heap[i], heap[p])) break;
        int t = heap[i]; heap[i] = heap[p]; heap[p] = t;
        i = p;
    }
}

static int heap_pop(void) {
    int res = heap[0];
    heap[0] = heap[--n_heap];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < n_heap && menor(heap[l], heap[m])) m = l;
        if (r < n_heap && menor(heap[r], heap[m])) m = r;
        if (m == i) break;
        int t = heap[i]; heap[i] = heap[m]; heap[m] = t;
        i = m;
    }
    return res;
}

/* Construye el arbol de Huffman; devuelve el indice de la raiz (-1 si no hay datos) */
static int construir_arbol(const uint64_t freq[256]) {
    for (int s = 0; s < 256; s++) {
        if (freq[s] == 0) continue;
        nodos[n_nodos] = (Nodo){ freq[s], -1, -1, s };
        heap_push(n_nodos++);
    }
    if (n_heap == 0) return -1;
    while (n_heap > 1) {
        int a = heap_pop();
        int b = heap_pop();
        nodos[n_nodos] = (Nodo){ nodos[a].freq + nodos[b].freq, a, b, -1 };
        heap_push(n_nodos++);
    }
    return heap[0];
}

static int es_hoja(int i) { return nodos[i].izq < 0 && nodos[i].der < 0; }

/* Codigos: un codigo puede medir hasta 255 bits, asi que se guarda como arreglo de 0/1 */
static uint8_t codigo[256][256];
static int     largo[256];

static void generar_codigos(int nodo, uint8_t *camino, int prof) {
    if (es_hoja(nodo)) {
        int s = nodos[nodo].simbolo;
        memcpy(codigo[s], camino, (size_t)prof);
        largo[s] = prof;
        return;
    }
    camino[prof] = 0;
    generar_codigos(nodos[nodo].izq, camino, prof + 1);
    camino[prof] = 1;
    generar_codigos(nodos[nodo].der, camino, prof + 1);
}

/* Serializacion en preorden: interno = 0 ; hoja = 1 + 8 bits del simbolo */
static void serializar_arbol(BitW *w, int nodo) {
    if (es_hoja(nodo)) {
        bw_bit(w, 1);
        for (int i = 7; i >= 0; i--)
            bw_bit(w, (nodos[nodo].simbolo >> i) & 1);
        return;
    }
    bw_bit(w, 0);
    serializar_arbol(w, nodos[nodo].izq);
    serializar_arbol(w, nodos[nodo].der);
}

/* --------------------------------- main ---------------------------------- */
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Uso: %s <archivo_entrada> [archivo_salida]\n", argv[0]);
        return 1;
    }
    const char *ruta_entrada = argv[1];
    char ruta_salida_defecto[4096];
    const char *ruta_salida;
    if (argc == 3) {
        ruta_salida = argv[2];
    } else {
        if (strlen(ruta_entrada) + 5 > sizeof ruta_salida_defecto) morir("ruta demasiado larga");
        snprintf(ruta_salida_defecto, sizeof ruta_salida_defecto, "%s.huf", ruta_entrada);
        ruta_salida = ruta_salida_defecto;
    }

    /* ---- leer el archivo completo en binario ---- */
    FILE *f = fopen(ruta_entrada, "rb");
    if (!f) { perror(ruta_entrada); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) morir("no se pudo medir el archivo");
    long medida = ftell(f);
    if (medida < 0) morir("no se pudo medir el archivo");
    rewind(f);
    size_t tam = (size_t)medida;
    uint8_t *datos = malloc(tam ? tam : 1);
    if (!datos) morir("sin memoria para leer el archivo");
    if (fread(datos, 1, tam, f) != tam) morir("error al leer el archivo");
    fclose(f);

    /* ---- frecuencias, arbol y codigos ---- */
    uint64_t freq[256] = {0};
    for (size_t i = 0; i < tam; i++) freq[datos[i]]++;

    int raiz = construir_arbol(freq);
    if (raiz >= 0 && !es_hoja(raiz)) {
        uint8_t camino[256];
        generar_codigos(raiz, camino, 0);
    }

    /* ---- serializar el arbol ---- */
    BitW arbol;
    bw_iniciar(&arbol, 400);
    if (raiz >= 0) serializar_arbol(&arbol, raiz);
    uint64_t bits_arbol = bw_bits_escritos(&arbol);
    bw_terminar(&arbol);

    /* ---- codificar los datos (solo si hay 2 o mas simbolos distintos) ---- */
    BitW cuerpo;
    bw_iniciar(&cuerpo, tam / 2 + 16);
    if (raiz >= 0 && !es_hoja(raiz)) {
        for (size_t i = 0; i < tam; i++) {
            int s = datos[i];
            for (int k = 0; k < largo[s]; k++)
                bw_bit(&cuerpo, codigo[s][k]);
        }
        bw_terminar(&cuerpo);
    }

    /* ---- cabecera ---- */
    iniciar_crc();
    uint8_t cab[TAM_CABECERA];
    memcpy(cab, MAGIC, 4);
    cab[4] = VERSION;
    escribir_be(cab + 5, (uint64_t)tam, 8);
    escribir_be(cab + 13, crc32(datos, tam), 4);
    escribir_be(cab + 17, bits_arbol, 2);

    /* ---- escribir el .huf ---- */
    FILE *out = fopen(ruta_salida, "wb");
    if (!out) { perror(ruta_salida); return 1; }
    if (fwrite(cab, 1, TAM_CABECERA, out) != TAM_CABECERA ||
        fwrite(arbol.buf, 1, arbol.len, out) != arbol.len ||
        fwrite(cuerpo.buf, 1, cuerpo.len, out) != cuerpo.len)
        morir("error al escribir el archivo de salida");
    if (fclose(out) != 0) morir("error al cerrar el archivo de salida");

    size_t total = TAM_CABECERA + arbol.len + cuerpo.len;
    printf("Archivo comprimido: %s\n", ruta_salida);
    printf("  Original : %zu bytes\n", tam);
    printf("  Cabecera : %d bytes | Arbol: %zu bytes | Datos: %zu bytes\n",
           TAM_CABECERA, arbol.len, cuerpo.len);
    printf("  Total    : %zu bytes", total);
    if (tam > 0) printf("  (%.1f%% del original)", 100.0 * (double)total / (double)tam);
    printf("\n");

    free(datos); free(arbol.buf); free(cuerpo.buf);
    return 0;
}
