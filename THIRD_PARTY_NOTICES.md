# Third-party notices

The following files are not covered by the license of this repository (PolyForm Noncommercial License 1.0.0, see `LICENSE`), or include code from other authors. Their original license headers are kept in the files.

| Files | Origin | License |
|---|---|---|
| `src/easylogging++.h`, `src/easylogging++.cc` | Easylogging++, Copyright (c) 2017 muflihun.com | MIT |
| `src/plot.h`, `src/plot.cpp`, `src/precomp.hpp` | Plot module for OpenCV (Nuno Moutinho), OpenCV license header | BSD 3-Clause |
| `src/filters.h`, `src/filters.cpp` | FIR filter class by Mike Perkins, Copyright (c) 2013 Cardinal Peak, LLC | BSD 3-Clause |
| `models/face_detection_yunet_2022mar_int8.onnx` | YuNet face detection model, OpenCV Zoo | MIT |
| `models/shape_predictor_68_face_landmarks_GTX.dat.bz2` | Facial landmark model (dlib shape predictor format) trained by the authors, see C. Álvarez Casado and M. Bordallo López, "Real-time face alignment: evaluation methods, training strategies and implementation optimization", *Journal of Real-Time Image Processing*, 2021 | See the note below |

Note on the facial landmark model: its training data include the iBUG 300-W dataset, whose license excludes commercial use, so the model is provided for research and other non-commercial uses only, and it cannot be used in commercial products. It can be replaced by another 68-point model in the dlib shape predictor format (see the README).

The application links to the following libraries, which are not included in this repository:

- Qt 5 (LGPL-3.0).
- OpenCV (Apache 2.0 for version 4.5 and newer).
- dlib (Boost Software License 1.0).
