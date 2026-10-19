#include <pthread.h>
#include <sched.h>
#include <iostream>
#include <vector>

const std::string pversion = "Thermapp version 12";

//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/core/utility.hpp"
#include <opencv2/imgproc/imgproc.hpp>


#include <string>
#include <sstream>
#include <cmath>       /* round, floor, ceil, trunc */
#include <cstdio>
#include <sys/timeb.h>
#include <string>


//THERMAPP INCLUDES
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "QtDebug"
#include <QtCore>
#include "easylogging++.h"
#include "timinglog.h"

// Globals
float g_hr, g_br, g_O2;
float g_hr_graph, g_br_graph;
int g_stable, g_seeuser;

void set_realtime_priority (void);

// Mirrored image compressed as JPEG for the MJPEG streams, with the number of the processed frame.
// Returns the size of the JPEG image (0 if there is no image).
static size_t sendImage(network &net, network::Stream stream, const cv::Mat &image, int frame)
{
    if (image.empty())
        return 0;
    cv::Mat mirrored;
    cv::flip(image, mirrored, 1);
    std::vector<uchar> jpeg;
    if (!cv::imencode(".jpg", mirrored, jpeg, {cv::IMWRITE_JPEG_QUALITY, 80}))
        return 0;
    size_t size = jpeg.size();
    net.sendFrame(stream, std::move(jpeg), static_cast<std::uint64_t>(frame));
    return size;
}


INITIALIZE_EASYLOGGINGPP


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    // Initialize logging
    el::Configurations conf("log.conf");
    el::Loggers::reconfigureAllLoggers(conf);
    LOG(INFO) << "RUNSTARTS";

    // Playing
    // int jx=1, jy=2;
    // float jz=3.14159;
    // el::Logger* defaultLogger = el::Loggers::getLogger("default");
    // defaultLogger->info("My first ultimate log message %v %v %v", jx,jy,jz);

    ///OPEN CAMERA OPENCV
    cam.initRGBCam();

    // Optional timing log (environment variable RT_RPPG_TIMING, see timinglog.h), one row per processed frame
    timingLog = timinglog::open("frames.csv", "frame,cam_seq,t_driver,t_read,t_start,t_frame,t_processed,t_gui,t_end,"
                                "face,stable,hr,rr,spo2,enc_main_us,jpeg_main_bytes,enc_pulse_us,jpeg_pulse_bytes");

//    qDebug() << "Tino1";

    ///Set GUI
    GUI.init();

//    qDebug() << "Tino2";

    ///Initialize processing
    proc.initProcess();

//    qDebug() << "Tino3";

    ///Initialize data container
    count = 0; n_of_ProcessedFrames = 0; noActiveCount = 0;

    DataContainer.faceActive=0;

    DataContainer.br_signal_value = 0;
    DataContainer.hr_signal_value = 0;

    DataContainer.hr_value = 0;
    DataContainer.br_value = 0;

    DataContainer.SpO2 = 0;
    g_hr = 0.0;
    g_br = 0.0;
    g_O2 = 0.0;
    g_hr_graph = 0.0;
    g_br_graph = 0.0;

    ///Start camera
    thread1 = cam.camReadThread();
    thread1.detach();

//    qDebug() << "Tino4";

    ///Start GUI
    thread2 = GUI.drawGUIThread();
    thread2.detach();

//    qDebug() << "Tino5";

    ///Start or initialize network
    // RESTful API (port 8080) and MJPEG streams (port 8081), each server in its own thread
    net.initNetwork(network::settingsFromEnvironment());

//    qDebug() << "Tino6";

    ///START MAIN LOOP
    //RUN every 33 ms to ensure 30fps.
    frameTimer = new QTimer(this);
    connect(frameTimer, SIGNAL(timeout()), this, SLOT(UpdateProcessFrame()));
    frameTimer->start(33);
}

MainWindow::~MainWindow()
{
    delete ui;
}


