/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * CC3069 - Computacion Paralela y Distribuida  |  Laboratorio 03
 * busqueda_clave_aes_mejorada.c
 *
 * Mejoras sobre la version secuencial original:
 *   [M1] make_key usa SHA-256 para distribuir el candidato en los
 *        128 bits completos de la clave, en lugar de dejar los primeros
 *        8 bytes en cero.  (Integrante 1)
 *   [M2] Soporte para clave aleatoria con --random, separando el rol
 *        del oraculo del proceso de busqueda.  (Integrante 2)
 *   [M3] Reporte de progreso periodico y bandera 'success' para
 *        mayor claridad en el flujo de control.  (Integrante 3)
 *
 * Limitaciones conocidas:
 *   - AES-ECB no usa IV; no apto para multiples bloques en produccion.
 *   - Se exploran solo 2^20 de las 2^128 claves posibles de AES-128.
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#define TOTAL_KEYS        (UINT64_C(1) << 20)
#define DEFAULT_KEY       UINT64_C(12345)
#define MESSAGE_LEN       16
#define PROGRESS_INTERVAL UINT64_C(131072)  /* cada 2^17 candidatas */

static const unsigned char message[] = "Puedes lograrlo!";

_Static_assert(sizeof(message) - 1 == MESSAGE_LEN,
               "El mensaje debe tener exactamente 16 bytes.");

static void fail(const char *desc)
{
    fprintf(stderr, "%s\n", desc);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

static double get_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("Error al consultar el reloj");
        exit(EXIT_FAILURE);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* [M1] Deriva la clave AES-128 aplicando SHA-256 al candidato y
 *      tomando los primeros 16 bytes, distribuyendo bits en toda la clave. */
static void make_key(uint64_t candidate, unsigned char key[16])
{
    unsigned char input[8];
    unsigned char hash[SHA256_DIGEST_LENGTH];

    for (int i = 0; i < 8; i++)
        input[7 - i] = (unsigned char)((candidate >> (8 * i)) & 0xFF);

    SHA256(input, sizeof(input), hash);
    memcpy(key, hash, 16);
}

/* Cifra (encrypt=1) o descifra (encrypt=0) un bloque con AES-128-ECB. */
static void crypt_block(EVP_CIPHER_CTX *ctx, uint64_t candidate,
                        const unsigned char *input, unsigned char *output,
                        int encrypt)
{
    unsigned char key[16];
    int written = 0, final_written = 0;

    make_key(candidate, key);

    if (EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL, encrypt) != 1)
        fail("Error al inicializar AES.");
    if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)
        fail("Error al configurar el relleno.");
    if (EVP_CipherUpdate(ctx, output, &written, input, MESSAGE_LEN) != 1)
        fail("Error al procesar el bloque.");
    if (EVP_CipherFinal_ex(ctx, output + written, &final_written) != 1)
        fail("Error al finalizar la operacion AES.");
    if (written + final_written != MESSAGE_LEN)
        fail("Longitud inesperada del resultado.");
}

int main(int argc, char *argv[])
{
    /* [M2] Clave aleatoria dentro del espacio explorado con --random. */
    uint64_t secret_key = DEFAULT_KEY;
    if (argc > 1 && strcmp(argv[1], "--random") == 0) {
        srand((unsigned int)time(NULL));
        secret_key = (uint64_t)rand() % TOTAL_KEYS;
        printf("Modo aleatorio: clave secreta = %" PRIu64 "\n", secret_key);
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) fail("No se pudo crear el contexto de OpenSSL.");

    unsigned char cipher[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH] = {0};
    unsigned char plain [MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH] = {0};

    crypt_block(ctx, secret_key, message, cipher, 1);

    uint64_t found  = UINT64_MAX;
    int      success = 0;
    double   start   = get_time();

    for (uint64_t k = 0; k < TOTAL_KEYS && !success; k++) {
        crypt_block(ctx, k, cipher, plain, 0);

        if (memcmp(plain, message, MESSAGE_LEN) == 0) {
            found   = k;
            success = 1;
        }

        /* [M3] Progreso cada PROGRESS_INTERVAL candidatas. */
        if (k > 0 && (k % PROGRESS_INTERVAL) == 0) {
            printf("  Progreso: %" PRIu64 " / %" PRIu64 " (%.1f %%)\r",
                   k, TOTAL_KEYS, 100.0 * (double)k / (double)TOTAL_KEYS);
            fflush(stdout);
        }
    }

    double elapsed = get_time() - start;
    printf("\n");

    if (success) {
        printf("Clave encontrada: %" PRIu64 "\n", found);
        printf("Mensaje:          ");
        fwrite(plain, 1, MESSAGE_LEN, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave en el rango explorado.\n");
    }

    printf("Ejecucion: secuencial (mejorada)\n");
    printf("Tiempo:    %.6f segundos\n", elapsed);
    printf("Rango:     0 .. %" PRIu64 " (2^20)\n", TOTAL_KEYS - 1);

    EVP_CIPHER_CTX_free(ctx);
    return EXIT_SUCCESS;
}
