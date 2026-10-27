# Compresor y Descompresor Huffman en C

## Descripción

Proyecto que implementa dos programas en lenguaje C para comprimir y descomprimir archivos mediante el algoritmo de Huffman.

- `comprimir.c`: calcula las frecuencias de los bytes, construye el árbol de Huffman, genera los códigos binarios y almacena los datos comprimidos.
- `descomprimir.c`: reconstruye el árbol a partir del archivo comprimido y recupera el contenido original.

## Formato de archivo

Los archivos comprimidos utilizan la extensión `.huf` y un formato binario propio identificado por la firma `HUFF`, versión 1.

La cabecera contiene:

| Campo | Tamaño |
|---|---:|
| Firma `HUFF` | 4 bytes |
| Versión del formato | 1 byte |
| Tamaño original | 8 bytes |
| CRC32 del original | 4 bytes |
| Longitud del árbol en bits | 2 bytes |

Después de la cabecera se almacena el árbol serializado en preorden y, a continuación, los datos codificados con Huffman.

Los campos numéricos de la cabecera se almacenan en orden big-endian. El CRC32 permite comprobar la integridad del contenido recuperado.

## Requisitos

- GCC.
- Biblioteca estándar de C.

## Compilación

Compresor:

```bash
gcc -O2 -Wall -Wextra comprimir.c -o comprimir
```

Descompresor:

```bash
gcc -O2 -Wall -Wextra descomprimir.c -o descomprimir
```

## Uso

Comprimir un archivo:

```bash
./comprimir archivo.txt
```

Esto genera `archivo.txt.huf`.

También se puede especificar el nombre de salida:

```bash
./comprimir archivo.txt salida.huf
```

Descomprimir un archivo:

```bash
./descomprimir archivo.txt.huf
```

El programa genera un archivo recuperado para evitar sobrescribir el original.

Para elegir explícitamente el archivo de salida:

```bash
./descomprimir archivo.txt.huf recuperado.txt
```

## Prueba de integridad

Se puede comparar el archivo original con el recuperado mediante:

```bash
cmp archivo.txt recuperado.txt
```

Si no se muestra ninguna diferencia, ambos archivos son idénticos.

## Lenguaje

C
