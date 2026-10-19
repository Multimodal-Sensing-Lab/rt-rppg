#!/usr/bin/env python3
"""Live evaluation of the complete rt-rppg system: camera, processing, GUI and network interface.

Starts the application with its timing log (RT_RPPG_TIMING, see src/timinglog.h), runs a sequence of load phases with
network clients on the same computer, samples the CPU, memory and temperature once per second, and writes everything
to a run folder. Only the Python standard library is used (Python 3.5 or newer), so no packages or internet access are
needed.

Phases (--phases, comma-separated name:seconds, run in order after a warm-up that is not evaluated):
  idle      no network clients
  api       the RESTful API is requested --api-hz times per second (default 10)
  streamN   the API as in 'api', plus N clients of the MJPEG streams (half /mainstream, half /pulsestream)

Run folder:
  info.json      computer, camera, settings, and start and end of each phase (monotonic clock, microseconds)
  frames.csv     one row per processed frame (written by the application)
  camera.csv     one row per captured frame (written by the application)
  api.csv        one row per API request
  streams.csv    one row per image received by the stream clients
  resources.csv  one row per second: CPU use of the application by thread, memory, system CPU, frequency, temperature
  oximeter.csv   pulse rate and SpO2 of a CMS50E pulse oximeter, once per second (if connected, see tools/cms50e.py)
  oximeter_wave.csv  its pulse wave, 60 samples per second
  app.log.gz     output of the application
  summary.txt    summary per phase (summary.json: the same values)

All the times are microseconds of the monotonic clock (CLOCK_MONOTONIC), shared by the application, the camera driver
(V4L2 buffer timestamps) and this script, so the latency from the camera driver to the stream clients can be computed,
and each oximeter value can be compared with the values shown by the application at the same time.

Usage:
  python3 tools/live_eval.py run [--app PATH] [--out FOLDER] [--warmup 30]
                                 [--phases idle:600,api:180,stream1:180,stream4:180,stream8:180]
                                 [--env RT_RPPG_EXPOSURE=330 --env RT_RPPG_GAIN=128] [--video FILE]
                                 [--oximeter auto|none|/dev/hidrawN]
  python3 tools/live_eval.py summary FOLDER

The application window must not receive key presses during the run: any key closes the application.
"""
import argparse
import csv
import gzip
import http.client
import bisect
import json
import math
import os
import platform
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import threading
import time

VERSION = '2026-10-18'
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cms50e   # noqa: E402 (tools/cms50e.py)


def mono_us():
    return int(time.monotonic() * 1e6)


def read_text(path, default=''):
    try:
        with open(path) as f:
            return f.read()
    except (OSError, UnicodeDecodeError):
        return default


def run_command(args, timeout=10):
    """Output of a command, or None if it is not available or fails."""
    if shutil.which(args[0]) is None:
        return None
    try:
        r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                           universal_newlines=True)
        return r.stdout
    except (OSError, subprocess.SubprocessError):
        return None


# ---------------------------------------------------------------------------------------------------------------------
# Computer information and resource sampling

def system_info(app):
    info = {'python': sys.version.split()[0], 'platform': platform.platform(), 'kernel': platform.release(),
            'logical_cpus': os.cpu_count()}
    models = [l.split(':', 1)[1].strip() for l in read_text('/proc/cpuinfo').splitlines() if l.startswith('model name')]
    info['cpu_model'] = models[0] if models else platform.processor()
    for line in read_text('/proc/meminfo').splitlines():
        if line.startswith('MemTotal:'):
            info['mem_total_kb'] = int(line.split()[1])
    for line in read_text('/etc/os-release').splitlines():
        if line.startswith('PRETTY_NAME='):
            info['os'] = line.split('=', 1)[1].strip('"')
    info['cpu_governor'] = read_text('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor').strip()
    info['no_turbo'] = read_text('/sys/devices/system/cpu/intel_pstate/no_turbo').strip()
    supplies = {}
    base = '/sys/class/power_supply'
    for name in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        online = read_text(os.path.join(base, name, 'online')).strip()
        status = read_text(os.path.join(base, name, 'status')).strip()
        supplies[name] = online or status
    info['power_supply'] = supplies
    info['display'] = os.environ.get('DISPLAY', '') or os.environ.get('WAYLAND_DISPLAY', '')
    info['session_type'] = os.environ.get('XDG_SESSION_TYPE', '')
    info['app_libraries'] = run_command(['ldd', app])
    info['nvidia_smi'] = run_command(['nvidia-smi', '-L'])
    return info


def find_temperature_file():
    """CPU package temperature (x86_pkg_temp or coretemp), or the first thermal zone."""
    zones = '/sys/class/thermal'
    candidates = []
    for z in sorted(os.listdir(zones)) if os.path.isdir(zones) else []:
        kind = read_text(os.path.join(zones, z, 'type')).strip()
        path = os.path.join(zones, z, 'temp')
        if kind == 'x86_pkg_temp':
            return path, kind
        candidates.append((path, kind))
    hwmon = '/sys/class/hwmon'
    for h in sorted(os.listdir(hwmon)) if os.path.isdir(hwmon) else []:
        name = read_text(os.path.join(hwmon, h, 'name')).strip()
        if name in ('coretemp', 'k10temp', 'zenpower'):
            return os.path.join(hwmon, h, 'temp1_input'), name
    return candidates[0] if candidates else (None, None)


def proc_ticks(stat_text):
    """utime + stime of a /proc/.../stat line, and the fields after the command name."""
    fields = stat_text[stat_text.rfind(')') + 2:].split()
    return int(fields[11]) + int(fields[12]), fields


