# Selección de eventos CVA6 para ACC y CartPole bare metal

Revisión: 10 de noviembre de 2026. Estudio de la bibliografía del catálogo ODS, el RTL local identificado en el inventario y resultados archivados del proyecto. No se han ejecutado nuevas medidas ni modificado RTL, firmware o catálogo.

## Recomendación y alcance

Usar primero los contadores existentes: **mcycle, minstret y dos grupos HPM**, uno para detectar limitaciones y otro para describir las operaciones del programa. La primera campaña no necesita añadir eventos al hardware. Las prioridades que siguen son una propuesta para esta tesis, motivada por los artículos; ningún artículo establece una selección universal óptima para ACC/OSQP y CartPole/SLSQP sobre nuestra configuración.

La fuente auditada es CVA6 base `4c02b24fe7c04690f626776a92274da24f80d1da`, con modificaciones locales, configuración `cv64a6_imafdc_sv39`: RV64GC, F/D, emisión escalar, dos puertos de commit, scoreboard de ocho entradas, I-cache de 16 KiB y D-cache WT de 32 KiB. Hay seis HPM de 64 bits, `mhpmcounter3..8`, con selección independiente de eventos; se añaden a los dos contadores fijos. El nombre ACC de nuestra aplicación no implica un acelerador ACCEL activo. Esta configuración no tiene RVV.

Esto identifica las fuentes relacionadas con las pruebas archivadas. El repositorio R-SHARC-FPGA no vincula todavía un bitstream nuevo con ellas. Antes de medir otra FPGA, se debe comprobar la configuración y procedencia de ese bitstream.

Fuentes locales: [inventario completo](cva6_event_inventory.md), [selectores y detectores](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/perf_counters.sv:95), [incremento HPM](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/perf_counters.sv:148), [contadores fijos](/home/jminiesta/Repositorios/SHARC_RISCV/CVA6_LINUX/cva6/core/csr_regfile.sv:1023).

## Bibliografía que determina la selección

Las filas indicadas corresponden a la tabla del ODS, incluida su cabecera. Se contrastaron los registros de profiling, microarquitectura, control óptimo y tiempo real; no se consideran las notas preliminares del catálogo como resultados experimentales.

