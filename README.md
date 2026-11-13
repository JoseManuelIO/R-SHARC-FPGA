# ACC y Cartpole baseline: planta Python original y MPC separado

La planta se ejecuta en Python en el host y el MPC en un proceso persistente:
x86 nativo, Spike RV64GC o CVA6/Verilator bare-metal. Se reutilizan sin cambios
`acc.py` y `base.py` de `R-SHARC/src/rsharc/dynamics/`. No se importa R-SHARC
ni se necesita su repositorio para ejecutar este benchmark.

## Fuentes necesarias

- `src/dynamics/cartpole.py`: planta original de Cartpole, compartiendo el integrador de `base.py`.
- `src/dynamics/acc.py`: clase original `ACCDynamics`, ecuaciones y
  perturbación del vehículo delantero.
- `src/dynamics/base.py`: clases originales `Dynamics`/`OdeDynamics` e
  integración con `scipy.integrate.solve_ivp`, sin cambiar sus parámetros.
- `src/dynamics/__init__.py`: inicializador vacío del paquete Python;
  no importa Cartpole ni otros módulos de SHARC.
- `src/acc/config.json`: configuración original determinista de 64 pasos,
  periodo 0,2 s, horizontes 5/5 e intervalo rho 25. Es la única configuración:
  la planta la lee y Make genera el header C++ correspondiente dentro de build/.
- `src/acc/main.cpp`: recibe estados, llama al controlador y devuelve control
  y diagnósticos. Una instancia de `ACC_Controller` durante toda la ejecución.
- `src/acc/ACC_Controller.cpp`, `.h` y `ACC_Step_Types.h`: controlador existente,
  conservado sin cambios.
- `src/acc/acc_channel.hpp`, `acc_capture.hpp`: contrato binario y captura.
- `scripts/run_closed_loop.py`: bucle de planta, ejecución, validación y preparación
  del runner. Invoca la dinámica original; no duplica sus ecuaciones.
- `scripts/cva6_bridge.hpp`: adaptador C++ del testbench de CVA6.

Se han eliminado la planta C++ con RK4, el bucle de retardos RK4 sin uso, la
cabecera con la referencia congelada RK4 y los dos scripts vacíos de ejecución.
Cartpole utiliza el mismo lanzador y puente; sus bibliotecas se conservan.

## Dependencias y preparación

GNU Make, CMake, patch, GCC/G++ con C++20 y Python >=3.11. Para la planta se
necesitan NumPy y SciPy, las mismas dependencias utilizadas por R-SHARC.
`requirements.txt` fija las versiones del entorno R-SHARC usado para validar:
NumPy 2.4.4 y SciPy 1.17.1. El manifiesto de ejecución registra también Python.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

Make selecciona `.venv/bin/python` si existe; se puede utilizar otro entorno
compatible mediante `make PYTHON=/ruta/al/python ...`. El código verifica las
versiones para evitar mezclar referencias calculadas con entornos diferentes.
El entorno `.venv/`, bytecode y productos build/ quedan fuera de Git.

Las bibliotecas C++ están en `external/`, con versiones en `external/versions.txt`.
ACC utiliza Eigen, JSON, libmpc y OSQP/QDLDL, sin NLopt. No se descargan ni
actualizan al compilar. Make aplica los parches existentes sobre copias en
build/, manteniendo intactos external/ y el controlador.

## Ejecutar x86 y Spike

```bash
make -j2 run-acc-x86
```

Para RV64 se requiere una toolchain `riscv64-unknown-elf` con C/C++ compatibles
con RV64GC/LP64D y medany. Validado con GCC RISC-V 15.2.0.

```bash
export RISCV_PREFIX=/ruta/al/bin/riscv64-unknown-elf
make -j2 acc
make run-acc
```

Los comandos lanzan la planta Python y el controlador. El ELF por sí solo
espera entradas externas. Los dos procesos intercambian estructuras binarias
por pipes; en Spike se utiliza su dispositivo HTIF de syscalls, sin SO.
El transporte conserva los bits de los números, sin conversión a texto.

## Integración y aplicación del control