class Monitor(threading.Thread):
    """Samples the CPU time of the application by thread group, its memory, the system CPU use, the mean CPU frequency
    and the CPU temperature once per second; the GPU use every 5 s if nvidia-smi is available."""

    GROUPS = ('main', 'camera', 'network', 'workers', 'other')

    def __init__(self, pid, path, state):
        threading.Thread.__init__(self, daemon=True)
        self.pid, self.path, self.state = pid, path, state
        self.stop_event = threading.Event()
        self.temperature_file, self.temperature_kind = find_temperature_file()
        self.nvidia = shutil.which('nvidia-smi') is not None

    def sample_threads(self):
        """CPU ticks of each thread, with its group: main (processing loop, GUI and JPEG encoding), camera, network,
        workers (threads with the name of the process, i.e. the threads created by the libraries, such as the OpenCV
        worker threads) and other (Qt, GTK, ...)."""
        stats = {}
        base = '/proc/%d/task' % self.pid
        for tid in os.listdir(base):
            text = read_text(os.path.join(base, tid, 'stat'))
            if text:
                stats[int(tid)] = (text[text.find('(') + 1:text.rfind(')')], proc_ticks(text)[0])
        process_name = stats[self.pid][0] if self.pid in stats else ''
        ticks = {}
        for tid, (comm, t) in stats.items():
            if tid == self.pid:
                g = 'main'
            elif comm == 'rt-camera':
                g = 'camera'
            elif comm.startswith('rt-net'):
                g = 'network'
            elif comm == process_name:
                g = 'workers'
            else:
                g = 'other'
            ticks[tid] = (g, t)
        return ticks

    @staticmethod
    def system_ticks():
        values = [int(v) for v in read_text('/proc/stat').splitlines()[0].split()[1:]]
        idle = values[3] + (values[4] if len(values) > 4 else 0)
        return sum(values[:8]), idle

    def run(self):
        hz = float(os.sysconf('SC_CLK_TCK'))
        header = ['t', 'phase', 'cpu_pct'] + ['cpu_%s_pct' % g for g in self.GROUPS] + \
                 ['rss_kb', 'hwm_kb', 'threads', 'system_cpu_pct', 'cpu_mhz', 'temp_c', 'script_cpu_pct',
                  'gpu_util_pct', 'gpu_mem_mb']
        with open(self.path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(header)
            prev = None
            next_t = time.monotonic()
            last_gpu = 0.0
            while not self.stop_event.is_set():
                try:
                    t = mono_us()
                    total = proc_ticks(read_text('/proc/%d/stat' % self.pid))[0]
                    threads = self.sample_threads()
                    script = proc_ticks(read_text('/proc/self/stat'))[0]
                    sys_total, sys_idle = self.system_ticks()
                except (OSError, IndexError, ValueError):
                    break           # the application has ended
                status = {}
                for line in read_text('/proc/%d/status' % self.pid).splitlines():
                    k, _, v = line.partition(':')
                    status[k] = v.split()[0] if v.split() else ''
                mhz = [float(l.split(':')[1]) for l in read_text('/proc/cpuinfo').splitlines() if l.startswith('cpu MHz')]
                temp = ''
                if self.temperature_file:
                    try:
                        temp = '%.1f' % (int(read_text(self.temperature_file).strip()) / 1000.0)
                    except ValueError:
                        pass
                gpu_util = gpu_mem = ''
                if self.nvidia and time.monotonic() - last_gpu >= 5:
                    last_gpu = time.monotonic()
                    out = run_command(['nvidia-smi', '--query-gpu=utilization.gpu,memory.used',
                                       '--format=csv,noheader,nounits'], timeout=5)
                    if out and ',' in out:
                        gpu_util, gpu_mem = [v.strip() for v in out.splitlines()[0].split(',')[:2]]
                if prev is not None:
                    dt = (t - prev['t']) / 1e6
                    pct = lambda ticks: '%.1f' % (100.0 * ticks / hz / dt)
                    groups = dict((g, 0) for g in self.GROUPS)
                    for tid, (g, ticks) in threads.items():
                        old = prev['threads'].get(tid)
                        groups[g] += ticks - (old[1] if old else 0)
                    # Threads that ended since the previous sample (e.g. API requests) are only in the total
                    groups['other'] = max(0, (total - prev['total']) - groups['main'] - groups['camera'] -
                                          groups['network'] - groups['workers'])
                    sys_dt = sys_total - prev['sys_total']
                    sys_pct = 100.0 * (1 - (sys_idle - prev['sys_idle']) / float(sys_dt)) if sys_dt > 0 else 0.0
                    w.writerow([t, self.state['phase'], pct(total - prev['total'])] +
                               [pct(groups[g]) for g in self.GROUPS] +
                               [status.get('VmRSS', ''), status.get('VmHWM', ''), status.get('Threads', ''),
                                '%.1f' % sys_pct, '%.0f' % (sum(mhz) / len(mhz)) if mhz else '', temp,
                                pct(script - prev['script']), gpu_util, gpu_mem])
                    f.flush()
                    self.state['cpu_pct'] = 100.0 * (total - prev['total']) / hz / dt
                prev = {'t': t, 'total': total, 'threads': threads, 'script': script, 'sys_total': sys_total,
                        'sys_idle': sys_idle}
                next_t += 1.0
                self.stop_event.wait(max(0.0, next_t - time.monotonic()))


# ---------------------------------------------------------------------------------------------------------------------
# Network clients

class ApiClient(threading.Thread):
    """Requests /vhealth.json at a fixed rate, each time with a new connection (the server closes it)."""

    def __init__(self, port, hz, writer, state):
        threading.Thread.__init__(self, daemon=True)
        self.port, self.period, self.writer, self.state = port, 1.0 / hz, writer, state
        self.stop_event = threading.Event()

    def run(self):
        next_t = time.monotonic()
        while not self.stop_event.is_set():
            t0 = mono_us()
            status, body, values = '', b'', {}
            try:
                c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=2)
                c.request('GET', '/vhealth.json')
                r = c.getresponse()
                body = r.read()
                status = r.status
                c.close()
                t1 = mono_us()
                values = json.loads(body.decode())
            except Exception as e:
                t1 = mono_us()
                status = type(e).__name__
            self.writer.writerow([self.state['phase'], t0, t1, status, len(body)] +
                                 [values.get(k, '') for k in ('hr', 'rr', 'spo2', 'user_present', 'stable',
                                                              'timestamp_ms')] + [int(time.time() * 1000)])
            next_t += self.period
            delay = next_t - time.monotonic()
            if delay < 0:                       # behind schedule: do not try to catch up
                next_t, delay = time.monotonic(), 0
            self.stop_event.wait(delay)


