#!/usr/bin/env python3
"""Measure rendered WAVs and recommend safe, RMS-based engine gains."""
from __future__ import annotations

import argparse
import array
import csv
import json
import math
import struct
import wave
import re
from pathlib import Path


def read_mono_wav(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as wav:
        channels, width, rate = wav.getnchannels(), wav.getsampwidth(), wav.getframerate()
        if wav.getcomptype() != "NONE" or width not in (1, 2, 3, 4):
            raise ValueError("need uncompressed 8/16/24/32-bit PCM")
        raw = wav.readframes(wav.getnframes())
    if channels == 1 and width in (2, 4):
        decoded = array.array("h" if width == 2 else "i"); decoded.frombytes(raw)
        if __import__("sys").byteorder != "little": decoded.byteswap()
        scale = 32768.0 if width == 2 else 2147483648.0
        return [sample / scale for sample in decoded], rate
    values = []
    frame_size = channels * width
    for offset in range(0, len(raw), frame_size):
        frame = raw[offset:offset + frame_size]
        samples = []
        for channel in range(channels):
            sample = frame[channel * width:(channel + 1) * width]
            if width == 1:
                samples.append((sample[0] - 128) / 128.0)
            elif width == 2:
                samples.append(struct.unpack("<h", sample)[0] / 32768.0)
            elif width == 3:
                value = int.from_bytes(sample + (b"\xff" if sample[2] & 0x80 else b"\0"), "little", signed=True)
                samples.append(value / 8388608.0)
            else:
                samples.append(struct.unpack("<i", sample)[0] / 2147483648.0)
        values.append(sum(samples) / len(samples))
    return values, rate


def percentile(values: list[float], percentage: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("no values")
    index = (len(ordered) - 1) * percentage / 100.0
    lo, hi = math.floor(index), math.ceil(index)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (index - lo)


def db(value: float) -> float:
    return 20.0 * math.log10(max(value, 1e-12))


def high_shelf(rate: int, frequency: float, gain_db: float, q: float):
    amplitude = 10.0 ** (gain_db / 40.0); w = 2.0 * math.pi * frequency / rate
    c, s, alpha = math.cos(w), math.sin(w), math.sin(w) / (2.0 * q)
    root = 2.0 * math.sqrt(amplitude) * alpha
    b0 = amplitude * ((amplitude + 1) + (amplitude - 1) * c + root)
    b1 = -2 * amplitude * ((amplitude - 1) + (amplitude + 1) * c)
    b2 = amplitude * ((amplitude + 1) + (amplitude - 1) * c - root)
    a0 = (amplitude + 1) - (amplitude - 1) * c + root
    a1 = 2 * ((amplitude - 1) - (amplitude + 1) * c)
    a2 = (amplitude + 1) - (amplitude - 1) * c - root
    return (b0 / a0, b1 / a0, b2 / a0), (a1 / a0, a2 / a0)


def high_pass(rate: int, frequency: float, q: float):
    w = 2.0 * math.pi * frequency / rate; c, s = math.cos(w), math.sin(w); alpha = s / (2.0 * q)
    a0 = 1.0 + alpha
    return ((1.0 + c) / (2.0 * a0), -(1.0 + c) / a0, (1.0 + c) / (2.0 * a0)), (-2.0 * c / a0, (1.0 - alpha) / a0)


def k_weighted_loudness(signal: list[float], rate: int) -> float:
    # ITU-R BS.1770 K-weighting: pre-filter shelf followed by the RLB high-pass.
    shelf = high_shelf(rate, 1681.974450955533, 3.99984385397, 0.7071752369554196)
    rlb = high_pass(rate, 38.13547087602444, 0.5003270373238773)
    sb, sa = shelf; hb, ha = rlb
    sx1 = sx2 = sy1 = sy2 = hx1 = hx2 = hy1 = hy2 = energy = 0.0
    for sample in signal:
        shelf_sample = sb[0] * sample + sb[1] * sx1 + sb[2] * sx2 - sa[0] * sy1 - sa[1] * sy2
        sx2, sx1, sy2, sy1 = sx1, sample, sy1, shelf_sample
        weighted = hb[0] * shelf_sample + hb[1] * hx1 + hb[2] * hx2 - ha[0] * hy1 - ha[1] * hy2
        hx2, hx1, hy2, hy1 = hx1, shelf_sample, hy1, weighted
        energy += weighted * weighted
    energy /= max(1, len(signal))
    return -0.691 + 10.0 * math.log10(max(energy, 1e-24))


def metrics(path: Path, trim_seconds: float, input_scale: float) -> dict[str, float | int | str]:
    signal, rate = read_mono_wav(path)
    signal = [sample * input_scale for sample in signal]
    trim = min(int(rate * trim_seconds), max(0, len(signal) // 4))
    signal = signal[trim:len(signal) - trim] or signal
    peak = max(map(abs, signal))
    rms = math.sqrt(sum(sample * sample for sample in signal) / len(signal))
    return {"file": str(path), "rate": rate, "frames": len(signal), "peak": peak, "peak_db": db(peak),
            "rms": rms, "rms_db": db(rms), "loudness_db": k_weighted_loudness(signal, rate)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", "-i", type=Path, default=Path("tracker/measurements"))
    parser.add_argument("--out-dir", "-o", type=Path, default=Path("results"))
    parser.add_argument("--reference", default="pcm", help="reference subdirectory (default: pcm)")
    parser.add_argument("--trim-seconds", type=float, default=0.1, help="ignore attack/release at each end")
    parser.add_argument("--headroom-db", type=float, default=1.0, help="minimum peak headroom for suggested gains")
    parser.add_argument("--input-scale", type=float, default=8.0, help="inverse scale used by the renderer (default: 8)")
    parser.add_argument("--include-vca", action="store_true", help="include separate *-vca diagnostics")
    args = parser.parse_args()
    groups = {directory.name: sorted(directory.rglob("*.wav")) for directory in args.input_dir.iterdir()
              if directory.is_dir() and (args.include_vca or not directory.name.endswith("-vca"))}
    if args.reference not in groups or not groups[args.reference]: parser.error(f"no WAVs for reference '{args.reference}'")
    rows = []
    for engine, files in groups.items():
        for file in files:
            try:
                row = metrics(file, args.trim_seconds, args.input_scale); row["engine"] = engine
                match = re.search(r"_(\d+)_p(\d+)_n", file.name)
                row["model"] = int(match.group(1)) if match else None
                row["patch"] = int(match.group(2)) if match else None
                shaper = re.search(r"_s(\d+)", file.name)
                row["shaper"] = int(shaper.group(1)) if shaper else None
                row["mode"] = "vca" if file.name.endswith("_vca.wav") else "trig"
                rows.append(row)
            except (OSError, ValueError, EOFError, wave.Error) as error:
                print(f"Skipping {file}: {error}")
    if not rows: parser.error("no readable WAV files")
    target = percentile([row["loudness_db"] for row in rows if row["engine"] == args.reference], 50)
    ceiling = -abs(args.headroom_db)
    report = {"method": "median BS.1770 K-weighted loudness, trimmed at both ends", "reference": args.reference,
              "target_loudness_db": target, "headroom_db": abs(args.headroom_db), "engines": {}}
    for engine in sorted({row["engine"] for row in rows}):
        engine_rows = [row for row in rows if row["engine"] == engine]
        loudness = percentile([row["loudness_db"] for row in engine_rows], 50)
        peak95 = percentile([row["peak_db"] for row in engine_rows], 95)
        requested = target - loudness
        suggested = max(-12.0, min(6.0, requested, ceiling - peak95))
        report["engines"][engine] = {"files": len(engine_rows), "median_loudness_db": loudness, "p95_peak_db": peak95,
                                     "requested_gain_db": requested, "suggested_gain_db": suggested, "suggested_gain_linear": 10 ** (suggested / 20)}
    args.out_dir.mkdir(parents=True, exist_ok=True)
    fields = ["engine", "model", "patch", "shaper", "mode", "file", "rate", "frames", "peak", "peak_db", "rms", "rms_db", "loudness_db"]
    with (args.out_dir / "amplitude_metrics.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
    models = {}
    for row in rows:
        if row["model"] is None or row["engine"].endswith("-vca"): continue
        models.setdefault((row["engine"], row["model"]), []).append(row)
    with (args.out_dir / "model_amplitude_summary.csv").open("w", newline="", encoding="utf-8") as file:
        fields = ["engine", "model", "files", "median_loudness_db", "p95_peak_db", "requested_gain_db", "safe_gain_db"]
        writer = csv.DictWriter(file, fieldnames=fields); writer.writeheader()
        for (engine, model), values in sorted(models.items()):
            loudness = percentile([value["loudness_db"] for value in values], 50)
            peak = percentile([value["peak_db"] for value in values], 95)
            request = target - loudness
            writer.writerow({"engine": engine, "model": model, "files": len(values), "median_loudness_db": loudness,
                             "p95_peak_db": peak, "requested_gain_db": request,
                             "safe_gain_db": max(-12.0, min(6.0, request, ceiling - peak))})
    mme = {}
    for row in rows:
        if row["engine"] == "mme" and row["model"] is not None and row["shaper"] is not None:
            mme.setdefault((row["model"], row["shaper"]), []).append(row)
    report["mme_shaper"] = {}
    for (model, shaper), values in sorted(mme.items()):
        loudness = percentile([value["loudness_db"] for value in values], 50)
        peak = percentile([value["peak_db"] for value in values], 95)
        gain = max(-12.0, min(6.0, target - loudness, ceiling - peak))
        report["mme_shaper"].setdefault(str(model), {})[str(shaper)] = {"gain_db": gain, "gain_linear": 10 ** (gain / 20)}
    with (args.out_dir / "compensation_gains.json").open("w", encoding="utf-8") as file:
        json.dump(report, file, indent=2); file.write("\n")
    for engine, result in report["engines"].items():
        print(f"{engine:12} {result['suggested_gain_db']:+.2f} dB  ({result['suggested_gain_linear']:.4f}x)")


if __name__ == "__main__": main()
