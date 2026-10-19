//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <dlib/opencv.h>
#include <dlib/image_processing/frontal_face_detector.h>

#define LANDMARKPOINTS 68


#ifndef DATAOBJECTCLASS_H
#define DATAOBJECTCLASS_H


class DataObjectClass
{
public:
    DataObjectClass();


    int nbr_of_frame;

    cv::Mat original_rgb_frame;
    cv::Mat crop_rgb_frame;


    std::vector<cv::Point> landmarks_rgb;

    // RT Signal values
    float hr_signal_value;
    float br_signal_value;

    float hr_value;
    float br_value;
    float SpO2;

    int faceActive;


    ~DataObjectClass();



};

#endif // DATAOBJECTCLASS_H
