# Inventario de eventos de CVA6 para ACC y CartPole bare metal

Fecha de revisión: 2026-11-10. Auditoría estática de fuentes locales; no se ejecutó ni reconstruyó RTL ni firmware.

## Versión y alcance

- Checkout: `/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6`.
- Revisión base Git: `4c02b24fe7c04690f626776a92274da24f80d1da`. Hay modificaciones locales; en `core/cva6.sv` el diff revisado solo añade una guarda al tracer de simulación.
- Configuración identificada en scripts y evidencias del proyecto: `cv64a6_imafdc_sv39`. RV64, F/D activos, emisión escalar, dos puertos de commit, scoreboard de ocho entradas, caché WT, I-cache 16 KiB/4 vías y D-cache 32 KiB/8 vías; CV-X-IF habilitado, sin RVV ni acelerador ACCEL (`COPRO_NONE`).
- Seis HPM genéricos de 64 bits (`mhpmcounter3..8`), selectores de cinco bits y `PerfCounterEn=1`. 22 selectores activos, 0 nulo y 23..31 sin detector.
- Los siete archivos clave comprobados coinciden byte a byte con el inventario local P04 archivado en R-SHARC. Las cuatro variantes del barrido I-cache 1/4/8/16 KiB conservan el mismo perf_counters.sv; cambia la configuración de caché.
- R-SHARC-FPGA no contiene actualmente una copia RTL ni una receta que vincule un nuevo bitstream. Este informe identifica las fuentes usadas por las pruebas archivadas; no certifica la procedencia de un binario o FPGA distinto.

Fuentes: [configuración](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/include/cv64a6_imafdc_sv39_config_pkg.sv:12), [número de HPM](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/include/ariane_pkg.sv:637), [banco HPM](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/perf_counters.sv:65).

## Cómo interpretar la exhaustividad

La tabla de selectores es exhaustiva: contiene todos los eventos actualmente implementados en la PMU auditada. Para señales internas, no existe un conjunto finito de todos los eventos posibles: se pueden crear combinaciones arbitrarias de señales, estados, opcodes y direcciones. El inventario siguiente cubre puntos de observación concretos verificados del pipeline, FPU, LSU, cachés WT, MMU, AXI y control, y define familias parametrizadas para desgloses por operación, puerto y estado.

Una señal presente en un submódulo no está necesariamente conectada a la PMU. Todos los eventos adicionales requieren detector, conexión y asignación de selector o contador. Los ID Sxxx del documento son referencias documentales, no códigos mhpmevent. Las fórmulas son especificaciones a implementar y validar, no un parche sintetizado.

## Contadores fijos

| CSR | Magnitud | Semántica |
|---|---|---|
| mcycle / cycle | Ciclos de reloj contados | Control inhibit bit 0; no aumenta en debug según csr_regfile. |
| minstret / instret | Instrucciones arquitectónicas retiradas | Suma los puertos de commit; inhibit bit 2; no es el OR de eventos de clase. |

Fuente: [core/csr_regfile.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/csr_regfile.sv:1023). No se ha identificado `mtime` como contador de eventos de este módulo; el temporizador de plataforma es otro bloque.

## Todos los selectores HPM existentes

| ID | Nombre habitual | Señal/base | Qué cuenta realmente |
|---|---|---|---|
| 1 | L1 I-cache misses | `l1_icache_miss_i` | Pulsos de refill cacheable aceptado; no tiempo de espera. |
| 2 | L1 D-cache misses | `l1_dcache_miss_i` | Asignación de MSHR cacheable en miss unit WT; no todos los fallos de lookup ni stores. |
| 3 | ITLB misses | `itlb_miss_i` | En esta MMU, evento asociado a miss de TLB compartida/PTW por instrucciones; no todo miss L1 ITLB. |
| 4 | DTLB misses | `dtlb_miss_i` | Misma restricción para datos; puede ser cero con traducción desactivada. |
| 5 | Loads | `load_event` | OR de puertos retirados con fu=LOAD: ciclos con ≥1 load, no suma de loads. |
| 6 | Stores | `store_event` | OR de puertos retirados con fu=STORE; no escrituras externas. |
| 7 | Excepciones | `ex_i.valid` | Ciclos con excepción válida en la entrada PMU; revisar persistencia y causa. |
| 8 | Retornos de excepción | `eret_i` | Ciclos/pulsos de retorno. |
| 9 | Branches | `branch_event` | OR de retiradas CTRL_FLOW, incluyendo saltos; no solo branches condicionales. |
| 10 | Branch mispredicts | `resolved_branch_i` | valid && is_mispredict, en resolución; no instrucciones retiradas. |
| 11 | Excepciones de branch | `branch_exceptions_i` | valid desde la unidad de branch. |
| 12 | Calls | `call_event` | OR: CTRL_FLOW, op ADD/JALR y rd=x1/x5; clasificación concreta del RTL. |
| 13 | Returns | `return_event` | OR: JALR y rd=x0; heurística que puede incluir otros saltos indirectos. |
| 14 | Scoreboard full | `sb_full_i` | Ciclos lleno, independientemente de si hay nueva candidata que necesite entrar. |
| 15 | Frontend vacío | `if_empty_i` | ~fetch_valid_if_id[0]; sin calificar halt; no causa exclusiva de pérdida de ciclos. |
| 16 | Solicitudes I-cache | `l1_icache_access_i.req` | Cuenta req por ciclo, no req && ready ni instrucciones. |
| 17 | Solicitudes D-cache | `l1_dcache_access_i` | OR de data_req de tres interfaces ex_cache, sin data_gnt. Puede repetir solicitudes. |
| 18 | Heurística de evicción D-cache | `miss_vld_bits_i` | Miss global y algún vector [0..2]==8hFF; hay 4 puertos. No usar como contador exacto de evicciones. |
| 19 | Flush ITLB | `i_tlb_flush_i` | Ciclos/pulsos de flush de traducción. |
| 20 | Instrucciones enteras | `int_event` | OR de retiradas ALU o MULT. No todas las instrucciones que usan GPR; incluye MUL/DIV y excluye otras clases. |
| 21 | Instrucciones FP | `fp_event` | OR de retiradas FPU/FPU_VEC. No cuenta necesariamente FLD/FSD, ni FLOPs, ni utilización FPU. |
| 22 | Stall RAW de emisión | `stall_issue_i` | Cableado a stall_raw[0], inicializado también con stall_i; no todas las burbujas, sin calificar candidata válida. |

