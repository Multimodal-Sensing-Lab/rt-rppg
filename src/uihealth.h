
#include "plot.h"
#include "dataobjectclass.h"
#include "facelandmarks.h"
#include <mutex>
#include <deque>

#ifndef UIHEALTH_H
#define UIHEALTH_H


class uihealth
{

public:
    uihealth();
    ~uihealth();

    void reset();
    void GUIlock();
    void GUIunlock();
    int isDisplayed()
    {return Displayed;}


    /// FUNCTIONS ///
    void init();

    void drawGUI();
    void showImage();

    std::thread drawGUIThread()
          {
           return std::thread([=] {drawGUI(); });
          }

    /// VARIABLES ///


    int DisplayON;

    int countUI;

    // General UI
    int width;
    int height;
    float margin_x;
    float margin_y;

    // Position viewfinder RGB+IR inactive state
    int width_viewfinder;
    int heigth_viewfinder;
    int viewfinder_rgb_x;
    int viewfinder_rgb_y;
    int viewfinder_ir_x;
    int viewfinder_ir_y;

    // Position central rectangle inactive state
    int size_central_rect_inactive;
    int pos_central_rect_inactive_x;
    int pos_central_rect_inactive_y;

    // Position central rectangle active state
    int width_central_rect_active;
    int height_central_rect_active;
    int pos_central_rect_active_x;
    int pos_central_rect_active_y;
    float relation_xy_central_rect_active;

    // Position side rectangle active state
    int width_side_rect_active;
    int height_side_rect_active;
    int pos_side_rect_active_x;
    int pos_side_rect_active_y;

    // Plots HR y BR samples
    int nbr_of_samples;
    int nbr_of_samples_HR;
    int nbr_of_samples_BR;

    // Plot signals size
    int width_vital_signals_plot;
    int height_vital_signals_plot;

    // Plot Heart Rate
    cv::Mat1d data_plotHR_x;
    cv::Mat1d data_plotHR_y;
    std::deque<double> signal_HR2;
    cv::Mat data_HR;
    cv::Ptr<plot::Plot2d> plot_HR;

    // Plot BR Rate
    cv::Mat1d data_plotBR_x;
    cv::Mat1d data_plotBR_y;
    std::deque<double> signal_BR2;
    cv::Mat data_BR;
    cv::Ptr<plot::Plot2d> plot_BR;

    // GUI images
    cv::Mat overlayed;
    cv::Mat common_layer; // Common Layer 4 channels image.
    cv::Mat gui_image;
    cv::Mat display_image;
    cv::Mat crop_rgb_image;

    // Vital signs data variables
    std::string HR;
    std::string BR;
    std::string SpO2;

    // Logos. Loaded all the time for avoid access to the harddrive
    cv::Mat heart_logo;
    cv::Mat breath_logo;

    DataObjectClass data_container;

    //flag
    int Displayed;

private:
    cv::Mat getGUIBase();
    void overlayImageAlpha(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location);
    void overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location);

    void plot_HRSignal(float hr_signal_value);
    void plot_BRSignal(float br_signal_value);

    void overlayImageBlack(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location);

    template <typename T>
    std::string to_string_with_precision(const T a_value, const int n = 6);

    //Mutex
    std::mutex mGUI;
};

#endif // UIHEALTH_H
