#include "uihealth.h"

#include "QtDebug"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "plot.h"
#include "opencv2/opencv_modules.hpp"

#include <iostream>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <unistd.h>

uihealth::uihealth()
{

}

uihealth::~uihealth()
{

}

void uihealth::reset()
{

    // Vital signs data init
    HR = "--";
    BR = "--";
    SpO2 = "--";

    data_container.nbr_of_frame = 0;
    countUI =0;

}


void uihealth::init(){

    countUI=0;

    //flag
    Displayed = 1;

    gui_image = cv::Mat::zeros(cv::Size(1280,720), CV_64FC1);
    display_image = cv::Mat::zeros(cv::Size(1280,720), CV_64FC1);
    data_container.original_rgb_frame = cv::Mat::zeros(cv::Size(1280,720), CV_64FC1);
    data_container.crop_rgb_frame = cv::Mat::zeros(cv::Size(384,288), CV_64FC1);

    // General UI
    width = 1280;
    height = 720;
    margin_x = 0.01*width;
    margin_y = 0.01*height;

    // Position viewfinder RGB+IR inactive state
    width_viewfinder = width*0.15625;
    heigth_viewfinder = height*0.2083;
    viewfinder_rgb_x = width - width_viewfinder*2 - margin_x;
    viewfinder_rgb_y = height - heigth_viewfinder - margin_y;
    viewfinder_ir_x = width - width_viewfinder - margin_x;
    viewfinder_ir_y = height - heigth_viewfinder - margin_y;

    // Position central rectangle inactive state
    size_central_rect_inactive = height*0.4;
    pos_central_rect_inactive_x = width/2 - size_central_rect_inactive/2;
    pos_central_rect_inactive_y = height/2 - size_central_rect_inactive/2;

    // Position central rectangle active state
    width_central_rect_active = width*0.53125;
    height_central_rect_active = height*0.75;
    pos_central_rect_active_x = width/2 - width_central_rect_active/2;
    pos_central_rect_active_y = height/2 - height_central_rect_active/2;
    relation_xy_central_rect_active = width_central_rect_active/(float)height_central_rect_active;

    // Position side rectangle active state
    width_side_rect_active = width*0.234375;
    height_side_rect_active = height*0.333333;
    pos_side_rect_active_x = width - width_side_rect_active - margin_x;
    pos_side_rect_active_y = height - height_side_rect_active - 120 /*signal screen height*/ - margin_y*2;

    // Plots HR y BR samples
    nbr_of_samples = 150;  //5 seconds
    nbr_of_samples_HR = nbr_of_samples;
    nbr_of_samples_BR = nbr_of_samples;

    // Plot signals dimensions
    width_vital_signals_plot = width*0.3125;
    height_vital_signals_plot = height*0.1667;

    // Plot Data Heart Rate
    data_plotHR_x = cv::Mat::zeros(1, nbr_of_samples_HR, CV_64FC1 );
    data_plotHR_y = cv::Mat::zeros(1, nbr_of_samples_HR, CV_64FC1 );
    //signal_HR = arma::zeros<arma::rowvec>(nbr_of_samples_HR);
    for (int i = 0; i < nbr_of_samples_HR; i++) { signal_HR2.push_back(0.0); }

    // Plot Data BR Rate
    data_plotBR_x = cv::Mat::zeros(1, nbr_of_samples_BR, CV_64FC1 );
    data_plotBR_y = cv::Mat::zeros(1, nbr_of_samples_BR, CV_64FC1 );
    //signal_BR = arma::zeros<arma::rowvec>(nbr_of_samples_BR);
    for (int i = 0; i < nbr_of_samples_BR; i++) { signal_BR2.push_back(0.0); }

    // Initialization as "linspace function for X axis. PLOTs HR & BR
    for ( int i = 0; i < nbr_of_samples_HR; i++ )
        data_plotHR_x.at<double>( 0, i ) = i;
    for ( int i = 0; i < nbr_of_samples_BR; i++ )
        data_plotBR_x.at<double>( 0, i ) = i;

    // Generating Layer with common elements in the GUI.
    common_layer =  getGUIBase();

    // Vital signs data init
    HR = "--";
    BR = "--";
    SpO2 = "--";

//    qDebug() << "INIT GUI";

}