class StreamClient(threading.Thread):
    """Receives an MJPEG stream and records the arrival time, size and frame number (X-Frame) of each image."""

    def __init__(self, k, port, path, writer, lock, state):
        threading.Thread.__init__(self, daemon=True)
        self.k, self.port, self.path, self.writer, self.lock, self.state = k, port, path, writer, lock, state
        self.stop_event = threading.Event()
        self.sock = None
        self.error = ''

    def stop(self):
        self.stop_event.set()
        if self.sock is not None:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def run(self):
        try:
            self.sock = socket.create_connection(('127.0.0.1', self.port), timeout=10)
            self.sock.sendall(('GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n' % self.path).encode())
            f = self.sock.makefile('rb', 1 << 16)
            if b' 200 ' not in f.readline():
                raise IOError('no stream')
            while f.readline().strip():         # response headers
                pass
            while not self.stop_event.is_set():
                line = f.readline()
                if not line:
                    break
                if not line.startswith(b'--'):
                    continue
                length, frame = None, ''
                while True:
                    h = f.readline()
                    if not h:
                        raise EOFError
                    h = h.strip()
                    if not h:
                        break
                    name, _, value = h.partition(b':')
                    name = name.strip().lower()
                    if name == b'content-length':
                        length = int(value)
                    elif name == b'x-frame':
                        frame = int(value)
                if length is None:
                    continue
                data = f.read(length)
                t = mono_us()
                if len(data) < length:
                    break
                with self.lock:
                    self.writer.writerow([self.state['phase'], self.k, self.path.strip('/'), frame, t, length])
        except Exception as e:
            if not self.stop_event.is_set():
                self.error = '%s: %s' % (type(e).__name__, e)
        finally:
            if self.sock is not None:
                self.sock.close()


class FramesTail(object):
    """Reads the rows added to frames.csv since the previous call (for the progress messages)."""

    def __init__(self, path):
        self.path, self.pos, self.rest = path, 0, ''

    def rows(self):
        try:
            with open(self.path) as f:
                f.seek(self.pos)
                data = f.read()
                self.pos = f.tell()
        except OSError:
            return []
        lines = (self.rest + data).split('\n')
        self.rest = lines.pop()
        return [l.split(',') for l in lines if l and not l.startswith('frame')]


class OximeterLogger(threading.Thread):
    """Records the pulse rate, SpO2 and pulse wave of a CMS50E oximeter (tools/cms50e.py)."""

    def __init__(self, oximeter, out, state):
        threading.Thread.__init__(self, daemon=True)
        self.oximeter, self.out, self.state = oximeter, out, state
        self.stop_event = threading.Event()

    def run(self):
        with open(os.path.join(self.out, 'oximeter.csv'), 'w', newline='') as fm, \
                open(os.path.join(self.out, 'oximeter_wave.csv'), 'w', newline='') as fw:
            m, w = csv.writer(fm), csv.writer(fw)
            m.writerow(['t', 'phase', 'pulse_rate', 'spo2', 'finger'])
            w.writerow(['t', 'wave', 'bar', 'finger'])
            self.oximeter.start()
            try:
                while not self.stop_event.is_set():
                    for t, kind, a, b, finger in self.oximeter.read(0.5):
                        if kind == 'measurement':
                            m.writerow([t, self.state['phase'], a, b, finger])
                            fm.flush()
                            self.state['oximeter'] = (a, b, finger)
                        else:
                            w.writerow([t, a, b, finger])
            except OSError as e:
                self.state['oximeter_error'] = str(e)
            finally:
                self.oximeter.stop()


def progress(tail, label, state=None):
    """Progress message from the new rows of frames.csv, and the rate of new camera frames (None if unknown)."""
    rows = tail.rows()
    if len(rows) < 2:
        return '%s: no frames processed' % label, None
    try:
        t = [int(r[4]) for r in rows]
        dt = (t[-1] - t[0]) / 1e6 or 1.0
        new = len(set(r[1] for r in rows))
        face = sum(int(r[9]) for r in rows)
        stable = [r for r in rows if r[9] == '1' and r[10] == '1']
    except (IndexError, ValueError):
        return '%s: (incomplete rows)' % label, None
    hr = ('HR %s bpm' % stable[-1][11]) if stable else 'HR not stable yet'
    oxi = ''
    if state and state.get('oximeter'):
        pr, spo2, finger = state['oximeter']
        oxi = '; oximeter %d bpm, SpO2 %d %%' % (pr, spo2) if finger else '; oximeter: no finger'
    return '%s: %.1f frames/s processed, %.1f new camera frames/s, face in %d%% of the frames, %s%s' % (
        label, (len(rows) - 1) / dt, new / dt, round(100.0 * face / len(rows)), hr, oxi), new / dt


# ---------------------------------------------------------------------------------------------------------------------
# Run

def parse_phases(text):
    phases, seen = [], {}
    for item in text.split(','):
        kind, _, seconds = item.strip().partition(':')
        if not (kind in ('idle', 'api') or (kind.startswith('stream') and kind[6:].isdigit() and int(kind[6:]) > 0)):
            raise SystemExit('Unknown phase %r: use idle, api or streamN (N clients)' % kind)
        seen[kind] = seen.get(kind, 0) + 1
        name = kind if seen[kind] == 1 else '%s#%d' % (kind, seen[kind])
        phases.append({'name': name, 'kind': kind, 'seconds': float(seconds)})
    return phases