Fuente completa: [core/perf_counters.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/perf_counters.sv:95); cableado: [core/cva6.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:1274).

Condiciones comunes: los HPM aumentan como máximo una unidad por ciclo; no cuentan durante debug, escrituras por su interfaz o inhibición individual. Los selectores de clase hacen OR de puertos. El contador fijo minstret sí suma retiradas. Fuente: [core/perf_counters.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/perf_counters.sv:148).

Los HPM16/17 no proporcionan denominadores de accesos aceptados; no calcular tasas exactas de miss con ellos sin validar el handshake. HPM18 se excluye de conclusiones cuantitativas de evicción. HPM22 no es una fracción causal de todos los stalls.

## Eventos adicionales construibles a partir de señales existentes

### Frontend y flujo de instrucciones

Fuente: [core/frontend/frontend.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:60).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S001 | Fetch solicitado | `icache_dreq_o.req` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:60)) | Ciclos con solicitud | Ya se aproxima en HPM16; no implica aceptación. |
| S002 | Fetch aceptado | `if_ready` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:107)) | Transacciones aceptadas | Usar handshake; no confundir con instrucciones individuales del bloque. |
| S003 | Respuesta de instrucciones | `icache_valid_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:95)) | Ciclos con respuesta registrada | Cuenta bloques de fetch, no instrucciones retiradas. |
| S004 | Presión de la cola de instrucciones | `instr_queue_ready` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:101)) | Ciclos con cola no disponible | No equivale por sí sola a ciclos críticos perdidos. |
| S005 | Replay del frontend | `replay` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:113)) | Ciclos con replay | Si se desean episodios, detectar flanco o transición. |
| S006 | Cancelación en etapa de fetch 1 | `icache_dreq_o.kill_s1` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:60)) | Ciclos activos | Combina mispredict, flush y replay; separar con las causas originales. |
| S007 | Cancelación en etapa de fetch 2 | `icache_dreq_o.kill_s2` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:60)) | Ciclos activos | Incluye predicción; no toda cancelación es fallo de predicción. |
| S008 | Predicción de cambio de flujo | `bp_valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:202)) | Ciclos con predicción | No es contador de aciertos sin asociar resolución. |
| S009 | Branch identificado en fetch | `is_branch` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:204)) | Número de posiciones válidas | Usar instrucciones consumidas para no contar replays dos veces. |
| S010 | Salto indirecto identificado | `is_jalr` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:208)) | Número de posiciones válidas | Misma necesidad de aceptación y clasificación. |
| S011 | Push de RAS | `ras_push` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:149)) | Pulsos | Operaciones de actualización de la pila de retorno. |
| S012 | Pop de RAS | `ras_pop` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:149)) | Pulsos | Operaciones de actualización; no equivale a aciertos de retorno. |
| S013 | Estado especulativo de fetch | `speculative_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:328)) | Ciclos especulativos | Actividad física, no trabajo útil retirado. |
| S014 | Predicciones BHT/BTB por posición | `bht_prediction` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/frontend.sv:139)) | Familia de conteos por resultado | Señales también en btb_prediction; alinear con dirección y consumo antes de contar. |

### Colas del frontend

Fuente: [core/frontend/instr_queue.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:109).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S015 | Colas de instrucciones llenas | `instr_queue_full` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:109)) | Ciclos por FIFO | Existe un vector; OR mide ciclos y popcount mide número de FIFOs llenas. |
| S016 | Colas de instrucciones vacías | `instr_queue_empty` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:110)) | Ciclos por FIFO | No sumar como instrucciones ni como causas exclusivas. |
| S017 | Cola de direcciones llena | `full_address` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:116)) | Ciclos | Recurso distinto de la cola de instrucciones. |
| S018 | Desbordamiento de instrucciones | `instr_overflow_fifo` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:147)) | Pulsos/posiciones | Intento de inserción no aceptado; no corrupción de datos. |
| S019 | Desbordamiento de direcciones | `address_overflow` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:117)) | Pulsos | Causa de replay. |
| S020 | Inserciones efectivas en FIFO | `push_instr_fifo` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:107)) | Entradas | Sumar posiciones aceptadas, respetar flush. |
| S021 | Extracciones de instrucciones | `pop_instr` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/frontend/instr_queue.sv:108)) | Entradas | Contabilizar cada posición; no retiradas arquitectónicas. |

### Emisión, dependencias y forwarding

Fuente: [core/issue_read_operands.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:47).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S022 | Instrucción candidata a emitir | `issue_instr_valid_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:47)) | Ciclos con candidata | No equivale a instrucción emitida. |
| S023 | Emisión aceptada | `issue_ack_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:49)) | Instrucciones/eventos | Calificar con valid, excepción y flush según semántica elegida. |
| S024 | Bloqueo RAW agregado | `stall_raw` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:143)) | Ciclos con condición | HPM22 usa stall_raw[0]; incorpora stall_i; no se califica por valid en el HPM. |
| S025 | Bloqueo del operando rs1 | `stall_rs1` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:143)) | Ciclos por operando | Calificar candidata válida; también hereda stall_i. |
| S026 | Bloqueo del operando rs2 | `stall_rs2` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:143)) | Ciclos por operando | Puede coincidir con rs1 y rs3. |
| S027 | Bloqueo del operando rs3 | `stall_rs3` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:143)) | Ciclos por operando | Principalmente tercer operando flotante en esta configuración. |
| S028 | Dependencia detectada rs1 | `rs1_has_raw` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:176)) | Ciclos con dependencia | Una dependencia puede resolverse mediante forwarding y no bloquear. |
| S029 | Dependencia detectada rs2 | `rs2_has_raw` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:181)) | Ciclos con dependencia | No equivale a stall. |
| S030 | Dependencia detectada rs3 | `rs3_has_raw` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:186)) | Ciclos con dependencia | Calificar uso real del operando. |
| S031 | Forwarding de rs1 | `forward_rs1` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:213)) | Usos | Contar al aceptar la instrucción, no cada ciclo de espera. |
| S032 | Forwarding de rs2 | `forward_rs2` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:213)) | Usos | Misma regla. |
| S033 | Forwarding de rs3 | `forward_rs3` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:213)) | Usos | Misma regla. |
| S034 | Unidad requerida no disponible | `fu_busy` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:144)) | Ciclos | Calificar issue_instr_valid_i; puede coincidir con RAW. |
| S035 | Desglose de unidad ocupada | `fus_busy` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:145)) | Familia por campo | Campos none/load/store/alu/alu2/ctrl_flow/mult/csr/fpu/fpu_vec/cvxif/accel/aes; contar solo cuando se demanda esa unidad. Campos opcionales pueden estar inactivos. |
| S036 | Productor responsable de RAW | `idx_hzd_rs1` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:174)) | Familia por unidad/operación productora | idx_hzd_rs2/rs3 y fwd_i.sbe permiten clasificar LOAD/FPU/etc.; hace falta comparación y verificar etiqueta válida. No existe un pulso único load_use_stall. |
| S037 | Banco de registro del operando | `rs1_fpr` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:177)) | Familia GPR/FPR | rs2_fpr/rs3_fpr también existen; cruzar con dependencia y aceptación. |
| S038 | Emisiones por unidad funcional | `fu_data_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:53)) | Familia por fu/operation | Usar valid específicos y handshake; no contar el valor del bus cuando no es válido. |