/**************************************************************/
/* Get Base of the GUI. Common parts in the different states. */
/**************************************************************/
cv::Mat uihealth::getGUIBase(){

    cv::Mat GUIBase(height, width, CV_8UC4);
    GUIBase = cv::Scalar(0,0,0,0);

    // INFO ---> Size of each logo is 170x170
    heart_logo = cv::imread("images/hr_logo.png", cv::IMREAD_UNCHANGED);
    breath_logo = cv::imread("images/br_logo.png", cv::IMREAD_UNCHANGED);

    // Overlaying [TODO] Relative size of logos with WIDTH and HEIGHT
    overlayImageAlpha(GUIBase, heart_logo, GUIBase, cv::Point(20, 190));
    overlayImageAlpha(GUIBase, breath_logo, GUIBase, cv::Point(20, 370));

//    qDebug() << "GUI BASE";

    return GUIBase;

}


void uihealth::drawGUI()
{

//            qDebug() << "Tino12:" << data_container.faceActive;
            int stateface = data_container.faceActive;
            gui_image = data_container.original_rgb_frame.clone();
            data_container.crop_rgb_frame.copyTo(crop_rgb_image);

            overlayImage(gui_image, common_layer, gui_image, cv::Point(0,0));

//            qDebug() << "Tino13:" << Displayed;

            if( stateface == 0)
            {


                // Rectangle no face detected +  warnings
                cv::rectangle(gui_image, cv::Rect(gui_image.cols/2 - size_central_rect_inactive/2, gui_image.rows/2 - size_central_rect_inactive/2 - margin_y*4, size_central_rect_inactive, size_central_rect_inactive), cv::Scalar(172, 89, 67), 4);

            }

//            qDebug() << "Tino14:" << stateface;

            if( stateface == 1)
            {
                // RGB CENTRAL RECT
                cv::resize(crop_rgb_image, crop_rgb_image, cv::Size(width_central_rect_active, height_central_rect_active));
                crop_rgb_image.copyTo(gui_image(cv::Rect(pos_central_rect_active_x - margin_x*2, pos_central_rect_active_y - margin_y*6.5, crop_rgb_image.cols, crop_rgb_image.rows)));
                cv::rectangle(gui_image, cv::Rect(pos_central_rect_active_x - margin_x*2, pos_central_rect_active_y - margin_y*6.5, crop_rgb_image.cols, crop_rgb_image.rows), cv::Scalar(255, 255, 255), 4);

                // Draw HR and BR Signals.
                plot_HRSignal(data_container.hr_signal_value);
                plot_BRSignal(data_container.br_signal_value);

            }


//            qDebug() << "Tino15:" << data_container.hr_value;

            cv::putText(gui_image, HR + "bpm", cv::Point(25, 280), 2, 1.4, cv::Scalar(255,255,255),3);
            if(data_container.nbr_of_frame > 60 && data_container.nbr_of_frame%3 == 0 && stateface == 1)
                HR = to_string_with_precision(data_container.hr_value, 1);

            cv::putText(gui_image, BR + "rpm", cv::Point(30, 470), 2, 1.4, cv::Scalar(255,255,255),3);
            if(data_container.nbr_of_frame > 60 && data_container.nbr_of_frame%3 == 0 && stateface == 1)
                BR = to_string_with_precision(data_container.br_value, 1);

            cv::putText(gui_image, "SpO2:" + SpO2 + "%", cv::Point(20, 320), 2, 1, cv::Scalar(255,255,255),2);
            if(data_container.nbr_of_frame > 60 && data_container.nbr_of_frame%3 == 0 && stateface == 1)
                SpO2 = to_string_with_precision(data_container.SpO2, 1);

            display_image = gui_image.clone();
//            qDebug() << "Tino16:" << data_container.br_value;
            showImage();
//            qDebug() << "Tino17:" << DisplayON;
}

void uihealth::showImage()
{

    cv::namedWindow("Face2PPG-RT", cv::WINDOW_NORMAL);
    cv::resizeWindow("Face2PPG-RT", 1280, 720);
    cv::imshow("Face2PPG-RT", display_image);  //This is the user interface window !!
//    cv::waitKey(1);

}

