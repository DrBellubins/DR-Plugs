import sys
import math
import numpy as np

from PyQt6.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QPushButton,
    QLineEdit,
    QSlider,
    QSpinBox,
    QDoubleSpinBox,
    QMessageBox,
    QCheckBox
)
from PyQt6.QtCore import Qt

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


def clamp(value, low, high):
    return max(low, min(high, value))


def parse_tunings(text):
    parts = [p.strip() for p in text.split(",") if p.strip()]
    values = [float(p) for p in parts]
    if not values:
        raise ValueError("No tunings provided.")
    return values


def build_quality_distributed_stage_delays(source_tunings, number_of_stages, size01):
    """
    Mirrors the intent of Chronoverb's BuildQualityDistributedStageDelays():
    - lower quality keeps spread across full time span
    - size scales delays with 0.25 + 0.75 * size01
    """
    clamped_stage_count = max(1, int(number_of_stages))
    clamped_size01 = clamp(float(size01), 0.0, 1.0)

    if not source_tunings:
        return []

    source_count = len(source_tunings)
    output_count = min(clamped_stage_count, source_count)

    final_delays = []
    scale = 0.25 + 0.75 * clamped_size01

    if output_count == 1:
        center_index = source_count // 2
        final_delays.append(source_tunings[center_index] * scale)
        return final_delays

    for stage_index in range(output_count):
        normalized_position = stage_index / (output_count - 1)
        source_index_float = normalized_position * (source_count - 1)

        source_index_a = int(math.floor(source_index_float))
        source_index_b = min(source_index_a + 1, source_count - 1)
        fraction = source_index_float - source_index_a

        interpolated_ms = (
            source_tunings[source_index_a] * (1.0 - fraction)
            + source_tunings[source_index_b] * fraction
        )

        final_delays.append(interpolated_ms * scale)

    return final_delays


class DiffusionAllpass:
    """
    Offline approximation of your DiffusionAllpass:
    y[n] = -g*x[n] + x[n-D] + g*y[n-D]

    Integer-sample delay is enough for this visualizer.
    """
    def __init__(self, delay_ms, gain, sample_rate):
        self.sample_rate = sample_rate
        self.gain = clamp(gain, -0.99, 0.99)
        self.delay_ms = max(1.0, float(delay_ms))
        self.delay_samples = max(1, int(round(self.delay_ms * self.sample_rate / 1000.0)))

        self.x_buffer = np.zeros(self.delay_samples + 2, dtype=np.float64)
        self.y_buffer = np.zeros(self.delay_samples + 2, dtype=np.float64)
        self.write_index = 0

    def process_sample(self, x):
        size = len(self.x_buffer)
        read_index = (self.write_index - self.delay_samples) % size

        x_delayed = self.x_buffer[read_index]
        y_delayed = self.y_buffer[read_index]

        y = -self.gain * x + x_delayed + self.gain * y_delayed

        self.x_buffer[self.write_index] = x
        self.y_buffer[self.write_index] = y
        self.write_index = (self.write_index + 1) % size

        return y


def process_allpass_chain(signal, stage_delays_ms, gain, sample_rate):
    if len(stage_delays_ms) == 0:
        return signal.copy()

    y = signal.copy()
    for delay_ms in stage_delays_ms:
        ap = DiffusionAllpass(delay_ms, gain, sample_rate)
        out = np.zeros_like(y)
        for i, sample in enumerate(y):
            out[i] = ap.process_sample(sample)
        y = out
    return y


def dbfs_safe(x, floor_db=-120.0):
    mag = np.maximum(np.abs(x), 10 ** (floor_db / 20.0))
    return 20.0 * np.log10(mag)


def make_impulse(total_samples, index=0, amplitude=1.0):
    x = np.zeros(total_samples, dtype=np.float64)
    if 0 <= index < total_samples:
        x[index] = amplitude
    return x