### Scoreboard

Fuente: [core/scoreboard.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:29).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S039 | Scoreboard lleno | `sb_full_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:29)) | Ciclos | Ya HPM14; su presencia no prueba que haya candidata bloqueada. |
| S040 | Bloqueo efectivo por capacidad | `decoded_instr_valid_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:56)) | Ciclos | Combinar con issue_full; son señales existentes, detector compuesto nuevo. |
| S041 | Ocupación del scoreboard | `mem_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:100)) | Suma/histograma de entradas issued | Necesita popcount de issued; no confundir issued con sbe.valid (resultado disponible). |
| S042 | Instrucciones vivas no canceladas | `still_issued` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:101)) | Ocupación | Vector ya calculado; útil si se habilita scoreboard especulativo. |
| S043 | Número de inserciones por ciclo | `num_issue` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:110)) | Instrucciones | Aplicar el mismo criterio de flush que el módulo. |
| S044 | Número de retiradas por ciclo | `num_commit` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:115)) | Entradas liberadas | Puede incluir entradas canceladas; para instrucciones arquitectónicas usar commit filtrado. |
| S045 | Entradas canceladas | `commit_drop_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:45)) | Entradas descartadas | Contar al consumir la entrada, no todos los ciclos que permanece. |
| S046 | Writebacks por puerto | `wt_valid_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:81)) | Resultados | Separar excepciones con ex_i; múltiples puertos pueden coincidir. |
| S047 | Resultado disponible en cabeza | `commit_instr_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/scoreboard.sv:43)) | Ciclos/lista de operaciones | Calificar la cabeza; no hay señal independiente head_valid de ocupación, usar mem_q e índice. |

### Retirada y clasificación de instrucciones

Fuente: [core/commit_stage.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:42).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S048 | Ciclos con 0/1/2 retiradas | `commit_ack_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:42)) | Histograma | Filtrar commit_drop_i y condiciones de excepción como hace el core; popcount, no OR. |
| S049 | Retiradas exactas por tipo de unidad | `commit_instr_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:38)) | Instrucciones | Filtrar por commit y sumar puertos: LOAD/STORE/ALU/CTRL_FLOW/MULT/CSR/FPU/CVXIF/AES; familias FPU_VEC/ACCEL opcionales. |
| S050 | Retiradas exactas por operación | `commit_instr_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:38)) | Una familia por cada valor op | Comparar fu_op con commit válido; permite separar suma/multiplicación/división/FMA/loads FP/etc. No requiere inventar códigos de ISA. |
| S051 | Retiradas comprimidas | `commit_instr_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:38)) | Instrucciones de 16/32 bits | Campo is_compressed, calificado por retirada; RVC activo. |
| S052 | Retiradas por PC/rango | `commit_instr_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:38)) | Instrucciones por región | Campo pc existente; comparadores nuevos, rango a fijar con ELF y linker. |
| S053 | Stores detenidos en commit | `commit_lsu_ready_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:70)) | Ciclos de demanda bloqueada | Combinar con cabeza válida STORE no AMO; no basta invertir ready. |
| S054 | Fence esperando stores pendientes | `no_st_pending_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:76)) | Ciclos | Calificar operación FENCE/FENCE_I/SFENCE_VMA/FENCE_T correspondiente. |
| S055 | AMO esperando respuesta | `amo_resp_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:54)) | Ciclos | Campo ack y operación de cabeza; validación de fronteras necesaria. |
| S056 | Bloqueo global de commit | `halt_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:28)) | Ciclos | Separar WFI/debug/otras causas con controller/CSR. |
| S057 | Actualizaciones de flags FP | `csr_write_fflags_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:64)) | Pulsos | Clasificar cinco bits de csr_wdata_o; ya se conocen en retirada. |
| S058 | Excepción de instrucción o CSR | `csr_exception_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/commit_stage.sv:66)) | Excepciones por causa | Separar ex_i/entrada de cabeza; contar aceptación, no un nivel repetido. |

### FPU escalar

Fuente: [core/fpu_wrap.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:120).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S059 | Operaciones aceptadas por FPnew | `fpu_in_valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:120)) | Operaciones | Calificar con fpu_in_ready y flush; interno al wrapper, no HPM actual. |
| S060 | Operaciones FP completadas | `fpu_out_valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:121)) | Resultados | fpu_out_ready está fijado a 1 en esta configuración. |
| S061 | Presión en entrada de FPnew | `fpu_in_ready` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:120)) | Ciclos con demanda no aceptada | Usar fpu_in_valid && !fpu_in_ready; no igualar !ready a uso útil de FPU. |
| S062 | Estado de retención del wrapper | `use_hold` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:127)) | Ciclos | El wrapper conserva una operación ante backpressure. |
| S063 | Operaciones por tipo FPnew | `fpu_op` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:110)) | Familia por operación | Calificar handshake; operaciones de fpnew_pkg; contrastar con fu_data_i.operation si se desean mnemonics exactos. |
| S064 | Operaciones FP por instrucción original | `fu_data_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:28)) | Familia por operation | FADD, FSUB, FMUL, FDIV, FSQRT, FMADD, FMSUB, FNMSUB, FNMADD, conversiones, comparaciones, min/max, clasificación y movimientos. Usar aceptación correcta del wrapper. |
| S065 | Operaciones por formato | `fpu_srcfmt` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:112)) | FP32/FP64 y conversiones | fpu_dstfmt/fpu_ifmt permiten desglose origen/destino; otros formatos desactivados. |
| S066 | Operaciones por redondeo | `fpu_rm_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:31)) | Familia por modo | Calificar operación aceptada y resolver modo dinámico con fpu_frm_i. |
| S067 | Flags FP por bit | `fpu_status` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:123)) | Resultados con cada flag | Bits NV/DZ/OF/UF/NX al resultado válido; no son traps necesariamente. Pueden coexistir. |
| S068 | Etiqueta de operación FP | `fpu_tag` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:118)) | Identificador | fpu_trans_id_o da etiqueta de salida. La etiqueta existe, pero medir latencia requiere almacenar ciclo de entrada y tratar flush/reutilización. |
| S069 | Ocupación global FPnew | `busy_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/fpu_wrap.sv:563)) | Ciclos con actividad interna | El puerto de FPnew existe pero está DESCONECTADO: .busy_o(/* unused */). Necesita cable nuevo, no se puede leer hoy en el wrapper. |

