#ifndef RSHARC_CVA6_PROFILE_H
#define RSHARC_CVA6_PROFILE_H

#include <stdint.h>

/*
 * Activar expresamente al compilar para CVA6.
 * Sin esta opción, las funciones devuelven "no disponible".
 */
#if defined(RSHARC_CVA6_PROFILE_ENABLE) && RSHARC_CVA6_PROFILE_ENABLE
# if !defined(__riscv) || !defined(__riscv_xlen) || __riscv_xlen != 64
#  error "El backend HPM de CVA6 requiere RV64"
# endif
# define CVA6_PROFILE_ACTIVE 1
#else
# define CVA6_PROFILE_ACTIVE 0
#endif

/* Seleccion por compilacion: 1 = A (diagnostico), 2 = B (actividad). */
#ifndef RSHARC_CVA6_PROFILE_GROUP
#define RSHARC_CVA6_PROFILE_GROUP 2
#endif

/*
 * Grupo B (predeterminado): actividad.
 *
 * events[0] -> mhpmcounter3 -> evento 5:  ciclos con retirada LOAD
 * events[1] -> mhpmcounter4 -> evento 6:  ciclos con retirada STORE
 * events[2] -> mhpmcounter5 -> evento 9:  ciclos con retirada CTRL_FLOW
 * events[3] -> mhpmcounter6 -> evento 10: prediccion incorrecta
 * events[4] -> mhpmcounter7 -> evento 20: ciclos con retirada ALU/MULT
 * events[5] -> mhpmcounter8 -> evento 21: ciclos con retirada FP
 */
#if RSHARC_CVA6_PROFILE_GROUP == 1
/* I-refill, D-read-MSHR, mispredict, scoreboard lleno, IF vacio, RAW. */
static const uint64_t cva6_profile_event_ids[6] = {
    1, 2, 10, 14, 15, 22
};
#elif RSHARC_CVA6_PROFILE_GROUP == 2
static const uint64_t cva6_profile_event_ids[6] = {
    5, 6, 9, 10, 20, 21
};
#else
#error "RSHARC_CVA6_PROFILE_GROUP debe ser 1 (A) o 2 (B)"
#endif

/*
 * Una lectura absoluta de los contadores.
 * La misma estructura sirve para guardar sus diferencias.
 */
typedef struct {
    uint64_t available;
    uint64_t cycles;
    uint64_t instret;
    uint64_t events[6];
} cva6_profile_sample;

/*
 * Configurar y habilitar los seis HPM.
 *
 * Devuelve:
 *   1: configuracion escrita y comprobada.
 *   0: backend desactivado o comprobacion fallida.
 *
 * Llamar fuera de las regiones medidas.
 */
static inline int cva6_profile_init(void)
{
#if CVA6_PROFILE_ACTIVE
    const uint64_t mask = UINT64_C(0x1F8); /* Bits 3..8. */
    uint64_t inhibit;
    uint64_t selected[6];

    __asm__ volatile(
        "csrr %0, 0x320"
        : "=r"(inhibit)
        :
        : "memory"
    );

    /* mcycle y minstret deben estar contando previamente. */
    if (inhibit & UINT64_C(0x5)) return 0;

    /* Pausar solo los seis HPM mientras se configuran. */
    __asm__ volatile(
        "csrs 0x320, %0"
        :
        : "r"(mask)
        : "memory"
    );

    /* Elegir el evento de cada contador. */
    __asm__ volatile(
        "csrw 0x323, %0\n\t"
        "csrw 0x324, %1\n\t"
        "csrw 0x325, %2\n\t"
        "csrw 0x326, %3\n\t"
        "csrw 0x327, %4\n\t"
        "csrw 0x328, %5"
        :
        : "r"(cva6_profile_event_ids[0]),
          "r"(cva6_profile_event_ids[1]),
          "r"(cva6_profile_event_ids[2]),
          "r"(cva6_profile_event_ids[3]),
          "r"(cva6_profile_event_ids[4]),
          "r"(cva6_profile_event_ids[5])
        : "memory"
    );

    /* Leer de vuelta los selectores para comprobarlos. */
    __asm__ volatile(
        "csrr %0, 0x323\n\t"
        "csrr %1, 0x324\n\t"
        "csrr %2, 0x325\n\t"
        "csrr %3, 0x326\n\t"
        "csrr %4, 0x327\n\t"
        "csrr %5, 0x328"
        : "=r"(selected[0]), "=r"(selected[1]),
          "=r"(selected[2]), "=r"(selected[3]),
          "=r"(selected[4]), "=r"(selected[5])
        :
        : "memory"
    );

    for (unsigned i = 0; i < 6; ++i) {
        if (selected[i] != cva6_profile_event_ids[i]) return 0;
    }

    /* Habilitar HPM conservando los demas bits de mcountinhibit. */
    __asm__ volatile(
        "csrc 0x320, %0"
        :
        : "r"(mask)
        : "memory"
    );

    __asm__ volatile(
        "csrr %0, 0x320"
        : "=r"(inhibit)
        :
        : "memory"
    );

    return (inhibit & (mask | UINT64_C(0x5))) == 0;
#else
    return 0;
#endif
}

