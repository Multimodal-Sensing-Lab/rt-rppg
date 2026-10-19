#ifndef FACELANDMARKS_H
#define FACELANDMARKS_H

//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <dlib/opencv.h>
#include <dlib/image_processing/frontal_face_detector.h>

#define LANDMARKPOINTS 68


class FaceLandmarks
{
public:
    FaceLandmarks();

    int badPositioning;

    std::vector<cv::Point> get68LandMarks( dlib::full_object_detection &shape);
    void get82Landmarks2(cv::Mat &image, std::vector<cv::Point> *LLpoints_wExtras);
    cv::Mat swapfacesfunc(cv::Mat &image_a, cv::Mat &image_b, std::vector<cv::Point> *LLpoints_a, std::vector<cv::Point> *LLpoints_b, std::vector<int> points);
    cv::Mat getNormalizedFace122(cv::Mat &image, std::vector<cv::Point> * landmarks);
    cv::Mat getNormalizedFace17(cv::Mat &image, std::vector<cv::Point> * landmarks);
    cv::Mat drawSimpleMesh17(cv::Mat image, std::vector<cv::Point> *landmarkpoints);
    cv::Rect getRectCheck(std::vector <cv::Point2f> tri, cv::Mat img);

    ~FaceLandmarks();

};

#endif // FACELANDMARKS_H