def api_ready(port):
    try:
        c = http.client.HTTPConnection('127.0.0.1', port, timeout=1)
        c.request('GET', '/vhealth.json')
        ok = c.getresponse().status == 200
        c.close()
        return ok
    except (OSError, http.client.HTTPException):
        return False


def default_app():
    for path in (os.path.join(REPO, 'rt_rppg'), os.path.join(REPO, 'build', 'rt_rppg')):
        if os.path.isfile(path):
            return path
    return os.path.join(REPO, 'rt_rppg')


def command_run(args):
    app = os.path.abspath(args.app or default_app())
    if not os.path.isfile(app):
        raise SystemExit('Application not found: %s (build it first, or give --app)' % app)
    out = os.path.abspath(args.out or os.path.join('live_runs', time.strftime('%Y%m%d-%H%M%S')))
    if os.path.exists(os.path.join(out, 'frames.csv')):
        raise SystemExit('%s already has a run; give another --out folder' % out)
    os.makedirs(out, exist_ok=True)
    phases = parse_phases(args.phases)

    env = dict(os.environ)
    env.pop('RT_RPPG_VIDEO', None)
    for item in args.env or []:
        name, _, value = item.partition('=')
        env[name] = value
    if args.video:
        env['RT_RPPG_VIDEO'] = os.path.abspath(args.video)
    env['RT_RPPG_TIMING'] = out
    env['RT_RPPG_NETWORK'] = '1'
    rest_port = int(env.get('RT_RPPG_REST_PORT', '8080'))
    stream_port = int(env.get('RT_RPPG_STREAM_PORT', '8081'))
    if api_ready(rest_port):
        raise SystemExit('Another program answers at port %d (is rt_rppg already running?)' % rest_port)

    info = {'script_version': VERSION, 'started': time.strftime('%Y-%m-%d %H:%M:%S %z'), 'argv': sys.argv,
            'app': app, 'settings': dict((k, v) for k, v in env.items() if k.startswith('RT_RPPG_')),
            'phases': phases, 'warmup_s': args.warmup, 'api_hz': args.api_hz, 'system': system_info(app)}
    if not info['system']['display']:
        print('Warning: no DISPLAY is set; the application needs a graphical session.')
    if any(v in ('0', 'Discharging') for k, v in info['system']['power_supply'].items() if k.startswith(('AC', 'ADP'))):
        print('Warning: the computer seems to run on battery; connect the power adapter.')

    state = {'phase': 'start', 'cpu_pct': None}
    oximeter_logger = None
    if args.oximeter != 'none':
        device = cms50e.find_device() if args.oximeter == 'auto' else args.oximeter
        if device is None:
            print('No CMS50E oximeter found; the run continues without reference values.')
        else:
            try:
                oximeter_logger = OximeterLogger(cms50e.CMS50E(device), out, state)
                print('Oximeter at %s: keep it on a finger of a hand that rests still.' % device)
            except OSError as e:
                print('Cannot open the oximeter at %s (%s); the run continues without it. %s'
                      % (device, e, cms50e.find_device.__doc__.split('. ', 1)[1].strip()))
    info['oximeter'] = oximeter_logger.oximeter.device if oximeter_logger else None
    log = open(os.path.join(out, 'app.log'), 'w')
    proc = subprocess.Popen([app], cwd=os.path.dirname(app), env=env, stdout=log, stderr=subprocess.STDOUT,
                            start_new_session=True)
    t_app = mono_us()
    print('Application started (pid %d); run folder %s' % (proc.pid, out))
    monitor = Monitor(proc.pid, os.path.join(out, 'resources.csv'), state)
    monitor.start()
    if oximeter_logger:
        oximeter_logger.start()
    tail = FramesTail(os.path.join(out, 'frames.csv'))
    api_file = open(os.path.join(out, 'api.csv'), 'w', newline='')
    api_writer = csv.writer(api_file)
    api_writer.writerow(['phase', 't_send', 't_done', 'status', 'bytes', 'hr', 'rr', 'spo2', 'user_present', 'stable',
                         'server_time_ms', 'client_time_ms'])
    streams_file = open(os.path.join(out, 'streams.csv'), 'w', newline='')
    streams_writer = csv.writer(streams_file)
    streams_writer.writerow(['phase', 'client', 'stream', 'frame', 't_recv', 'bytes'])
    streams_lock = threading.Lock()
    interrupted, errors = False, []

    def alive():
        return proc.poll() is None

    def wait(seconds, label):
        """Waits while the application runs, with a progress message every 30 s and at the end; returns the rate of
        new camera frames of the last message."""
        start = time.monotonic()
        end = start + seconds
        last = start
        rate = None
        while alive() and time.monotonic() < end:
            time.sleep(min(0.5, max(0.0, end - time.monotonic())))
            if time.monotonic() - last >= 30 or (time.monotonic() >= end and time.monotonic() - last >= 5):
                last = time.monotonic()
                message, rate = progress(tail, '%s %.0f/%.0f s' % (label, last - start, seconds), state)
                print('  ' + message)
        return rate

    try:
        t_wait = time.monotonic()
        while alive() and not api_ready(rest_port) and time.monotonic() - t_wait < 60:
            time.sleep(0.5)
        if not alive():
            raise RuntimeError('the application ended at the start (see app.log)')
        state['phase'] = 'warmup'
        print('Warm-up of %d s: sit still facing the camera, with the face inside the rectangle.' % args.warmup)
        tail.rows()
        rate = wait(args.warmup, 'warm-up')
        if not alive():
            raise RuntimeError('the application ended during the warm-up (see app.log)')
        if rate is not None and rate < 27 and not args.video:
            print('  Warning: the camera delivers %.1f frames/s. More light on the face, or a fixed exposure '
                  '(--env RT_RPPG_EXPOSURE=330 --env RT_RPPG_GAIN=128), gives 30 frames/s.' % rate)
        info['v4l2'] = run_command(['v4l2-ctl', '-d', '/dev/video0', '--all']) if not args.video else None
        # Whether the application is among the processes that use the NVIDIA GPU (if any); the list of the other
        # processes is not stored
        pmon = run_command(['nvidia-smi', 'pmon', '-c', '1'], timeout=15)
        if pmon is not None:
            info['app_uses_nvidia_gpu'] = any(l.split()[1:2] == [str(proc.pid)] for l in pmon.splitlines()
                                              if not l.startswith('#'))

        for i, ph in enumerate(phases):
            state['phase'] = ph['name']
            clients = []
            if ph['kind'] != 'idle':
                clients.append(ApiClient(rest_port, args.api_hz, api_writer, state))
            if ph['kind'].startswith('stream'):
                n = int(ph['kind'][6:])
                for k in range(n):
                    path = '/mainstream' if k % 2 == 0 else '/pulsestream'
                    clients.append(StreamClient(k, stream_port, path, streams_writer, streams_lock, state))
            ph['t_start'] = mono_us()
            ph['wall_start'] = time.strftime('%H:%M:%S')
            for c in clients:
                c.start()
            print('Phase %d/%d: %s, %d s' % (i + 1, len(phases), ph['name'], ph['seconds']))
            wait(ph['seconds'], ph['name'])
            ok = alive()
            ph['t_end'] = mono_us()
            for c in clients:
                c.stop() if isinstance(c, StreamClient) else c.stop_event.set()
            for c in clients:
                c.join(5)
                if getattr(c, 'error', ''):
                    errors.append('%s client %d: %s' % (ph['name'], c.k, c.error))
            api_file.flush()
            streams_file.flush()
            if not ok:
                raise RuntimeError('the application ended during phase %s (see app.log)' % ph['name'])
    except KeyboardInterrupt:
        interrupted = True
        print('Interrupted; stopping the application.')
        for ph in phases:                       # the interrupted phase ends here and is still summarized
            if 't_start' in ph and 't_end' not in ph:
                ph['t_end'] = mono_us()
                ph['interrupted'] = True
    except RuntimeError as e:
        errors.append(str(e))
        print('Error: %s' % e)
    finally:
        state['phase'] = 'stop'
        if alive():
            os.killpg(proc.pid, signal.SIGTERM)     # same shutdown as a key press (restores the camera)
            try:
                proc.wait(20)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        monitor.stop_event.set()
        monitor.join(5)
        if oximeter_logger:
            oximeter_logger.stop_event.set()
            oximeter_logger.join(5)
            if state.get('oximeter_error'):
                errors.append('oximeter: ' + state['oximeter_error'])
        log.close()
        api_file.close()
        streams_file.close()

    with open(os.path.join(out, 'app.log'), 'rb') as src, gzip.open(os.path.join(out, 'app.log.gz'), 'wb') as dst:
        shutil.copyfileobj(src, dst)
    os.remove(os.path.join(out, 'app.log'))
    info.update(t_app_start=t_app, t_app_end=mono_us(), exit_code=proc.returncode, interrupted=interrupted,
                errors=errors, temperature_sensor=monitor.temperature_kind)
    with open(os.path.join(out, 'info.json'), 'w') as f:
        json.dump(info, f, indent=1)
    for e in errors:
        print('Error: ' + e)
    print()
    print(command_summary(argparse.Namespace(folder=out)))