//*********************************//
//    VISUALIZATION FUNCTIONS      //
//*********************************//

// FOR TEXTING
template <typename T>
std::string uihealth::to_string_with_precision(const T a_value, const int n)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(n) << a_value;
    return out.str();
}


// HR PLOT
void uihealth::plot_HRSignal(float hr_signal_value)  //Last value of the signal !!!!  (not the whole signal)
{
    float hr;
    hr = hr_signal_value;
    if (hr < 0.1 && hr > -0.1) {hr = 0;}  //There is a bug in plot for small numbers. Needs to be done

    //Avoiding using ARMADILLLO
    //signal_HR = arma::shift(signal_HR,  -1);
    //signal_HR(nbr_of_samples_HR - 1) = hr;
    //data_plotHR_y  = cv::Mat(1, nbr_of_samples_HR, CV_64FC1, signal_HR.memptr());

    signal_HR2.pop_front(); signal_HR2.push_back(hr);
    cv::Mat1d data_plotHR_y2;
    for (unsigned int i = 0; i < signal_HR2.size(); i++) { data_plotHR_y2.push_back(signal_HR2.at(i)); }


    plot_HR = plot::Plot2d::create( data_plotHR_x, data_plotHR_y2 );
    plot_HR->setPlotBackgroundColor( cv::Scalar( 255, 255, 255 ) );
    plot_HR->setPlotLineColor( cv::Scalar( 0, 0, 255 ) );
    plot_HR->setPlotAxisColor( cv::Scalar( 0, 0, 0 ) );
    plot_HR->setPlotGridColor( cv::Scalar( 200, 200, 200 ) );
    plot_HR->setPlotLineWidth( 2 );
    plot_HR->render(data_BR);
    cv::resize(data_BR, data_BR,cv::Size(width_vital_signals_plot, height_vital_signals_plot));
    data_BR.copyTo(gui_image(cv::Rect(width - width_vital_signals_plot*2 - margin_x*2, height - height_vital_signals_plot -margin_y, data_BR.cols, data_BR.rows)));

}

// HR PLOT
void uihealth::plot_BRSignal(float br_signal_value)
{

    float br;
    br = br_signal_value;
    if (br < 0.1 && br > -0.1) {br = 0;} //There is a bug in plot for small numbers. Needs to be done

    //signal_BR = arma::shift(signal_BR,  -1);
    //signal_BR(nbr_of_samples_BR - 1) = br;
    //data_plotBR_y  = cv::Mat(1, nbr_of_samples_BR, CV_64FC1, signal_BR.memptr());

    signal_BR2.pop_front(); signal_BR2.push_back(br);
    cv::Mat1d data_plotBR_y2;
    for (unsigned int i = 0; i < signal_BR2.size(); i++) { data_plotBR_y2.push_back(signal_BR2.at(i)); }

    plot_BR = plot::Plot2d::create( data_plotBR_x, data_plotBR_y2 );
    plot_BR->setPlotBackgroundColor( cv::Scalar( 255, 255, 255 ) );
    plot_BR->setPlotLineColor( cv::Scalar( 255, 0, 0 ) );
    plot_BR->setPlotAxisColor( cv::Scalar( 0, 0, 0 ) );
    plot_BR->setPlotGridColor( cv::Scalar( 200, 200, 200 ) );
    plot_BR->setPlotLineWidth( 2 );
    plot_BR->render(data_BR);
    cv::resize(data_BR, data_BR,cv::Size(width_vital_signals_plot, height_vital_signals_plot));
    data_BR.copyTo(gui_image(cv::Rect(width - width_vital_signals_plot - margin_x, height - height_vital_signals_plot -margin_y, data_BR.cols, data_BR.rows)));
}


