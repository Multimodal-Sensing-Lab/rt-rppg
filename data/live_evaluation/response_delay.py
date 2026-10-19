"""Delay with which the HR of the application and the pulse rate displayed by the oximeter follow the changes of the
pulse rate, from the measurements of tools/live_eval.py (Section 5.3.5 of the paper).

Reference: the beat-to-beat pulse rate obtained from the pulse wave of the oximeter (oximeter_wave.csv, about 60 samples
per second), which is not averaged. The pulse wave is band-pass filtered (0.6-3.5 Hz), its peaks are detected, and the
intervals between consecutive peaks (0.33-1.5 s, within 25% of the median of the 9 surrounding intervals) give the
beat-to-beat rate. The HR of the application (last processed frame), the pulse rate of the oximeter and the beat-to-beat
rate are sampled every second over the evaluated phases, their slow changes are removed (moving average of 60 s), and
the delay of each displayed value is the lag (in steps of 1 s, up to 20 s) of maximum correlation with the beat-to-beat
rate. The same is done for the SpO2 of the application with respect to that of the oximeter (up to 30 s).

Increases of the pulse rate: the beat-to-beat rate is smoothed (centred moving average of 5 s) and its peaks, at least
20 s apart, are found. For each peak, the rise of each series is its maximum from 5 s before to 15 s after the peak
minus its median from 40 to 10 s before the peak; the peaks with a rise of the beat-to-beat rate of at least 8 bpm are
listed with the rises of the HR of the application and of the pulse rate displayed by the oximeter.

FOLDER is a run folder of tools/live_eval.py, or a folder with several run folders. Requires numpy and scipy.

Usage:  python3 data/live_evaluation/response_delay.py data/live_evaluation/laptop
The results of each session are printed and written to response_delay.json in FOLDER.
"""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from scipy.signal import butter, filtfilt, find_peaks


def load(run, name):
    with open(run / name, newline='') as f:
        return list(csv.DictReader(f))


def beat_to_beat(run, t0, t1):
    """Times (s) and beat-to-beat pulse rate (bpm) from the pulse wave of the oximeter."""
    rows = load(run, 'oximeter_wave.csv')
    t = np.array([int(r['t']) for r in rows], float) / 1e6
    rate = len(t) / (t[-1] - t[0])
    ts = t.copy()                       # the samples of a report are spread back from the time it was received
    i = 0
    while i < len(t):
        j = i
        while j + 1 < len(t) and t[j + 1] == t[i]:
            j += 1
        ts[i:j + 1] = t[i] - (j - i - np.arange(j - i + 1)) / rate
        i = j + 1
    wave = np.array([float(r['wave']) for r in rows])
    finger = np.array([int(r['finger']) for r in rows], float)
    fs = 60.0
    grid = np.arange(max(ts[0], t0 - 20), min(ts[-1], t1 + 5), 1 / fs)
    x = np.interp(grid, ts, wave)
    b, a = butter(3, [0.6 / (fs / 2), 3.5 / (fs / 2)], 'band')
    y = filtfilt(b, a, x - x.mean())
    peaks, _ = find_peaks(y, distance=int(0.33 * fs), prominence=0.6 * np.percentile(np.abs(y), 75))
    peaks = peaks[np.interp(grid[peaks], ts, finger) > 0.5]
    tb = grid[peaks]
    ibi = np.diff(tb)
    median = np.array([np.median(ibi[max(0, k - 4):k + 5]) for k in range(len(ibi))])
    ok = (ibi > 0.33) & (ibi < 1.5) & (np.abs(ibi - median) < 0.25 * median)
    return tb[1:][ok], 60 / ibi[ok], float(ok.mean())