//THREAD PROCESS
void MainWindow::UpdateProcessFrame()
{
    // SIGINT or SIGTERM received (main.cpp): same shutdown as a key press
    extern std::atomic<bool> g_stopRequested;
    if (g_stopRequested) { shutdown(); return; }

//    qDebug() << "Tino7:" << n_of_ProcessedFrames;

    if (count == 0) {
        qDebug() << "PROC THREAD STARTED: Frame " << count;
        set_realtime_priority ();
        m_time.start();
        t0=m_time.elapsed();
    }
    else if (count%30 == 0)
    {
        t1=m_time.elapsed();
        qDebug() << "time per 30 frames:" << (t1 - t0) << " ms " << "Frame" << count;
        t0=t1;
    }

//    qDebug() << "Tino8";

    /// Start global timer
    double  timer = 0;
    timer = (double)cv::getTickCount();
    const long long tStart = timinglog::nowUs();


    //Update counter of frames processed
    n_of_ProcessedFrames++;

    //Get OpenCV Camera Image;
    RGBframe = cam.getRGBframe();
    const long long tFrame = timinglog::nowUs();


    ////////////////////
    ///PROCESSING //////
    ////////////////////

//    double  timer = 0;
//    int64 time0 = cv::getTickCount();

     // CALL PROCESS
    proc.detectFacesRGB(RGBframe); cam.newRGBframe = 0; // qDebug() << "ProcessFrame" << n_of_ProcessedFrames << "set to 0" << "CamFrame:" << cam.n_of_RGBFrames;
    const long long tProcessed = timinglog::nowUs();

//    int64 time1 = cv::getTickCount();
//    double time_fd = (time1 - time0) / cv::getTickFrequency();

//    qDebug() << "Face Detection: " << time_fd;


    ////////////////////
    ///UI AND DISPLAY///
    ////////////////////


    //FILL DATA CONTAINER

    cam.originalRGB.copyTo(DataContainer.original_rgb_frame);
    RGBframe.copyTo(DataContainer.crop_rgb_frame);

    DataContainer.nbr_of_frame = count;

    DataContainer.faceActive = proc.FaceActive;

//    qDebug() << "Tino11:" << proc.FaceActive;

    g_stable=0;
    g_seeuser=0;
    if(proc.FaceActive)
    {
        noActiveCount=0;  //Face is on the screen, so reset the counter of "no faces"

        //Copy data to data container. In principle, this could be sent anywhere
        //GUI.GUIlock();  //Thread safe, probably not needed
        DataContainer.landmarks_rgb = proc.RGBlandmarks;

        DataContainer.hr_signal_value = proc.sig.last_pulse;
        DataContainer.br_signal_value = proc.sig.last_breath;

        DataContainer.hr_value = proc.sig.heartRate;
        DataContainer.br_value = proc.sig.breathingRate;

        DataContainer.SpO2 = proc.sig.SpO2;

        //This are the locations of the flags: 
        // (sigStable)Is the signal stable? 
        // (userReset) Is the user reset and not in the FoV? 
        // (badPositioning) Is the face badly positioned, even when it is detected?	
        g_stable = proc.sig.sigStable;
        g_seeuser = 1-proc.sig.userReset;
        // int badPositioning = proc.lan.badPositioning;

        //GUI.GUIunlock(); //Thread safe, probably not needed

    } else {
        noActiveCount++; //qDebug() << "NoActiveCount" << noActiveCount;

        if (noActiveCount==60) {reset(); }

    }

    GUI.GUIlock();
    GUI.data_container = DataContainer; //qDebug() << "DisplayedMainWindowBefore" << GUI.Displayed;
    GUI.Displayed = 0;  //qDebug() << "DisplayedMainWindowAfter" << GUI.Displayed;
    // Set global answers for network queries
    // Flags are set above
    g_hr = DataContainer.hr_value;
    g_br = DataContainer.br_value;
    g_O2 = DataContainer.SpO2;
    g_hr_graph = DataContainer.hr_signal_value;
    g_br_graph = DataContainer.br_signal_value;


    // Log the values for test purposes
//    char rmabuff[100];
//    std::string srma;
//    cv::Mat temp, temp2;
//    sprintf (rmabuff, "%05.1f,%05.1f,%05.1f,%d,%d,%08.1f,%08.1f", g_hr,g_br,g_O2,g_seeuser,g_stable,
//	g_hr_graph, g_br_graph);
//    srma = rmabuff;
//    LOG_EVERY_N (31, INFO) << srma;


    GUI.GUIunlock();


    //DRAW GUI UNTHREADED
    GUI.drawGUI();
    const long long tGui = timinglog::nowUs();




    ////////////////////////////
    ///SEND TO NETWORK    //////
    ////////////////////////////

    // Executed every 33 ms: store the latest values for the RESTful API, and encode the images
    // only when a client watches the streams. The clients are served in the network threads.
    VitalSigns values;
    values.hr = g_hr;
    values.br = g_br;
    values.spo2 = g_O2;
    values.seeUser = g_seeuser;
    values.stable = g_stable;
    values.hrGraph = g_hr_graph;
    values.brGraph = g_br_graph;
    net.setVitalSigns(values);

    long long encMain = 0, encPulse = 0;
    size_t jpegMain = 0, jpegPulse = 0;
    if (net.isStreaming(network::MAIN_STREAM)) {
        const long long t = timinglog::nowUs();
        jpegMain = sendImage(net, network::MAIN_STREAM, DataContainer.crop_rgb_frame, count);
        encMain = timinglog::nowUs() - t;
    }
    if (net.isStreaming(network::PULSE_STREAM)) {
        const long long t = timinglog::nowUs();
        jpegPulse = sendImage(net, network::PULSE_STREAM, proc.sig.eulerian, count);
        encPulse = timinglog::nowUs() - t;
    }

    if (timingLog) {
        const long long tEnd = timinglog::nowUs();
        std::fprintf(timingLog, "%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d,%s,%s,%s,%lld,%zu,%lld,%zu\n",
                     count, cam.usedSeq, cam.usedDriverUs, cam.usedReadUs, tStart, tFrame, tProcessed, tGui, tEnd, DataContainer.faceActive, g_stable, timinglog::decimal(g_hr).c_str(),
                     timinglog::decimal(g_br).c_str(), timinglog::decimal(g_O2).c_str(), encMain, jpegMain,
                     encPulse, jpegPulse);
        if (count % 30 == 0) std::fflush(timingLog);
    }


    count++;

}


