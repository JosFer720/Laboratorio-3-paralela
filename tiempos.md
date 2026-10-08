# Laboratorio 03 — Tiempos de ejecución y Speedup

## Entorno de prueba

| Campo | Valor |
|---|---|
| Máquina objetivo | MacBook Air M2 (Apple Silicon, arm64) |
| SO | macOS (Homebrew) |
| Compilador | gcc / mpicc (Open MPI) |
| OpenSSL | 3.x vía Homebrew |
| Espacio de claves | 2²⁰ = 1 048 576 candidatas |
| Clave secreta | 12 345 |
| Mensaje | `Puedes lograrlo!` (16 bytes) |

---

## Ejercicio 2 — Versión secuencial original

```
gcc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_secuencial.c \
    -o busqueda_clave_aes_secuencial -lcrypto
./busqueda_clave_aes_secuencial
```

| Métrica | Resultado |
|---|---|
| Clave encontrada | 12345 |
| Mensaje recuperado | `Puedes lograrlo!` |
| Tiempo de ejecución | 0.003905 s |

---

## Ejercicio 3 — Versión secuencial mejorada

```
gcc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_mejorada.c \
    -o busqueda_clave_aes_mejorada -lcrypto
./busqueda_clave_aes_mejorada
```

| Métrica | Resultado |
|---|---|
| Clave encontrada | 12345 |
| Mensaje recuperado | `Puedes lograrlo!` |
| Tiempo de ejecución (T_seq) | 0.008125 s |

> **T_seq** es el valor de referencia para calcular el Speedup en el ejercicio 4.

---

## Ejercicio 4 — Versión paralela Open MPI

```
mpicc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_mpi.c \
      -o busqueda_clave_aes_mpi -lcrypto
mpirun -np <N> ./busqueda_clave_aes_mpi <T_seq>
```

### Resultados

| N (procesos) | Tiempo paralelo (s) | Speedup = T_seq / T_par |
|:---:|:---:|:---:|
| 1 (referencia) | 0.008125 | 1.0000 |
| 2 | 0.451749 | 0.0180 |
| 3 | 0.208036 | 0.0391 |
| 4 | 0.160880 | 0.0505 |

---

## Resultados de referencia (WSL / Alpine Linux x86-64)

> Estos tiempos se obtuvieron en WSL sobre Windows como verificación de
> correctitud. **No representan el rendimiento real en Mac M2**; sirven
> únicamente para confirmar que los programas compilan y encuentran la
> clave correcta.

### Secuencial original

```
Clave encontrada: 12345
Mensaje: Puedes lograrlo!
Ejecucion: secuencial
Tiempo: 0.005131 segundos
```

### Secuencial mejorada

```
Clave encontrada: 12345
Mensaje:          Puedes lograrlo!
Ejecucion: secuencial (mejorada)
Tiempo:    0.008650 segundos
Rango:     0 .. 1048575 (2^20)
```

### Paralela MPI (WSL, referencia T_seq = 0.008650 s)

| N | Tiempo (s) | Speedup |
|:---:|:---:|:---:|
| 2 | 0.415373 | 0.0208 |
| 3 | 0.288914 | 0.0299 |
| 4 | 0.251050 | 0.0345 |

**¿Por qué el Speedup es < 1 en WSL?**

La clave secreta es 12 345, que se encuentra en la posición 12 345 de
1 048 575 (~1.2 % del espacio). El proceso 0 la halla casi de inmediato;
los demás procesos apenas hacen trabajo útil. El overhead de inicialización
MPI (fork, red loopback, handshake) domina sobre el cómputo real,
resultando en tiempo paralelo mayor que el secuencial.

En Mac M2 con ejecución nativa el overhead MPI es mucho menor.
Para observar Speedup > 1 conviene cambiar `SECRET_KEY` a un valor
cercano al final del espacio (ej. `UINT64_C(1048500)`), forzando que
todos los procesos recorran casi todo su rango.

---

## Análisis de Speedup (para completar con datos de Mac M2)

La ley de Amdahl establece:

```
S(n) = 1 / (f_s + (1 - f_s) / n)
```

donde `f_s` es la fracción serial del programa (cifrado inicial,
Bcast, reducción final) y `n` el número de procesos.

Para este programa la fracción paralela es la búsqueda por fuerza
bruta, que escala linealmente con `n`. El Speedup teórico máximo
cuando `f_s → 0` es igual a `n`.
