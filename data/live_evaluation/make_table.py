"""Numbers of Table 6 of the paper (live operation of the complete system) from the measurements of tools/live_eval.py.

FOLDER is a run folder of tools/live_eval.py, or a folder with several run folders (sessions on the same computer).
For each phase, pooled over the sessions that include it, and for all the phases together: loop and camera rates,
camera frames not processed, loop interval, loop work, latency from the camera driver to the GUI image and its parts,
CPU and memory, API response time, JPEG encoding, stream throughput and latency, the agreement of the HR and SpO2 with
the pulse oximeter (each oximeter value paired with the last processed frame, with a face and stable values; hr_loa:
95% limits of agreement of Bland and Altman, bias +- 1.96 SD), and the mean time per frame of the core modules (from
the cumulative times printed in app.log.gz). With several sessions, the same numbers are also given for each session.
Requires numpy.

Usage:  python3 data/live_evaluation/make_table.py data/live_evaluation/laptop
The numbers are printed and written to table_numbers.json in FOLDER.
"""
import bisect
import csv
import gzip
import json
import re
import sys
from pathlib import Path

import numpy as np

MODULES = {'FD': 'face_detection', 'FL': 'face_alignment', 'FN': 'face_normalization', 'FsEG': 'skin_segmentation',
           'rPPG': 'rgb_to_bvp', 'FILT': 'filtering', 'SPECT': 'spectral_analysis'}
TIMES = re.compile(r'Times at frame:\s+(\d+)\s+- \[(\w+)\]:\s+([0-9.eE+-]+)')


def load(run, name, ints=()):
    with open(run / name, newline='') as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ints:
            r[k] = int(r[k])
    return rows


class Session:
    def __init__(self, run):
        self.name = run.name
        info = json.loads((run / 'info.json').read_text())
        self.phases = [(p['name'], p['t_start'], p['t_end']) for p in info['phases']]
        self.frames = load(run, 'frames.csv', ('frame', 'cam_seq', 't_driver', 't_read', 't_start', 't_frame',
                                               't_processed', 't_gui', 't_end', 'face', 'stable', 'enc_main_us',
                                               'jpeg_main_bytes', 'enc_pulse_us', 'jpeg_pulse_bytes'))
        self.camera = load(run, 'camera.csv', ('cam_seq', 't_driver', 't_read'))
        self.api = load(run, 'api.csv', ('t_send', 't_done'))
        self.streams = load(run, 'streams.csv', ('client', 'frame', 't_recv', 'bytes'))
        self.resources = load(run, 'resources.csv', ('t',))
        self.oxi = load(run, 'oximeter.csv', ('t', 'pulse_rate', 'spo2', 'finger'))
        prev = None
        for f in self.frames:
            f['new'] = f['cam_seq'] != prev
            prev = f['cam_seq']
        self.processed = {f['cam_seq'] for f in self.frames}
        self.by_frame = {f['frame']: f for f in self.frames}
        self.ft = [f['t_start'] for f in self.frames]
        # Cumulative module times (s) printed after each processed frame; frame n of the log is row n - 1 of frames.csv
        self.cumulative = {m: {} for m in MODULES}
        with gzip.open(str(run / 'app.log.gz'), 'rt', errors='replace') as f:
            for line in f:
                m = TIMES.search(line)
                if m and m.group(2) in MODULES:
                    self.cumulative[m.group(2)][int(m.group(1))] = float(m.group(3))

    def collect(self, t0, t1):
        """Samples of the interval [t0, t1), to be pooled with those of other phases or sessions."""
        F = [f for f in self.frames if t0 <= f['t_start'] < t1]
        N = [f for f in F if f['new']]
        C = [c for c in self.camera if t0 <= c['t_read'] < t1]
        c = {'seconds': (t1 - t0) / 1e6, 'iterations': len(F), 'new': len(N), 'camera': len(C),
             'not_processed': sum(x['cam_seq'] not in self.processed for x in C),
             'interval': list(np.diff([f['t_start'] for f in F]) / 1e3),
             'work': [(f['t_end'] - f['t_start']) / 1e3 for f in F],
             'lat': [(f['t_gui'] - f['t_driver']) / 1e3 for f in N],
             'driver_to_read': [(f['t_read'] - f['t_driver']) / 1e3 for f in N],
             'read_to_start': [(f['t_start'] - f['t_read']) / 1e3 for f in N],
             'start_to_gui': [(f['t_gui'] - f['t_start']) / 1e3 for f in N],
             'face': [f['face'] for f in F],
             'resources': [r for r in self.resources if t0 <= r['t'] < t1]}
        A = [a for a in self.api if t0 <= a['t_send'] < t1]
        c['api'] = [(a['t_done'] - a['t_send']) / 1e3 for a in A if a['status'] == '200']
        c['api_n'] = len(A)
        for key in ('main', 'pulse'):
            E = [f for f in F if f['jpeg_%s_bytes' % key] > 0]
            c['enc_' + key] = [f['enc_%s_us' % key] / 1e3 for f in E]
            c['jpeg_' + key] = [f['jpeg_%s_bytes' % key] for f in E]
        S = [r for r in self.streams if t0 <= r['t_recv'] < t1]
        clients = sorted({(r['phase'], r['client']) for r in S})
        c['client_rates'] = [sum(1 for r in S if (r['phase'], r['client']) == k) / c['seconds'] for k in clients]
        c['stream_bytes'] = sum(r['bytes'] for r in S)
        c['stream_lat'] = [(r['t_recv'] - self.by_frame[r['frame']]['t_driver']) / 1e3
                           for r in S if r['frame'] in self.by_frame]
        c['stream_delivery'] = [(r['t_recv'] - self.by_frame[r['frame']]['t_end']) / 1e3
                                for r in S if r['frame'] in self.by_frame]
        c['hr_app'], c['hr_ref'], c['spo2_app'], c['spo2_ref'] = [], [], [], []
        for o in self.oxi:
            if not (t0 <= o['t'] < t1) or not o['finger']:
                continue
            f = self.frames[bisect.bisect_right(self.ft, o['t']) - 1]
            if f['face'] and f['stable']:
                c['hr_app'].append(float(f['hr'])); c['hr_ref'].append(o['pulse_rate'])
                c['spo2_app'].append(float(f['spo2'])); c['spo2_ref'].append(o['spo2'])
        start = {f['frame']: f['t_start'] for f in F}
        for m, name in MODULES.items():
            cum = self.cumulative[m]
            c['module_' + name] = [1e3 * (cum[n] - cum[n - 1]) for n in sorted(cum)
                                   if n - 1 in cum and n - 1 in start]
        return c