# ---------------------------------------------------------------------------------------------------------------------
# Summary

def quantile(sorted_values, q):
    if not sorted_values:
        return None
    k = (len(sorted_values) - 1) * q
    i = int(k)
    j = min(i + 1, len(sorted_values) - 1)
    return sorted_values[i] + (sorted_values[j] - sorted_values[i]) * (k - i)


def dist(values, scale=1.0):
    """n, mean, SD, min, median, p95, p99 and max, with the values multiplied by scale."""
    v = sorted(x * scale for x in values)
    if not v:
        return None
    return {'n': len(v), 'mean': statistics.mean(v), 'sd': statistics.pstdev(v), 'min': v[0],
            'p50': quantile(v, 0.5), 'p95': quantile(v, 0.95), 'p99': quantile(v, 0.99), 'max': v[-1]}


def read_csv(path):
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


def to_int(rows, keys):
    out = []
    for r in rows:
        try:
            for k in keys:
                r[k] = int(r[k])
        except (ValueError, TypeError, KeyError):
            continue                            # incomplete last row
        out.append(r)
    return out


def pearson(x, y):
    if len(x) < 3:
        return None
    mx, my = statistics.mean(x), statistics.mean(y)
    sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
    sxx = sum((a - mx) ** 2 for a in x)
    syy = sum((b - my) ** 2 for b in y)
    return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else None


def agreement(app, ref):
    """Agreement of the application values with the reference values (same length)."""
    if not app:
        return None
    d = [a - r for a, r in zip(app, ref)]
    return {'n': len(d), 'mae': statistics.mean(abs(x) for x in d), 'bias': statistics.mean(d),
            'sd': statistics.pstdev(d), 'rmse': math.sqrt(statistics.mean(x * x for x in d)),
            'pearson_r': pearson(app, ref), 'app_range': [min(app), max(app)], 'ref_range': [min(ref), max(ref)]}


def compare_oximeter(oximeter, frames, frame_times, t0, t1, lag_s=0):
    """Pairs each oximeter value (finger in) of [t0, t1) with the values shown by the application at the same time
    (the last processed frame, with a face and stable values). lag_s > 0 compares with the oximeter value lag_s
    seconds later."""
    hr_app, hr_ref, spo2_app, spo2_ref = [], [], [], []
    for o in oximeter:
        t_app = o['t'] - int(lag_s * 1e6)
        if not (t0 <= o['t'] < t1) or not o['finger'] or not (t0 <= t_app < t1):
            continue
        i = bisect.bisect_right(frame_times, t_app) - 1
        if i < 0 or not frames[i]['face'] or not frames[i]['stable'] or float(frames[i]['hr']) <= 0:
            continue
        hr_app.append(float(frames[i]['hr']))
        hr_ref.append(o['pulse_rate'])
        spo2_app.append(float(frames[i]['spo2']))
        spo2_ref.append(o['spo2'])
    return agreement(hr_app, hr_ref), agreement(spo2_app, spo2_ref)


