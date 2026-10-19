#!/usr/bin/env python3
"""Runs the rt-rppg application on the videos of the public datasets used in the paper, as pre-recorded input.

Each video replaces the camera (RT_RPPG_VIDEO: its frames are delivered at the frame rate of the file, see the README)
and is played once (RT_RPPG_VIDEO_LOOP=0), with the timing log of the application (RT_RPPG_TIMING) and without the
network interface. The HR, RR and SpO2 shown by the application are written once per second of video, next to the
reference HR of the dataset, and the HR of the application is compared with it.

Datasets (folder layout of the public releases; ROOT is the folder of the dataset or of one subset):
  cohface   COHFACE: ROOT/<subject>/<session>/data.avi; the reference HR is computed from the pulse signal of
            data.hdf5, as the frequency of its spectral peak over 10-s windows (needs numpy, and h5py or PyTables)
  ubfc1     UBFC-RPPG, DATASET_1: ROOT/<subject>/vid.avi, reference HR in gtdump.xmp
  ubfc2     UBFC-RPPG, DATASET_2: ROOT/<subject>/vid.avi, reference HR in ground_truth.txt
  lgi-ppgi  LGI-PPGI: ROOT/<id>/<name>/<name>_<scenario>/cv_camera_sensor_stream_handler.avi, reference HR in
            cms50_stream_handler.xml (60 values per second)

Output folder (--out):
  <video>/frames.csv, camera.csv  timing log of the application (see src/timinglog.h)
  <video>/app.log                 output of the application
  <video>/hr.csv                  one row per second of video: HR, RR and SpO2 of the application (last processed
                                  frame), its face and stability flags, and the reference HR
  summary.csv                     one row per video: duration, seconds with a stable HR of the application, and MAE
                                  and bias of that HR with respect to the reference

The application processes the central 480x360 region of 1280x720 frames, and the frames of these datasets (640x480)
are scaled to a height of 720 pixels, so the face must be in the central part of the image, as with a camera. The HR
of the application is computed from the last 10 s of signal, its first stable values appear after about 10 s, and it
follows the changes of HR with a delay of a few seconds. These results are therefore not those of the accuracy
evaluation of the paper (Table 2), which was performed offline with a Python implementation of the processing chain
and the evaluation protocol described in the paper.

The videos are processed in real time, so a dataset takes as long as its videos. Finished videos are skipped when the
command is run again (--redo processes them again).

Usage:
  python3 tools/run_datasets.py DATASET ROOT [--out FOLDER] [--app PATH] [--max-videos N] [--seconds S] [--show]
"""
import argparse
import bisect
import csv
import math
import os
import re
import signal
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIDEO_FILES = {'cohface': 'data.avi', 'ubfc1': 'vid.avi', 'ubfc2': 'vid.avi',
               'lgi-ppgi': 'cv_camera_sensor_stream_handler.avi'}
FIRST_FRAME = 4     # the camera thread reads four frames before the first one it delivers (src/cameras.cpp)


