#ifndef CAMERAS_H
#define CAMERAS_H


//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/core/utility.hpp"
#include <opencv2/imgproc/imgproc.hpp>
#include <mutex>
#include <cstdio>

#include "process.h"

class cameras
{


public:

    int CameraON;

    std::mutex mCam;

    cv::Mat aux0, aux1;

    //process proc;
    int count;
    int n_of_RGBFrames;

    int newRGBframe;
    cv::Mat RGBframe;
    cv::Mat originalRGB;

    cv::VideoCapture cap;
    //Video file input (environment variable RT_RPPG_VIDEO), for tests without a camera
    bool fromFile = false;
    double fileFps = 30.0;
    bool loopFile = true;       //RT_RPPG_VIDEO_LOOP=0: the application closes at the end of the file instead of restarting it
    //Manual exposure and gain (environment variables RT_RPPG_EXPOSURE and RT_RPPG_GAIN), restored when the camera thread ends
    bool manualExposure = false;
    double previousGain = -1;
    static void toAppFrame(const cv::Mat &in, cv::Mat &out);
    //Optional timing log (environment variable RT_RPPG_TIMING, see timinglog.h): times of the latest frame (aux1),
    //and number and times of the frame returned by getRGBframe
    std::FILE *timingLog = nullptr;
    bool driverTimestamps = false;
    long long frameDriverUs = -1, frameReadUs = 0;
    int usedSeq = 0;
    long long usedDriverUs = -1, usedReadUs = 0;



    void initRGBCam();



    cv::Mat getRGBframe();




    void camRead();

    std::thread camReadThread()
          {
           return std::thread([=] {camRead(); });
          }


    int RGBisNew();

    void camlock()
    {
        mCam.lock();
    }

    void camunlock()
    {
        mCam.unlock();
    }

};

#endif // CAMERAS_H
