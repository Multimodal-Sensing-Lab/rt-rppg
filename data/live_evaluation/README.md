# Measurements of the live evaluation

Measurements of the live evaluation of the complete system reported in the paper (Section 5.3.5, Table 6 and Figure 4), recorded with `tools/live_eval.py` and the timing log of the application (`RT_RPPG_TIMING`, see `src/timinglog.h`). No images or video are included: only times, sizes, CPU and memory samples, the HR, RR and SpO2 values, and the values and pulse wave of the pulse oximeter.

The same participant (an author of the paper) sat at rest in front of the camera in all the sessions, with a few deep breaths so that the HR varied slightly, wearing a Contec CMS50E fingertip pulse oximeter connected through USB (`tools/cms50e.py`). The camera was a Trust USB Camera, a low-cost consumer webcam, 1280×720 MJPEG at 30 fps through V4L2, with a fixed exposure of 33 ms (`RT_RPPG_EXPOSURE=330`) and a fixed gain adjusted to the lighting of each session (`RT_RPPG_GAIN`, in `info.json`). The network clients ran on the same computer as the application.

The sessions were recorded with the code of this repository as of 18-19 October 2026. The released version only differs in its console output and window titles: the debug messages of the original code, which that version still printed (they appear in `app.log.gz`), are commented out, except the processing times of the modules, and the windows are titled Face2PPG-RT instead of VisualHealth. The processing is the same.

## Laptop: `laptop/session1`, `session2`, `session3` (19 October 2026)

- **Computer:** the laptop used for the processing times of the paper (Table 5): Intel Core i7-6700HQ (4 cores, 8 hardware threads), Ubuntu 18.04.6, on AC power. The application was built with `rt-rppg.pro` (g++ 7.5) and the libraries of the original application (Qt 5.13.0, and OpenCV 4.5.5 and dlib 19.23 built from source).
- **Lighting:** daylight; gain 48, 32 and 48 in sessions 1, 2 and 3.
- **Phases:** session 1 as desktop session 1 below (22 minutes with network load); sessions 2 and 3, 600 s without network clients (`idle`). All after a warm-up of 30 s (not evaluated).
- Between 243 and 284 s after the start of the evaluated phases of session 1, other processes increased the CPU load of the laptop (all its hardware threads for about 5 s, see `system_cpu_pct` in `resources.csv`); five camera frames were lost and the latency increased up to 250 ms.

## Desktop computer: `desktop/session1` (18 October 2026), `desktop/session2` (19 October 2026)

- **Computer:** AMD Ryzen 7 3700X (8 cores, 16 hardware threads), 64 GB of RAM, Ubuntu 24.04. The application was built with `rt-rppg.pro` and the packages of the distribution (Qt 5.15.13, OpenCV 4.6.0, dlib 19.24).
- **Lighting:** session 1 in the evening, indoor lighting, gain 128; session 2 with daylight, gain 48.
- **Phases:** session 1: a warm-up of 30 s (not evaluated); then 600 s without network clients (`idle`); 180 s of requests to the RESTful API at 10 per second (`api`); and 180 s each with the API requests plus 1, 4 and 8 clients of the MJPEG streams (`stream1`, `stream4`, `stream8`). The clients ran on the same computer. Session 2: a warm-up of 30 s and 600 s without network clients.

## Files

All the times are microseconds of the monotonic clock of the computer (`CLOCK_MONOTONIC`), so they can be compared between files.