| Referencia del catálogo | Evidencia y acceso contrastado | Aplicación a nuestra selección |
|---|---|---|
| Fila 162, *Supporting RISC-V Performance Counters Through Linux Performance Analysis Tools*, 2023, [DOI](https://doi.org/10.1109/asap57973.2023.00027). Antecedente del mismo grupo: [CARRV 2021, PDF completo](https://carrv.github.io/2021/papers/CARRV2021_paper_29_Domingos.pdf) | El antecedente demuestra profiling sobre CVA6 con Linux y combina IPC, cachés, scoreboard e IF vacío. Sus asignaciones de contador son distintas de las nuestras. No se ha verificado el texto completo de la edición ASAP 2023. | Conservar esos ejes de observación; sustituir la infraestructura Linux por acceso CSR desde bare metal. Usar nuestras definiciones RTL, sin copiar literalmente las tasas del trabajo antiguo. |
| Fila 164, Ottavi et al., *Reducing Load-Use Dependency-Induced Performance Penalty in the Open-Source RISC-V CVA6 CPU*, 2023, [DOI](https://doi.org/10.1109/dsd60849.2023.00066) | Abstract del catálogo y referencia del autor; texto completo no verificado. Identifica dependencias load-use como causa importante de stalls de CVA6 en Embench-IoT. | Prioridad alta a HPM22 junto con loads y caché de datos. Es un precedente para investigar dependencias, no una demostración del cuello de botella de nuestro solver ni de que HPM22 mida solo load-use. |
| Fila 166, Million et al., *Breaking the Memory Wall with a Flexible Open-Source L1 Data-Cache*, 2024, [repositorio institucional y texto indexado](https://upcommons.upc.edu/entities/publication/3d98b4ef-8c4f-448e-b05d-5e6988a7f67d) | Contrasta cachés de CVA6 y destaca concurrencia, pipelining y comportamiento de accesos indirectos, incluidas matrices dispersas. | Priorizar HPM2 y 5/6 en ACC. El número de misses no caracteriza por sí solo latencia ni concurrencia de memoria. HPDcache es una alternativa arquitectónica posterior; nuestra caché auditada es WT convencional. |
| Fila 185, Weingarten et al., *Icicle*, IISWC 2025, [PDF completo](https://takhandipu.github.io/papers/weingarten-icicle-iiswc-2025.pdf) | Implementa TMA en Rocket y BOOM, añade eventos y valida con trazas. Muestra que señales incompletas pueden atribuir incorrectamente los stalls. No estudia CVA6. | Cruzar frontend, dependencias, memoria y especulación; evitar sumar eventos solapados o convertir misses en tiempo perdido mediante una penalización fija. Nuestra PMU no permite reproducir directamente su desglose TMA. |
| Fila 107, Dong et al., *Characterizing and Optimizing Real-Time Optimal Control for Embedded SoCs*, IISWC 2025, [PDF completo](https://people.eecs.berkeley.edu/~chrisdong/IISWC_2025.pdf) | Estudia TinyMPC, kernels y aplicaciones completas sobre arquitecturas escalares, vectoriales y matriciales. Describe dependencias y problemas de aprovechamiento de operadores pequeños. No evalúa nuestro OSQP/SLSQP ni este CVA6. | Combinar medición de kernels con latencia completa del controlador. HPM21 aporta contexto de operaciones FP; HPM5/6 y 22 ayudan a explorar su alimentación y dependencias. No presupone que vectorizar o acelerar una operación mejore el lazo completo. |
| Fila 172, Jimenez Arribas et al., *Design and implementation of a synchronous Hardware Performance Monitor for a RISC-V space-oriented processor*, registro 2024, [preprint completo](https://arxiv.org/pdf/2404.05389) | Propaga información de eventos para sincronizar su anotación con las instrucciones. Es otro procesador. | Documentar etapa de generación y unidad de conteo; validar por separado actividad física e instrucciones retiradas. Un nombre de evento no garantiza atribución exacta a una instrucción. |
| Fila 174, Morgado et al., *CARM Tool*, IISWC 2024, [documentación primaria del proyecto](https://champ-hub.github.io/projects/The_CARM_Tool/) | Caracterización de memoria y cómputo mediante contadores y/o instrumentación binaria. La documentación describe análisis por regiones y microbenchmarks. | Inspira el contraste memoria/cómputo y el uso de microbenchmarks. Nuestros eventos actuales no bastan para obtener bytes transferidos ni FLOPs exactos: no presentar un roofline cuantitativo obtenido solo de HPM5/6/21. |
| Fila 176, Rocha et al., *RVEBS: Event-Based Sampling on RISC-V*, DATE 2025, [PDF completo](https://hpcas.inesc-id.pt/~unify/papers/conf_date25.pdf) | Muestreo por eventos en CVA6/OpenPiton con nuevos CSRs, lógica de captura y cambios en caché. | Precedente directo de profiling avanzado en CVA6. No está disponible con nuestra PMU sin modificar hardware; dejarlo para una necesidad demostrada de atribución a PC con pocas sondas. |
| Fila 188, *Lightweight Performance Monitoring of Real-Time Applications in RISC-V Platforms*, DSD 2025, [DOI](https://doi.org/10.1109/dsd67783.2025.00062), [publicación del equipo](https://www2.isep.ipp.pt/softcps/?page_id=45) | Abstract del catálogo y publicación del equipo; texto completo no verificado. Su objetivo incluye acceso a métricas con poco overhead y memoria. | Favorece una interfaz pequeña y medir el coste de las sondas. No demuestra que la biblioteca del artículo sea directamente utilizable en nuestro M-mode bare metal. |
| Fila 187, *Eliminating Write-After-Write Hazards to Improve Performance in Embedded Processors*, DSD 2025, [DOI](https://doi.org/10.1109/dsd67783.2025.00046) | Abstract del catálogo: estudia WAW sobre una variante superscalar de CVA6. Texto completo no verificado. | Recuerda que RAW no cubre todos los hazards. HPM22 no diagnostica WAW; no atribuirle un desglose completo del backend. |
| Fila 197, Chen et al., *A Language Approach to Fine-Grained Microarchitectural Observation*, OOPSLA 2026, [abstract oficial](https://2026.splashcon.org/details/oopsla-2026/112/A-Language-Approach-to-Fine-Grained-Microarchitectural-Observation) | El abstract describe pruebas de hipótesis y eventos temporales construidos desde trazas sobre XiangShan. No se ha verificado el PDF completo. | Refuerza validar hipótesis concretas con trazas breves cuando un contador agregado no resuelve la causa. No constituye una implementación para CVA6. |
| Fila 518, *Work-in-Progress: Worst-Case Execution-Time Measurement Techniques for Nonlinear Model Predictive Controllers*, 2024, [DOI](https://doi.org/10.1109/codes-isss60120.2024.00009) | Abstract del catálogo: compara generación de pruebas y subestimación del WCET, incluyendo péndulo invertido. Texto completo no verificado. | Incluir estados y dificultades de solver diferentes. El máximo observado de ciclos no es una cota WCET garantizada. |

También se revisaron los registros de modelos CVA6/gem5 de las filas 161, 167 y 177, y predictibilidad de las filas 154 y 157. Son útiles para exploración o análisis temporal, pero no sustituyen la validación contra nuestro RTL/configuración. El estudio no permite afirmar que nadie haya perfilado anteriormente estos casos exactos: hay precedentes de PMU en CVA6 y de caracterización MPC; la novedad de la combinación concreta requiere una búsqueda específica adicional.

## Qué medir obligatoriamente

**Siempre:** ciclos `C = Δmcycle` e instrucciones retiradas `I = Δminstret`, con `CPI = C/I`. Son la referencia para distinguir reducción del trabajo ejecutado y mejora de la ejecución de ese trabajo. No ocuparán ninguno de los seis HPM genéricos.

**Para la campaña principal:** los once selectores distintos de las dos pasadas A/B siguientes. La prioridad mayor de diagnóstico corresponde a A. B es necesaria para interpretar las diferencias entre ACC y CartPole y fundamentar propuestas de aceleración.

| ID | Prioridad | Información útil para nuestros casos | Interpretación que debe conservarse |
|---|---|---|---|
| 22 | Principal | Dependencias al emitir: cadenas de operaciones de QDLDL, reducciones, álgebra de SLSQP y modelo no lineal. Es la primera señal a investigar en el ACC ya archivado. | `stall_raw[0]`, sin calificar candidata válida; incluye la inicialización con `stall_i`. No equivale a todos los stalls ni identifica el productor LOAD/FPU. |
| 2 | Principal | Reposición de datos en accesos dispersos, índices, factores y vectores. Contrasta hipótesis de localidad. | Asignaciones de MSHR para lecturas cacheables de la caché WT. No mide espera por hit, latencia individual, todos los stores ni bytes. |
| 1 | Principal | Huella de código, bibliotecas, cambios de fase y costes de primera llamada. Especial interés en CartPole con solver y funciones matemáticas. | Refills cacheables de instrucciones aceptados; no ciclos perdidos por esos refills. |
| 14 | Principal como comprobación | Detecta presión del scoreboard y falta de entradas para instrucciones en vuelo. Ayuda a separar este síntoma de RAW. | Scoreboard lleno; no store buffer lleno ni bloqueo causal calificado por candidata válida. Puede resultar poco frecuente y ese resultado también es informativo. |
| 15 | Principal | Indica escasez de instrucciones en la interfaz de entrada; cruzar con I-cache y mispredicts. | `~fetch_valid_if_id[0]`, sin calificar halt/ready. No es el porcentaje exacto de tiempo frontend-bound. |
| 10 | Principal | Examina especulación en decisiones, restricciones, convergencia y bucles de librería. Permite evitar culpar siempre a memoria o FP. | Resoluciones con predicción errónea. Etapa/población distintas del evento 9; no multiplicar por una penalización fija para obtener ciclos perdidos. |
| 5, 6 | Principal de caracterización | Actividad de cargas y stores frente al cálculo, diferencias entre kernels y efecto del layout. | OR de puertos de commit: ciclos con alguna retirada de esa clase, no suma exacta de instrucciones ni tráfico externo. |
| 21 | Principal de caracterización | Presencia de cómputo FP en doble precisión y comparación entre modelo, solver y kernels. | Ciclos con retirada FPU/FPU_VEC; no FLOPs, ocupación FPU ni FP load/store. No distingue FADD/FMUL/FMA/FDIV. |
| 9 | Principal de caracterización | Contexto de flujo de control para comparar fases y builds, junto con el evento 10. | CTRL_FLOW incluye saltos; OR de commit. No es un denominador validado de tasa exacta de mispredicts. |
| 20 | Principal de caracterización | Trabajo ALU/MULT de índices, direcciones y control en relación con las operaciones FP. | Subconjunto de clases enteras, agregado por OR; no todo el trabajo con registros enteros. |

No sumar las clases como si reprodujeran `minstret`: el contador fijo suma las retiradas y los HPM de clase hacen OR. Aunque la emisión sea escalar, hay dos puertos de commit y no se debe suponer equivalencia sin validación.

**Resto de selectores, decisión explícita:**

| ID | Tratamiento propuesto | Motivo |
|---|---|---|
| 7, 8 | Campaña de control; conservar además registro de traps en todas las ejecuciones | Excepciones y retornos pueden revelar emulación, interrupciones o contaminación de la región. `ex_i.valid` no debe interpretarse automáticamente como un número exacto de entradas a handler. |
| 16, 17 | Secundarios; útiles también para continuidad con capturas anteriores | Cuentan solicitudes activas por ciclo sin handshake de aceptación. No son denominadores válidos de miss rate ni ancho de banda. |
| 3, 4, 19 | Condicionales | TLB y flush de traducción. Confirmar privilegio, `satp` y, para accesos de datos M-mode, `MPRV`/privilegio efectivo. Bare metal no garantiza por sí solo ausencia de traducción, pero no justifican una campaña principal si está desactivada. |
| 11 | Diagnóstico de excepciones de branch | Útil si aparecen traps de esa procedencia; no mide predicción errónea. |
| 12, 13 | Baja prioridad | Heurísticas de calls/returns. Las sondas por región y el mapa de funciones dan información más directa para los solvers. HPM13 puede incluir otros JALR con rd=x0. |
| 18 | Excluir de conclusiones cuantitativas sobre evicciones | El detector es una heurística de miss global y vectores llenos, sin asociación correcta al puerto causante y con cobertura parcial de puertos. No lo usaría como evidencia de sustituciones reales. |

Todos los eventos anteriores ya están conectados. Tener señales de otros eventos disponibles internamente no los convierte en selectores utilizables desde software.

## Dos pasadas principales y controles

Cada columna es un contador físico; los números de las celdas son valores de selector, no números de CSR.

| Pasada | mhpmcounter3 | mhpmcounter4 | mhpmcounter5 | mhpmcounter6 | mhpmcounter7 | mhpmcounter8 |
|---|---|---|---|---|---|---|
| A: diagnóstico | 1 I-refill | 2 D-read-MSHR | 10 mispredict | 14 scoreboard lleno | 15 IF vacío | 22 señal RAW emisión |
| B: operaciones | 5 loads | 6 stores | 9 CTRL_FLOW | 10 mispredict | 20 ALU/MULT | 21 FP |
| C: control, secundaria | 1 I-refill | 2 D-read-MSHR | 7 excepción válida | 8 retorno excepción | 16 I-req | 17 D-req |

Medir `mcycle` y `minstret` en todas. El evento 10 se repite en A/B como referencia adicional; C conserva anclas de caché. Los grupos antiguos `[1,2,16,17,14,22]` siguen siendo útiles y no requieren descartar las capturas: A sustituye los dos contadores de solicitudes por frontend vacío y predicción para ampliar el diagnóstico.

No hacer multiplexación por tiempo dentro de un solve corto o de fases diferentes. Reproducir las mismas entradas, parámetros y condiciones iniciales con otro grupo. Preferir el mismo ELF con selección del grupo en arranque y fuera de las regiones medidas. Cambiar el layout del código puede cambiar las cachés y la predicción.

No se deben combinar razones de pasadas con trabajo distinto. Comprobar resultados numéricos, estado del solver, iteraciones/evaluaciones, instrucciones y repetibilidad de ciclos; si difieren, investigar antes de unir los datos. Un snapshot recién inicializado y un paso con historia del lazo no son equivalentes. No se ha demostrado aún la repetibilidad de los grupos propuestos.

## Aplicación a ACC y CartPole

**ACC / OSQP.** Primera granularidad: llamada completa, modelo/discretización y restricciones, construcción del QP, conversión sparse, `osqp_setup`, `osqp_solve` y cleanup. El código actual sitúa [optimize en ACC](/home/jminiesta/Repositorios/R-SHARC-FPGA/src/acc/ACC_Controller.cpp:130) y [setup/solve en LOptimizer](/home/jminiesta/Repositorios/R-SHARC-FPGA/external/libmpc/include/mpc/LMPC/LOptimizer.hpp:261).

Segunda granularidad, dentro de solve: QDLDL_Lsolve/Ltsolve, productos dispersos, actualizaciones de vectores, residuales/terminación y refactorizaciones por cambios de rho cuando estén presentes. Medir por separado si añadir todas las sondas perturba demasiado la ejecución. No asumir que todas estas fases aparecen en cada llamada.

La [evidencia archivada de ACC](/home/jminiesta/Repositorios/R-SHARC/docs/Results_HSCC_Conference/ACC_Resultados_x86_Spike_CVA6.md:80) proporciona una razón concreta para esta prioridad:

| Captura aislada k=17 | Valor archivado o cálculo descriptivo |
|---|---:|
| Ciclos de llamada completa | 142.005.936 |
| Ciclos de osqp_solve | 139.993.118; 98,583 % de la llamada |
| Activaciones HPM22 dentro de solve | 29.204.080; 20,861 % de sus ciclos |
| Activaciones HPM14 dentro de solve | 5.029; 0,00359 % de sus ciclos |
| Eventos HPM2 dentro de solve | 28.223 |
| Eventos HPM1 dentro de solve | 2.043 |

Esto prioriza investigar dependencias frente a la hipótesis de scoreboard lleno en ESA captura. No prueba que el 20,861 % sea tiempo recuperable ni que el productor sea una carga o una operación FP. La captura k=17 llega a 5.000 iteraciones y `MAX_ITERATION`; no representa por sí sola todos los casos. En k=0 el peso relativo de setup es mayor. El perfil x86 de kernels no demuestra sus porcentajes de tiempo sobre RTL.

El informe histórico llama a HPM14 «Store buffer full» en su tabla; el RTL revisado indica **scoreboard lleno**. Se corrige aquí su interpretación, sin modificar el documento original. Tampoco hay todavía medidas archivadas de predicción o un desglose causal completo de stalls en esas capturas.

**CartPole / SLSQP.** Separar llamada completa a optimize, evaluaciones de coste, restricciones/modelo, cálculo de derivadas según la implementación enlazada y álgebra interna SLSQP: H12, DOT, AXPY y otros kernels que confirme el perfil. Evitar contar dos veces callbacks incluidos dentro del solver al sumar tiempos. Las evaluaciones del solver y sus iteraciones son magnitudes distintas; no sustituir unas por otras cuando el runtime no exporte iteraciones.

El [modelo actual](/home/jminiesta/Repositorios/R-SHARC-FPGA/src/cartpole/NLMPCController.cpp:41) contiene double y sin/cos/pow; el [optimizador](/home/jminiesta/Repositorios/R-SHARC-FPGA/external/libmpc/include/mpc/NLMPC/NLOptimizer.hpp:64) selecciona LD_SLSQP. Eso motiva examinar dependencias, actividad FP, huella de código y memoria. No demuestra qué llamadas matemáticas sobreviven a la compilación ni que dominen el tiempo: comprobar desensamblado y región medida.

Las variantes CartPole RVV de Spike archivadas en el repositorio vecino corresponden a otra configuración de ejecución. No proporcionan medidas de utilización vectorial ni de caché de este CVA6 escalar.

## Métricas y protocolo para una tesis defendible

Para cada región guardar valores brutos, ciclos, instrucciones, entrada/caso, estado del solver y número de operaciones algorítmicas disponibles.

- `CPI = C/I`. La identidad `C = I × CPI` permite distinguir cambios de cantidad de trabajo y ejecución por instrucción; con pasos diferentes no implica una causa microarquitectural única.
- `1000 × E1/I`: refills de instrucciones por mil instrucciones retiradas.
- `1000 × E2/I`: asignaciones cacheables de MSHR de lectura por mil instrucciones retiradas.
- `1000 × E10/I`: resoluciones con mispredict por mil instrucciones retiradas. Es una densidad, no una probabilidad de fallo de todas las instrucciones de salto.
- `E22/C`, `E14/C`, `E15/C`: fracciones de ciclos con esas señales activas, etiquetadas por su nombre. No categorías causales disjuntas; no sumarlas ni restarlas de 100 % para obtener tiempo útil.
- `1000 × E5/6/9/20/21 / I`: densidades de ciclos con retiradas de cada clase. No porcentajes exactos de mezcla de instrucciones, FLOPs ni bytes.
- Ciclos por iteración OSQP o por evaluación/callback cuando se mida esa región. Separar setup y cambios de algoritmo; una media de solve por iteración no caracteriza el coste de todas las iteraciones.
- Latencia completa, mediana, dispersión y máximo observado sobre casos definidos; plazos incumplidos. Para convertir ciclos a tiempo, identificar frecuencia medida/validada o declarar explícitamente la frecuencia supuesta.

**Acceso bare metal.** Programar `mhpmevent3..8` desde M-mode, controlar los seis HPM con sus bits de `mcountinhibit` y leer deltas alrededor de regiones C/C++. Son contadores de hardware existentes; las sondas solo los configuran y leen. Si el runtime está en U-mode, se necesita configuración privilegiada de acceso; no asumir que puede escribir los selectores directamente. Linux, perf y OpenSBI no son requisitos de la medición M-mode.

Configurar/inhibir fuera de la región, fijar el orden del compilador en las sondas y documentar la definición exacta de inicio/fin. Los contadores se leen secuencialmente, por lo que la simultaneidad y el coste de las fronteras deben caracterizarse. Mantener `mcycle` libre si el runtime lo usa como reloj del lazo; no congelar inadvertidamente su base de tiempo al controlar HPM. Un fence de memoria cambia lo que se incluye y su coste: usarlo solo si forma parte de la definición de la región, igual en todos los ensayos.

**Validación mínima de la campaña, antes de atribuir causas:**

1. Conservar hash de ELF, bibliotecas y RTL, configuración, flags, reloj y entorno de memoria. Verificar que se usa el hardware identificado.
2. Comprobar semántica con microbenchmarks: operaciones independientes/dependientes; instrucciones/cargas conocidas; working sets que cambien la localidad; comportamiento de branches. Contrastar ventanas pequeñas con commit/trazas RTL cuando estén disponibles, sin ejecutar una traza de toda la aplicación por defecto.
3. Medir marcador vacío y comparar sondas OFF/ON. Publicar overhead; una resta de coste constante no corrige automáticamente cambios de caché o layout. Las sondas de kernels muy pequeños pueden ser inaceptables: medir lotes de llamadas o usar ventanas agregadas.
4. Hacer A/B sobre los mismos casos con condiciones iniciales definidas. Separar primera llamada, estado caliente y ejecución con historia del lazo; no asumir que una función llamada warmup deja idéntico estado de caché.
5. Incluir llamadas fáciles, cercanas al límite y terminadas por máximo de iteraciones; cambios de restricciones/estado; horizontes y tolerancias relevantes. Evitar deducir WCET de un solo caso difícil.
6. Para comparar optimizaciones, usar primero replay de entradas congeladas y después lazo cerrado. En lazo cerrado los cambios de latencia pueden cambiar estados futuros y dificultad del solver, lo que mezcla efectos.
7. Relacionar cualquier mejora con ciclos completos y corrección del control, residual/objetivo y estado del solver. Los contadores no miden por sí solos energía, Fmax ni calidad del control.

Un experimento que modifica layout, tamaño de problema, orden de operaciones o compilación es una prueba de hipótesis. Una correlación entre dos contadores no demuestra causalidad. Si una transformación reduce HPM22 y tiempo a igualdad de trabajo/resultados, la hipótesis de dependencias gana apoyo; identificar LOAD frente a FPU todavía puede exigir traza o eventos adicionales.

## Ampliaciones, solo después del diagnóstico inicial

La PMU actual permite una caracterización útil sin cambios RTL, pero no un reparto exhaustivo y exacto de todos los ciclos por causa. Si la tesis necesita ese reparto, priorizar ampliaciones ligadas a una hipótesis observada:

| Síntoma | Ampliación con mayor utilidad | Estado actual |
|---|---|---|
| HPM22 alto sin explicación | Stall con candidata válida y productor de dependencia LOAD/FPU/ALU; distinguir RAW de WAW/otras restricciones | Hay señales de hazard y scoreboard, pero el detector compuesto y su conexión requieren trabajo y validación. |
| Sensibilidad a memoria sin aumento proporcional de HPM2 | Espera/ocupación MSHR, aceptación de loads, replays, colas y latencia de respuesta; presión real del store buffer | Hay señales internas. No son selectores existentes ni se obtienen restando HPM5 y HPM17. |
| Sospecha de cómputo FP serial | Operaciones FP aceptadas por tipo; espera por operandos/recurso; latencia mediante identificación de transacciones | HPM21 es insuficiente. El busy de FPnew no está expuesto a la PMU actual; no suponer un contador de utilización ya disponible. |
| IF vacío alto con pocos refills | Falta de instrucción válida cuando el consumidor está listo, separando flush, halt y recuperación de predicción | Necesita condiciones adicionales; HPM15 solo no reproduce el frontend-bound de Icicle. |
| Muchas regiones pequeñas y coste de sondas excesivo | Trazas breves de simulación o muestreo por eventos atribuido a PC | La observación depende del simulador; RVEBS requeriría integración hardware, no solo configurar un CSR existente. |

La aportación más sólida que permiten preparar estas opciones es relacionar **caso de control → esfuerzo del solver → kernels → señales microarquitecturales → latencia y efecto en el lazo**, con semántica y límites explícitos. Añadir muchos eventos sin esa relación no resuelve por sí mismo el diagnóstico.