def natural_key(path):
    return [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', path)]


def find_videos(dataset, root):
    name = VIDEO_FILES[dataset]
    videos = [os.path.join(folder, name) for folder, _, files in os.walk(root) if name in files]
    return sorted(videos, key=natural_key)


def video_id(root, video):
    rel = os.path.relpath(os.path.dirname(video), root)
    return 'video' if rel == '.' else rel.replace(os.sep, '_')


# Reference HR of each dataset: list of (time in s from the start of the video, HR in bpm)

def reference_ubfc1(folder):
    """gtdump.xmp: one row per sample with time (ms), HR, SpO2 and PPG."""
    with open(os.path.join(folder, 'gtdump.xmp'), newline='') as f:
        return [(float(r[0]) / 1000, float(r[1])) for r in csv.reader(f) if len(r) >= 2 and float(r[1]) > 0]


def reference_ubfc2(folder):
    """ground_truth.txt: three lines with the PPG signal, the HR and the time (s) of each sample."""
    with open(os.path.join(folder, 'ground_truth.txt')) as f:
        lines = [l.split() for l in f.read().splitlines() if l.strip()]
    return [(float(t), float(h)) for h, t in zip(lines[1], lines[2]) if float(h) > 0]


def reference_lgi(folder):
    """cms50_stream_handler.xml: one entry per sample (60 per second) with the sample number, the pulse rate of the
    oximeter and its pulse wave."""
    root = ElementTree.parse(os.path.join(folder, 'cms50_stream_handler.xml')).getroot()
    out = []
    for item in root.iter():
        fields = list(item)
        if len(fields) == 3 and all(len(list(x)) == 0 for x in fields):
            index, rate = int(fields[0].text), float(fields[1].text)
            if rate > 0:
                out.append((index / 60.0, rate))
    return out


def reference_cohface(folder):
    """data.hdf5: pulse signal of the contact sensor and its time (s). The HR is the frequency of the spectral peak
    (0.7-3 Hz) of the pulse signal over 10-s windows centered on each second."""
    try:
        import numpy as np
    except ImportError:
        print('  numpy is needed for the reference HR of COHFACE; it is left empty')
        return []
    path = os.path.join(folder, 'data.hdf5')
    try:
        import h5py
        with h5py.File(path, 'r') as f:
            pulse, t = np.asarray(f['pulse'][:], float), np.asarray(f['time'][:], float)
    except ImportError:
        try:
            import tables
        except ImportError:
            print('  h5py or PyTables is needed for the reference HR of COHFACE; it is left empty')
            return []
        with tables.open_file(path) as f:
            pulse, t = np.asarray(f.root.pulse.read(), float), np.asarray(f.root.time.read(), float)
    fs = 1.0 / np.median(np.diff(t))
    out = []
    for s in range(5, int(t[-1] - 5) + 1):
        x = pulse[(t >= s - 5) & (t < s + 5)]
        if len(x) < 8 * fs:
            continue
        x = (x - x.mean()) * np.hanning(len(x))
        n = 8 * len(x)
        power = np.abs(np.fft.rfft(x, n)) ** 2
        freq = np.fft.rfftfreq(n, 1 / fs)
        band = (freq >= 0.7) & (freq <= 3.0)
        out.append((float(s), float(60 * freq[band][np.argmax(power[band])])))
    return out


REFERENCES = {'cohface': reference_cohface, 'ubfc1': reference_ubfc1, 'ubfc2': reference_ubfc2,
              'lgi-ppgi': reference_lgi}


def interpolate(series, t):
    """Value of a list of (time, value) at time t (linear), or None outside the series."""
    if not series or t < series[0][0] or t > series[-1][0]:
        return None
    i = bisect.bisect_left(series, (t, -math.inf))
    if series[i][0] == t or i == 0:
        return series[i][1]
    (t0, v0), (t1, v1) = series[i - 1], series[i]
    return v0 + (v1 - v0) * (t - t0) / (t1 - t0)


def default_app():
    for path in (os.path.join(REPO, 'rt_rppg'), os.path.join(REPO, 'build', 'rt_rppg')):
        if os.path.isfile(path):
            return path
    return os.path.join(REPO, 'build', 'rt_rppg')


def run_video(app, video, out, seconds, show):
    """Runs the application on one video, once, and returns the frame rate of the file."""
    env = dict(os.environ, RT_RPPG_VIDEO=video, RT_RPPG_VIDEO_LOOP='0', RT_RPPG_TIMING=out, RT_RPPG_NETWORK='0')
    if not show:
        env.setdefault('QT_QPA_PLATFORM', 'offscreen')
    log_path = os.path.join(out, 'app.log')
    with open(log_path, 'w') as log:
        # the application finds models/ and log.conf in its own folder
        proc = subprocess.Popen([app], cwd=os.path.dirname(app), env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            proc.wait(timeout=seconds if seconds else 4 * 3600)
        except subprocess.TimeoutExpired:
            proc.send_signal(signal.SIGTERM)        # same shutdown as a key press
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
        except KeyboardInterrupt:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=30)
            raise
    with open(log_path, errors='replace') as log:
        m = re.search(r'Video file input:.* at ([0-9.]+) fps', log.read())
    if not m:
        raise RuntimeError('the application did not open the video, see ' + log_path)
    return float(m.group(1))


def read_frames(path):
    rows = []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            try:
                rows.append((int(r['cam_seq']), int(r['face']), int(r['stable']), float(r['hr']), float(r['rr']),
                             float(r['spo2'])))
            except (ValueError, TypeError, KeyError):
                continue                            # incomplete last row
    return rows


def per_second(frames, fps, reference):
    """One row per second of video: values of the last frame processed up to that time, and the reference HR."""
    times = [(seq - 1 + FIRST_FRAME) / fps for seq, *_ in frames]
    rows = []
    for s in range(1, int(times[-1]) + 1):
        i = bisect.bisect_right(times, s) - 1
        if i < 0:
            continue
        _, face, stable, hr, rr, spo2 = frames[i]
        ref = interpolate(reference, s)
        rows.append([s, round(hr, 1), round(rr, 1), round(spo2, 1), face, stable,
                     '' if ref is None else round(ref, 1)])
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('dataset', choices=sorted(VIDEO_FILES))
    parser.add_argument('root', help='folder of the dataset')
    parser.add_argument('--out', help='output folder (default: dataset_runs/<dataset>)')
    parser.add_argument('--app', help='rt_rppg executable (default: rt_rppg or build/rt_rppg of the repository)')
    parser.add_argument('--max-videos', type=int, help='process only the first N videos')
    parser.add_argument('--seconds', type=float, help='process only the first S seconds of each video (tests)')
    parser.add_argument('--show', action='store_true', help='show the window of the application')
    parser.add_argument('--redo', action='store_true', help='process again the videos already processed')
    args = parser.parse_args()

    app = os.path.abspath(args.app or default_app())
    if not os.path.isfile(app):
        sys.exit('Application not found: %s (build it first, see the README, or use --app)' % app)
    out_root = os.path.abspath(args.out or os.path.join('dataset_runs', args.dataset))
    videos = find_videos(args.dataset, args.root)[:args.max_videos]
    if not videos:
        sys.exit('No %s files found in %s' % (VIDEO_FILES[args.dataset], args.root))
    print('%d videos of %s; output in %s' % (len(videos), args.dataset, out_root))

    summary = []
    for k, video in enumerate(videos, 1):
        name = video_id(args.root, video)
        out = os.path.join(out_root, name)
        hr_path = os.path.join(out, 'hr.csv')
        if os.path.isfile(hr_path) and not args.redo:
            print('[%d/%d] %s: already processed' % (k, len(videos), name))
        else:
            os.makedirs(out, exist_ok=True)
            print('[%d/%d] %s ...' % (k, len(videos), name), end='', flush=True)
            start = time.time()
            fps = run_video(app, video, out, args.seconds, args.show)
            frames = read_frames(os.path.join(out, 'frames.csv'))
            if not frames:
                print(' no frames processed, see %s' % os.path.join(out, 'app.log'))
                continue
            rows = per_second(frames, fps, REFERENCES[args.dataset](os.path.dirname(video)))
            with open(hr_path, 'w', newline='') as f:
                w = csv.writer(f)
                w.writerow(['t_s', 'hr', 'rr', 'spo2', 'face', 'stable', 'reference_hr'])
                w.writerows(rows)
            print(' %.0f s of video at %.1f fps in %.0f s' % (rows[-1][0] if rows else 0, fps, time.time() - start))
        with open(hr_path, newline='') as f:
            rows = list(csv.DictReader(f))
        pairs = [(float(r['hr']), float(r['reference_hr'])) for r in rows
                 if r['face'] == '1' and r['stable'] == '1' and float(r['hr']) > 0 and r['reference_hr'] != '']
        d = [a - b for a, b in pairs]
        summary.append([name, len(rows), sum(1 for r in rows if r['stable'] == '1' and r['face'] == '1'), len(d),
                        round(statistics.mean(abs(x) for x in d), 2) if d else '',
                        round(statistics.mean(d), 2) if d else ''])
    with open(os.path.join(out_root, 'summary.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['video', 'seconds', 'stable_seconds', 'paired_seconds', 'hr_mae_bpm', 'hr_bias_bpm'])
        w.writerows(summary)
    maes = [r[4] for r in summary if r[4] != '']
    if maes:
        print('HR of the application vs. reference: mean of the per-video MAE %.2f bpm over %d videos (%s)'
              % (statistics.mean(maes), len(maes), os.path.join(out_root, 'summary.csv')))


if __name__ == '__main__':
    main()
