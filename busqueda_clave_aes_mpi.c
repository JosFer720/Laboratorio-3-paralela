/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * CC3069 - Computacion Paralela y Distribuida  |  Laboratorio 03
 * busqueda_clave_aes_mpi.c
 *
 * Version paralela con Open MPI de la busqueda de clave AES.
 *
 * Estrategia:
 *   - Rank 0 cifra el mensaje y hace Bcast del ciphertext.
 *   - El rango [0, TOTAL_KEYS) se divide en bloques contiguos,
 *     uno por proceso (distribucion ceil/floor para el residuo).
 *   - Cada proceso busca en su sub-rango de forma independiente.
 *   - MPI_Allreduce(MPI_MIN) comparte la clave encontrada; UINT64_MAX
 *     indica que ningun proceso la encontro.
 *   - El tiempo de pared se obtiene como MPI_Reduce(MPI_MAX).
 *
 * Compilacion (Mac M2):
 *   mpicc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_mpi.c \
 *         -I/opt/homebrew/opt/openssl@3/include \
 *         -L/opt/homebrew/opt/openssl@3/lib -o busqueda_clave_aes_mpi -lcrypto
 *
 * Ejecucion:
 *   mpirun -np <N> ./busqueda_clave_aes_mpi [tiempo_secuencial_seg]
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <mpi.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#define TOTAL_KEYS  (UINT64_C(1) << 20)
#define SECRET_KEY  UINT64_C(12345)
#define MESSAGE_LEN 16

static const unsigned char message[] = "Puedes lograrlo!";

_Static_assert(sizeof(message) - 1 == MESSAGE_LEN,
               "El mensaje debe tener exactamente 16 bytes.");

static void fail(int rank, const char *desc)
{
    fprintf(stderr, "[rank %d] %s\n", rank, desc);
    ERR_print_errors_fp(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
}

/* Deriva la clave AES-128 con SHA-256 (misma logica que version mejorada). */
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
        fail(-1, "Error al inicializar AES.");
    if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)
        fail(-1, "Error al configurar el relleno.");
    if (EVP_CipherUpdate(ctx, output, &written, input, MESSAGE_LEN) != 1)
        fail(-1, "Error al procesar el bloque.");
    if (EVP_CipherFinal_ex(ctx, output + written, &final_written) != 1)
        fail(-1, "Error al finalizar la operacion AES.");
    if (written + final_written != MESSAGE_LEN)
        fail(-1, "Longitud inesperada del resultado.");
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double t_seq = (argc > 1) ? atof(argv[1]) : 0.0;

    /* Rank 0 cifra el mensaje; todos reciben el ciphertext via Bcast. */
    unsigned char cipher[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH];
    memset(cipher, 0, sizeof(cipher));

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) fail(rank, "No se pudo crear el contexto de OpenSSL.");

    if (rank == 0)
        crypt_block(ctx, SECRET_KEY, message, cipher, 1);

    MPI_Bcast(cipher, MESSAGE_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    /* Distribuir [0, TOTAL_KEYS) en bloques contiguos entre los procesos. */
    uint64_t base  = TOTAL_KEYS / (uint64_t)size;
    uint64_t extra = TOTAL_KEYS % (uint64_t)size;

    uint64_t my_start, my_count;
    if ((uint64_t)rank < extra) {
        my_count = base + 1;
        my_start = (uint64_t)rank * (base + 1);
    } else {
        my_count = base;
        my_start = extra * (base + 1) + ((uint64_t)rank - extra) * base;
    }
    uint64_t my_end = my_start + my_count;

    /* Busqueda local. */
    unsigned char plain[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH];
    memset(plain, 0, sizeof(plain));

    uint64_t local_found = UINT64_MAX;
    double   t_start     = MPI_Wtime();

    for (uint64_t k = my_start; k < my_end; k++) {
        crypt_block(ctx, k, cipher, plain, 0);
        if (memcmp(plain, message, MESSAGE_LEN) == 0) {
            local_found = k;
            break;
        }
    }

    double t_end = MPI_Wtime();

    /* Compartir resultado: el minimo descarta UINT64_MAX si alguien encontro la clave. */
    uint64_t global_found = UINT64_MAX;
    MPI_Allreduce(&local_found, &global_found,
                  1, MPI_UNSIGNED_LONG_LONG, MPI_MIN, MPI_COMM_WORLD);

    /* Tiempo de pared = maximo entre todos los procesos. */
    double local_elapsed = t_end - t_start;
    double global_elapsed = 0.0;
    MPI_Reduce(&local_elapsed, &global_elapsed,
               1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("--------------------------------------------------\n");
        printf("Ejecucion: paralela con Open MPI\n");
        printf("Procesos:  %d\n", size);
        printf("Rango:     0 .. %" PRIu64 " (2^20)\n", TOTAL_KEYS - 1);

        if (global_found != UINT64_MAX) {
            unsigned char verified[MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH];
            crypt_block(ctx, global_found, cipher, verified, 0);
            printf("Clave encontrada: %" PRIu64 "\n", global_found);
            printf("Mensaje:          ");
            fwrite(verified, 1, MESSAGE_LEN, stdout);
            putchar('\n');
        } else {
            printf("No se encontro la clave en el rango explorado.\n");
        }

        printf("Tiempo:    %.6f segundos\n", global_elapsed);

        if (t_seq > 0.0)
            printf("Speedup (n=%d):  %.4f x  (T_seq=%.6f s)\n",
                   size, t_seq / global_elapsed, t_seq);

        printf("--------------------------------------------------\n");
    }

    EVP_CIPHER_CTX_free(ctx);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