### Multiplicación y división enteras

Fuente: [core/mult.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/mult.sv:37).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S070 | Operaciones de multiplicación emitidas | `mul_valid_op` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/mult.sv:37)) | Operaciones | Familia MUL/MULH/MULHU/MULHSU/MULW/CLMUL/CLMULH/CLMULR, según decode y extensiones. |
| S071 | Operaciones de división/resto presentadas | `div_valid_op` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/mult.sv:36)) | Operaciones | Calificar aceptación de serdiv; in_rdy_o no tiene handshake convencional estable por defecto. |
| S072 | Resultados del multiplicador | `mul_valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/mult.sv:28)) | Resultados | No es ocupación de pipeline ni instrucciones retiradas. |
| S073 | Resultados del divisor | `div_valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/mult.sv:29)) | Resultados | La salida puede esperar arbitraje; distinguir aceptación y validez. |
| S074 | Contención salida divisor/multiplicador | `div_ready_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/mult.sv:30)) | Ciclos de respuesta bloqueada | Calificar div_valid; el multiplicador tiene prioridad. |

### Estado del divisor entero

Fuente: [core/serdiv.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/serdiv.sv:62).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S075 | Divisor calculando | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/serdiv.sv:62)) | Ciclos en DIVIDE | No confundir con FDIV, que vive en FPnew. |
| S076 | Divisor esperando consumo | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/serdiv.sv:62)) | Ciclos en FINISH | Usar out_vld_o/out_rdy_i para resultados aceptados. |
| S077 | Entrada del divisor | `in_vld_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/serdiv.sv:38)) | Operaciones | Reproducir aceptación de FSM; STABLE_HANDSHAKE=0 puede bajar ready durante la aceptación. |

### LSU: cargas

Fuente: [core/load_unit.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:37).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S078 | Solicitudes de carga | `valid_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:37)) | Ciclos con demanda | No equivale a solicitudes aceptadas en caché. |
| S079 | Buffer de cargas lleno | `ldbuf_full` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:119)) | Ciclos | Dos entradas configuradas; calificar demanda para bloqueo efectivo. |
| S080 | Ocupación buffer de cargas | `ldbuf_valid_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:116)) | Ocupación/histograma | Popcount; ldbuf_flushed_q separa solicitudes canceladas. |
| S081 | Carga aceptada por D-cache | `ldbuf_w` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:121)) | Transacciones | Equivale a data_req && data_gnt en este módulo. |
| S082 | Respuesta de carga | `ldbuf_r` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:124)) | Respuestas | Puede corresponder a operación cancelada; usar metadatos de buffer. |
| S083 | Carga bloqueada por alias con stores | `page_offset_matches_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:71)) | Ciclos con coincidencia y demanda | Coincidencia conservadora de offset, no prueba igualdad de dirección completa. |
| S084 | Carga no idempotente detenida | `stall_ni` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:223)) | Ciclos | Combina stores en vuelo y orden de commit; PMA y configuración condicionan actividad. |
| S085 | Espera de turno en commit para carga NI | `not_commit_time` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:221)) | Ciclos calificados | Por sí sola es comparación de IDs; combinar con demanda NI. |
| S086 | Ciclos por estado de carga | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:94)) | Un contador por estado de FSM | Estados: IDLE, WAIT_GNT, SEND_TAG, WAIT_PAGE_OFFSET, ABORT_TRANSACTION, ABORT_TRANSACTION_NI, WAIT_TRANSLATION, WAIT_FLUSH y WAIT_WB_EMPTY. |
| S087 | Solicitudes de traducción de cargas | `translation_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/load_unit.sv:51)) | Ciclos/solicitudes | Calificar handshake si se desea número de traducciones, no nivel. |

### LSU: stores

Fuente: [core/store_unit.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:38).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S088 | Demanda de store | `valid_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:38)) | Ciclos con candidata | Diferenciar entrada, buffer y escritura final de memoria. |
| S089 | Store consumido por LSU | `pop_st_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:42)) | Operaciones | Semántica de consumo de cola, no finalización externa. |
| S090 | Store listo para buffer | `st_valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:129)) | Operaciones | Calificar st_ready y flush. |
| S091 | Buffer de stores no disponible | `store_buffer_ready` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:279)) | Ciclos de demanda bloqueada | Interno a store_unit; calificar store_buffer_valid. |
| S092 | Buffer AMO no disponible | `amo_buffer_ready` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:279)) | Ciclos de demanda bloqueada | RVA activo; observar solo cuando se demanda AMO. |
| S093 | Traducción DTLB de store | `dtlb_hit_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:74)) | Aciertos/espera calificados | No contar ciclos sin solicitud como hits. |
| S094 | Estado de ejecución store | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_unit.sv:125)) | Familia por estado | Estados IDLE, VALID_STORE, WAIT_TRANSLATION y WAIT_STORE_READY; cada comparación es un detector nuevo. |

### Buffer de stores

