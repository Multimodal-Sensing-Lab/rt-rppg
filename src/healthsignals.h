#ifndef HEALTHSIGNALS_H
#define HEALTHSIGNALS_H

#include <vector>
#include <deque>
#include <thread>
#include <mutex>


#include <dlib/image_keypoint.h>
//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/core/utility.hpp"
#include <opencv2/imgproc/imgproc.hpp>

#include "filters.h"
#include "normal.h"


#define FACE_DOWNSAMPLE_RATIO 2
#define SKIP_FRAMES 3
#define LANDMARKS 68

class healthsignals
{


public:

    //CLASS MEMBERS

    int sigStable; int userReset;

    int seconds;
    double currentFps;

    double current_ppgB;

    double last_pulse;
    double last_breath;

    //Normalized face
    cv::Mat normalFace;
    cv::Mat eulerian;

    //Time
    std::deque<double> timestamps;

    //Breathing
    std::deque<double> raw_breathing;

    std::deque<double> filtered_resp;

    //BreathingRate to be forwarded
    float breathingRate;

    //Heart PPG in Lab Space a channel
    std::deque<double> raw_ppgN;

    //Heart PPG in RGB Space a channel
    std::deque<double> raw_ppgR;
    std::deque<double> raw_ppgG;
    std::deque<double> raw_ppgB;

    //Heart signal filtered
    std::deque<double> filtered_pulseN;

    //Red and green filtered in the heart band (pulsatile components used for SpO2)
    std::deque<double> filtered_ppgR;
    std::deque<double> filtered_ppgG;


    //HeartRate and SpO2 to be forwarded
    float heartRate;
    float SpO2;

    //Counters
    int signalCounter;

    double total_time_fseg;
    double total_time_filt;
    double total_time_rPPG;
    double total_time_Spect;
    int64 time0;
    int64 time1;
    int64 time2;

    //Filters
    Filter *filterDB, *filterDG, *filterDR;

    Filter *filterN;  //Filter for the HR

    Filter *filterBreath; //Filter for the BR

    Filter *filterNowHR;  //Instant filters
    Filter *filterNowBR;  //Instant filters


    //METHODS

    healthsignals();
    ~healthsignals();

    void initSignals();
    void resetValues();
    void addSignals();
    void calculateSignals();

    cv::Mat EulerianMagnify(cv::Mat frameinput);


    void heart(); void heart0();
    void breath(); void breath0();
    void spo2();

    void time();

    void reset();

};

#endif // SIGNALS_H
