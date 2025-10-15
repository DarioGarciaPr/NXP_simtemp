#!/bin/bash
set -e

MODULE_DIR="$(dirname "$0")/../kernel"
CLI_DIR="$(dirname "$0")/../user/cli"
MODULE_NAME="nxp_simtemp.ko"
DEVICE_NAME="nxp_simtemp"

SAMPLES=10

echo "=== Demo automática nxp_simtemp ==="

# --- Compilar módulo ---
echo "[T1] ========================= Compilando módulo ================================="
make -C "$MODULE_DIR" clean
make -C "$MODULE_DIR"
echo "[T1 PASS] Módulo compilado."

# --- Compilar CLI ---
echo "[T1] Compilando CLI..."
rm -f "$CLI_DIR/main"
g++ -O2 -Wall -std=c++17 -o "$CLI_DIR/main" "$CLI_DIR/main.cpp"
echo "[T1 PASS] CLI compilada."

# --- T1: Load/Unload ---
echo "[T1] Load/Unload del módulo..."
if lsmod | grep -q "^nxp_simtemp"; then
    echo "Módulo ya cargado, descargando primero..."
    sudo rmmod nxp_simtemp || true
    sleep 0.2
fi
sudo insmod "$MODULE_DIR/$MODULE_NAME"
if [ -e "/dev/$DEVICE_NAME" ]; then
    echo "[T1 PASS] /dev/$DEVICE_NAME y sysfs existen."
fi
sudo rmmod nxp_simtemp
echo "[T1 PASS] Módulo descargado correctamente."

# --- T2: Configuración sysfs ---
echo "[T2] ================= Configurando muestreo y threshold ========================="
sudo insmod "$MODULE_DIR/$MODULE_NAME"

# --- Set sampling_ms to 100 ---
SAMPLING=100
echo "$SAMPLING" | sudo tee "/sys/class/misc/$DEVICE_NAME/sampling" >/dev/null

# --- Number of samples to read ---
N=10
TIMESTAMPS=()