Fuente: [core/store_buffer.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:70).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S095 | Ocupación especulativa de stores | `speculative_status_cnt_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:70)) | Suma/histograma | Registro de ocupación existente. |
| S096 | Ocupación de stores comprometidos | `commit_status_cnt_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:71)) | Suma/histograma | Distinto del write buffer de la D-cache. |
| S097 | Cola especulativa no disponible | `ready_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:37)) | Ciclos con demanda bloqueada | Calificar valid_i. |
| S098 | Cola de commit no disponible | `commit_ready_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:36)) | Ciclos con commit solicitado | Calificar commit_i. |
| S099 | Stores pendientes de commit a memoria | `no_st_pending_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:29)) | Ciclos con pendientes | Invertir no_st_pending_o; es estado, no causalidad. |
| S100 | Buffer de stores completamente vacío | `store_buffer_empty_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/store_buffer.sv:30)) | Ciclos | Incluye especulativos y comprometidos. |

### Caché de instrucciones

Fuente: [core/cache_subsystem/cva6_icache.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:133).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S101 | Ciclos por estado I-cache | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:133)) | Seis estados: FLUSH/IDLE/READ/MISS/KILL_ATRANS/KILL_MISS | Comparaciones de estado sin reconstruir transacciones. |
| S102 | Hit de tags I-cache | `cl_hit` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:85)) | Hits calificados | Reproducir fase READ, traducción válida, caché habilitada y ausencia de cancelación. |
| S103 | Solicitud de refill aceptada | `mem_data_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:63)) | Transacciones | Calificar mem_data_ack_i; clasificar paddr_is_nc. |
| S104 | Espera de aceptación de refill | `mem_data_ack_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:64)) | Ciclos | Calificar mem_data_req_o. |
| S105 | Refill recibido | `mem_rtrn_vld_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:61)) | Respuestas | Filtrar rtype==ICACHE_IFILL_ACK. |
| S106 | Evicción real I-cache | `cache_wren` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:87)) | Reemplazos | Combinar con all_ways_valid y refill cacheable; update_lfsr ya deriva esa combinación. |
| S107 | Invalidación I-cache | `inv_en` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:102)) | Pulsos | No confundir con flush completo. |
| S108 | Finalización flush I-cache | `flush_done` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:104)) | Pulsos | Separar petición mantenida flush_i de finalización. |
| S109 | Acceso no cacheable I-cache | `paddr_is_nc` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/cva6_icache.sv:84)) | Transacciones calificadas | También se activa cuando la caché está deshabilitada. |

### Controladores de lectura D-cache WT

Fuente: [core/cache_subsystem/wt_dcache_ctrl.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:73).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S110 | Ciclos por estado del controlador | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:73)) | Ocho estados: IDLE/READ/MISS_REQ/MISS_WAIT/KILL_MISS/KILL_MISS_ACK/REPLAY_REQ/REPLAY_READ | Existe una instancia por puerto de lectura; mantener identidad del puerto. |
| S111 | Accesos aceptados D-cache por puerto | `req_port_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:32)) | Transacciones data_req && req_port_o.data_gnt | Métrica alternativa más precisa que HPM17. |
| S112 | Solicitudes pendientes de grant | `req_port_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:32)) | Ciclos data_req && !data_gnt | Presión de interfaz; no todos son ciclos críticos de CPU. |
| S113 | Respuesta D-cache por puerto | `req_port_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:33)) | Respuestas data_rvalid | Separar canceladas con kill_req y operación/ID. |
| S114 | Hit real por puerto de lectura | `rd_hit_oh_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:59)) | Hits calificados | Reproducir rama READ/REPLAY_READ con tag válido y sin wr_cl_vld_i ni !rd_ack_q. |
| S115 | Arbitraje de acceso a arrays | `rd_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:53)) | Ciclos rd_req_o && !rd_ack_i | El controlador solicita arrays; no es fallo de caché. |
| S116 | Replay de arrays | `wr_cl_vld_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:48)) | Entradas en REPLAY_REQ | Calificar rama de comparación; wr_cl_vld_i sola no es replay. |
| S117 | Solicitud de miss | `miss_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:35)) | Solicitud/aceptación | Usar miss_ack_i; contar por puerto y separar miss_nc_o. |
| S118 | Replay por colisión | `miss_replay_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:45)) | Pulsos calificados | La FSM puede reintentar una misma operación. |
| S119 | Acceso no cacheable | `miss_nc_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:42)) | Transacciones calificadas | No contar el nivel sin una solicitud. |
| S120 | Cancelación de solicitud D-cache | `req_port_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_ctrl.sv:32)) | Pulsos/estados con kill_req | Distinguir orden cancelada de respuesta sintética de cierre. |

### Miss unit de D-cache WT

Fuente: [core/cache_subsystem/wt_dcache_missunit.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:121).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S121 | Ciclos por estado miss unit | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:121)) | Ocho estados: IDLE/DRAIN/AMO/FLUSH/INIT/STORE_WAIT/LOAD_WAIT/AMO_WAIT | STORE_WAIT/LOAD_WAIT esperan aceptación de interfaz; la respuesta larga se observa también con mshr_vld_q/stores_inflight_q. |
| S122 | Asignaciones MSHR cacheables | `mshr_allocate` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:137)) | Transacciones | Calificar !miss_nc_i[miss_port_idx]; fuente de HPM2. |
| S123 | Asignaciones MSHR no cacheables | `mshr_allocate` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:137)) | Transacciones | Calificar miss_nc_i[miss_port_idx]. |
| S124 | MSHR ocupado | `mshr_vld_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:136)) | Ciclos | Una lectura en vuelo; no contador de misses adicionales. |
| S125 | Colisión lectura-lectura | `mshr_rdrd_collision` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:158)) | Ciclos/solicitudes por puerto | Calificar miss_req_i; comparación puede estar activa sin demanda. |
| S126 | Colisión escritura con lectura pendiente | `mshr_rdwr_collision` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:159)) | Ciclos/solicitudes | Calificar puerto de escritura. |
| S127 | Colisión lectura con escritura en vuelo | `tx_rdwr_collision` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:159)) | Ciclos/solicitudes | Calificar solicitud seleccionada. |
| S128 | Replay generado por puerto | `miss_replay_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:55)) | Reintentos | Vector: OR cuenta ciclos y popcount número de puertos. |
| S129 | Lectura completada | `load_ack` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:155)) | Respuestas | Una finalización de la transacción de lectura pendiente. |
| S130 | Escritura completada | `store_ack` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:155)) | Respuestas | Distinguir de aceptación y de store retirado. |
| S131 | AMO completada | `amo_ack` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:155)) | Respuestas | No equivale al número de instrucciones AMO retiradas sin comprobar el flujo. |
| S132 | Escrituras en vuelo | `stores_inflight_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:336)) | Ocupación/histograma | Estado ya mantenido por hardware. |
| S133 | Refill cacheable escrito | `cl_write_en` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:154)) | Líneas | Separa load_ack de relleno real; NC no rellena. |
| S134 | Reemplazo real D-cache | `mshr_allocate` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:137)) | Líneas a reemplazar | Calificar cacheable y AND de miss_vld_bits_i[miss_port_idx]; no usar OR de puertos no seleccionados como HPM18. |
| S135 | Flush D-cache terminado | `flush_ack_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_missunit.sv:33)) | Pulsos | El tiempo de flush se obtiene de estado/petición, no de pulsos solamente. |

### Write buffer D-cache WT

Fuente: [core/cache_subsystem/wt_dcache_wbuffer.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:66).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S136 | Write buffer vacío | `empty_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:66)) | Ciclos | Distinto de store_buffer_empty_o de LSU. |
| S137 | Write buffer lleno | `full` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:188)) | Ciclos | Aun lleno puede admitir escritura sobre una entrada coincidente. |
| S138 | Demanda rechazada por write buffer | `rdy` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:188)) | Ciclos con solicitud y !rdy | Calificar req_port_i.data_req; rdy incluye hit sobre entrada existente. |
| S139 | Ocupación de write buffer | `valid` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:20)) | Número de entradas | Popcount por ciclo, no OR si se desea ocupación. |
| S140 | Bytes válidos/sucios | `bdirty` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:175)) | Bytes/ocupación | Combinar con máscaras valid y transacciones; no equivale a tráfico AXI. |
| S141 | Store fusionado con entrada existente | `wbuffer_hit_oh` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:174)) | Transacciones | Calificar solicitud aceptada. |
| S142 | Escrituras con transacciones pendientes | `tx_vld_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:109)) | Ocupación | Vector de IDs de transacciones. |
| S143 | Solicitud write-through emitida | `miss_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:74)) | Transacciones aceptadas | Calificar miss_ack_i y metadatos NC/size. |
| S144 | Write-through completado | `miss_rtrn_vld_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:83)) | Respuestas | Identificador miss_rtrn_id_i disponible. |
| S145 | Stores no idempotentes pendientes | `not_ni_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_dcache_wbuffer.sv:67)) | Ciclos | Invertir señal; útil si hay periféricos o memoria NI. |