def render_diffusion_preview(
    sample_rate=48000,
    total_ms=1600.0,
    nominal_delay_ms=1000.0,
    source_tunings=None,
    quality=8,
    size01=1.0,
    allpass_gain=0.7,
    diffusion_amount=0.5,
    centered_swell_ratio=0.25,
    diffusion_comp_bias=1.5,
    include_early_branch=True,
):
    """
    Produces:
    - raw delay impulse response
    - diffused approximation

    This is designed to visually resemble the Chronoverb diffusion shape,
    not to exactly null against the plugin.
    """
    if source_tunings is None:
        source_tunings = [10.0, 15.0, 22.5, 33.75, 50.6, 75.9, 113.9, 170.8]

    total_samples = int(total_ms * sample_rate / 1000.0)
    nominal_delay_samples = int(round(nominal_delay_ms * sample_rate / 1000.0))

    raw = np.zeros(total_samples, dtype=np.float64)
    if nominal_delay_samples < total_samples:
        raw[nominal_delay_samples] = 1.0

    stage_delays_ms = build_quality_distributed_stage_delays(
        source_tunings, quality, size01
    )

    total_delay_diffusion_ms = sum(stage_delays_ms)
    static_comp_ms = total_delay_diffusion_ms * centered_swell_ratio * diffusion_comp_bias
    early_read_ms = max(1.0, nominal_delay_ms - static_comp_ms)

    # Pre-write diffusion:
    # An impulse at t=0 goes through the diffusion chain first, then lands near the nominal delay.
    impulse = make_impulse(total_samples, index=0, amplitude=1.0)
    diffused_input = process_allpass_chain(impulse, stage_delays_ms, allpass_gain, sample_rate)

    # Shift that diffused shape to nominal delay time.
    prewrite_branch = np.zeros(total_samples, dtype=np.float64)
    for i, sample in enumerate(diffused_input):
        target = i + nominal_delay_samples
        if target < total_samples:
            prewrite_branch[target] += sample

    # Early-read branch approximation:
    # another diffused version centered earlier to create pre-swell around the main tap
    early_branch = np.zeros(total_samples, dtype=np.float64)
    if include_early_branch:
        early_delay_samples = int(round(early_read_ms * sample_rate / 1000.0))
        early_impulse = np.zeros(total_samples, dtype=np.float64)
        if early_delay_samples < total_samples:
            early_impulse[early_delay_samples] = 1.0

        early_branch = process_allpass_chain(
            early_impulse, stage_delays_ms, allpass_gain, sample_rate
        )

    # Approximate Chronoverb-style amount shaping.
    amt = clamp(diffusion_amount, 0.0, 1.0)
    diffusion_drive = clamp(amt * 2.0, 0.0, 1.0)
    clean_tap_gain = (1.0 - diffusion_drive) ** 4
    diffused_tap_gain = math.sin(diffusion_drive * (math.pi * 0.5))

    if amt <= 0.5:
        # lower half: emphasize delay diffusion behavior
        diffused_component = prewrite_branch * 0.55 + early_branch * 0.45
    else:
        # upper half: denser blend
        reverb_blend = (amt - 0.5) * 2.0
        dense_branch = process_allpass_chain(
            prewrite_branch, stage_delays_ms, min(0.95, allpass_gain + 0.05), sample_rate
        )
        delay_gain = math.cos(reverb_blend * (math.pi * 0.5))
        reverb_gain = math.sin(reverb_blend * (math.pi * 0.5))
        diffused_component = (prewrite_branch * delay_gain + dense_branch * reverb_gain) * 0.6 + early_branch * 0.4

    diffused = raw * clean_tap_gain + diffused_component * diffused_tap_gain

    # Normalize only for display so the plot remains readable.
    peak = max(np.max(np.abs(raw)), np.max(np.abs(diffused)), 1e-9)
    raw_display = raw / peak
    diffused_display = diffused / peak

    t_ms = np.arange(total_samples) * 1000.0 / sample_rate

    debug = {
        "stage_delays_ms": stage_delays_ms,
        "total_delay_diffusion_ms": total_delay_diffusion_ms,
        "static_comp_ms": static_comp_ms,
        "early_read_ms": early_read_ms,
        "nominal_delay_ms": nominal_delay_ms,
    }

    return t_ms, raw_display, diffused_display, debug


