"""Verify the packaged headless audio path; never opens a device or a native window."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import wave

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--breaks', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
reports = []


def render(name, source, factory=1, bars=1, flags=(), block=257, rate=48000):
    output = (args.output / (name + '.wav')).resolve()
    command = [str(args.executable.resolve()), '--render', str(source.resolve()), str(output),
               str(factory), str(bars), '--sample-rate', str(rate), '--block-size', str(block), *flags]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (name, result.stdout, result.stderr)
    report = json.loads(Path(str(output) + '.render.json').read_text(encoding='utf-8-sig'))
    with wave.open(str(output), 'rb') as audio:
        assert audio.getframerate() == rate and audio.getnchannels() == 2 and audio.getsampwidth() == 3
        assert audio.getnframes() == report['outputFrames'] > 0
        assert abs(audio.getnframes() - report['expectedOutputSeconds'] * rate) <= 1.00001
        pcm = audio.readframes(audio.getnframes())
    samples = [int.from_bytes(pcm[i:i+3], 'little', signed=True) / 8388608
               for i in range(0, len(pcm), 6)]
    assert report['headless'] and report['application'] == 'SHFLR 0.3.0'
    assert all(math.isfinite(value) and abs(value) <= 1.000001 for value in samples)
    report.update(test=name, pcmSha256=hashlib.sha256(pcm).hexdigest())
    reports.append(report)
    print('PASS', name, 'frames', report['outputFrames'], 'delay', report['latencySamples'], flush=True)
    return samples, report


def write_source(name, samples, rate=48000):
    path = args.output / name
    with wave.open(str(path), 'wb') as output:
        output.setnchannels(1); output.setsampwidth(3); output.setframerate(rate)
        output.writeframes(b''.join(int(max(-1, min(1, x)) * 8388607).to_bytes(3, 'little', signed=True)
                                    for x in samples))
    return path


# Literal fixture: one drum onset 12 ms after beat two in a two-second bar.
rate = 48000
source = []
for sample in range(2 * rate):
    age = sample / rate - 0.512
    source.append(0.65 * min(1, age / 0.00025) * math.exp(-age / 0.04)
                  * math.sin(2 * math.pi * 410 * age) if age >= 0 else 0)
drum = write_source('synthetic-late-snare.wav', source)


def entrance(samples):
    start = rate // 2
    return next(i for i in range(int(rate * .04)) if abs(samples[start + i]) > .035) / rate * 1000


entrances = []
for amount in (0, 50, 100):
    samples, report = render('onset-' + str(amount), drum, 45, flags=('--tighten', str(amount)))
    entrances.append(entrance(samples))
    assert report['tightenPercent'] == amount
assert 10 < entrances[0] < 14, entrances
assert entrances[0] - entrances[2] > 8 and abs(entrances[1] - (entrances[0] + entrances[2]) / 2) < 2, entrances
zero, _ = render('manual-zero', drum, 45, flags=('--tighten', '100', '--nudge-colour', '1', '--nudge-ms', '0'))
assert abs(entrance(zero) - entrances[0]) < .1
for offset in (-8, 8):
    samples, report = render('manual-' + str(offset), drum, 45,
                            flags=('--tighten', '100', '--nudge-colour', '1', '--nudge-ms', str(offset)))
    assert abs(entrance(samples) - (entrances[0] - offset)) < 1
    assert abs(report['sourceOffsetsMs'][1] - offset) < .001 and report['sourceTimingStates'][1] == 4

# The original is continuous. Repeating its first half-wave creates a source
# jump at the next phrase. This tests an edit click, not an impulse in the input
# itself (which attack-preserving smoothing deliberately retains).
step = write_source('synthetic-smooth-wave.wav', [.6 * math.cos(2 * math.pi * i / rate) for i in range(2 * rate)])
hard, _ = render('hard-jump', step, 6, flags=('--smooth-ms', '0'))
soft, _ = render('smooth-jump', step, 6, flags=('--smooth-ms', '5'))
def largest_step(samples):
    return max(abs(samples[i] - samples[i-1]) for i in range(rate - 480, rate + 480))
assert largest_step(hard) > 1 and largest_step(soft) < .02
constant = write_source('synthetic-continuous.wav', [.3] * (2 * rate))
continuous, _ = render('continuous-untouched', constant, 1, flags=('--smooth-ms', '10'))
assert max(abs(value - .3) for value in continuous) < 1e-6
rests, _ = render('exact-rests', constant, 55, flags=('--smooth-ms', '10', '--tighten', '100'))
assert all(value == 0 for value in rests[6000:12000])

small, small_report = render('blocks-64', drum, 45, flags=('--tighten', '100'), block=64)
large, large_report = render('blocks-1024', drum, 45, flags=('--tighten', '100'), block=1024)
assert len(small) == len(large) and max(abs(a-b) for a,b in zip(small, large)) <= 2/8388608
assert small_report['latencySamples'] == large_report['latencySamples']

# The real-break sweep is a stability/render check; listening approval remains with the user.
amen = args.breaks / 'KAB1_174_AmenBreak_Cut_01.wav'
apache = args.breaks / 'KAPB1_174_ApacheBreak_01_HighTone_Normal.wav'
think = args.breaks / 'KTB1_174_ThinkBreakOne_Sampler.wav'
hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (amen, apache, think)}
detected = 0
for factory in (1, 13, 33, 40, 46, 55, 62):
    one = factory in (1, 13, 40, 46, 55)
    for amount in (0, 50, 100):
        samples, report = render(f'factory-{factory:02d}-tighten-{amount}', amen if one else apache,
                                 factory, 1 if one else 2, ('--tighten', str(amount)), rate=44100)
        assert any(abs(value) > .001 for value in samples)
        detected += sum(status == 2 for status in report['sourceTimingStates'])
for amount in (50, 100):
    render('think-tighten-' + str(amount), think, 21, 4, ('--tighten', str(amount)), block=511)
assert detected > 0, 'real-break sweep found no confident phrase starts'
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in hashes.items())

for index, flags in enumerate((('--tighten', '101'), ('--tighten', '-1'), ('--smooth-ms', 'nan'),
                               ('--smooth-ms', '1.2.3'), ('--smooth-ms', '10.1'), ('--nudge-ms', '1'),
                               ('--nudge-colour', '0'), ('--nudge-colour', '8', '--nudge-ms', '0'),
                               ('--nudge-colour', '0', '--nudge-ms', '-30.1'))):
    output = (args.output / f'invalid-{index}.wav').resolve()
    result = subprocess.run([str(args.executable.resolve()), '--render', str(drum.resolve()), str(output),
                             '1', '1', *flags], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and not output.exists(), (flags, result.stderr)

(args.output / 'summary.json').write_text(json.dumps({'passed': len(reports), 'invalidCommandsRejected': 9,
    'syntheticEntrancesMs': entrances, 'smoothMaxStep': largest_step(soft),
    'hardMaxStep': largest_step(hard), 'sourcesUnchanged': True, 'renders': reports}, indent=2))
print('PASS all signal oracles, real-break renders, block invariance, source integrity and CLI rejections', flush=True)