### MMU y TLB

Fuente: [core/cva6_mmu/cva6_mmu.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:42).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S146 | Traducción de fetch habilitada | `enable_translation_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:42)) | Ciclos | En bare metal M-mode/Bare puede estar inactiva: cero es esperado. |
| S147 | Traducción de datos habilitada | `en_ld_st_translation_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:44)) | Ciclos | Depende también de privilegio efectivo y configuración. |
| S148 | Hit L1 ITLB | `itlb_lu_hit` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:155)) | Solicitudes calificadas | Añadir demanda de fetch/traducción. HPM3 no equivale a todos los misses L1. |
| S149 | Hit L1 DTLB | `dtlb_lu_hit` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:164)) | Solicitudes calificadas | Añadir demanda de datos/traducción. |
| S150 | Acceso TLB compartida | `shared_tlb_access` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:167)) | Accesos | Señal ya generada. |
| S151 | Hit TLB compartida | `shared_tlb_hit` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:168)) | Hits calificados | Asociar al acceso correcto según pipeline. |
| S152 | Miss TLB compartida/PTW iniciado | `shared_tlb_miss` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:167)) | Pulsos | Base de HPM3/4 según origen; no cada fallo de L1 TLB. |
| S153 | PTW activo | `ptw_active` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:138)) | Ciclos | No equivale automáticamente a CPU detenida todo ese tiempo. |
| S154 | PTW por instrucciones/datos | `walking_instr` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:139)) | Ciclos calificados por ptw_active | Particionar I/D. |
| S155 | Error acceso de traducción I | `iaccess_err` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:133)) | Excepciones calificadas | Pulsos/aceptación a definir. |
| S156 | Error acceso de traducción D | `daccess_err` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:135)) | Excepciones calificadas | Misma regla. |
| S157 | Excepción PMP del PTW | `ptw_access_exception` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_mmu.sv:143)) | Excepciones calificadas | No mezclar con page fault. |

### Estados y errores PTW