`ACCDynamics` define las ecuaciones originales. `OdeDynamics.evolve_state()`
invoca `solve_ivp(f, [t0, tf], x0_flat, t_eval=np.linspace(t0, tf, 100))`,
exactamente como en R-SHARC. Los 100 puntos son puntos de salida; el solver
selecciona sus pasos internos. No queda un segundo integrador RK4.

El escenario conserva actualizaciones de control en los instantes de muestreo:
se calcula el nuevo control y la planta avanza con el control previamente
aplicado, quedando el nuevo para el siguiente intervalo. La perturbación se
mantiene durante ese intervalo. Esta entrega no incorpora el planificador de
retardos de SHARC ni convierte el tiempo de pared de Verilator en retardo físico.

## CVA6/Verilator

ACC y Cartpole comparten el adaptador del testbench. Un runner que solo vuelca RAM al terminar
no permite simular la planta externa. Para preparar el adaptador a partir de
un build CVA6 compatible existente:

```bash
make prepare-acc-runner \
  CVA6_TB=/ruta/al/corev_apu/tb/ariane_tb.cpp \
  CVA6_BUILD=/ruta/al/build/existente
export CVA6_RUNNER="$PWD/build/cva6-mpc-channel/Variane_testharness"
```

La preparación copia el build a build/cva6-mpc-channel/ y recompila el testbench
C++ y su soporte, reutilizando el modelo RTL generado. No cambia RTL, PMA ni
el runner original. El build donante debe conservar sus objetos y fuentes.
Se rechazan anclajes desconocidos y un directorio de salida ya existente.
La procedencia y los hashes se guardan en acc-channel-provenance.json.
El cambio de planta C++ a Python no requiere regenerar este canal.

La configuración donante usa RV64GC en modo máquina, SRAM de 256 MiB desde
0x80000000 y D-cache write-through. El servicio CVA6 bypassa la D-cache mediante
su CSR 0x7c1 durante cada intercambio y restaura el valor antes del MPC, para
leer las escrituras del host sin líneas obsoletas. Esto puede alterar el estado
de caché respecto al antiguo programa con planta interna; sus tiempos no se
consideran equivalentes. Spike no utiliza esa adaptación específica de CVA6.

```bash
make -j2 run-acc-verilator ACC_STEPS=2 MAX_CYCLES=30000000 HOST_TIMEOUT=2400
```

Para los 64 pasos, especialmente en Héctor:

```bash
make -j2 run-acc-verilator ACC_STEPS=64 \
  MAX_CYCLES=4000000000 HOST_TIMEOUT=345600
make validate-acc ACC_STEPS=64
```

Make calcula primero la referencia x86 con la planta original y después ejecuta
y compara RTL. El adaptador espera al host entre ciclos completos: la integración
Python no suma ciclos del core. Los contadores cubren calculateStep(), fuera
de la comunicación y de la planta. El runner y la toolchain son herramientas
externas y deben estar disponibles en el servidor.

Para trasladarlo a Héctor, copiar/clonar fuentes, external/, parches y
requirements.txt, excluyendo build/ y .venv/. Preparar allí el entorno Python y
usar o preparar su runner compatible. No copiar caches de compilación del portátil.

## Construcción, resultados y validación

Para compilar sin ejecutar: `make acc-x86`, `make acc-verilator ACC_STEPS=2`.
ACC_STEPS admite de 1 a 64. Variables: RISCV_PREFIX, PYTHON, HOST_CC, HOST_CXX,
SPIKE, CVA6_RUNNER, MAX_CYCLES, HOST_TIMEOUT y SEED. Por defecto RTL permite
4.000 millones de ciclos, 86.400 s del host y semilla 475092611.

Los ejecutables quedan en build/{x86,rv64,verilator}/acc/<pasos>/acc.elf.
Cada perfil genera su acc_config.hpp a partir del mismo config.json. Ya no se
compila un ejecutable de planta host. Objetos y solver se reutilizan mientras
no cambien sus entradas. Cambiar ACC_STEPS no recompila OSQP. `make V=1 ...`
muestra comandos y `make PROFILE=x86 clean` limpia el perfil seleccionado.

