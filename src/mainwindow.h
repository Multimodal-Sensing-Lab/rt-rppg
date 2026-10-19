#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtCore>
#include <QtGui>
#include "cameras.h"
#include "process.h"

#include "dataobjectclass.h"
#include "uihealth.h"
#include "network.h"
#include <thread>
#include "opencv2/opencv.hpp"


namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

public slots:

     void UpdateProcessFrame();

     // Stops the camera, the processing and the network, then quits (any key, SIGINT or SIGTERM)
     void shutdown();

protected:

     void keyPressEvent(QKeyEvent *e);

private slots:

     void reset();

private:

     QTime m_time;
     float t0, t1;

     std::thread thread1;
     std::thread thread2;

    Ui::MainWindow *ui;

    cameras cam;
    process proc;

    QTimer *frameTimer;

    cv::Mat RGBframe;

    int count;
    int n_of_ProcessedFrames;
    int noActiveCount; //Counts the number of frame without a face. When it is 60 (two seconds) resets the user

    //GUI
    DataObjectClass DataContainer; // Container with the data that will be sent to the display. The results of the algorithm are copied here
    uihealth GUI;  //GUI inteface

    network net;   // RESTful API and MJPEG streams

    std::FILE *timingLog = nullptr;   // optional timing log (RT_RPPG_TIMING), one row per processed frame

};

#endif // MAINWINDOW_H