Fuente: [core/cva6_mmu/cva6_ptw.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_ptw.sv:110).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S158 | Ciclos por estado PTW | `state_q` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_ptw.sv:110)) | IDLE/WAIT_GRANT/PTE_LOOKUP/WAIT_RVALID/PROPAGATE_ERROR/PROPAGATE_ACCESS_ERROR/LATENCY | No son todos estados causalmente exclusivos respecto al core completo. |
| S159 | Error de página PTW | `ptw_error_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_ptw.sv:40)) | Pulsos | Configurar traducción para generar casos de prueba. |
| S160 | Error de acceso PTW | `ptw_access_exception_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_ptw.sv:43)) | Pulsos | Separar causa PMP/acceso de fallo de página. |
| S161 | Lectura PTE aceptada | `req_port_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6_mmu/cva6_ptw.sv:55)) | Transacciones | data_req && req_port_i.data_gnt; no usar nivel sin handshake. |

### Bus AXI del core

Fuente: [core/cva6.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S162 | Direcciones de lectura aceptadas | `noc_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349)) | Transacciones AR | ar_valid && noc_resp_i.ar_ready; comprobar tipos/enlace del target. |
| S163 | Direcciones de escritura aceptadas | `noc_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349)) | Transacciones AW | aw_valid && noc_resp_i.aw_ready. |
| S164 | Beats de escritura aceptados | `noc_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349)) | Beats W | w_valid && noc_resp_i.w_ready; no confundir con transacciones completas. |
| S165 | Beats de lectura aceptados | `noc_resp_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:351)) | Beats R | r_valid && noc_req_o.r_ready. |
| S166 | Lecturas completadas | `noc_resp_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:351)) | Transacciones | Handshake R y r.last. |
| S167 | Escrituras completadas | `noc_resp_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:351)) | Transacciones B | b_valid && noc_req_o.b_ready. |
| S168 | Backpressure de canales AXI | `noc_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349)) | Cinco contadores AR/AW/W/R/B | Cada canal valid && !ready; son ciclos de interfaz, no necesariamente stalls de CPU. |
| S169 | Errores AXI | `noc_resp_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:351)) | Respuestas con error | Clasificar r.resp/b.resp y calificar handshake. |
| S170 | Tráfico útil de escritura | `noc_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349)) | Bytes | Popcount w.strb en cada beat aceptado. |
| S171 | Distribución de tamaños y ráfagas | `noc_req_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cva6.sv:349)) | Familia por size/len/burst/id | Campos ar/aw; no instrucciones ni bytes sin conversión. |

### Colas del adaptador AXI WT

Fuente: [core/cache_subsystem/wt_axi_adapter.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_axi_adapter.sv:74).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S172 | Cola de solicitudes I-cache llena | `icache_data_full` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_axi_adapter.sv:74)) | Ciclos | Calificar icache_data_req_i para presión efectiva. |
| S173 | Cola de solicitudes D-cache llena | `dcache_data_full` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_axi_adapter.sv:76)) | Ciclos | Calificar dcache_data_req_i. |
| S174 | Cola de respuestas B llena | `b_full` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_axi_adapter.sv:421)) | Ciclos | Limita axi_wr_rdy. |
| S175 | Aceptación de solicitud I-cache al adaptador | `icache_data_ack_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_axi_adapter.sv:37)) | Transacciones | Ya calificada con petición. |
| S176 | Aceptación de solicitud D-cache al adaptador | `dcache_data_ack_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/cache_subsystem/wt_axi_adapter.sv:45)) | Transacciones | Separar lectura/escritura con el tipo de solicitud. |

### Control, excepciones y privilegio

Fuente: [core/controller.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:33).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S177 | Flush por etapa | `flush_if_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:33)) | Familia IF/ID/EX/unissued/BP | También flush_id_o/flush_ex_o/flush_unissued_instr_o/flush_bp_o; señales pueden mantenerse varios ciclos. |
| S178 | Flush por causa | `resolved_branch_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:91)) | Familia mispredict/excepción/CSR/fences | Causas ex_valid_i,flush_csr_i,fence_i,fence_i_i,sfence_vma_i,fence_t_i; no imponer exclusividad sin política. |
| S179 | Halt de commit | `halt_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:67)) | Ciclos | Separar causas con halt_csr_i/halt_acc_i. |
| S180 | Halt del frontend | `halt_frontend_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:65)) | Ciclos | Puede diferir del halt de commit. |
| S181 | Stall del subsistema de caché | `stall_cache_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:71)) | Ciclos | Estado del control, no necesariamente una solicitud bloqueada. |
| S182 | Actividad por privilegio | `priv_lvl_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:83)) | Ciclos M/S/U | Crear filtrado en detector; permisos de lectura CSR no filtran por sí solos eventos. |
| S183 | Solicitud de interrupción de temporizador | `time_irq_i` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/controller.sv:81)) | Ciclos pendientes/episodios | Para interrupciones tomadas usar excepción aceptada con cause correspondiente. |

### CV-X-IF: interfaz presente pero dependiente del coprocesador

Fuente: [core/issue_read_operands.sv](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:115).

| Ref. | Evento/familia | Señal existente | Magnitud y condición | Límites |
|---|---|---|---|---|
| S184 | Offload aceptado | `x_transaction_accepted_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:115)) | Operaciones | CvxifEn=1; requiere unidad externa y opcode ejecutado. |
| S185 | Offload rechazado | `x_transaction_rejected` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:221)) | Operaciones | No prueba presencia de acelerador útil. |
| S186 | Solicitud CV-X-IF pendiente | `x_issue_valid_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:105)) | Ciclos | Calificar !x_issue_ready_i para backpressure. |
| S187 | Transferencia de registros CV-X-IF | `x_register_valid_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:109)) | Transferencias | Calificar ready y protocolo. |
| S188 | Notificación de commit CV-X-IF | `x_commit_valid_o` ([fuente](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/issue_read_operands.sv:112)) | Notificaciones | Distinguir commit/kill según protocolo. |

## Familias que necesitan estado de monitorización adicional

Las siguientes se pueden implementar con información existente, pero no tienen un pulso o contador listo. No se deben clasificar como solo cablear una señal:

| Medida | Información disponible | Lógica adicional necesaria |
|---|---|---|
| Latencia por operación FPU | Etiquetas de entrada/salida y reloj | Tabla por trans_id con ciclo inicial, validez y cancelación; resolver reutilización. |
| Latencia por carga o transacción AXI | ID, handshake, respuesta y reloj | Tabla de pendientes; manejar beats, IDs reutilizados, orden y cancelaciones. |
| Tiempo de recuperación de mispredict | Resolución, flush, emisión y retirada | Elegir inicio/fin y mantener estado; no usar solo conteo de mispredict. |
| Histograma de rachas de stall | Pulsos/condiciones RAW/FU/LSU | Acumulador de longitud y bins. |
| Atribución causal exclusiva del CPI | Todas las causas pueden coincidir | Política explícita de prioridad/exclusividad y validación por trazas. |
| Muestras con PC al umbral | Eventos y PC existen en distintas etapas | Captura coherente y buffer; estilo RVEBS, no basta interrupción ordinaria. |
| Hits/misses por dirección o función | Direcciones, PC e IDs | Asociar las etapas y transacciones; instrumentar rangos o mantener etiquetas. |

## Señales y eventos que no deben darse por activos

- RVV y FP vectorial están deshabilitados: los nombres FPU_VEC o interfaces de aceleración no prueban actividad vectorial.
- El bloque ACCEL está deshabilitado por COPRO_NONE; no confundir el nombre del caso de control ACC con el puerto ACCEL del procesador.
- CV-X-IF está habilitado pero su uso depende del coprocesador y del programa. No se observó una integración nueva ni se ejecutó su protocolo en esta auditoría.
- ALU2 y segundo puerto de emisión corresponden al modo superscalar, deshabilitado aquí. Los dos puertos de commit sí están configurados.
- Hipervisor, FP16/FP8 y CLIC están deshabilitados. No presentar eventos de esas ramas como disponibles en la instancia activa.
- ITLB/DTLB/PTW existen, pero en ejecución sin traducción sus eventos pueden permanecer en cero; M-mode no utiliza traducción ordinaria de instrucciones y MPRV puede afectar datos.
- No se identificó un pulso explícito stall_waw ni fp_dependency_stall. Para dependencia por productor se usa la información RAW/scoreboard y se construye el detector.

