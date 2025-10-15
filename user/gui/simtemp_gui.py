import os
import tkinter as tk
from tkinter import ttk
import random

# Clear console
os.system('clear')

DEVICE_PATH = "/dev/nxp_simtemp"
SYSFS_THRESHOLD = f"/sys/class/misc/nxp_simtemp/threshold"

class SimTempGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("NXP SimTemp Monitor")
        self.root.geometry("550x550")
        self.root.configure(bg="#f3f4f6")

        # GUI variables
        self.temperature_var = tk.StringVar(value="-- °C")
        self.status_var = tk.StringVar(value="Disconnected")
        self.threshold_var = tk.StringVar(value="Low threshold: 20°C")
        self.source_var = tk.StringVar(value="Source: Simulation")
        self.threshold_origin_var = tk.StringVar(value="Threshold origin: Default")
        self.low_threshold = 20.0  # Initial low threshold
        self.alert_count = 0
        self.alert_var = tk.StringVar(value=f"Low threshold alerts: {self.alert_count}")

        # Title
        ttk.Label(root, text="NXP SimTemp Monitor", font=("Helvetica", 16, "bold")).pack(pady=10)

        # Temperature display
        self.temp_label = ttk.Label(root, textvariable=self.temperature_var, font=("Helvetica", 32))
        self.temp_label.pack(pady=10)

        # Status
        ttk.Label(root, textvariable=self.status_var, font=("Helvetica", 12)).pack(pady=5)

        # Threshold control frame
        frame = ttk.LabelFrame(root, text="Low Threshold Control")
        frame.pack(pady=10)

        # Threshold display
        ttk.Label(frame, textvariable=self.threshold_var, font=("Helvetica", 11)).pack(padx=10, pady=5)
        ttk.Label(frame, textvariable=self.threshold_origin_var, font=("Helvetica", 10, "italic")).pack(padx=10, pady=2)

        # Entry field for low threshold
        self.entry_var = tk.StringVar(value=str(self.low_threshold))
        entry = ttk.Entry(frame, textvariable=self.entry_var, width=10)
        entry.pack(padx=10, pady=5)

        # Buttons
        ttk.Button(frame, text="Update Low Threshold (GUI)", command=self.update_low_threshold).pack(pady=5)
        ttk.Button(frame, text="Read Low Threshold (SysFS)", command=self.read_threshold_sysfs).pack(pady=5)
        ttk.Button(frame, text="Write Low Threshold (SysFS)", command=self.write_threshold_sysfs).pack(pady=5)

        # Alert counter
        self.alert_label = ttk.Label(root, textvariable=self.alert_var, font=("Helvetica", 12, "bold"))
        self.alert_label.pack(pady=5)

        # Source display
        ttk.Label(root, textvariable=self.source_var, font=("Helvetica", 10, "italic")).pack(pady=5)

        # Detect source
        self.detect_source()

        # Periodic temperature update
        self.update_temperature()

    # Detect if device exists
    def detect_source(self):
        if os.path.exists(DEVICE_PATH):
            self.source_var.set("Source: Kernel (/dev/nxp_simtemp)")
        else:
            self.source_var.set("Source: Simulation (device not found)")

    # Read temperature from device or simulate
    def read_temperature(self):
        if os.path.exists(DEVICE_PATH):
            try:
                fd = os.open(DEVICE_PATH, os.O_RDONLY | os.O_NONBLOCK)
                data = os.read(fd, 1024).decode().strip()
                os.close(fd)
                if data:
                    for part in data.split():
                        if part.startswith("temp="):
                            return float(part.split("=")[1][:-1])
                return None
            except Exception:
                return None
        else:
            return random.uniform(20.0, 50.0)

    # Periodic temperature update
    def update_temperature(self):
        temp = self.read_temperature()
        if temp is None:
            self.status_var.set("Error reading temperature")
            self.temp_label.configure(foreground="gray")
        else:
            self.temperature_var.set(f"{temp:.1f} °C")

            # Check low threshold
            if temp < self.low_threshold:
                self.status_var.set("⚠️ Low temperature alert")
                self.temp_label.configure(foreground="blue")
                self.alert_count += 1
                self.alert_var.set(f"Low threshold alerts: {self.alert_count}")
                self.alert_label.configure(foreground="red")  # highlight alert
            else:
                self.status_var.set("Temperature normal")
                self.temp_label.configure(foreground="green")
                self.alert_label.configure(foreground="black")

        # Repeat every second
        self.root.after(1000, self.update_temperature)

    # Update low threshold in GUI
    def update_low_threshold(self):
        try:
            val = float(self.entry_var.get())
            self.low_threshold = val
            self.threshold_var.set(f"Low threshold: {self.low_threshold:.1f} °C")
            self.threshold_origin_var.set("Threshold origin: GUI")
        except ValueError:
            self.threshold_var.set("Invalid value")

    # Read low threshold from sysfs
    def read_threshold_sysfs(self):
        if os.path.exists(SYSFS_THRESHOLD):
            try:
                with open(SYSFS_THRESHOLD, "r") as f:
                    val = int(f.read().strip())
                    self.low_threshold = val / 1000.0
                    self.entry_var.set(str(self.low_threshold))
                    self.threshold_var.set(f"Low threshold: {self.low_threshold:.1f} °C")
                    self.threshold_origin_var.set("Threshold origin: SysFS")
            except Exception:
                self.threshold_var.set("Error reading sysfs")
        else:
            self.threshold_var.set("Sysfs not available")

    # Write low threshold to sysfs
    def write_threshold_sysfs(self):
        if os.path.exists(SYSFS_THRESHOLD):
            try:
                val = float(self.entry_var.get())
                with open(SYSFS_THRESHOLD, "w") as f:
                    f.write(str(int(val * 1000)))
                self.low_threshold = val
                self.threshold_var.set(f"Low threshold: {self.low_threshold:.1f} °C")
                self.threshold_origin_var.set("Threshold origin: SysFS")
            except Exception:
                self.threshold_var.set("Error writing sysfs")
        else:
            self.threshold_var.set("Sysfs not available")


if __name__ == "__main__":
    root = tk.Tk()
    app = SimTempGUI(root)
    root.mainloop()
