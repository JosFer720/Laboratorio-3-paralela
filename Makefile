CC             = gcc
MPICC          = mpicc
CFLAGS         = -std=c11 -O2 -Wall -Wextra
OPENSSL_PREFIX = /opt/homebrew/opt/openssl@3
LDFLAGS        = -I$(OPENSSL_PREFIX)/include -L$(OPENSSL_PREFIX)/lib -lcrypto
NP             = 4
T_SEQ          =

.PHONY: all seq seq_imp mpi run_seq run_seq_imp run_mpi run_mpi_speedup clean

all: seq seq_imp mpi

seq:     busqueda_clave_aes_secuencial
seq_imp: busqueda_clave_aes_mejorada
mpi:     busqueda_clave_aes_mpi

busqueda_clave_aes_secuencial: busqueda_clave_aes_secuencial.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

busqueda_clave_aes_mejorada: busqueda_clave_aes_mejorada.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

busqueda_clave_aes_mpi: busqueda_clave_aes_mpi.c
	$(MPICC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run_seq: seq
	./busqueda_clave_aes_secuencial

run_seq_imp: seq_imp
	./busqueda_clave_aes_mejorada

run_mpi: mpi
	mpirun -np $(NP) ./busqueda_clave_aes_mpi

run_mpi_speedup: mpi
	mpirun -np $(NP) ./busqueda_clave_aes_mpi $(T_SEQ)

clean:
	rm -f busqueda_clave_aes_secuencial busqueda_clave_aes_mejorada busqueda_clave_aes_mpi