/**************************************************************/
/*        Create layer with alpha images. BGRA to BGRA        */
/**************************************************************/
void uihealth::overlayImageAlpha(const cv::Mat &background, const cv::Mat &foreground,
                                 cv::Mat &output, cv::Point2i location)
{
    //background.copyTo(output);
    output = background;

    // start at the row indicated by location, or at row 0 if location.y is negative.
    for(int y = std::max(location.y , 0); y < background.rows; ++y)
    {
        int fY = y - location.y; // because of the translation

        // we are done of we have processed all rows of the foreground image.
        if(fY >= foreground.rows)
            break;

        // start at the column indicated by location,

        // or at column 0 if location.x is negative.
        for(int x = std::max(location.x, 0); x < background.cols; ++x)
        {
            int fX = x - location.x; // because of the translation.

            // we are done with this row if the column is outside of the foreground image.
            if(fX >= foreground.cols)
                break;


            for(int c = 0; c < output.channels(); ++c)
            {
                unsigned char foregroundPx =
                        foreground.data[fY * foreground.step + fX * foreground.channels() + c];
                unsigned char backgroundPx =
                        background.data[y * background.step + x * background.channels() + c];
                output.data[y*output.step + output.channels()*x + c] =
                        backgroundPx  + foregroundPx;
            }
        }
    }
}


/**************************************************************/
/*     Create layer with 3 channels images. BGRA -> BGR       */
/**************************************************************/
void uihealth::overlayImage(const cv::Mat &background, const cv::Mat &foreground,
                            cv::Mat &output, cv::Point2i location)
{
    //background.copyTo(output);
    output = background;

    // start at the row indicated by location, or at row 0 if location.y is negative.
    for(int y = std::max(location.y , 0); y < background.rows; ++y)
    {
        int fY = y - location.y; // because of the translation

        // we are done of we have processed all rows of the foreground image.
        if(fY >= foreground.rows)
            break;

        // start at the column indicated by location,

        // or at column 0 if location.x is negative.
        for(int x = std::max(location.x, 0); x < background.cols; ++x)
        {
            int fX = x - location.x; // because of the translation.

            // we are done with this row if the column is outside of the foreground image.
            if(fX >= foreground.cols)
                break;

            // determine the opacity of the foregrond pixel, using its fourth (alpha) channel.
            double opacity =
                    ((double)foreground.data[fY * foreground.step + fX * foreground.channels() + 3])

                    / 255.;


            // and now combine the background and foreground pixel, using the opacity,

            // but only if opacity > 0.
            for(int c = 0; opacity > 0 && c < output.channels(); ++c)
            {
                unsigned char foregroundPx =
                        foreground.data[fY * foreground.step + fX * foreground.channels() + c];
                unsigned char backgroundPx =
                        background.data[y * background.step + x * background.channels() + c];
                output.data[y*output.step + output.channels()*x + c] =
                        backgroundPx * (1.-opacity) + foregroundPx * opacity;
            }
        }
    }
}

void uihealth::overlayImageBlack(const cv::Mat &background, const cv::Mat &foreground,
                                 cv::Mat &output, cv::Point2i location)
{
    background.copyTo(output);

    // start at the row indicated by location, or at row 0 if location.y is negative.
    for(int y = std::max(location.y , 0); y < background.rows; ++y)
    {
        int fY = y - location.y; // because of the translation

        // we are done of we have processed all rows of the foreground image.
        if(fY >= foreground.rows)
            break;

        // start at the column indicated by location,

        // or at column 0 if location.x is negative.
        for(int x = std::max(location.x, 0); x < background.cols; ++x)
        {
            int fX = x - location.x; // because of the translation.

            // we are done with this row if the column is outside of the foreground image.
            if(fX >= foreground.cols)
                break;

            // determine the opacity of the foregrond pixel, using its fourth (alpha) channel.
            double opacity =
                    ((double)foreground.data[fY * foreground.step + fX * foreground.channels() + 3])

                    / 255.;


            // and now combine the background and foreground pixel, using the opacity,

            // but only if opacity > 0.
            for(int c = 0; opacity > 0 && c < output.channels(); ++c)
            {
                unsigned char foregroundPx =
                        foreground.data[fY * foreground.step + fX * foreground.channels() + c];
                unsigned char backgroundPx =
                        background.data[y * background.step + x * background.channels() + c];

                if( int(backgroundPx) > 10){
                    output.data[y*output.step + output.channels()*x + c] =
                            backgroundPx * (1.-opacity) + foregroundPx * opacity;
                }else{
                    output.data[y*output.step + output.channels()*x + c] =
                            backgroundPx;
                }
            }
        }
    }
}



void uihealth::GUIlock()
{
    mGUI.lock();
}
void uihealth::GUIunlock()
{
    mGUI.unlock();
}
