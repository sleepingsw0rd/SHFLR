"""Noninteractive integration checks. Reads local BeatPlace breaks; never redistributes them."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import wave


def pcm24(path):
    with wave.open(str(path), "rb") as audio:
        assert audio.getsampwidth() == 3, path
        info = (audio.getframerate(), audio.getnchannels(), audio.getnframes())
        data = audio.readframes(audio.getnframes())
    values = [int.from_bytes(data[i:i + 3], "little", signed=True) for i in range(0, len(data), 3)]
    return info, values, hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--breaks", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    amen = args.breaks / "KAB1_174_AmenBreak_Cut_01.wav"
    apache = args.breaks / "KAPB1_174_ApacheBreak_01_HighTone_Normal.wav"
    think = args.breaks / "KTB1_174_ThinkBreakOne_Sampler.wav"
    original_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in (amen, apache, think)}
    results = []
    captures = {}

    def render(name, source, factory, bars, rate=44100, block=257, expected_bpm=174):
        output = args.output / (name + ".wav")
        command = [str(args.executable.resolve()), "--render", str(source.resolve()), str(output.resolve()),
                   str(factory), str(bars), "--sample-rate", str(rate), "--block-size", str(block), "--smooth-ms", "0"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
        metadata = json.loads(Path(str(output) + ".render.json").read_text(encoding="utf-8-sig"))
        info, samples, digest = pcm24(output)
        assert info[0] == rate and info[1] == 2 and info[2] > 0, name
        assert abs(info[2] - metadata["expectedOutputSeconds"] * rate) <= 1.00001, (name, info, metadata)
        rms = math.sqrt(sum((v / 8388608) ** 2 for v in samples) / len(samples))
        assert rms > 0.0001, (name, "silent render")
        assert metadata["headless"] and abs(metadata["bpm"] - expected_bpm) < 0.01, metadata
        metadata.update(test=name, pcmSha256=digest, rms=rms, peak=max(abs(v) for v in samples) / 8388608)
        results.append(metadata)
        # Keep only PCM used by independent checks, rather than all full loops.
        if name in {"factory-01", "factory-13", "factory-33", "factory-40", "factory-46",
                    "factory-55", "factory-56", "apache-48k-64", "apache-48k-257", "apache-48k-1024",
                    "rests-64", "rests-1024"}:
            captures[name] = (info, samples)
        print(f"PASS {name}: {info[2]} frames, {rate} Hz, delay {metadata['latencySamples']} samples", flush=True)

    for factory in range(63):
        one_bar = (1 <= factory <= 6 or 13 <= factory <= 15 or 19 <= factory <= 25
                   or 39 <= factory <= 42 or 45 <= factory <= 48 or 51 <= factory <= 52
                   or 55 <= factory <= 58)
        render(f"factory-{factory:02d}", amen if one_bar else apache, factory, 1 if one_bar else 2)
    # Every new pattern also sees Think's four-bar source at its actual tempo.
    # The input window remains the preset's one/two-bar setting.
    for factory in range(13, 63):
        render(f"think-factory-{factory:02d}", think, factory, 4, 48000, 511)
    render("apache-48k-64", apache, 9, 2, 48000, 64)
    render("apache-48k-257", apache, 9, 2, 48000, 257)
    render("apache-48k-1024", apache, 9, 2, 48000, 1024)
    render("amen-96k", amen, 2, 1, 96000, 511)
    render("think-four-source-bars", think, 12, 4, 44100, 128)
    for factory in (26, 29, 32):
        render(f"amen-two-bar-pattern-{factory}", amen, factory, 1, 44100, 257)
    render("rests-64", amen, 55, 1, 44100, 64)
    render("rests-1024", amen, 55, 1, 44100, 1024)
    # A private half-speed copy keeps the original beat positions at 87 BPM.
    # No source file is modified and no break audio enters the release archive.
    slowed = args.output / "amen-half-speed-source.wav"
    with wave.open(str(amen), "rb") as reader:
        params = reader.getparams()
        frames = reader.readframes(reader.getnframes())
    with wave.open(str(slowed), "wb") as writer:
        writer.setparams(params._replace(framerate=params.framerate // 2))
        writer.writeframes(frames)
    for factory in (52, 55, 56, 61):
        render(f"hiphop-87bpm-{factory}", slowed, factory, 1, 48000, 511, expected_bpm=87)

    reference_info, reference = captures["apache-48k-64"]
    for name in ("apache-48k-257", "apache-48k-1024"):
        info, samples = captures[name]
        assert info == reference_info, (name, "block-dependent loop length")
        difference = max(abs(a - b) for a, b in zip(reference, samples))
        assert difference <= 2, (name, "block-dependent samples", difference)

    # Independent provenance oracle with Smooth disabled: straight playback
    # equals the original PCM. SoundRenderChecks covers smoothing separately.
    source_info, source, _ = pcm24(amen)
    output_info, output = captures["factory-01"]
    assert source_info == output_info
    difference = 0
    for i, (dry, wet) in enumerate(zip(source, output)):
        difference = max(difference, abs(wet - dry))
    assert difference <= 2, ("straight phrase provenance", difference)
    # Literal first-three-beat oracle: repeat the original first 3/4 of the
    # source, with the final repeat cut after two beats. Derive directly from
    # source PCM, independent of the C++ pattern expansion and render mapping.
    three_info, three_output = captures["factory-13"]
    assert three_info == (source_info[0], source_info[1], source_info[2] * 2)
    three_frames = source_info[2] * 3 // 4
    three_difference = 0
    for frame in range(three_info[2]):
        local = frame % three_frames
        for channel in range(2):
            dry = source[local * 2 + channel]
            wet = three_output[frame * 2 + channel]
            three_difference = max(three_difference, abs(wet - dry))
    assert three_difference <= 2, ("first-three-beat phrase provenance", three_difference)

    def check_segments(name, source_path, source_bars, segments):
        """Literal source-start/length in sixteenths; None denotes exact silence."""
        dry_info, dry, _ = pcm24(source_path)
        wet_info, wet = captures[name]
        assert dry_info[:2] == wet_info[:2]
        cell_frames = dry_info[2] / (source_bars * 16)
        start = 0.0
        maximum = 0
        silent_frames = 0
        for source_cell, length in segments:
            end = start + length * cell_frames
            for frame in range(math.ceil(start - 1e-7), min(wet_info[2], math.ceil(end - 1e-7))):
                if source_cell is None:
                    assert wet[frame * 2:frame * 2 + 2] == [0, 0], (name, "rest leaked audio", frame)
                    silent_frames += 1
                    continue
                local = frame - start
                source_frame = source_cell * cell_frames + local
                first = math.floor(source_frame)
                fraction = source_frame - first
                for channel in range(2):
                    a = dry[first * 2 + channel]
                    b = dry[min(first + 1, dry_info[2] - 1) * 2 + channel]
                    expected = round(a + fraction * (b - a))
                    maximum = max(maximum, abs(wet[frame * 2 + channel] - expected))
            start = end
        assert abs(start - wet_info[2]) < 1e-6, (name, "oracle must cover full output")
        assert maximum <= 3, (name, "literal PCM source mapping", maximum)
        return {"maxLsb": maximum, "exactSilentFrames": silent_frames}

    oracles = {
        "sixBeat": check_segments("factory-33", apache, 2, [(0, 24), (0, 24), (0, 16)]),
        "kickRoll": check_segments("factory-40", amen, 1,
            [(0, 1), (1, 3), (4, 4), (8, 8)] + [(0, 1)] * 4 + [(4, 4), (8, 8)]),
        "snareRoll": check_segments("factory-46", amen, 1,
            [(0, 4), (4, 1), (5, 3), (8, 8), (0, 4)] + [(4, 1)] * 4 + [(8, 8)]),
        "kickGaps": check_segments("factory-55", amen, 1,
            [(0, 1), (None, 1), (0, 1), (None, 1), (4, 4), (8, 4), (12, 4)]),
        "snareGaps": check_segments("factory-56", amen, 1,
            [(0, 4), (4, 1), (None, 1), (4, 1), (None, 1), (8, 4), (12, 4)])
    }
    for name in ("rests-64", "rests-1024"):
        info, samples = captures[name]
        assert info == captures["factory-55"][0]
        assert max(abs(a - b) for a, b in zip(samples, captures["factory-55"][1])) <= 2
    assert results[0]["latencySamples"] == 0, "original should need no lookahead"
    assert results[9]["latencySamples"] > 0, "bar swap must read ahead"

    missing = subprocess.run([str(args.executable.resolve()), "--render", str(args.output / "missing.wav"),
                              str(args.output / "must-not-exist.wav")], capture_output=True, timeout=10)
    assert missing.returncode != 0 and not (args.output / "must-not-exist.wav").exists()
    for path, digest in original_hashes.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, "source file was changed"
    summary = {"passed": len(results), "blockInvariantMaxLsb": 2, "straightOracleMaxLsb": difference,
               "threeBeatOracleMaxLsb": three_difference,
               "literalOracles": oracles,
               "sourcesUnchanged": True, "renders": results}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"PASS {len(results)} renders; block invariance; straight oracle ({difference} LSB); three-beat oracle ({three_difference} LSB); missing-file rejection; sources unchanged")


if __name__ == "__main__":
    main()
