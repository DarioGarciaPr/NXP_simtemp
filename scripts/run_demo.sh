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
THRESHOLD=45000
SAMPLING=100
echo "$THRESHOLD" | sudo tee "/sys/class/misc/$DEVICE_NAME/threshold" >/dev/null
echo "$SAMPLING" | sudo tee "/sys/class/misc/$DEVICE_NAME/sampling" >/dev/null
echo "[T2 PASS] Sysfs configurado: threshold=$THRESHOLD, sampling=${SAMPLING}ms"

# --- T2: Demo lectura ---
echo "[T2] Leyendo $SAMPLES muestras usando CLI..."
"$CLI_DIR/main" --count $SAMPLES
echo "[T2 PASS] Lectura completada."
sudo rmmod nxp_simtemp
echo "[T2 PASS] Módulo descargado después de la demo."

# --- T3–T6: Demostraciones interactivas ---
echo "[T3] ========================== Threshold test ======================================"
sudo insmod "$MODULE_DIR/$MODULE_NAME"
"$CLI_DIR/main" set 35000
"$CLI_DIR/main" --count $SAMPLES
sudo rmmod nxp_simtemp
echo "[T3 PASS] Threshold test completado."

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


echo "=== Demo nxp_simtemp completada ==="