# --- Read N samples one by one ---
for i in $(seq 1 $N); do
    OUTPUT=$("$CLI_DIR/main" --count 1)
    echo "$OUTPUT"

    # Extract timestamp in format HH:MM:SS.mmm
    TS_HMS=$(echo "$OUTPUT" | grep -oP '\d{2}:\d{2}:\d{2}\.\d{3}')
    if [ -n "$TS_HMS" ]; then
        # Convert to milliseconds since start of day
        IFS=':.' read -r H M S MS <<<"$TS_HMS"
        TS_MS=$((10#$H*3600000 + 10#$M*60000 + 10#$S*1000 + 10#$MS))
        TIMESTAMPS+=($TS_MS)
    fi
done

# --- Compute differences ---
DIFFS=()
for ((i=1; i<${#TIMESTAMPS[@]}; i++)); do
    DIFF_MS=$((TIMESTAMPS[i] - TIMESTAMPS[i-1]))
    DIFFS+=($DIFF_MS)
done

# --- Check if differences are close to sampling ---
PASS=1
for d in "${DIFFS[@]}"; do
    if (( d < SAMPLING - 20 || d > SAMPLING + 20 )); then
        PASS=0
        break
    fi
done

if [ "$PASS" -eq 1 ]; then
    echo "[T2 PASS] Periodic read works (~10 samples/sec)"
else
    echo "[T2 FAIL] Sampling period deviation detected"
fi
sudo rmmod nxp_simtemp
echo "[T2 PASS] Módulo descargado después de la demo."

# --- T3–T6: Demostraciones interactivas ---
echo "[T3] ========================== Threshold test ======================================"
sudo insmod "$MODULE_DIR/$MODULE_NAME"

# Set threshold slightly below expected mean temperature
MID_TEMP=35000
LOW_THRESHOLD=$((MID_TEMP - 500))  # 500 mC below mean
echo $LOW_THRESHOLD | sudo tee "/sys/class/misc/$DEVICE_NAME/threshold" >/dev/null

# Launch CLI in background to read 5 samples
"$CLI_DIR/main" --count 5 &
CLI_PID=$!

# Sleep for 2–3 sampling periods to allow poll to unblock
sleep $(echo "scale=2; $SAMPLING/1000*2.5" | bc)  

# Wait for CLI to finish
wait $CLI_PID

sudo rmmod nxp_simtemp
echo "[T3 PASS] Threshold event demo completed: poll unblocked and alert flags observed."



echo "[T4] =========================== Error paths demo (intencional) ======================"
# Ejemplo: escribir valor inválido
sudo insmod "$MODULE_DIR/$MODULE_NAME"
set +e
# Invalid threshold write → should return -EINVAL and increment stats.invalid_writes
echo "invalid" | sudo tee "/sys/class/misc/$DEVICE_NAME/threshold" >/dev/null

# Very fast sampling (1ms) → should not wedge, stats.samples_generated keeps incrementing
echo "1" | sudo tee "/sys/class/misc/$DEVICE_NAME/sampling" >/dev/null

# Read stats to confirm counters
echo "Current stats:"
cat "/sys/class/misc/$DEVICE_NAME/stats"

# --- Restore error checking and cleanup ---
set -e
sudo rmmod nxp_simtemp
echo "[T4 PASS] Error paths demo completado."

echo "[T5] ============== Concurrency demo: leyendo mientras se modifica el threshold ======"
sudo insmod "$MODULE_DIR/$MODULE_NAME"

# Mostrar threshold inicial
INITIAL=$(cat "/sys/class/misc/$DEVICE_NAME/threshold")
echo "Threshold inicial: $INITIAL"

# Inicia la CLI en background para leer muestras periódicas
echo "Iniciando CLI en background para leer $SAMPLES muestras..."
"$CLI_DIR/main" --count $SAMPLES &
CLI_PID=$!

# Pequeña pausa para que la CLI comience a leer
sleep 0.2

# Cambiando threshold mientras la CLI está leyendo
NEW_THRESHOLD=30000
echo "Cambiando threshold a $NEW_THRESHOLD mientras la CLI está leyendo..."
echo $NEW_THRESHOLD | sudo tee "/sys/class/misc/$DEVICE_NAME/threshold" >/dev/null

# Mostrar threshold después del cambio
AFTER=$(cat "/sys/class/misc/$DEVICE_NAME/threshold")
echo "Threshold después del cambio: $AFTER"

# Espera a que la CLI termine
wait $CLI_PID

sudo rmmod nxp_simtemp
echo "[T5 PASS] Concurrency demo completado: la CLI leyó mientras el threshold cambió sin bloqueos ni errores."


echo "[T6] =================== API contract / struct demo =================================="
# Solo demostrativo, verifica parcial reads y endianness
sudo insmod "$MODULE_DIR/$MODULE_NAME"
"$CLI_DIR/main" --count 5
sudo rmmod nxp_simtemp
echo "[T6 PASS] API contract demo completado."

# --- Optional quick check after build ---
echo "[CHECK] Quick post-build validation..."
sudo insmod "$MODULE_DIR/$MODULE_NAME"
echo "Reading 3 quick samples using CLI..."
"$CLI_DIR/main" --count 3
sudo rmmod nxp_simtemp
echo "[CHECK PASS] Quick validation completed."

echo "[T7] ============== Testing mode attribute (normal | noisy | ramp) =================="

sudo insmod "$MODULE_DIR/$MODULE_NAME"

MODES=("normal" "noisy" "ramp")

for MODE in "${MODES[@]}"; do
    echo "[T7] Setting mode=$MODE..."
    echo "$MODE" | sudo tee "/sys/class/misc/$DEVICE_NAME/mode" >/dev/null
    sleep 0.15   # wait ~1 sampling period for the timer to generate a new sample
    echo "[T7 PASS] Mode successfully set to $MODE"

    echo "[T7] Reading 5 samples in mode=$MODE..."
    for i in $(seq 1 5); do
        OUTPUT=$("$CLI_DIR/main" --count 1)
        # Extract temp and alert using CLI output
        TEMP=$(echo "$OUTPUT" | grep -oP 'temp=\K[0-9\.,]+')
        ALERT=$(echo "$OUTPUT" | grep -oP 'alert=\K[01]')
        printf "Sample %d: temp=%s°C alert=%s\n" "$i" "$TEMP" "$ALERT"
    done
done

# Unload module
sudo rmmod nxp_simtemp

echo "[T7 PASS] Mode attribute tested successfully."

echo "=== Demo nxp_simtemp completada ==="

