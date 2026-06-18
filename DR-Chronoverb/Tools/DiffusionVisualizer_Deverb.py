import sys
import math
import numpy as np
import matplotlib.ticker as ticker

from PyQt6.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QPushButton,
    QLineEdit,
    QSpinBox,
    QDoubleSpinBox,
    QMessageBox,
    QComboBox,
)
from PyQt6.QtCore import Qt

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


def clamp(value, low, high):
    return max(low, min(high, value))


def parse_csv_floats(text):
    parts = [p.strip() for p in text.split(",") if p.strip()]
    values = [float(p) for p in parts]
    if not values:
        raise ValueError("No values provided.")
    return values


def build_quality_distributed_stage_delays(source_tunings, number_of_stages, size01):
    """
    Mirrors the intent of Chronoverb's BuildQualityDistributedStageDelays():
    - lower quality still spans the full tuning range
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


def build_quality_distributed_values(source_values, number_of_stages):
    """
    Distributes/interpolates arbitrary staged values across the active stage count
    in the same style as the tuning distribution logic, but without size scaling.
    Used here for per-stage gain multipliers.
    """
    clamped_stage_count = max(1, int(number_of_stages))

    if not source_values:
        return []

    source_count = len(source_values)
    output_count = min(clamped_stage_count, source_count)

    final_values = []

    if output_count == 1:
        center_index = source_count // 2
        final_values.append(float(source_values[center_index]))
        return final_values

    for stage_index in range(output_count):
        normalized_position = stage_index / (output_count - 1)
        source_index_float = normalized_position * (source_count - 1)

        source_index_a = int(math.floor(source_index_float))
        source_index_b = min(source_index_a + 1, source_count - 1)
        fraction = source_index_float - source_index_a

        interpolated_value = (
            float(source_values[source_index_a]) * (1.0 - fraction)
            + float(source_values[source_index_b]) * fraction
        )

        final_values.append(interpolated_value)

    return final_values


class DiffusionAllpass:
    """
    Offline approximation of Chronoverb's current single-buffer Schroeder-style allpass:

        delayed = interp(buffer, read_pos)
        v = x + g * delayed
        y = delayed - g * v
        buffer[write] = v

    Includes a small delay smoothing step to better resemble the C++ implementation.
    """
    def __init__(self, delay_ms, gain, sample_rate):
        self.sample_rate = float(sample_rate)
        self.gain = clamp(float(gain), -0.99, 0.99)
        self.delay_ms = max(1.0, float(delay_ms))
        self.delay_samples = max(1.0, self.delay_ms * self.sample_rate / 1000.0)
        self.smoothed_delay_samples = self.delay_samples

        buffer_size = max(4, int(math.ceil(self.delay_samples)) + 2)
        self.buffer = np.zeros(buffer_size, dtype=np.float64)
        self.write_index = 0

    def process_sample(self, x):
        size = len(self.buffer)

        self.smoothed_delay_samples += 0.0025 * (
            self.delay_samples - self.smoothed_delay_samples
        )

        read_pos = self.write_index - self.smoothed_delay_samples

        while read_pos < 0.0:
            read_pos += size
        while read_pos >= size:
            read_pos -= size

        index_a = int(math.floor(read_pos))
        index_b = (index_a + 1) % size
        frac = read_pos - index_a

        delayed = (
            self.buffer[index_a] * (1.0 - frac)
            + self.buffer[index_b] * frac
        )

        v = x + self.gain * delayed
        y = delayed - self.gain * v

        self.buffer[self.write_index] = v
        self.write_index = (self.write_index + 1) % size

        return y


def process_allpass_chain_per_stage(signal, stage_delays_ms, stage_gains, sample_rate):
    if len(stage_delays_ms) == 0:
        return signal.copy()

    if len(stage_delays_ms) != len(stage_gains):
        raise ValueError("Stage delays and stage gains must have the same length.")

    y = signal.copy()

    for delay_ms, gain in zip(stage_delays_ms, stage_gains):
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


def shift_signal(signal, delay_samples):
    out = np.zeros_like(signal)
    delay_samples = int(delay_samples)

    if delay_samples < 0:
        raise ValueError("delay_samples must be non-negative.")

    for i, sample in enumerate(signal):
        target = i + delay_samples
        if target < len(signal):
            out[target] += sample

    return out


def render_deverb_preview(
    sample_rate=48000,
    total_ms=1600.0,
    nominal_delay_ms=1000.0,
    source_tunings=None,
    source_stage_gain_multipliers=None,
    quality=8,
    size01=1.0,
    diffusion_amount=0.5,
    feedback_gain=0.5,
    feedback_passes=6,
    diffusion_comp_bias=0.25,
):
    """
    Approximate current Deverb topology:

        input + feedback
        -> diffusion chain
        -> clean/diffused write blend
        -> delay line write
        -> read at user delay time only
        -> feedback recirculation

    This intentionally does NOT use the older early-read hybrid branch.
    """
    if source_tunings is None:
        source_tunings = [11.0, 13.0, 23.0, 31.0, 43.0, 53.0, 73.0, 83.0]

    if source_stage_gain_multipliers is None:
        source_stage_gain_multipliers = [1.00, 0.97, 1.02, 0.95, 1.01, 0.96, 0.99, 0.94]

    total_samples = int(total_ms * sample_rate / 1000.0)
    nominal_delay_samples = int(round(nominal_delay_ms * sample_rate / 1000.0))

    raw = np.zeros(total_samples, dtype=np.float64)
    if nominal_delay_samples < total_samples:
        raw[nominal_delay_samples] = 1.0

    stage_delays_ms = build_quality_distributed_stage_delays(
        source_tunings, quality, size01
    )

    stage_gain_multipliers = build_quality_distributed_values(
        source_stage_gain_multipliers, quality
    )

    amt = clamp(diffusion_amount, 0.0, 1.0)
    blend_amount = math.sin(amt * (math.pi * 0.5))

    # Mirrors DeverbDiffusionChain's gain drive idea:
    # gainDrive = min(1.0, diffusionAmount * 2.0)
    # baseGain = gainDrive * MaxAllpassGain
    base_gain = min(1.0, amt * 2.0) * 0.58

    resolved_stage_gains = [
        clamp(base_gain * multiplier, -0.99, 0.99)
        for multiplier in stage_gain_multipliers
    ]

    total_chain_delay_ms = sum(stage_delays_ms)
    static_comp_ms = total_chain_delay_ms * diffusion_comp_bias
    effective_read_ms = max(1.0, nominal_delay_ms - static_comp_ms)
    effective_read_samples = int(round(effective_read_ms * sample_rate / 1000.0))

    accumulated = np.zeros(total_samples, dtype=np.float64)

    # first pass is the dry input impulse at t=0
    current_input = make_impulse(total_samples, index=0, amplitude=1.0)

    for pass_index in range(feedback_passes):
        diffused = process_allpass_chain_per_stage(
            current_input,
            stage_delays_ms,
            resolved_stage_gains,
            sample_rate,
        )

        write_signal = (
            current_input * (1.0 - blend_amount)
            + diffused * blend_amount
        )

        delayed_output = shift_signal(write_signal, effective_read_samples)

        pass_gain = feedback_gain ** pass_index
        accumulated += delayed_output * pass_gain

        current_input = delayed_output * feedback_gain

    peak = max(np.max(np.abs(raw)), np.max(np.abs(accumulated)), 1e-9)
    raw_display = raw / peak
    processed_display = accumulated / peak
    t_ms = np.arange(total_samples) * 1000.0 / sample_rate

    debug = {
        "stage_delays_ms": stage_delays_ms,
        "stage_gain_multipliers": stage_gain_multipliers,
        "resolved_stage_gains": resolved_stage_gains,
        "total_chain_delay_ms": total_chain_delay_ms,
        "static_comp_ms": static_comp_ms,
        "effective_read_ms": effective_read_ms,
        "nominal_delay_ms": nominal_delay_ms,
        "blend_amount": blend_amount,
        "base_gain": base_gain,
        "feedback_passes": feedback_passes,
        "feedback_gain": feedback_gain,
    }

    return t_ms, raw_display, processed_display, debug


def render_reverb_preview(
    sample_rate=48000,
    total_ms=1600.0,
    source_tunings=None,
    source_stage_gain_multipliers=None,
    quality=8,
    size01=1.0,
    diffusion_amount=1.0,
    feedback_gain=0.5,
    feedback_passes=6,
):
    """
    Approximate Reverb.cpp's broad behavior:
    input+feedback -> diffusion chain -> recirculate

    Still simplified, but uses distributed per-stage gains for consistency.
    """
    if source_tunings is None:
        source_tunings = [29.0, 37.0, 43.0, 53.0, 71.0, 89.0, 113.0, 149.0]

    if source_stage_gain_multipliers is None:
        source_stage_gain_multipliers = [1.0] * 8

    total_samples = int(total_ms * sample_rate / 1000.0)

    stage_delays_ms = build_quality_distributed_stage_delays(
        source_tunings, quality, size01
    )

    stage_gain_multipliers = build_quality_distributed_values(
        source_stage_gain_multipliers, quality
    )

    amt = clamp(diffusion_amount, 0.0, 1.0)
    base_gain = min(0.99, 0.7 * max(0.0, amt))
    resolved_stage_gains = [
        clamp(base_gain * multiplier, -0.99, 0.99)
        for multiplier in stage_gain_multipliers
    ]

    raw = make_impulse(total_samples, index=0, amplitude=1.0)

    accumulated = np.zeros(total_samples, dtype=np.float64)
    current_pass = make_impulse(total_samples, index=0, amplitude=1.0)

    for pass_index in range(feedback_passes):
        diffused_pass = process_allpass_chain_per_stage(
            current_pass,
            stage_delays_ms,
            resolved_stage_gains,
            sample_rate,
        )

        gain = feedback_gain ** pass_index
        accumulated += diffused_pass * gain
        current_pass = diffused_pass * feedback_gain

    peak = max(np.max(np.abs(raw)), np.max(np.abs(accumulated)), 1e-9)
    raw_display = raw / peak
    processed_display = accumulated / peak

    t_ms = np.arange(total_samples) * 1000.0 / sample_rate

    debug = {
        "stage_delays_ms": stage_delays_ms,
        "stage_gain_multipliers": stage_gain_multipliers,
        "resolved_stage_gains": resolved_stage_gains,
        "total_diffusion_ms": sum(stage_delays_ms),
        "feedback_passes": feedback_passes,
        "feedback_gain": feedback_gain,
        "base_gain": base_gain,
    }

    return t_ms, raw_display, processed_display, debug


class MplCanvas(FigureCanvas):
    def __init__(self):
        self.figure = Figure(figsize=(10, 6), tight_layout=True)
        self.ax_wave = self.figure.add_subplot(211)
        self.ax_db = self.figure.add_subplot(212)
        super().__init__(self.figure)


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Chronoverb Deverb/Reverb Visualizer")
        self.resize(1200, 850)

        self.canvas = MplCanvas()

        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["Deverb", "Reverb"])
        self.mode_combo.currentIndexChanged.connect(self._on_mode_changed)

        self.tunings_edit = QLineEdit("11.0, 13.0, 23.0, 31.0, 43.0, 53.0, 73.0, 83.0")
        self.stage_gains_edit = QLineEdit("1.00, 0.97, 1.02, 0.95, 1.01, 0.96, 0.99, 0.94")

        self.sample_rate_spin = QSpinBox()
        self.sample_rate_spin.setRange(8000, 384000)
        self.sample_rate_spin.setValue(48000)

        self.total_ms_spin = QDoubleSpinBox()
        self.total_ms_spin.setRange(200.0, 5000.0)
        self.total_ms_spin.setValue(1600.0)
        self.total_ms_spin.setSuffix(" ms")

        self.nominal_delay_spin = QDoubleSpinBox()
        self.nominal_delay_spin.setRange(1.0, 3000.0)
        self.nominal_delay_spin.setValue(1000.0)
        self.nominal_delay_spin.setSuffix(" ms")

        self.quality_spin = QSpinBox()
        self.quality_spin.setRange(1, 8)
        self.quality_spin.setValue(8)

        self.size_spin = QDoubleSpinBox()
        self.size_spin.setRange(0.0, 1.0)
        self.size_spin.setSingleStep(0.01)
        self.size_spin.setValue(1.0)

        self.amount_spin = QDoubleSpinBox()
        self.amount_spin.setRange(0.0, 1.0)
        self.amount_spin.setSingleStep(0.01)
        self.amount_spin.setValue(0.5)

        self.comp_bias_spin = QDoubleSpinBox()
        self.comp_bias_spin.setRange(0.0, 5.0)
        self.comp_bias_spin.setSingleStep(0.01)
        self.comp_bias_spin.setValue(0.25)

        self.feedback_gain_spin = QDoubleSpinBox()
        self.feedback_gain_spin.setRange(0.0, 0.99)
        self.feedback_gain_spin.setSingleStep(0.01)
        self.feedback_gain_spin.setValue(0.5)

        self.feedback_passes_spin = QSpinBox()
        self.feedback_passes_spin.setRange(1, 20)
        self.feedback_passes_spin.setValue(6)

        self.render_button = QPushButton("Render")
        self.render_button.clicked.connect(self.render)

        self.debug_label = QLabel()
        self.debug_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.debug_label.setWordWrap(True)

        controls = QGridLayout()
        row = 0

        controls.addWidget(QLabel("Mode:"), row, 0)
        controls.addWidget(self.mode_combo, row, 1)
        row += 1

        controls.addWidget(QLabel("Stage tunings (ms, comma-separated):"), row, 0)
        controls.addWidget(self.tunings_edit, row, 1, 1, 3)
        row += 1

        controls.addWidget(QLabel("Stage gain multipliers (comma-separated):"), row, 0)
        controls.addWidget(self.stage_gains_edit, row, 1, 1, 3)
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
        controls.addWidget(QLabel("Diffusion amount:"), row, 2)
        controls.addWidget(self.amount_spin, row, 3)
        row += 1

        controls.addWidget(QLabel("Feedback gain:"), row, 0)
        controls.addWidget(self.feedback_gain_spin, row, 1)
        controls.addWidget(QLabel("Feedback passes:"), row, 2)
        controls.addWidget(self.feedback_passes_spin, row, 3)
        row += 1

        controls.addWidget(QLabel("Diffusion compensation bias:"), row, 0)
        controls.addWidget(self.comp_bias_spin, row, 1)
        row += 1

        controls.addWidget(self.render_button, row, 0, 1, 4)
        row += 1

        layout = QVBoxLayout()
        layout.addLayout(controls)
        layout.addWidget(self.canvas)
        layout.addWidget(self.debug_label)
        self.setLayout(layout)

        self._on_mode_changed(self.mode_combo.currentIndex())
        self.render()

    def _on_mode_changed(self, index):
        is_reverb = (index == 1)

        self.nominal_delay_spin.setVisible(not is_reverb)
        nominal_delay_label = self._label_for_widget(self.nominal_delay_spin)
        if nominal_delay_label is not None:
            nominal_delay_label.setVisible(not is_reverb)

        self.comp_bias_spin.setVisible(not is_reverb)
        comp_bias_label = self._label_for_widget(self.comp_bias_spin)
        if comp_bias_label is not None:
            comp_bias_label.setVisible(not is_reverb)

        if is_reverb:
            self.tunings_edit.setText("29.0, 37.0, 43.0, 53.0, 71.0, 89.0, 113.0, 149.0")
            self.stage_gains_edit.setText("1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00")
            self.amount_spin.setValue(1.0)
        else:
            self.tunings_edit.setText("11.0, 13.0, 23.0, 31.0, 43.0, 53.0, 73.0, 83.0")
            self.stage_gains_edit.setText("1.00, 0.97, 1.02, 0.95, 1.01, 0.96, 0.99, 0.94")
            self.amount_spin.setValue(0.5)

        self.render()

    def _label_for_widget(self, widget):
        grid = self.layout().itemAt(0).layout()
        for i in range(grid.count()):
            item = grid.itemAt(i)
            if item.widget() is widget:
                row, _, _, _ = grid.getItemPosition(i)
                label_item = grid.itemAtPosition(row, 0)
                if label_item is not None:
                    return label_item.widget()
        return None

    def render(self):
        try:
            tunings = parse_csv_floats(self.tunings_edit.text())
            stage_gain_values = parse_csv_floats(self.stage_gains_edit.text())
            is_reverb = (self.mode_combo.currentIndex() == 1)

            if is_reverb:
                t_ms, raw, processed, debug = render_reverb_preview(
                    sample_rate=self.sample_rate_spin.value(),
                    total_ms=self.total_ms_spin.value(),
                    source_tunings=tunings,
                    source_stage_gain_multipliers=stage_gain_values,
                    quality=self.quality_spin.value(),
                    size01=self.size_spin.value(),
                    diffusion_amount=self.amount_spin.value(),
                    feedback_gain=self.feedback_gain_spin.value(),
                    feedback_passes=self.feedback_passes_spin.value(),
                )

                debug_text = (
                    "Effective stage delays (ms): "
                    + ", ".join(f"{x:.2f}" for x in debug["stage_delays_ms"])
                    + "\nStage gain multipliers: "
                    + ", ".join(f"{x:.3f}" for x in debug["stage_gain_multipliers"])
                    + "\nResolved stage gains: "
                    + ", ".join(f"{x:.3f}" for x in debug["resolved_stage_gains"])
                    + f"\nTotal diffusion ms: {debug['total_diffusion_ms']:.2f}"
                    + f"\nBase allpass gain: {debug['base_gain']:.3f}"
                    + f"\nFeedback passes: {debug['feedback_passes']}"
                    + f"  Feedback gain: {debug['feedback_gain']:.3f}"
                )
            else:
                t_ms, raw, processed, debug = render_deverb_preview(
                    sample_rate=self.sample_rate_spin.value(),
                    total_ms=self.total_ms_spin.value(),
                    nominal_delay_ms=self.nominal_delay_spin.value(),
                    source_tunings=tunings,
                    source_stage_gain_multipliers=stage_gain_values,
                    quality=self.quality_spin.value(),
                    size01=self.size_spin.value(),
                    diffusion_amount=self.amount_spin.value(),
                    feedback_gain=self.feedback_gain_spin.value(),
                    feedback_passes=self.feedback_passes_spin.value(),
                    diffusion_comp_bias=self.comp_bias_spin.value(),
                )

                debug_text = (
                    "Effective stage delays (ms): "
                    + ", ".join(f"{x:.2f}" for x in debug["stage_delays_ms"])
                    + "\nStage gain multipliers: "
                    + ", ".join(f"{x:.3f}" for x in debug["stage_gain_multipliers"])
                    + "\nResolved stage gains: "
                    + ", ".join(f"{x:.3f}" for x in debug["resolved_stage_gains"])
                    + f"\nTotal chain delay ms: {debug['total_chain_delay_ms']:.2f}"
                    + f"\nStatic compensation ms: {debug['static_comp_ms']:.2f}"
                    + f"\nEffective read ms: {debug['effective_read_ms']:.2f}"
                    + f"\nNominal delay ms: {debug['nominal_delay_ms']:.2f}"
                    + f"\nWrite blend amount: {debug['blend_amount']:.3f}"
                    + f"\nBase allpass gain: {debug['base_gain']:.3f}"
                    + f"\nFeedback passes: {debug['feedback_passes']}"
                    + f"  Feedback gain: {debug['feedback_gain']:.3f}"
                )

            ax1 = self.canvas.ax_wave
            ax2 = self.canvas.ax_db
            ax1.clear()
            ax2.clear()

            total_ms_val = self.total_ms_spin.value()

            ax1.plot(t_ms, raw, color="blue", linewidth=1.5, label="Raw impulse")
            ax1.plot(t_ms, processed, color="red", linewidth=1.2, label="Processed")
            ax1.set_title(f"Waveform Overlay ({'Reverb' if is_reverb else 'Deverb'} mode)")
            ax1.set_xlabel("Time (ms)")
            ax1.set_ylabel("Amplitude")
            ax1.grid(True, alpha=0.25)
            ax1.legend(loc="upper right")
            ax1.set_xlim(0.0, total_ms_val)
            ax1.xaxis.set_major_locator(ticker.MultipleLocator(100))

            ax2.plot(t_ms, dbfs_safe(raw), color="blue", linewidth=1.5, label="Raw impulse")
            ax2.plot(t_ms, dbfs_safe(processed), color="red", linewidth=1.2, label="Processed")
            ax2.set_title("Magnitude Overlay (dB)")
            ax2.set_xlabel("Time (ms)")
            ax2.set_ylabel("dB")
            ax2.grid(True, alpha=0.25)
            ax2.legend(loc="upper right")
            ax2.set_xlim(0.0, total_ms_val)
            ax2.set_ylim(-120, 6)
            ax2.xaxis.set_major_locator(ticker.MultipleLocator(100))

            self.canvas.draw()
            self.debug_label.setText(debug_text)

        except Exception as exc:
            QMessageBox.critical(self, "Render Error", str(exc))


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()