## Capacidad de ampliación y comprobaciones para usar el inventario

- Quedan nueve valores de selector sin detector (23..31), no nueve contadores adicionales. Para exponer simultáneamente un catálogo mayor que 31 eventos se debe ampliar la codificación/selector, usar bancos explícitos u otra interfaz; revisar truncamientos en escrituras.
- Los seis HPM pueden seleccionar seis eventos a la vez. Medir grupos por replay determinista preservando entrada, warm start, condiciones iniciales y configuración; no mezclar grupos de pasos distintos del lazo.
- Validar reset, inhibit, debug, lecturas/escrituras, solapamientos, dos commits, solicitudes sostenidas sin grant, replays, cancelaciones y errores. Una señal bool sostenida cuenta ciclos; un handshake cuenta operaciones aceptadas.
- Si se necesitan eventos por operación exacta o dos fuentes concurrentes, usar suma de ocurrencias/popcount; el incremento booleano actual colapsa concurrencia.
- En FPGA se requiere síntesis e implementación de los cambios. En Verilator, regenerar/recompilar el modelo. No se ha realizado ninguna de esas operaciones aquí.
- Registrar firmware, configuración y hash de fuentes/binary antes de relacionar conteos con ACC/OSQP o CartPole/SLSQP.

## Evidencias previas encontradas en el proyecto

El proyecto R-SHARC ya contiene inventario HPM P04 y capturas bare metal P06. Su README limita P06 a atribución descriptiva de ciclos/instrucciones y advierte que los HPM no dan tasas exactas de miss ni fracciones causales de stalls. Esto es evidencia existente, no una validación ejecutada nuevamente en esta tarea.

[Inventario local P04](/home/jminiesta/Repositorios/R-SHARC/artifacts/acc_profile/p04_20261009/local_inventory/inventory.json), [revisión de diseño P04](/home/jminiesta/Repositorios/R-SHARC/scripts/hector/review_acc_hpm_inventory.py:65), [estado documentado](/home/jminiesta/Repositorios/R-SHARC/scripts/hector/README.md:15).

## Huellas de los archivos inspeccionados

| Fuente | SHA-256 |
|---|---|
| `core/cache_subsystem/cva6_icache.sv` | `300eb1a49a906643c1340ec0790fc850af03ac76bf4d43ede2f480687d8c6360` |
| `core/cache_subsystem/wt_axi_adapter.sv` | `8141dfb8af2f098529b421a1b281bb167a74f4e79bcf7881a795aad3c286d3dd` |
| `core/cache_subsystem/wt_dcache_ctrl.sv` | `d2a8a3207ce394d9e703a8dea514759d14ce9995091c0e88c8dbf3b291861e51` |
| `core/cache_subsystem/wt_dcache_missunit.sv` | `f7aed679ebd9fe990b88f064b8a9200be9b5f7c83277d10c7bc0af147cbedf64` |
| `core/cache_subsystem/wt_dcache_wbuffer.sv` | `4565fbe5f043c8ee1a52254e7f4b4ff7152aa81f2d8e68b6cc9211e5581f1cf8` |
| `core/commit_stage.sv` | `bf59bfa36960ec6227cb49d86e60f8b8f9507590f64ecfafffa9d8d8f94abb60` |
| `core/controller.sv` | `d9c9cc1dea0c2a4aa2be29093b0466f11f7c28c0717825e8b16418d181b44b5c` |
| `core/csr_regfile.sv` | `769879d3b3776bdd0a3c1afaba8eec28940f1474c00ea3e8fc5bae84902f48da` |
| `core/cva6.sv` | `0efa3a78cc09416a7f4d787408d5d1620aa37a47039b70c53b3134ce4d766764` |
| `core/cva6_mmu/cva6_mmu.sv` | `4859d957d56541c27928e347cee7ef800486cf3b1ad91aa53a9e3e044344c480` |
| `core/cva6_mmu/cva6_ptw.sv` | `82dab3584098056cff37c18e3070875b0b71ba185e90fd878151fc197c0d9505` |
| `core/fpu_wrap.sv` | `19532a3b0836f0e31ed5e98f6b59252ce03acfa868e9605e2947d06b2eedc58b` |
| `core/frontend/frontend.sv` | `b7c253822fa0d8e46bcbd2f1b0779de5d60df1915e3ac60719e2508ba99cb8d1` |
| `core/frontend/instr_queue.sv` | `c202b68bd44cf21ed0e374483c0dc70e6579174686d7a4d2b9022a818320ce3d` |
| `core/include/ariane_pkg.sv` | `81ea34908a5440f3789616b72592cb128a4dffbdb5a580061463b7e551bcde92` |
| `core/include/build_config_pkg.sv` | `f8da8e2736ed10549187d06c2bd58f4d562ad3967ec62b3def290e0fc29e2f1b` |
| `core/include/cv64a6_imafdc_sv39_config_pkg.sv` | `288a01463356f6e107cd18f729de2f301b155f4f6da0652b58b0011e5bdad5d7` |
| `core/issue_read_operands.sv` | `5a3eb7c76f423a904c4d877957895eea5ac853e0ad368766caa756d2cfe389d5` |
| `core/load_unit.sv` | `2c2b71a94f8dbdceda0e46b2c8ca23e1a2e3d2d3621bbd39cf87c87ee591a72c` |
| `core/mult.sv` | `e6660a01fc56369e2e1ad96ff89fa1fcafde795d398eaf5c812d7e9afd84ec59` |
| `core/perf_counters.sv` | `4f381974ded0277f663c2991bcddb93a2a9c5de43b3847686607f293a5bc0fd8` |
| `core/scoreboard.sv` | `f9386b9ffb873f2223718518766642388ecd27f11d4021524a3e794275586ea9` |
| `core/serdiv.sv` | `7b4d4b0be7512ec025fd3f4cec4e140e2f4466115237dd244fabc6b2095425f6` |
| `core/store_buffer.sv` | `9d5cdfaa0bd1546587f2a37707eee09535fc8a471e4b31da3d9e8b577833fb03` |
| `core/store_unit.sv` | `4dfb8051044bf37a74754ae7b1a5b104cd4b86f3e9d3648996ad474027e91a0e` |