def pool(parts):
    p = {}
    for k, v in parts[0].items():
        p[k] = sum((x[k] for x in parts), [] if isinstance(v, list) else 0)
    return p


def q(x, p):
    return float(np.percentile(np.asarray(x, float), p)) if len(x) else float('nan')


def stats(c, all_phases=False):
    d = c['seconds']
    iv = np.array(c['interval'])
    s = {'seconds': d, 'iterations_per_s': c['iterations'] / d, 'new_frames_per_s': c['new'] / d,
         'camera_fps': c['camera'] / d, 'repeated_pct': 100 * (1 - c['new'] / c['iterations']),
         'not_processed_pct': 100 * c['not_processed'] / c['camera'],
         'interval_mean': iv.mean(), 'interval_sd': iv.std(), 'interval_max': iv.max(),
         'interval_gt50_pct': 100 * (iv > 50).mean(), 'interval_gt50_n': int((iv > 50).sum()),
         'work_median': q(c['work'], 50), 'work_p99': q(c['work'], 99), 'work_max': max(c['work']),
         'lat_gui_median': q(c['lat'], 50), 'lat_gui_p95': q(c['lat'], 95), 'lat_gui_p99': q(c['lat'], 99),
         'lat_gui_max': max(c['lat']), 'driver_to_read_median': q(c['driver_to_read'], 50),
         'read_to_start_median': q(c['read_to_start'], 50), 'start_to_gui_median': q(c['start_to_gui'], 50),
         'face_pct': 100 * np.mean(c['face'])}
    R = c['resources']
    for k in ('cpu_pct', 'cpu_main_pct', 'cpu_camera_pct', 'cpu_network_pct', 'cpu_workers_pct', 'cpu_other_pct',
              'system_cpu_pct', 'cpu_mhz'):
        s[k] = float(np.mean([float(r[k]) for r in R]))
    s['rss_mb_max'] = max(float(r['rss_kb']) for r in R) / 1024
    s['rss_mb_min'] = min(float(r['rss_kb']) for r in R) / 1024
    s['temp_max'] = max(float(r['temp_c']) for r in R if r['temp_c'])
    if c['api_n']:
        al = c['api']
        s.update(api_n=c['api_n'], api_errors=c['api_n'] - len(al), api_median=q(al, 50), api_p99=q(al, 99),
                 api_max=max(al))
    for key in ('main', 'pulse'):
        if len(c['enc_' + key]) > 0.5 * c['iterations']:
            s['enc_%s_median' % key] = q(c['enc_' + key], 50)
            s['enc_%s_p99' % key] = q(c['enc_' + key], 99)
            s['jpeg_%s_kb' % key] = float(np.mean(c['jpeg_' + key])) / 1024
    if c['stream_lat']:
        s.update(stream_images_per_s=None if all_phases else float(np.mean(c['client_rates'])),
                 stream_mbit_s=None if all_phases else 8 * c['stream_bytes'] / d / 1e6,
                 stream_lat_median=q(c['stream_lat'], 50), stream_lat_p99=q(c['stream_lat'], 99),
                 stream_delivery_median=q(c['stream_delivery'], 50), stream_delivery_p99=q(c['stream_delivery'], 99))
    hr_a, hr_r = c['hr_app'], c['hr_ref']
    e = np.array(hr_a) - np.array(hr_r)
    es = np.array(c['spo2_app']) - np.array(c['spo2_ref'])
    s.update(pairs=len(e), hr_mae=np.abs(e).mean(), hr_bias=e.mean(), hr_sd=e.std(), hr_rmse=np.sqrt((e ** 2).mean()),
             hr_loa=[round(float(e.mean() - 1.96 * e.std(ddof=1)), 3), round(float(e.mean() + 1.96 * e.std(ddof=1)), 3)],
             hr_r=float(np.corrcoef(hr_a, hr_r)[0, 1]), hr_within3=100 * (np.abs(e) <= 3).mean(),
             hr_within5=100 * (np.abs(e) <= 5).mean(), hr_within10=100 * (np.abs(e) <= 10).mean(),
             hr_ref_range=[min(hr_r), max(hr_r)], hr_app_range=[min(hr_a), max(hr_a)],
             spo2_mae=np.abs(es).mean(), spo2_bias=es.mean(),
             spo2_ref_range=[min(c['spo2_ref']), max(c['spo2_ref'])])
    modules = {m[7:]: float(np.mean(c[m])) for m in c if m.startswith('module_') and c[m]}
    modules['total'] = sum(modules.values())
    s['modules_ms'] = {k: round(v, 4) for k, v in modules.items()}
    return {k: (round(float(v), 4) if isinstance(v, (float, np.floating)) else v) for k, v in s.items()}


