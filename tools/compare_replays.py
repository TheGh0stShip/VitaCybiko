#!/usr/bin/env python3
"""Compare deterministic core replays; host timings are not Vita FPS evidence."""
import argparse
import csv
import json
import os
from pathlib import Path
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path,
                        help='new directory for CSV files and summary.json')
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('replay_args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.runs < 1 or not args.replay_args:
        parser.error('positive --runs and replay arguments are required')
    replay_args = args.replay_args
    if replay_args[0] == '--':
        replay_args = replay_args[1:]
    args.output.mkdir(parents=True, exist_ok=False)
    times = {'baseline': [], 'candidate': []}
    fingerprints = None
    for run in range(args.runs):
        # Alternate order to reduce systematic warm-cache/thermal bias.
        order = ('baseline', 'candidate') if run % 2 == 0 else ('candidate', 'baseline')
        for name in order:
            output = args.output / f'{name}-{run}.csv'
            env = dict(os.environ, CYBIKO_REPLAY_LOG=str(output.resolve()))
            result = subprocess.run([str(getattr(args, name).resolve()), *replay_args],
                                    env=env, capture_output=True, text=True, timeout=600)
            (args.output / f'{name}-{run}.txt').write_text(result.stdout + result.stderr)
            result.check_returncode()
            with output.open(newline='') as stream:
                rows = list(csv.DictReader(stream))
            if not rows:
                raise RuntimeError(f'{name}: empty replay')
            state = [{k: v for k, v in row.items() if k != 'core_ms'} for row in rows]
            if fingerprints is None:
                fingerprints = state
            if state != fingerprints:
                mismatch = next((i for i, (a, b) in enumerate(zip(fingerprints, state))
                                 if a != b), min(len(state), len(fingerprints)))
                raise RuntimeError(f'{name} run {run}: state diverged at frame {mismatch}')
            total = sum(float(row['core_ms']) for row in rows)
            times[name].append(total)
            print(f'{name} run {run}: {total:.3f} ms core; fingerprints match', flush=True)
    medians = {name: statistics.median(values) for name, values in times.items()}
    report = {
        'scope': 'host core CPU time, NOT physical-Vita frame rate',
        'frames_per_run': len(fingerprints), 'runs_per_build': args.runs,
        'all_fingerprints_match': True, 'core_ms': times, 'median_core_ms': medians,
        'median_reduction_percent': 100 * (1 - medians['candidate'] / medians['baseline']),
    }
    (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
