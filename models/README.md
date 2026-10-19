# Models

The application loads two models from this folder: the YuNet face detector and the ERT-GTX facial landmark model.

## Face detection: YuNet

`face_detection_yunet_2022mar_int8.onnx` is the int8 version of the YuNet face detector from the OpenCV Model Zoo:

- https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet

More recent versions of the model can be found in the [OpenCV Model Zoo](https://github.com/opencv/opencv_zoo). The original algorithm and its training code are in:

- https://github.com/ShiqiYu/libfacedetection.train

> W. Wu, H. Peng and S. Yu, "YuNet: A Tiny Millisecond-level Face Detector", *Machine Intelligence Research*, 2023. https://doi.org/10.1007/s11633-023-1423-y

The model is distributed by the OpenCV Model Zoo under the MIT license. The application runs it with the DNN module of OpenCV (`src/process.cpp`); see the main README for the optional GPU execution and another model file (`RT_RPPG_DNN`, `RT_RPPG_FACE_MODEL`).

## Facial landmarks: ERT-GTX

`shape_predictor_68_face_landmarks_GTX.dat.bz2` is a 68-point facial landmark model in the dlib shape predictor format (Ensemble of Regression Trees), trained by Constantino Álvarez Casado and Miguel Bordallo López. It is also distributed in the dlib model repository:

- https://github.com/davisking/dlib-models

The build decompresses it to `shape_predictor_68_face_landmarks_gtx.dat`, the file loaded by the application (`src/process.cpp`).

The GTX model is the result of applying a set of training strategies and implementation optimizations described in:

> C. Álvarez Casado and M. Bordallo López, "Real-time face alignment: evaluation methods, training strategies and implementation optimization", *Journal of Real-Time Image Processing*, 18, 2239–2267, 2021. https://doi.org/10.1007/s11554-021-01107-w

The resulting model is smaller, faster, smoother and more accurate. All the details related to its training and testing are in the following GitLab repository: https://gitlab.com/visualhealth/vhpapers/real-time-facealignment

It is trained on the iBUG 300-W dataset (https://ibug.doc.ic.ac.uk/resources/facial-point-annotations/):

> C. Sagonas, E. Antonakos, G. Tzimiropoulos, S. Zafeiriou and M. Pantic, "300 Faces In-The-Wild Challenge: database and results", *Image and Vision Computing*, 47, 3–18, 2016 (Special Issue on Facial Landmark Localisation "In-The-Wild"). https://doi.org/10.1016/j.imavis.2016.01.002

**License note.** The license of the 300-W dataset excludes commercial use. As noted in the dlib model repository, Stefanos Zafeiriou, one of the creators of the dataset, asked that the models trained on it carry a note saying that they cannot be used in a commercial product. Contact a lawyer or Imperial College London to find out whether you can use this model in a commercial product. For commercial applications, the model can be replaced by another 68-point model in the dlib format, trained on data that allow commercial use (see the main README).

**Face detector.** The model has an increased robustness to the face detector used. However, it works best when the bounding boxes are square, as with the HOG face detector of dlib or its CNN-based `mmod_human_face_detector.dat` face detector, and it does not work as well with face detectors that produce rectangular boxes. The application therefore makes the boxes of YuNet square before the face alignment (`src/process.cpp`).