void MainWindow::reset()
{
    qDebug() << "RESETTING USER !!";
    proc.sig.reset();
    GUI.reset();
}


void MainWindow::keyPressEvent(QKeyEvent *)
{
    shutdown();
}

void MainWindow::shutdown()
{
    static bool done = false;
    if (done) return;
    done = true;

    cam.camlock();
    cam.CameraON = 0;
    cam.camunlock();
    sleep(2);

    //GUI.GUIlock();
    //GUI.DisplayON = 0;
    //GUI.GUIunlock();
    //sleep(5);

    frameTimer->stop();
    if (timingLog) { std::fclose(timingLog); timingLog = nullptr; }
    net.stopNetwork();
    sleep(2);
    LOG (INFO) << "RUNENDS";


    QCoreApplication::quit();

}


void set_realtime_priority() {

     int ret;

     // We'll operate on the currently running thread.
     pthread_t this_thread = pthread_self();

     // struct sched_param is used to store the scheduling priority
     struct sched_param params;

     // We'll set the priority to the maximum.
     // params.sched_priority = sched_get_priority_max(SCHED_FIFO);
     params.sched_priority = 59;

     std::cout << "Trying to set thread realtime prio = " << params.sched_priority << std::endl;

     // Attempt to set thread real-time priority to the SCHED_FIFO policy
     ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);
     if (ret != 0) {
         // Print the error
         std::cout << "Unsuccessful in setting thread realtime prio" << std::endl;
         return;     
     }

     // Now verify the change in thread priority
     int policy = 0;
     ret = pthread_getschedparam(this_thread, &policy, &params);
     if (ret != 0) {
         std::cout << "Couldn't retrieve real-time scheduling paramers" << std::endl;
         return;
     }

     // Check the correct policy was applied
     if(policy != SCHED_FIFO) {
         std::cout << "Scheduling is NOT SCHED_FIFO!" << std::endl;
     } else {
         std::cout << "SCHED_FIFO OK" << std::endl;
     }

     // Print thread scheduling priority
     std::cout << "Thread priority is " << params.sched_priority << std::endl; 
}