class MplCanvas(FigureCanvas):
    def __init__(self):
        self.figure = Figure(figsize=(10, 6), tight_layout=True)
        self.ax_wave = self.figure.add_subplot(211)
        self.ax_db = self.figure.add_subplot(212)
        super().__init__(self.figure)


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Chronoverb Diffusion Visualizer")
        self.resize(1200, 800)

        self.canvas = MplCanvas()

        self.tunings_edit = QLineEdit("10.0, 15.0, 22.5, 33.75, 50.6, 75.9, 113.9, 170.8")

        self.sample_rate_spin = QSpinBox()
        self.sample_rate_spin.setRange(8000, 384000)
        self.sample_rate_spin.setValue(48000)

        self.total_ms_spin = QDoubleSpinBox()
        self.total_ms_spin.setRange(200.0, 5000.0)
        self.total_ms_spin.setValue(1600.0)
        self.total_ms_spin.setSuffix(" ms")

        self.nominal_delay_spin = QDoubleSpinBox()
        self.nominal_delay_spin.setRange(1.0, 2000.0)
        self.nominal_delay_spin.setValue(1000.0)
        self.nominal_delay_spin.setSuffix(" ms")

        self.quality_spin = QSpinBox()
        self.quality_spin.setRange(1, 8)
        self.quality_spin.setValue(8)

        self.size_spin = QDoubleSpinBox()
        self.size_spin.setRange(0.0, 1.0)
        self.size_spin.setSingleStep(0.01)
        self.size_spin.setValue(1.0)

        self.gain_spin = QDoubleSpinBox()
        self.gain_spin.setRange(0.0, 0.99)
        self.gain_spin.setSingleStep(0.01)
        self.gain_spin.setValue(0.7)

        self.amount_spin = QDoubleSpinBox()
        self.amount_spin.setRange(0.0, 1.0)
        self.amount_spin.setSingleStep(0.01)
        self.amount_spin.setValue(0.5)

        self.centered_swell_spin = QDoubleSpinBox()
        self.centered_swell_spin.setRange(0.0, 2.0)
        self.centered_swell_spin.setSingleStep(0.01)
        self.centered_swell_spin.setValue(0.25)

        self.comp_bias_spin = QDoubleSpinBox()
        self.comp_bias_spin.setRange(0.0, 5.0)
        self.comp_bias_spin.setSingleStep(0.01)
        self.comp_bias_spin.setValue(1.5)

        self.early_branch_check = QCheckBox("Include early diffused branch")
        self.early_branch_check.setChecked(True)

        self.render_button = QPushButton("Render")
        self.render_button.clicked.connect(self.render)

        self.debug_label = QLabel()
        self.debug_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)

        controls = QGridLayout()
        row = 0

        controls.addWidget(QLabel("Delay tunings (ms, comma-separated):"), row, 0)
        controls.addWidget(self.tunings_edit, row, 1, 1, 3)
        row += 1

        controls.addWidget(QLabel("Sample rate:"), row, 0)
        controls.addWidget(self.sample_rate_spin, row, 1)
        controls.addWidget(QLabel("Render length:"), row, 2)
        controls.addWidget(self.total_ms_spin, row, 3)
        row += 1

        controls.addWidget(QLabel("Nominal delay:"), row, 0)
        controls.addWidget(self.nominal_delay_spin, row, 1)
        controls.addWidget(QLabel("Quality stages:"), row, 2)
        controls.addWidget(self.quality_spin, row, 3)
        row += 1

        controls.addWidget(QLabel("Diffusion size:"), row, 0)
        controls.addWidget(self.size_spin, row, 1)
        controls.addWidget(QLabel("Allpass gain:"), row, 2)
        controls.addWidget(self.gain_spin, row, 3)
        row += 1

        controls.addWidget(QLabel("Diffusion amount:"), row, 0)
        controls.addWidget(self.amount_spin, row, 1)
        controls.addWidget(QLabel("Centered swell ratio:"), row, 2)
        controls.addWidget(self.centered_swell_spin, row, 3)
        row += 1

        controls.addWidget(QLabel("Compensation bias:"), row, 0)
        controls.addWidget(self.comp_bias_spin, row, 1)
        controls.addWidget(self.early_branch_check, row, 2, 1, 2)
        row += 1

        controls.addWidget(self.render_button, row, 0, 1, 4)
        row += 1

        layout = QVBoxLayout()
        layout.addLayout(controls)
        layout.addWidget(self.canvas)
        layout.addWidget(self.debug_label)
        self.setLayout(layout)

        self.render()

    def render(self):
        try:
            tunings = parse_tunings(self.tunings_edit.text())

            t_ms, raw, diffused, debug = render_diffusion_preview(
                sample_rate=self.sample_rate_spin.value(),
                total_ms=self.total_ms_spin.value(),
                nominal_delay_ms=self.nominal_delay_spin.value(),
                source_tunings=tunings,
                quality=self.quality_spin.value(),
                size01=self.size_spin.value(),
                allpass_gain=self.gain_spin.value(),
                diffusion_amount=self.amount_spin.value(),
                centered_swell_ratio=self.centered_swell_spin.value(),
                diffusion_comp_bias=self.comp_bias_spin.value(),
                include_early_branch=self.early_branch_check.isChecked(),
            )

            ax1 = self.canvas.ax_wave
            ax2 = self.canvas.ax_db
            ax1.clear()
            ax2.clear()

            ax1.plot(t_ms, raw, color="blue", linewidth=1.5, label="Raw delay")
            ax1.plot(t_ms, diffused, color="red", linewidth=1.2, label="Diffused")
            ax1.set_title("Waveform Overlay")
            ax1.set_xlabel("Time (ms)")
            ax1.set_ylabel("Amplitude")
            ax1.grid(True, alpha=0.25)
            ax1.legend(loc="upper right")
            ax1.set_xlim(0.0, self.total_ms_spin.value())

            ax2.plot(t_ms, dbfs_safe(raw), color="blue", linewidth=1.5, label="Raw delay")
            ax2.plot(t_ms, dbfs_safe(diffused), color="red", linewidth=1.2, label="Diffused")
            ax2.set_title("Magnitude Overlay (dB)")
            ax2.set_xlabel("Time (ms)")
            ax2.set_ylabel("dB")
            ax2.grid(True, alpha=0.25)
            ax2.legend(loc="upper right")
            ax2.set_xlim(0.0, self.total_ms_spin.value())
            ax2.set_ylim(-120, 6)

            self.canvas.draw()

            self.debug_label.setText(
                "Effective stage delays (ms): "
                + ", ".join(f"{x:.2f}" for x in debug["stage_delays_ms"])
                + "\n"
                + f"Total delay diffusion ms: {debug['total_delay_diffusion_ms']:.2f}\n"
                + f"Static compensation ms: {debug['static_comp_ms']:.2f}\n"
                + f"Early read ms: {debug['early_read_ms']:.2f}\n"
                + f"Nominal delay ms: {debug['nominal_delay_ms']:.2f}"
            )

        except Exception as exc:
            QMessageBox.critical(self, "Render Error", str(exc))


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()