def summarize_phase(name, t0, t1, frames, camera, api, streams, resources, capture_time, frame_end, processed):
    d = (t1 - t0) / 1e6
    s = {'phase': name, 'seconds': d}
    F = [f for f in frames if t0 <= f['t_start'] < t1]
    C = [c for c in camera if t0 <= c['t_read'] < t1]
    if F:
        new = [f for f in F if f['is_new'] and capture_time(f) > 0]
        s['processed_fps'] = len(F) / d
        s['new_frames_fps'] = len(new) / d
        s['repeated_pct'] = 100.0 * (len(F) - len(new)) / len(F)
        s['face_pct'] = 100.0 * sum(f['face'] for f in F) / len(F)
        s['stable_pct'] = 100.0 * sum(1 for f in F if f['face'] and f['stable']) / len(F)
        s['tick_interval_ms'] = dist([b['t_start'] - a['t_start'] for a, b in zip(F, F[1:])], 1e-3)
        s['late_ticks_pct'] = 100.0 * sum(1 for a, b in zip(F, F[1:]) if b['t_start'] - a['t_start'] > 50000) / len(F)
        s['loop_ms'] = {
            'frame': dist([f['t_frame'] - f['t_start'] for f in F], 1e-3),
            'processing': dist([f['t_processed'] - f['t_frame'] for f in F], 1e-3),
            'gui': dist([f['t_gui'] - f['t_processed'] for f in F], 1e-3),
            'network': dist([f['t_end'] - f['t_gui'] for f in F], 1e-3),
            'total': dist([f['t_end'] - f['t_start'] for f in F], 1e-3)}
        s['latency_ms'] = {
            'wait': dist([f['t_start'] - f['t_read'] for f in new], 1e-3),
            'to_processed': dist([f['t_processed'] - capture_time(f) for f in new], 1e-3),
            'to_gui': dist([f['t_gui'] - capture_time(f) for f in new], 1e-3),
            'to_end': dist([f['t_end'] - capture_time(f) for f in new], 1e-3)}
        s['driver_timestamps'] = any(f['t_driver'] > 0 for f in new)
        hr = [float(f['hr']) for f in F if f['face'] and f['stable']]
        s['hr_median'] = statistics.median(hr) if hr else None
        enc = {}
        for key in ('main', 'pulse'):
            E = [f for f in F if f['jpeg_%s_bytes' % key] > 0]
            if E:
                enc[key] = {'frames_pct': 100.0 * len(E) / len(F),
                            'encode_ms': dist([f['enc_%s_us' % key] for f in E], 1e-3),
                            'jpeg_kb': statistics.mean(f['jpeg_%s_bytes' % key] for f in E) / 1024.0}
        s['jpeg'] = enc
    if C:
        times = [c['t_driver'] if c['t_driver'] > 0 else c['t_read'] for c in C]
        intervals = [b - a for a, b in zip(times, times[1:])]
        s['camera_fps'] = len(C) / d
        s['camera_interval_ms'] = dist(intervals, 1e-3)
        med = quantile(sorted(intervals), 0.5) if intervals else 0
        s['camera_gaps'] = sum(1 for x in intervals if med and x > 1.5 * med)
        s['never_processed_pct'] = 100.0 * sum(1 for c in C if c['cam_seq'] not in processed) / len(C)
    R = [r for r in resources if r['phase'] == name]
    if R:
        def col(key):
            return [float(r[key]) for r in R if r.get(key, '') != '']
        s['cpu_pct'] = dist(col('cpu_pct'))
        s['cpu_groups_pct'] = dict((g, statistics.mean(col('cpu_%s_pct' % g))) for g in Monitor.GROUPS
                                   if col('cpu_%s_pct' % g))
        s['rss_mb'] = dist(col('rss_kb'), 1 / 1024.0)
        s['system_cpu_pct'] = statistics.mean(col('system_cpu_pct')) if col('system_cpu_pct') else None
        s['cpu_mhz'] = statistics.mean(col('cpu_mhz')) if col('cpu_mhz') else None
        s['temp_c_max'] = max(col('temp_c')) if col('temp_c') else None
        s['script_cpu_pct'] = statistics.mean(col('script_cpu_pct')) if col('script_cpu_pct') else None
        s['gpu_util_pct'] = statistics.mean(col('gpu_util_pct')) if col('gpu_util_pct') else None
    A = [a for a in api if a['phase'] == name]
    if A:
        ok = [a for a in A if a['status'] == '200']
        s['api'] = {'requests': len(A), 'errors': len(A) - len(ok), 'rate_hz': len(A) / d,
                    'latency_ms': dist([int(a['t_done']) - int(a['t_send']) for a in ok], 1e-3),
                    'value_age_ms': dist([int(a['client_time_ms']) - int(a['server_time_ms']) for a in ok
                                          if a['server_time_ms'] not in ('', '0')])}
    S = [r for r in streams if r['phase'] == name]
    if S:
        clients = sorted(set(r['client'] for r in S))
        per_client = [sum(1 for r in S if r['client'] == k) / d for k in clients]
        lat = [r['t_recv'] - capture_time(frame_end[r['frame']]) for r in S
               if r['frame'] in frame_end and capture_time(frame_end[r['frame']]) > 0]
        delivery = [r['t_recv'] - frame_end[r['frame']]['t_end'] for r in S if r['frame'] in frame_end]
        s['streams'] = {'clients': len(clients), 'images_fps_per_client': dist(per_client),
                        'mbit_s_total': 8.0 * sum(r['bytes'] for r in S) / d / 1e6,
                        'latency_ms': dist(lat, 1e-3), 'delivery_ms': dist(delivery, 1e-3)}
    return s