/*
 * Leer los ocho contadores.
 * No reinicia los valores ni cambia los eventos seleccionados.
 * En CVA6, utilizar despues de que init() devuelva 1.
 */
static inline cva6_profile_sample cva6_profile_read(void)
{
    cva6_profile_sample sample = {
        0, 0, 0, {0, 0, 0, 0, 0, 0}
    };

#if CVA6_PROFILE_ACTIVE
    sample.available = 1;

    __asm__ volatile(
        "csrr %0, 0xB00\n\t"
        "csrr %1, 0xB02\n\t"
        "csrr %2, 0xB03\n\t"
        "csrr %3, 0xB04\n\t"
        "csrr %4, 0xB05\n\t"
        "csrr %5, 0xB06\n\t"
        "csrr %6, 0xB07\n\t"
        "csrr %7, 0xB08"
        : "=r"(sample.cycles), "=r"(sample.instret),
          "=r"(sample.events[0]), "=r"(sample.events[1]),
          "=r"(sample.events[2]), "=r"(sample.events[3]),
          "=r"(sample.events[4]), "=r"(sample.events[5])
        :
        : "memory"
    );
#endif

    return sample;
}

/*
 * Calcular lo ocurrido entre dos lecturas del mismo grupo.
 */
static inline cva6_profile_sample cva6_profile_delta(
    cva6_profile_sample begin,
    cva6_profile_sample end)
{
    cva6_profile_sample result = {
        0, 0, 0, {0, 0, 0, 0, 0, 0}
    };

    if (!begin.available || !end.available) return result;

    result.available = 1;
    result.cycles = end.cycles - begin.cycles;
    result.instret = end.instret - begin.instret;

    for (unsigned i = 0; i < 6; ++i) {
        result.events[i] = end.events[i] - begin.events[i];
    }

    return result;
}

/* Acumulados por paso. Las lecturas de entrada son locales a cada llamada. */
/* 0: sin sondas internas; 1: solo mcycle; 2: ocho contadores (historico). */
#ifndef RSHARC_CVA6_REGION_MODE
#define RSHARC_CVA6_REGION_MODE 2
#endif
#if RSHARC_CVA6_REGION_MODE < 0 || RSHARC_CVA6_REGION_MODE > 2
#error "RSHARC_CVA6_REGION_MODE debe ser 0, 1 o 2"
#endif
enum { CP_LSQ, CP_H12, CP_DOT, CP_AXPY, CP_MODEL_JAC, CP_REGION_COUNT };

#ifdef __cplusplus
extern "C" {
#endif
extern cva6_profile_sample cp_totals[CP_REGION_COUNT];
extern uint64_t cp_calls[CP_REGION_COUNT];
#ifdef __cplusplus
}
#endif

#if RSHARC_CVA6_REGION_MODE == 1
typedef uint64_t cp_region_sample;
static inline cp_region_sample cp_region_read(void)
{
    uint64_t cycles = 0;
#if CVA6_PROFILE_ACTIVE
    __asm__ volatile("csrr %0, 0xB00" : "=r"(cycles) :: "memory");
#endif
    return cycles;
}
static inline cp_region_sample cp_region_delta(uint64_t begin, uint64_t end)
{
    return end - begin;
}
#else
typedef cva6_profile_sample cp_region_sample;
#define cp_region_read cva6_profile_read
#define cp_region_delta cva6_profile_delta
#endif

static inline void cp_accumulate(unsigned region, cp_region_sample delta)
{
#if CVA6_PROFILE_ACTIVE
#if RSHARC_CVA6_REGION_MODE == 1
    cp_totals[region].cycles += delta;
#else
    if (!delta.available) return;
    cp_totals[region].available = 1;
    cp_totals[region].cycles += delta.cycles;
    cp_totals[region].instret += delta.instret;
    for (unsigned i = 0; i < 6; ++i)
        cp_totals[region].events[i] += delta.events[i];
#endif
    ++cp_calls[region];
#else
    (void)region;
    (void)delta;
#endif
}

/* Descriptor en reserved: marca, direccion fisica, tamano; sin cambiar mailbox. */
#define CVA6_REGION_MAGIC UINT64_C(0x5253485245473031)
typedef struct {
    uint64_t schema; /* 1: full, 2: cycles, 3: off; mismo tamano de paquete. */
    uint64_t k;
    uint64_t event_ids[6];
    cva6_profile_sample global;
    cva6_profile_sample regions[CP_REGION_COUNT];
    uint64_t calls[CP_REGION_COUNT];
} cva6_region_packet;

/*
 * Detener los seis HPM al finalizar el estudio.
 * mcycle y minstret continuan contando.
 */
