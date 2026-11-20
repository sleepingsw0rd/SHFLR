"""Exercise the factory browser catalog and render its new bank without a window/device."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import subprocess
import wave

from RenderChecks import pcm24


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--breaks', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--catalog-only', action='store_true')
    parser.add_argument('--from-index', type=int, default=63)
    parser.add_argument('--previous-catalog', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = str(args.executable.resolve())
    catalog_path = args.output / 'catalog.json'
    command = subprocess.run([executable, '--catalog', str(catalog_path.resolve())],
                             capture_output=True, text=True, timeout=30)
    assert command.returncode == 0, ('catalog command', command.stdout, command.stderr)
    catalog = json.loads(catalog_path.read_text(encoding='utf-8-sig'))
    entries = catalog['presets']
    assert len(entries) == 231, ('expected 203 existing plus 28 new presets', len(entries))
    assert catalog['application'] == 'SHFLR 0.3.0'
    assert [p['index'] for p in entries] == list(range(len(entries)))
    assert len(set(p['name'] for p in entries)) == len(entries)
    categories = ['DnB', 'Jungle', 'Hip-hop', 'Buildups', 'Breakdowns', 'Shuffles', 'Fills']
    counts = Counter((p['sourceBars'], p['category']) for p in entries[63:203])
    assert counts == Counter({(bars, category): 10 for bars in (1, 2) for category in categories}), counts
    additions = Counter((p['sourceBars'], p['category']) for p in entries[203:])
    assert additions == Counter({(bars, category): count for bars in (1, 2)
                                 for category, count in [('Buildups', 6), ('Fills', 8)]}), additions
    if args.previous_catalog:
        previous = json.loads(args.previous_catalog.read_text(encoding='utf-8-sig'))['presets']
        assert entries[:len(previous)] == previous, 'previous catalog changed'
    for p in entries:
        assert p['sourceBars'] in (1, 2) and p['outputBars'] in (1, 2, 4, 8), p
        assert p['category'] in ['Essentials'] + categories, p
        assert len(p['cells']) == p['sourceBars'] * 16, p
        assert len(p['order']) == len(p['orderLengths']) > 0, p
    assert 0 <= args.from_index < len(entries)
    selected = entries[args.from_index:]
    print('PASS catalog: 231 presets; 12 two-bar kick builds and 16 end shuffles added', flush=True)
    if args.catalog_only:
        return
    assert args.breaks is not None, '--breaks required for rendering'
    sources = {
        1: args.breaks / 'KAB1_174_AmenBreak_Cut_01.wav',
        2: args.breaks / 'KAPB1_174_ApacheBreak_01_HighTone_Normal.wav',
    }
    source_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources.values()}
    dry = {bars: pcm24(path) for bars, path in sources.items()}
    results = []

    def render(entry, suffix='', block=257, smooth=0, source_override=None):
        output = args.output / f"factory-{entry['index']:03d}{suffix}.wav"
        source = source_override or sources[entry['sourceBars']]
        result = subprocess.run([executable, '--render', str(source.resolve()), str(output.resolve()),
            str(entry['index']), str(entry['sourceBars']), '--block-size', str(block),
            '--smooth-ms', str(smooth)], capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (entry['name'], result.stdout, result.stderr)
        report = json.loads(Path(str(output) + '.render.json').read_text(encoding='utf-8-sig'))
        info, samples, digest = pcm24(output)
        assert report['headless'] and report['factoryName'] == entry['name'], report
        assert report['outputBars'] == entry['outputBars'] and info[1] == 2, report
        assert abs(info[2] - report['expectedOutputSeconds'] * info[0]) <= 1.00001, report
        rms = math.sqrt(sum((v / 8388608) ** 2 for v in samples) / len(samples))
        assert rms > 0.0001, (entry['name'], 'silent render')
        report.update(pcmSha256=digest, rms=rms, category=entry['category'])
        results.append(report)
        return info, samples

    def oracle(entry, wet_info, wet):
        """Independently expand source ranges/caps/rests; inspect PCM inside every occurrence."""
        dry_info, source, _ = dry[entry['sourceBars']]
        assert wet_info[:2] == dry_info[:2]
        cell_frames = dry_info[2] / (16 * entry['sourceBars'])
        target_cells = 16 * entry['outputBars']
        position = 0
        occurrence = 0
        maximum = 0
        silent = 0
        while position < target_cells:
            index = occurrence % len(entry['order'])
            token = entry['order'][index]
            source_start = None
            if token >= 8:
                length = [1, 2, 4, 8, 16][token - 8]
            else:
                region = [i for i, colour in enumerate(entry['cells']) if colour == token]
                assert region and region == list(range(region[0], region[-1] + 1)), entry
                source_start = region[0]
                length = min(len(region), entry['orderLengths'][index] or len(region))
            length = min(length, target_cells - position)
            start = position * cell_frames
            end = (position + length) * cell_frames
            # Check full rest interiors and dense representative audio positions.
            first = math.ceil(start + 2)
            last = min(wet_info[2], math.floor(end - 2))
            stride = 1 if source_start is None else max(1, (last - first) // 32)
            for frame in range(first, last, stride):
                if source_start is None:
                    assert wet[frame * 2:frame * 2 + 2] == [0, 0], (entry['name'], 'rest leaked', frame)
                    silent += 1
                else:
                    source_frame = source_start * cell_frames + frame - start
                    a = math.floor(source_frame)
                    fraction = source_frame - a
                    b = min(a + 1, dry_info[2] - 1)
                    for channel in range(2):
                        expected = round(source[a * 2 + channel] + fraction *
                                         (source[b * 2 + channel] - source[a * 2 + channel]))
                        maximum = max(maximum, abs(wet[frame * 2 + channel] - expected))
            position += length
            occurrence += 1
        assert maximum <= 3, (entry['name'], 'source provenance', maximum)
        return {'maximumLsb': maximum, 'exactSilentFrames': silent}

    oracles = {}
    for entry in selected:
        info, wet = render(entry)
        oracles[entry['name']] = oracle(entry, info, wet)
        print(f"PASS {entry['index']}: {entry['name']}", flush=True)

    block_differences = {}
    for bars in (1, 2):
        for category in sorted({p['category'] for p in selected if p['sourceBars'] == bars}):
            entry = next(p for p in selected if p['sourceBars'] == bars and p['category'] == category)
            a_info, a = render(entry, '-smooth64', 64, 2)
            b_info, b = render(entry, '-smooth1024', 1024, 2)
            assert a_info == b_info
            delta = max(abs(x - y) for x, y in zip(a, b))
            assert delta <= 2, (entry['name'], 'block-dependent PCM', delta)
            block_differences[entry['name']] = delta
    # Private half-speed source copies verify longer real-time intervals at87BPM;
    # their PCM stays unchanged, and no factory audio is packaged or modified.
    for bars, source_path in sources.items():
        slowed = args.output / f'source-{bars}-bars-87bpm.wav'
        with wave.open(str(source_path), 'rb') as reader:
            params = reader.getparams()
            frames = reader.readframes(reader.getnframes())
        with wave.open(str(slowed), 'wb') as writer:
            writer.setparams(params._replace(framerate=params.framerate // 2))
            writer.writeframes(frames)
        for category in ('Buildups', 'Fills'):
            entry = next((p for p in selected if p['sourceBars'] == bars and p['category'] == category), None)
            if entry is None:
                continue
            render(entry, '-87bpm', 511, 2, slowed)
            assert abs(results[-1]['bpm'] - 87) < 0.01
    for path, digest in source_hashes.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, 'source modified'
    summary = {'catalogCount': len(entries), 'verifiedPresets': len(selected), 'fromIndex': args.from_index, 'renders': len(results),
               'sourcesUnchanged': True, 'sourceOracles': oracles,
               'blockInvariance': block_differences, 'results': results}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(f'PASS {len(results)} renders, {len(selected)} source mappings, {len(block_differences)} block-invariance checks, 87BPM checks')


if __name__ == '__main__':
    main()
