#ifndef PROCESS_H
#define PROCESS_H

//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
//#include <opencv2/video/tracking.hpp>
//#include <opencv2/calib3d/calib3d.hpp>
//#include "opencv2/features2d/features2d.hpp"
#include "opencv2/core/utility.hpp"
#include <opencv2/imgproc/imgproc.hpp>


#include <dlib/opencv.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing.h>
//#include <dlib/image_keypoint/draw_surf_points.h>
#include <dlib/image_io.h>
#include <dlib/image_keypoint.h>

#include "healthsignals.h"
#include "facelandmarks.h"
#include "filters.h"
#include "mtfilterstabilizer.h"

#define FACE_DOWNSAMPLE_RATIO 2
#define SKIP_FRAMES 3
#define LANDMARKS 68

class process
{


public:

    process();
    ~process();

    //Counters, time measurement, etc...
    int countRGB;

    int FaceActive;
    double total_time_fd;
    double total_time_fl;
    double total_time_fn;
    int64 time0;
    int64 time1;
    int64 time2;

    healthsignals sig;
    FaceLandmarks lan;
    MTFilterStabilizer estabilizer;

    bool control_init;

    // DLIB FACE DETECTOR
//    dlib::frontal_face_detector detector;
    cv::Ptr<cv::FaceDetectorYN> detector;

    // LANDMARK MODEL LOADING
    dlib::shape_predictor pose_model;

    //Landmarks
    dlib::full_object_detection shapeIn;
    std::vector<cv::Point> RGBlandmarks;

    //RESULTS (Faces, landmarks)
    std::vector<dlib::rectangle> faces;
    cv::Mat faces_temp;

    void initProcess();
    void detectFacesRGB(cv::Mat frame);

    void selectNormalFaceRGB(cv::Mat img);

};


#endif // PROCESS_H