static inline void cva6_profile_stop(void)
{
#if CVA6_PROFILE_ACTIVE
    const uint64_t mask = UINT64_C(0x1F8);

    __asm__ volatile(
        "csrs 0x320, %0"
        :
        : "r"(mask)
        : "memory"
    );
#endif
}

/*
 * Publicar los HPM durante send(), con el bypass de cache activo.
 * La marca se escribe despues de los seis valores; send() publica state=3
 * tras su fence. No cambia el tamano ni los campos matematicos del canal.
 */
#define CVA6_PROFILE_MAGIC UINT64_C(0x525348504D303031)

static inline void cva6_profile_publish(
    volatile uint64_t reserved[7],
    const cva6_profile_sample *sample)
{
    reserved[0] = 0;

    for (unsigned i = 0; i < 6; ++i) {
        reserved[i + 1] = sample->available ? sample->events[i] : 0;
    }

    if (sample->available) {
        reserved[0] = CVA6_PROFILE_MAGIC;
    }
}
/*
 * Solo se incluye al compilar el puente del simulador en el host.
 * Escribe una linea JSON por paso completado.
 */
#ifdef RSHARC_CVA6_PROFILE_HOST
#include <stdio.h>

static inline void cva6_region_log(FILE *stream, const cva6_region_packet *packet)
{
    static const char *names[CP_REGION_COUNT] = {
        "lsq", "h12", "h12_dot", "h12_axpy", "model_jac"
    };
    if (packet->schema == 3) return;
    for (unsigned r = 0; r < CP_REGION_COUNT; ++r) {
        const cva6_profile_sample *sample = &packet->regions[r];
        if (packet->schema == 2) {
            fprintf(stream,
                "RSHARC_REGION_CYCLES_V1 {\"schema\":1,\"mode\":\"cycles\","
                "\"k\":%llu,\"region\":\"%s\",\"available\":%llu,"
                "\"calls\":%llu,\"cycles\":%llu,\"instret\":null,"
                "\"event_ids\":[],\"counts\":null,\"mpc_cycles\":%llu,"
                "\"cycles_percent\":",
                (unsigned long long)packet->k, names[r],
                (unsigned long long)sample->available,
                (unsigned long long)packet->calls[r],
                (unsigned long long)sample->cycles,
                (unsigned long long)packet->global.cycles);
            if (sample->available && packet->global.cycles)
                fprintf(stream, "%.6f", 100.0 * sample->cycles / packet->global.cycles);
            else fputs("null", stream);
            fputs("}\n", stream);
            continue;
        }
        fprintf(stream,
            "RSHARC_REGION_HPM_V1 {\"schema\":1,\"k\":%llu,"
            "\"region\":\"%s\",\"available\":%llu,\"calls\":%llu,"
            "\"cycles\":%llu,\"instret\":%llu,\"mpc_cycles\":%llu,"
            "\"mpc_instret\":%llu,\"cycles_percent\":",
            (unsigned long long)packet->k, names[r],
            (unsigned long long)sample->available,
            (unsigned long long)packet->calls[r],
            (unsigned long long)sample->cycles,
            (unsigned long long)sample->instret,
            (unsigned long long)packet->global.cycles,
            (unsigned long long)packet->global.instret);
        if (sample->available && packet->global.cycles)
            fprintf(stream, "%.6f", 100.0 * sample->cycles / packet->global.cycles);
        else fputs("null", stream);
        fputs(",\"event_ids\":[", stream);
        for (unsigned i = 0; i < 6; ++i)
            fprintf(stream, "%s%llu", i ? "," : "",
                    (unsigned long long)packet->event_ids[i]);
        fputs("],\"counts\":[", stream);
        for (unsigned i = 0; i < 6; ++i)
            fprintf(stream, "%s%llu", i ? "," : "",
                    (unsigned long long)sample->events[i]);
        fputs("]}\n", stream);
    }
    fflush(stream);
}

static inline void cva6_profile_log(
    FILE *stream,
    uint64_t k,
    uint64_t cycles,
    uint64_t instret,
    const uint64_t reserved[7])
{
    if (reserved[0] != CVA6_PROFILE_MAGIC) return;

    fprintf(stream,
        "RSHARC_HPM_V1 {\"schema\":1,\"k\":%llu,"
        "\"cycles\":%llu,\"instret\":%llu,\"event_ids\":[",
        (unsigned long long)k,
        (unsigned long long)cycles,
        (unsigned long long)instret);

    for (unsigned i = 0; i < 6; ++i) {
        fprintf(stream, "%s%llu", i ? "," : "",
                (unsigned long long)cva6_profile_event_ids[i]);
    }

    fputs("],\"counts\":[", stream);

    for (unsigned i = 0; i < 6; ++i) {
        fprintf(stream, "%s%llu", i ? "," : "",
                (unsigned long long)reserved[i + 1]);
    }

    fputs("]}\n", stream);
    fflush(stream);
}
#endif /* RSHARC_CVA6_PROFILE_HOST */
#endif /* RSHARC_CVA6_PROFILE_H */