Cada ejecución guarda capture.bin, plant.log, console.log, runtime_result.json,
run.json y, cuando se compara, comparison.json. El manifiesto liga la captura
al ELF, fuentes/configuración de planta, entorno Python y runner. Cambiar esas
entradas invalida los resultados anteriores, incluidos los de la planta RK4.
Los errores y timeouts terminan los procesos lanzados y marcan la ejecución FAIL.

La validación comprueba tamaño/ABI, finalización, finitud, estados del solver,
factibilidad de las respuestas y continuidad. No se compara implícitamente
contra la antigua trayectoria RK4. La referencia normal es x86 usando también
la planta Python original y la misma configuración.

Se distinguen tolerancias históricas e igualdad exacta de bits. Para exigir
igualdad exacta de Spike con x86:

```bash
.venv/bin/python scripts/run_closed_loop.py compare \
  --observed build/rv64/acc/64/results/capture.bin \
  --reference build/x86/acc/64/results/capture.bin --steps 64 --exact
```

Las tolerancias no se amplían: estado 1e-4, control 0,05, tiempo/perturbación
1e-12, coste 100, primal 2e-4, dual 1e-3 e iteraciones 25. Los rho adicionales
se comparan por bits; las tolerancias históricas no les asignan un umbral.
Los contadores/heap se excluyen de la comparación matemática y en x86 se
identifican como no disponibles.

Se mantienen -O2 -DNDEBUG, doble precisión, -fno-fast-math, -ffp-contract=off,
EIGEN_DONT_VECTORIZE y QDLDL genérico. OSQP usa DLONG=ON, DFLOAT=OFF,
profiling/impresión/CTRLC/MKL OFF y API dinámica. No hay KKT especializado ni RVV.

## Estado con la planta Python original (11 de noviembre de 2026)

- acc.py y base.py: copias idénticas a las fuentes de R-SHARC.
- x86 y Spike: 64 pasos, igualdad exacta entre ambos, incluidos los rho.
- Las 64 transiciones de planta se han contrastado contra los módulos originales
  importados directamente desde R-SHARC, con igualdad exacta.
- CVA6/Verilator: dos pasos completos con la planta original, terminación
  correcta tras 11.709.144 ciclos y 732.36 s del host; tolerancias históricas
  PASS e igualdad exacta FAIL por las discrepancias de aritmética RTL.
- Los 64 pasos RTL y la ejecución en Héctor están pendientes.

Cambiar RK4 por solve_ivp altera ligeramente la trayectoria respecto a la
implementación anterior: no se presenta como idéntica. El MPC no se modifica.
Las pequeñas discrepancias de FPU ya identificadas en el runner RTL siguen
abiertas; adoptar la planta original no supone corregir su aritmética.

## Cartpole escalar: x86 y Spike

Configuracion: `src/cartpole/config.json`, baseline scalar_tuned800_warm_off_64,
SLSQP, horizontes 20/4, Ts=0.1 s, maxeval=800, warm start desactivado y semilla 0.
Las bibliotecas de `external/` se conservan; Cartpole enlaza NLopt y usa libmpc
sin las extensiones historicas de diagnostico. `main.cpp` mantiene una instancia
MPC y recibe el estado y control previo del host.

```bash
make run-cartpole-x86 CARTPOLE_STEPS=64
make run-cartpole CARTPOLE_STEPS=64 RISCV_PREFIX=/ruta/riscv64-unknown-elf SPIKE=/ruta/spike
```

`run-cartpole` ejecuta x86 y despues Spike y compara los campos numericos
publicados. Informa MATH_EXACT y los errores maximos sin aplicar tolerancias
inventadas ni dar por validada la equivalencia entre arquitecturas. Se excluyen
contadores y uso de memoria. Para exigir igualdad bit a bit, usar el subcomando
`compare --controller cartpole --observed ... --reference ... --steps 64 --exact`
del lanzador. Una diferencia hace fallar esa comprobacion estricta.
Los resultados quedan en `build/{x86,rv64}/cartpole/<pasos>/results/`:
`capture.json`, `run.json`, `plant.log`, `console.log` y, al comparar, `comparison.json`.