def fmt(dd, keys=('p50', 'p95', 'p99', 'max'), digits=1):
    if not dd:
        return '-'
    return ', '.join('%s %.*f' % (k if k != 'p50' else 'median', digits, dd[k]) for k in keys)


def phase_text(s):
    lines = ['%s (%.0f s%s)' % (s['phase'], s['seconds'], ', interrupted' if s.get('interrupted') else '')]
    if 'processed_fps' in s:
        lines.append('  Frames: %.2f/s processed, %.2f/s new camera frames (%.1f%% repeated); face %.0f%%, stable %.0f%%'
                     % (s['processed_fps'], s['new_frames_fps'], s['repeated_pct'], s['face_pct'], s['stable_pct']))
    if 'camera_fps' in s:
        lines.append('  Camera: %.2f frames/s, interval ms: mean %.2f, SD %.2f, max %.1f; gaps %d; '
                     'never processed %.1f%%' % (s['camera_fps'], s['camera_interval_ms']['mean'],
                                                 s['camera_interval_ms']['sd'], s['camera_interval_ms']['max'],
                                                 s['camera_gaps'], s['never_processed_pct']))
    if 'tick_interval_ms' in s:
        t = s['tick_interval_ms']
        lines.append('  Loop interval ms: mean %.2f, SD %.2f, p99 %.1f, max %.1f; > 50 ms: %.2f%%'
                     % (t['mean'], t['sd'], t['p99'], t['max'], s['late_ticks_pct']))
        for k in ('frame', 'processing', 'gui', 'network', 'total'):
            lines.append('  Loop %-10s ms: mean %.2f, %s' % (k, s['loop_ms'][k]['mean'], fmt(s['loop_ms'][k])))
        src = 'camera driver' if s['driver_timestamps'] else 'frame read'
        for k, label in (('wait', 'frame read -> loop start'), ('to_processed', src + ' -> processed'),
                         ('to_gui', src + ' -> GUI image'), ('to_end', src + ' -> values and JPEG ready')):
            lines.append('  Latency %-36s ms: %s' % (label, fmt(s['latency_ms'][k])))
        for key, e in sorted(s['jpeg'].items()):
            lines.append('  JPEG %-5s: %.0f%% of frames, %.1f kB, encoding ms: %s'
                         % (key, e['frames_pct'], e['jpeg_kb'], fmt(e['encode_ms'], digits=2)))
        if s['hr_median'] is not None:
            lines.append('  HR median (stable frames): %.1f bpm' % s['hr_median'])
    if 'cpu_pct' in s:
        groups = ', '.join('%s %.1f' % (g, s['cpu_groups_pct'][g]) for g in Monitor.GROUPS
                           if g in s['cpu_groups_pct'])
        lines.append('  CPU %% of one core: mean %.1f, p95 %.1f, max %.1f (%s); system %.1f%% of all cores'
                     % (s['cpu_pct']['mean'], s['cpu_pct']['p95'], s['cpu_pct']['max'], groups,
                        s['system_cpu_pct'] or 0))
        lines.append('  Memory (RSS) MB: mean %.1f, max %.1f; CPU %s MHz; max temperature %s C; script CPU %.1f%%%s'
                     % (s['rss_mb']['mean'], s['rss_mb']['max'],
                        '%.0f' % s['cpu_mhz'] if s['cpu_mhz'] else '-',
                        '%.0f' % s['temp_c_max'] if s['temp_c_max'] is not None else '-', s['script_cpu_pct'] or 0,
                        '; GPU %.0f%%' % s['gpu_util_pct'] if s['gpu_util_pct'] is not None else ''))
    if 'api' in s:
        a = s['api']
        lines.append('  API: %d requests (%.1f/s), %d errors; latency ms: %s; value age ms: %s'
                     % (a['requests'], a['rate_hz'], a['errors'], fmt(a['latency_ms'], digits=2),
                        fmt(a['value_age_ms'], keys=('p50', 'max'), digits=0)))
    if s.get('oximeter_hr'):
        h, o = s['oximeter_hr'], s.get('oximeter_spo2')
        lines.append('  Oximeter: %d paired values; HR MAE %.1f bpm, bias %+.1f, SD %.1f, r %s (app %.0f-%.0f, '
                     'oximeter %d-%d bpm)' % (h['n'], h['mae'], h['bias'], h['sd'],
                                              '%.2f' % h['pearson_r'] if h['pearson_r'] is not None else '-',
                                              h['app_range'][0], h['app_range'][1], h['ref_range'][0],
                                              h['ref_range'][1]))
        if o:
            lines.append('            SpO2 MAE %.1f %%, bias %+.1f (app %.1f-%.1f, oximeter %d-%d %%)'
                         % (o['mae'], o['bias'], o['app_range'][0], o['app_range'][1], o['ref_range'][0],
                            o['ref_range'][1]))
    if 'streams' in s:
        st = s['streams']
        lines.append('  Streams: %d clients, %.1f images/s per client (min %.1f), %.1f Mbit/s in total'
                     % (st['clients'], st['images_fps_per_client']['mean'], st['images_fps_per_client']['min'],
                        st['mbit_s_total']))
        lines.append('  Stream latency (capture -> received) ms: %s' % fmt(st['latency_ms']))
        lines.append('  Stream delivery (JPEG ready -> received) ms: %s' % fmt(st['delivery_ms'], digits=2))
    return '\n'.join(lines)


