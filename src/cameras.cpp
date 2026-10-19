#include "cameras.h"
#include "timinglog.h"
#include <iostream>
#include "QtDebug"
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>

extern std::atomic<bool> g_stopRequested;     // main.cpp: closes the application as Ctrl+C does


//Converts a frame of another size to the 1280x720 frames expected by the application:
//scaled to a height of 720 pixels and centred horizontally (padded or cropped).
void cameras::toAppFrame(const cv::Mat &in, cv::Mat &out)
{
    if (in.empty() || (in.cols == 1280 && in.rows == 720)) { out = in; return; }
    cv::Mat scaled;
    double scale = 720.0 / in.rows;
    cv::resize(in, scaled, cv::Size((int)std::lround(in.cols * scale), 720));
    out = cv::Mat::zeros(cv::Size(1280, 720), in.type());
    int w = std::min(scaled.cols, 1280);
    scaled(cv::Rect((scaled.cols - w) / 2, 0, w, 720)).copyTo(out(cv::Rect((1280 - w) / 2, 0, w, 720)));
}


int cameras::RGBisNew()
{
    return newRGBframe;
}

void cameras::initRGBCam()
{

    count = 0;
    newRGBframe = 0;

    /// OPEN CAMERA OPENCV
    const char *video = std::getenv("RT_RPPG_VIDEO");   // optional video file instead of the camera (tests)
    if (video && *video) {
        fromFile = cap.open(video);
        double f = cap.get(cv::CAP_PROP_FPS);
        if (f > 1 && f < 121) fileFps = f;
        const char *loop = std::getenv("RT_RPPG_VIDEO_LOOP");
        loopFile = !(loop && std::strcmp(loop, "0") == 0);
        qDebug() << "Video file input:" << video << "at" << fileFps << "fps" << (loopFile ? "(loop)" : "(once)");
    } else {
#ifdef __linux__
        // V4L2 directly: with the default backend, some OpenCV builds (e.g. Ubuntu 24.04) use GStreamer,
        // which fails to start the camera with the settings below
        if (!cap.open(0, cv::CAP_V4L2)) cap.open(0);
#else
        cap.open(0);        // open the default camera
#endif
        // the processing expects 1280x720 frames; many webcams deliver 640x480 unless asked
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        cap.set(cv::CAP_PROP_FPS, 30);
    }
    if(!cap.isOpened()) {
        qDebug() << "OpenCV Camera Error";
        std::cerr << "Could not open " << (video && *video ? "the video file" : "the camera") << std::endl;
        std::exit(1);
    }else{
        qDebug() << " Camera Opened:" << cap.get(cv::CAP_PROP_FRAME_WIDTH);
        qDebug() << " Camera Opened:" << cap.get(cv::CAP_PROP_FRAME_HEIGHT);
        qDebug() << " Camera backend:" << cap.getBackendName().c_str();
    }

    // Timing log: with V4L2, the time of each frame in the driver (buffer timestamp, monotonic clock) is also written
    timingLog = timinglog::open("camera.csv", "cam_seq,t_driver,t_read");
    driverTimestamps = timingLog != nullptr && !fromFile && cap.getBackendName() == "V4L2";

    // check if we succeeded

//    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J','P','G'));
//    cap.set(cv::CAP_PROP_FPS, 30);
//    cap.set(cv::CAP_PROP_CONVERT_RGB, false);

//    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
//    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);

    //cap.set(CV_CAP_PROP_FRAME_WIDTH, 1920);
    //cap.set(CV_CAP_PROP_FRAME_HEIGHT,1080);


    cap >>originalRGB;
    cap >>aux0;
    cap >>aux1; //Fill original frame once
    if (originalRGB.cols != 1280 || originalRGB.rows != 720)
        qDebug() << "Frames of" << originalRGB.cols << "x" << originalRGB.rows << "are scaled and centred in 1280x720 frames";
    toAppFrame(originalRGB, originalRGB); toAppFrame(aux0, aux0); toAppFrame(aux1, aux1);

    // Optional manual exposure and gain, in the units of the camera driver (V4L2: exposure in 100-us steps).
    // With automatic exposure, many webcams lower their frame rate in low light; a fixed exposure of at most 33 ms keeps
    // 30 fps, and the gain compensates the brightness. Some cameras apply these settings only while streaming, so they
    // are set after the first frames.
    const char *exposure = std::getenv("RT_RPPG_EXPOSURE");
    const char *gain = std::getenv("RT_RPPG_GAIN");
    if (!fromFile && exposure && *exposure) {
        previousGain = cap.get(cv::CAP_PROP_GAIN);
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);     // V4L2 manual mode
        cap.set(cv::CAP_PROP_EXPOSURE, std::atof(exposure));
        if (gain && *gain) cap.set(cv::CAP_PROP_GAIN, std::atof(gain));
        manualExposure = true;
        qDebug() << "Manual exposure" << cap.get(cv::CAP_PROP_EXPOSURE) << "gain" << cap.get(cv::CAP_PROP_GAIN);
    }