def per_minute(session):
    t0, t1 = session.phases[0][1], session.phases[-1][2]
    return [sum(1 for t in session.ft if m <= t < m + 60000000) / 60 for m in range(t0, t1 - 59999999, 60000000)]


def table(sessions):
    """Numbers of each phase, pooled over the sessions that include it, and of all the phases together."""
    names = []
    for s in sessions:
        names += [n for n, _, _ in s.phases if n not in names]
    out = {}
    for name in names:
        out[name] = stats(pool([s.collect(t0, t1) for s in sessions for n, t0, t1 in s.phases if n == name]))
    out['all'] = stats(pool([s.collect(s.phases[0][1], s.phases[-1][2]) for s in sessions]), all_phases=True)
    rates = sum((per_minute(s) for s in sessions), [])
    out['per_minute_iterations'] = [round(min(rates), 4), round(max(rates), 4), len(rates)]
    return out


folder = Path(sys.argv[1])
runs = [folder] if (folder / 'info.json').exists() else sorted(p for p in folder.iterdir() if (p / 'info.json').exists())
sessions = [Session(r) for r in runs]
out = table(sessions)
if len(sessions) > 1:
    out['sessions'] = {s.name: table([s]) for s in sessions}
print(json.dumps(out, indent=1))
(folder / 'table_numbers.json').write_text(json.dumps(out, indent=1))
