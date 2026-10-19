"""Figure 4 of the paper: agreement of the HR of the application with the pulse oximeter during live operation.

(a) HR of the application, pulse rate displayed by the oximeter and beat-to-beat pulse rate obtained from the pulse
    wave of the oximeter (see response_delay.py), in the first 300 s of the third session on the laptop.
(b) Bland-Altman plot of all the paired values (one per second) of the five sessions, with the bias and the 95% limits
    of agreement of each computer; the pairs are those of make_table.py.

Requires numpy, scipy and matplotlib.

Usage:  python3 data/live_evaluation/plot_agreement.py data/live_evaluation figure.pdf
"""
import bisect
import json
import sys
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from response_delay import beat_to_beat, load  # noqa: E402


def phases(run):
    info = json.loads((run / 'info.json').read_text())
    return info['phases'][0]['t_start'], info['phases'][-1]['t_end']


def pairs(run):
    """HR of the application and pulse rate of the oximeter, each oximeter value with the last processed frame."""
    t0, t1 = phases(run)
    frames = load(run, 'frames.csv')
    ft = [int(f['t_start']) for f in frames]
    app, ref = [], []
    for o in load(run, 'oximeter.csv'):
        t = int(o['t'])
        if not (t0 <= t < t1) or o['finger'] != '1':
            continue
        f = frames[bisect.bisect_right(ft, t) - 1]
        if f['face'] == '1' and f['stable'] == '1':
            app.append(float(f['hr']))
            ref.append(float(o['pulse_rate']))
    return np.array(app), np.array(ref)


def main():
    base, out = Path(sys.argv[1]), sys.argv[2]
    plt.rcParams.update({'font.family': 'serif', 'font.serif': ['DejaVu Serif'], 'mathtext.fontset': 'dejavuserif',
                         'font.size': 7.5, 'axes.linewidth': 0.6, 'xtick.major.width': 0.6,
                         'ytick.major.width': 0.6, 'legend.frameon': False})
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(3.4, 4.6), gridspec_kw={'height_ratios': [1, 1.15]})

    run = base / 'laptop' / 'session3'
    t0, t1 = (t / 1e6 for t in phases(run))
    frames = load(run, 'frames.csv')
    ft = np.array([int(f['t_start']) for f in frames]) / 1e6
    hr = np.array([float(f['hr']) for f in frames])
    oxi = load(run, 'oximeter.csv')
    ot = np.array([int(o['t']) for o in oxi]) / 1e6
    pr = np.array([float(o['pulse_rate']) for o in oxi])
    tb, beats, _ = beat_to_beat(run, t0, t1)
    grid = np.arange(np.ceil(t0), np.ceil(t0) + 300)
    ax1.plot(tb - t0, beats, color='0.65', lw=0.6, label='Beat-to-beat')
    ax1.step(grid - t0, pr[np.searchsorted(ot, grid, side='right') - 1], where='post', color='#1f5fa8', lw=1.1,
             label='Oximeter')
    ax1.step(grid - t0, hr[np.searchsorted(ft, grid, side='right') - 1], where='post', color='#c8102e', lw=1.1,
             label='Application')
    ax1.set_xlim(0, 300)
    ax1.set_ylim(56, 98)
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('HR (bpm)')
    ax1.legend(loc='upper center', ncol=3, fontsize=6.5, handlelength=1.5, columnspacing=1.2, borderaxespad=0.3)
    ax1.set_title('(a)', loc='left', fontsize=8)
    ax1.grid(alpha=0.25, lw=0.4)

    limit = 20
    hidden = 0
    for name, folder, color, marker in (('Laptop', 'laptop', '#6a3d9a', 'o'), ('Desktop', 'desktop', '#2a9d55', '^')):
        runs = [base / folder] if (base / folder / 'info.json').exists() else \
            sorted(p for p in (base / folder).iterdir() if (p / 'info.json').exists())
        app, ref = (np.concatenate(x) for x in zip(*[pairs(r) for r in runs]))
        diff, mean = app - ref, (app + ref) / 2
        hidden += int((np.abs(diff) > limit).sum())
        bias, sd = diff.mean(), diff.std(ddof=1)
        ax2.scatter(mean, np.clip(diff, -limit, limit), s=2.5, color=color, marker=marker, alpha=0.25, lw=0,
                    rasterized=True, label='{} ({:,} pairs)'.format(name, len(diff)))
        for y, style in ((bias, '-'), (bias - 1.96 * sd, '--'), (bias + 1.96 * sd, '--')):
            ax2.axhline(y, color=color, ls=style, lw=0.8)
    ax2.set_xlim(55, 95)
    ax2.set_ylim(-limit, limit)
    ax2.set_xlabel('Mean of the application and the oximeter (bpm)')
    ax2.set_ylabel('Application $-$ oximeter (bpm)')
    leg = ax2.legend(loc='upper right', fontsize=6.5, markerscale=3, handletextpad=0.2, borderaxespad=0.2)
    for h in leg.legend_handles:
        h.set_alpha(1)
    ax2.set_title('(b)', loc='left', fontsize=8)
    ax2.grid(alpha=0.25, lw=0.4)
    fig.tight_layout(h_pad=0.8)
    fig.savefig(out, dpi=300)
    print('%s written; %d pairs outside +-%d bpm drawn at the border' % (out, hidden, limit))


if __name__ == '__main__':
    main()