//    cv::imwrite("hola1.jpg", aux0);
//    cv::imwrite("hola2.jpg", aux1);
    RGBframe = cv::Mat::zeros(cv::Size(384,288), CV_64FC1);

    cap >>aux1; //Fill originalframe once
    toAppFrame(aux1, aux1);



    double fps = cap.get(cv::CAP_PROP_FPS);
    qDebug() << "FPS " << fps;

    CameraON = 1;

}


void cameras::camRead()
{

    qDebug() << "CAM THREAD STARTED";
    timinglog::nameThread("rt-camera");
    int dropped_frames=0;
    n_of_RGBFrames = 0;
    auto fileStart = std::chrono::steady_clock::now();
    long fileFrames = 0;

    while (CameraON)
    {


    cap >> aux0;
    long long tDriver = driverTimestamps ? std::llround(1000.0 * cap.get(cv::CAP_PROP_POS_MSEC)) : -1;
    if (fromFile) {
        if (aux0.empty()) {                 // end of the file: start again, or close the application
            if (!loopFile) {
                qDebug() << "End of the video file";
                g_stopRequested = true;
                break;
            }
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            cap >> aux0;
        }
        // deliver the frames at the frame rate of the file, as a camera would
        fileFrames++;
        std::this_thread::sleep_until(fileStart + std::chrono::microseconds((long long)(1e6 * fileFrames / fileFps)));
    }

//    qDebug() << "Is empty:" << aux0.empty();
    toAppFrame(aux0, aux0);     // no change for 1280x720 frames
    long long tRead = timinglog::nowUs();
    camlock();

    if (newRGBframe == 1) {/*qDebug() << "lost RGB frame" << dropped_frames;*/ dropped_frames++;}

    aux1 = aux0.clone();
    n_of_RGBFrames++;
    newRGBframe=1;
    frameDriverUs = tDriver;
    frameReadUs = tRead;
    int seq = n_of_RGBFrames;

    camunlock();

    if (timingLog) {
        std::fprintf(timingLog, "%d,%lld,%lld\n", seq, tDriver, tRead);
        if (seq % 30 == 0) std::fflush(timingLog);
    }

    }

    if (timingLog) { std::fclose(timingLog); timingLog = nullptr; }
    if (manualExposure) {                       // give the camera back its automatic exposure
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 3);     // V4L2 aperture priority (automatic)
        if (previousGain >= 0) cap.set(cv::CAP_PROP_GAIN, previousGain);
    }
    qDebug() << "CAM THREAD ENDED";

}


///Returns an RGBframe from webcam
cv::Mat cameras::getRGBframe()
{

    //cv::Rect myROI(640-192, 360-144 , 384 , 288);
    cv::Rect myROI(640-240, 360-180 , 480, 360);


    // USE THREADS
    ///CRITICAL SECTION BEGINS///


    camlock();
    if(newRGBframe == 0) {
//      qDebug() << "FRAME RGB NOT READY !!!";
    }
    usedSeq = n_of_RGBFrames; usedDriverUs = frameDriverUs; usedReadUs = frameReadUs;
    newRGBframe = 0;
    originalRGB = aux1;

//    RGBframe = originalRGB(myROI).clone();
    try { RGBframe = originalRGB(myROI).clone();} catch (cv::Exception& e){ /*qDebug() << "got you cam!";*/ }
    camunlock();

    ///CRITICAL SECTION ENDS///


    //Rotate and flip to match

    //cv::rotate(cropped, cropped, 2);
    //cv::flip(RGBframe, RGBframe, 1);
//    cv::imwrite("hola3.jpg", originalRGB);
//    cv::imwrite("hola3b.jpg", RGBframe);
    cv::Size s(384, 288);
    cv::resize(RGBframe, RGBframe, s);
//    cv::imwrite("hola4.jpg", RGBframe);
    return RGBframe;
}