def highpass(x, n=60):
    pad = np.pad(x, (n // 2, n - n // 2 - 1), mode='edge')
    return x - np.convolve(pad, np.ones(n) / n, 'valid')


def delay(ref, x, max_lag):
    """Lag (s) and correlation of the best match x(t) ~ ref(t - lag); a positive lag means that x is delayed."""
    best = (0, -2.0)
    for lag in range(-max_lag, max_lag + 1):
        a, b = (ref[:len(ref) - lag], x[lag:]) if lag >= 0 else (ref[-lag:], x[:len(x) + lag])
        r = float(np.corrcoef(a, b)[0, 1])
        if r > best[1]:
            best = (lag, r)
    return best


def increases(beats, app, oximeter, minimum=8.0):
    """Rises (bpm) of the beat-to-beat rate, the application and the oximeter at the large increases of the rate."""
    ref = np.convolve(np.pad(beats, 2, mode='edge'), np.ones(5) / 5, 'valid')
    peaks, _ = find_peaks(ref, distance=20)
    events = []
    for p in peaks:
        if p < 40 or p + 15 >= len(ref):
            continue
        rise = [float(x[p - 5:p + 16].max() - np.median(x[p - 40:p - 10])) for x in (ref, app, oximeter)]
        if rise[0] >= minimum:
            events.append([int(p)] + [round(r, 2) for r in rise])
    return events


def session(run):
    info = json.loads((run / 'info.json').read_text())
    t0, t1 = info['phases'][0]['t_start'] / 1e6, info['phases'][-1]['t_end'] / 1e6
    frames = load(run, 'frames.csv')
    ft = np.array([int(f['t_start']) for f in frames]) / 1e6
    oxi = load(run, 'oximeter.csv')
    ot = np.array([int(o['t']) for o in oxi]) / 1e6
    tb, beats, kept = beat_to_beat(run, t0, t1)
    grid = np.arange(np.ceil(t0), np.floor(t1))
    last = np.searchsorted(ft, grid, side='right') - 1
    app_hr = np.array([float(frames[i]['hr']) for i in last])
    app_spo2 = np.array([float(frames[i]['spo2']) for i in last])
    valid = np.array([frames[i]['face'] == '1' and frames[i]['stable'] == '1' for i in last])
    last = np.searchsorted(ot, grid, side='right') - 1
    oxi_hr = np.array([float(oxi[i]['pulse_rate']) for i in last])
    oxi_spo2 = np.array([float(oxi[i]['spo2']) for i in last])
    valid &= np.array([oxi[i]['finger'] == '1' for i in last])
    beats_1s = np.interp(grid, tb, beats)
    ref = highpass(beats_1s)
    app, oximeter = highpass(app_hr), highpass(oxi_hr)
    out = {'seconds': len(grid), 'valid_pct': 100 * float(valid.mean()), 'beats': len(tb),
           'beats_kept_pct': 100 * kept}
    out['hr_app_delay_s'], out['hr_app_r'] = delay(ref, app, 20)
    out['hr_oximeter_delay_s'], out['hr_oximeter_r'] = delay(ref, oximeter, 20)
    out['hr_app_vs_oximeter_delay_s'], out['hr_app_vs_oximeter_r'] = delay(oximeter, app, 20)
    out['spo2_app_vs_oximeter_delay_s'], out['spo2_app_vs_oximeter_r'] = delay(highpass(oxi_spo2),
                                                                                highpass(app_spo2), 30)
    out['sd_beat_to_beat_bpm'] = float(ref.std())
    out['sd_app_bpm'], out['sd_oximeter_bpm'] = float(app.std()), float(oximeter.std())
    out['increases'] = {'columns': ['second', 'beat_to_beat_rise', 'app_rise', 'oximeter_rise'],
                        'events': increases(beats_1s, app_hr, oxi_hr)}
    return {k: (round(v, 3) if isinstance(v, float) else v) for k, v in out.items()}


if __name__ == '__main__':
    folder = Path(sys.argv[1])
    runs = [folder] if (folder / 'info.json').exists() else sorted(p for p in folder.iterdir()
                                                                   if (p / 'info.json').exists())
    result = {r.name: session(r) for r in runs}
    print(json.dumps(result, indent=1))
    (folder / 'response_delay.json').write_text(json.dumps(result, indent=1))