La planta usa `src/dynamics/cartpole.py` y `base.py` sin cambiar sus ecuaciones
ni la llamada original a SciPy solve_ivp. Cada intervalo integra con el control
anterior; el nuevo control se aplica al siguiente intervalo, igual que en ACC.
Son 64 intervalos y 64 llamadas MPC para CARTPOLE_STEPS=64. Este calendario es
distinto del experimento archivado con retardos propios (64 intervalos y 9 MPC).
EXECUTION_PASS acredita terminacion y datos validos; `solver_error_steps` e
`infeasible_steps` se informan por separado. No acredita estabilizacion ni
paridad con el MPC original: esa comparacion requiere entradas comunes.
El indicador is_feasible es el que publica libmpc; no sustituye la auditoria
adicional de residuos de las ecuaciones que llevaba el experimento historico.

El ELF para Verilator compila. El puente compartido esta preparado; la
ejecucion real del MPC Cartpole en RTL y en Hector sigue pendiente.

Validacion temporal de esta migracion: 64 pasos x86 y Spike completos, sin
codigos negativos de solver y con is_feasible=true en las 64 respuestas. Al
recompilar el controlador original y su libmpc con diagnosticos, usando el mismo
NLopt, configuracion y transporte, los 64 registros numericos coinciden exactamente
con la version migrada dentro de cada arquitectura. Esta comprobacion no reproduce
el calendario historico de retardos. Entre x86 y Spike hay diferencias desde k=2:
maximo 6.678608127641539e-8 en control, 9.623352836829469e-9 en estado y
2.9089008313221143e-7 en coste; tambien aparecen en la version original recompilada.
La ejecucion ACC x86 de 64 pasos conserva exactamente la captura anterior.


## Cartpole en CVA6/Verilator y Hector

Preparar una vez el runner compartido sobre el build CVA6 compatible del servidor:

```bash
make prepare-cartpole-runner CVA6_TB=/ruta/ariane_tb.cpp CVA6_BUILD=/ruta/build/existente
export CVA6_RUNNER="$PWD/build/cva6-mpc-channel/Variane_testharness"
make run-cartpole-verilator CARTPOLE_STEPS=1 RISCV_PREFIX=/ruta/bin/riscv64-unknown-elf
# Tras revisar la captura y comparison.json del paso real:
make run-cartpole-verilator CARTPOLE_STEPS=64 RISCV_PREFIX=/ruta/bin/riscv64-unknown-elf
```

Cada comando genera primero la referencia x86 de los mismos pasos. El host
integra la planta original y el controlador RV64 resuelve el MPC, con el mismo
calendario de Spike. Resultados: `build/verilator/cartpole/<pasos>/results/`.
El runner anterior exclusivo de ACC debe prepararse de nuevo; los alias
`prepare-acc-runner` y `prepare-cartpole-runner` invocan el mismo `prepare-runner`.
La preparacion recompila el adaptador C++ sobre una copia del modelo ya generado,
sin modificar RTL. No volver a prepararlo en un directorio existente; seleccionar
otro con `CVA6_RUNNER_DIR` si cambian el puente o las cabeceras del protocolo.

El presupuesto Cartpole es `CARTPOLE_MAX_CYCLES=100000000000` y el timeout de pared
`CARTPOLE_HOST_TIMEOUT=1209600` segundos (14 dias); son limites de parada,
no estimaciones de duracion. Los 64 MPC ya consumen unos 6873 millones de
instrucciones en Spike: el limite ACC de 4000 millones de ciclos resulta corto.
La preparacion registra si los contadores del testbench y del modelo generado
permiten limites superiores a 32 bits; si no, el lanzador rechaza ese presupuesto.
Mantiene los limites y comandos propios de ACC.

Copiar fuentes, scripts y external/ al servidor; recrear .venv y recompilar alli.
El runner necesita el build generado, sus fuentes y herramientas compatibles
existentes en ese servidor. No basta copiar solo cartpole.elf: necesita este
lanzador, la planta Python y el runner con el canal preparado. Las rutas se pasan
por variables Make; no se exige tener R-SHARC instalado para ejecutar.

La comprobacion temporal del intercambio de mensajes no sustituye un paso real
MPC en RTL. La validacion completa de 64 pasos y la paridad numerica con x86
siguen pendientes hasta obtener sus capturas. No se añaden tests al repositorio.