def command_summary(args):
    folder = args.folder
    with open(os.path.join(folder, 'info.json')) as f:
        info = json.load(f)
    frames = to_int(read_csv(os.path.join(folder, 'frames.csv')),
                    ('frame', 'cam_seq', 't_driver', 't_read', 't_start', 't_frame', 't_processed', 't_gui', 't_end',
                     'face', 'stable', 'enc_main_us', 'jpeg_main_bytes', 'enc_pulse_us', 'jpeg_pulse_bytes'))
    camera = to_int(read_csv(os.path.join(folder, 'camera.csv')), ('cam_seq', 't_driver', 't_read'))
    # A processed frame is new if its camera frame was not processed before (the loop always takes the latest
    # camera frame); camera frames that never reach the loop are replaced by a newer one before the next iteration
    previous = None
    for f in frames:
        f['is_new'] = f['cam_seq'] != previous
        previous = f['cam_seq']
    processed = set(f['cam_seq'] for f in frames)
    api = read_csv(os.path.join(folder, 'api.csv'))
    streams = to_int(read_csv(os.path.join(folder, 'streams.csv')), ('client', 'frame', 't_recv', 'bytes'))
    resources = read_csv(os.path.join(folder, 'resources.csv'))
    oximeter = to_int(read_csv(os.path.join(folder, 'oximeter.csv')), ('t', 'pulse_rate', 'spo2', 'finger'))
    frame_times = [f['t_start'] for f in frames]

    def capture_time(f):
        return f['t_driver'] if f['t_driver'] > 0 else f['t_read']

    frame_end = dict((f['frame'], f) for f in frames)
    phases = [p for p in info['phases'] if 't_start' in p and 't_end' in p]
    result = {'folder': folder, 'phases': []}
    sysinfo = info.get('system', {})
    text = ['Run %s (%s), %s, %s, %s' % (os.path.basename(os.path.normpath(folder)), info.get('started', ''),
                                         sysinfo.get('cpu_model', ''), sysinfo.get('os', ''),
                                         ' '.join('%s=%s' % kv for kv in sorted(info.get('settings', {}).items())
                                                  if kv[0] != 'RT_RPPG_TIMING'))]
    for p in phases:
        s = summarize_phase(p['name'], p['t_start'], p['t_end'], frames, camera, api, streams, resources,
                            capture_time, frame_end, processed)
        s['interrupted'] = bool(p.get('interrupted'))
        if oximeter:
            s['oximeter_hr'], s['oximeter_spo2'] = compare_oximeter(oximeter, frames, frame_times, p['t_start'],
                                                                    p['t_end'])
        result['phases'].append(s)
        text.append(phase_text(s))
    if phases:
        # Whole evaluated period: processing rate per minute, to show the stability over the extended operation
        t0, t1 = phases[0]['t_start'], phases[-1]['t_end']
        per_minute = []
        for m0 in range(t0, t1 - 60000000 + 1, 60000000):
            per_minute.append(sum(1 for f in frames if m0 <= f['t_start'] < m0 + 60000000) / 60.0)
        F = [f for f in frames if t0 <= f['t_start'] < t1]
        new = sum(1 for f in F if f['is_new'])
        overall = {'seconds': (t1 - t0) / 1e6, 'processed_fps': len(F) / ((t1 - t0) / 1e6),
                   'new_frames_fps': new / ((t1 - t0) / 1e6), 'per_minute_fps': dist(per_minute)}
        result['overall'] = overall
        pm = overall['per_minute_fps']
        text.append('All phases (%.0f s): %.2f frames/s processed, %.2f/s new camera frames; per minute: min %s, max %s'
                    % (overall['seconds'], overall['processed_fps'], overall['new_frames_fps'],
                       '%.2f' % pm['min'] if pm else '-', '%.2f' % pm['max'] if pm else '-'))
        if oximeter:
            hr, spo2 = compare_oximeter(oximeter, frames, frame_times, t0, t1)
            overall['oximeter_hr'], overall['oximeter_spo2'] = hr, spo2
            # The application shows the HR of the last 10 s and the oximeter averages a few beats: agreement when the
            # application value is compared with the oximeter value some seconds earlier or later (for information)
            overall['oximeter_hr_mae_by_lag_s'] = dict(
                (lag, (compare_oximeter(oximeter, frames, frame_times, t0, t1, lag)[0] or {}).get('mae'))
                for lag in range(-10, 11))
            if hr:
                text.append('All phases, oximeter: %d paired values; HR MAE %.1f bpm, bias %+.1f, SD %.1f, r %s'
                            '%s' % (hr['n'], hr['mae'], hr['bias'], hr['sd'],
                                    '%.2f' % hr['pearson_r'] if hr['pearson_r'] is not None else '-',
                                    '; SpO2 MAE %.1f %%, bias %+.1f' % (spo2['mae'], spo2['bias']) if spo2 else ''))
    text.append('Exit code %s%s' % (info.get('exit_code'), ', interrupted' if info.get('interrupted') else ''))
    for e in info.get('errors', []):
        text.append('Error: ' + e)
    out = '\n\n'.join(text)
    with open(os.path.join(folder, 'summary.txt'), 'w') as f:
        f.write(out + '\n')
    with open(os.path.join(folder, 'summary.json'), 'w') as f:
        json.dump(result, f, indent=1)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = parser.add_subparsers(dest='command')
    r = sub.add_parser('run', help='run the application with the load phases')
    r.add_argument('--app', help='rt_rppg executable (default: rt_rppg or build/rt_rppg of the repository)')
    r.add_argument('--out', help='run folder (default: live_runs/<date-time>)')
    r.add_argument('--phases', default='idle:600,api:180,stream1:180,stream4:180,stream8:180')
    r.add_argument('--warmup', type=float, default=30)
    r.add_argument('--api-hz', type=float, default=10)
    r.add_argument('--env', action='append', metavar='NAME=VALUE', help='environment variable of the application')
    r.add_argument('--video', help='video file instead of the camera (tests)')
    r.add_argument('--oximeter', default='auto', help='CMS50E oximeter: auto (default, used if connected), none, '
                                                      'or /dev/hidrawN')
    s = sub.add_parser('summary', help='summarize a run folder')
    s.add_argument('folder')
    args = parser.parse_args()
    if args.command == 'run':
        command_run(args)
    elif args.command == 'summary':
        print(command_summary(args))
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