| File | One row per | Columns |
|---|---|---|
| `frames.csv` | iteration of the main loop | `frame` iteration number; `cam_seq` camera frame used; `t_driver` V4L2 timestamp of that frame; `t_read` time at which the decoded frame was available; `t_start`, `t_frame`, `t_processed`, `t_gui`, `t_end` start of the iteration and end of the frame retrieval, processing, GUI image and network step; `face`, `stable` flags; `hr`, `rr`, `spo2` displayed values; `enc_main_us`, `jpeg_main_bytes`, `enc_pulse_us`, `jpeg_pulse_bytes` encoding time and size of the JPEG image of each stream (0 without clients) |
| `camera.csv` | captured frame | `cam_seq`, `t_driver`, `t_read` |
| `api.csv` | API request | `phase`; `t_send`, `t_done` request and complete response; `status`; `bytes`; returned values; `server_time_ms`, `client_time_ms` wall-clock times (ms) of the values and of the response |
| `streams.csv` | image received by a stream client | `phase`, `client`, `stream`, `frame` (iteration number, `X-Frame` header), `t_recv`, `bytes` |
| `resources.csv` | second | `phase`; CPU of the application in % of one core, in total and by thread group (`main`: processing loop, GUI and JPEG encoding; `camera`; `network`; `workers`: threads created by the libraries, such as the OpenCV worker threads; `other`); `rss_kb`, `hwm_kb` memory; `system_cpu_pct` (all programs, % of all hardware threads); `cpu_mhz`; `temp_c`; `script_cpu_pct` (measurement script and clients); GPU use of the whole computer |
| `oximeter.csv` | oximeter value (1 per second) | `phase`, `pulse_rate` (bpm), `spo2` (%), `finger` (0 when no finger is detected) |
| `oximeter_wave.csv` | pulse wave sample (60 per second) | `wave` amplitude (0–127), `bar` pulse bar, `finger` |
| `app.log.gz` | — | output of the application, including the cumulative face detection, landmark and normalization times printed at every frame |
| `info.json` | — | settings, computer, camera and the start and end of each phase (`t_start`, `t_end`) |
| `summary.txt`, `summary.json` | — | summary of each phase by `tools/live_eval.py` |
| `table_numbers.json` | — | in `laptop/` and `desktop/`: numbers of Table 6 and of the text, by `make_table.py`, pooled over the sessions of the computer and for each session |
| `response_delay.json` | — | in `laptop/` and `desktop/`: delay of the HR of the application and of the pulse rate of the oximeter with respect to the beat-to-beat pulse rate, and their rises at the increases of the beat-to-beat pulse rate of at least 8 bpm, by `response_delay.py` |

## Reproducing the summary and Table 6

```bash
python3 tools/live_eval.py summary data/live_evaluation/laptop/session1     # Python standard library only; each session
python3 data/live_evaluation/make_table.py data/live_evaluation/laptop        # requires numpy; pools the sessions
python3 data/live_evaluation/make_table.py data/live_evaluation/desktop
python3 data/live_evaluation/response_delay.py data/live_evaluation/laptop    # requires numpy and scipy
python3 data/live_evaluation/response_delay.py data/live_evaluation/desktop
python3 data/live_evaluation/plot_agreement.py data/live_evaluation figure4.pdf      # requires matplotlib
```

`make_table.py` also gives the mean time per frame of the core modules (face detection, face alignment, face normalization, skin segmentation, RGB to BVP, filtering and spectral analysis), from the cumulative times that the application prints for every frame (`app.log.gz`).

`make_table.py` also gives the 95% limits of agreement of Bland and Altman of the HR (`hr_loa`).

`response_delay.py` obtains the beat-to-beat pulse rate from the peaks of the pulse wave of the oximeter, samples it and the displayed values every second, removes their slow changes (moving average of 60 s), and gives the lag of maximum correlation of the HR of the application and of the pulse rate of the oximeter with respect to it (positive: delayed), and the lag of the SpO2 of the application with respect to that of the oximeter. It also finds the increases of the beat-to-beat pulse rate (5-s moving average) of at least 8 bpm and gives, for each one, the rise of the beat-to-beat rate, of the HR of the application and of the pulse rate of the oximeter (maximum from 5 s before to 15 s after the peak, minus the median from 40 to 10 s before it).

`plot_agreement.py` draws Figure 4 of the paper: the HR of the application, the pulse rate of the oximeter and the beat-to-beat pulse rate in the first 300 s of `laptop/session3`, and the Bland–Altman plot of all the paired values.
