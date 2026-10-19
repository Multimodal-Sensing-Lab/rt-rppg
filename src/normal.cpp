#include "normal.h"
#include <opencv2/imgproc/imgproc.hpp>
#include "QtDebug"
#include <math.h>

namespace cv {

void normalization(InputArray _a, OutputArray _b) {

    
    _a.getMat().copyTo(_b);
    Mat b = _b.getMat();
    Scalar mean, stdDev;
    meanStdDev(b, mean, stdDev);
    b = (b - mean[0]) / stdDev[0];
}



double bpm(Mat1d pulse, double  Fps)
{

    double currentFps = Fps;

    double bpm=0; double Fbpm=0;
    Mat powerSpectrum, powerSpectrumComplex;

    //qDebug() << pulse.rows << pulse.cols << "before";


    // Hamming window multiplication
    for (int i = 0; i< pulse.rows; i++)
    {
        double multiplier = 0.5* ( 1 - cos(2 * 3.14159265*i/(pulse.rows-1) ) );
        pulse.at<double>(i) =  multiplier * pulse.at<double>(i);
    }

    //Zero padding
    cv::copyMakeBorder(pulse, pulse, pulse.rows*2, pulse.rows*2, 0, 0, BORDER_REPLICATE, 0);

    //qDebug() << pulse.rows << pulse.cols << "after padding";

    //FFT
    dft(pulse, powerSpectrumComplex, DFT_COMPLEX_OUTPUT);
    cv::Mat spl[2];
    cv::split(powerSpectrumComplex, spl);


    //total number of samples
    const int total = pulse.rows;

    // band limit
    const int low = total * 55./60./currentFps + 1;
    const int high = total * 240./60./currentFps + 1;
    spl[0].rowRange(0, min((size_t)low, (size_t)total)) = ZERO;
    spl[0].pop_back(min((size_t)(total - high), (size_t)total));
    spl[1].rowRange(0, min((size_t)low, (size_t)total)) = ZERO;
    spl[1].pop_back(min((size_t)(total - high), (size_t)total));

    pow(spl[0], 2, spl[0]);
    pow(spl[1], 2, spl[1]);
    cv::add (spl[0], spl[1], powerSpectrum);


    int idx[5];
    if (!powerSpectrum.empty()) {
        // grab index of max power spectrum


        bpm = 0;
        while( bpm   < 50  || bpm > 120)
        {
            minMaxIdx(powerSpectrum, 0, 0, 0, &idx[0]);
            bpm = idx[0] * currentFps * 60. / total;
            powerSpectrum.at<double>(idx[0] ) = 0;
            //      qDebug() << idx[0] << "checking loop" << bpm;
        }


        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[1]); powerSpectrum.at<double>(idx[1]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[2]); powerSpectrum.at<double>(idx[2]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[3]); powerSpectrum.at<double>(idx[3]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[4]); powerSpectrum.at<double>(idx[4]) = 0;



        // calculate BPM
        bpm = idx[0] * currentFps * 60. / total; // constant 30 = 60 BPM / 2

    }

//    qDebug() << "bpm is: " << bpm << "with ID:" <<  idx[0] << "second opinions are:" << idx[1] << idx[2] << idx[3] << idx[4]
//             << "avg" << ((idx[1]+idx[2]+idx[3]+idx[4])/4);

    // average calculated BPMs since last time
    Fbpm = bpm;

    //qDebug() << "Final BPM reported is: " << Fbpm;

    return Fbpm;

}


double rpm(Mat1d resp, double Fps)
{

    double currentFps = Fps;

    double rpm=0; double Frpm=0;
    Mat powerSpectrum, powerSpectrumComplex;

    //qDebug() << resp.rows << resp.cols << "before";


    // Hamming window multiplication
    for (int i = 0; i< resp.rows; i++)
    {
        double multiplier = 0.5* ( 1 - cos(2 * 3.14159265*i/(resp.rows-1) ) );
        resp.at<double>(i) =  multiplier * resp.at<double>(i);
    }

    //Zero padding
    cv::copyMakeBorder(resp, resp, resp.rows*2, resp.rows*2, 0, 0, BORDER_REPLICATE, 0);

    //qDebug() << resp.rows << resp.cols << "after padding";

    //FFT
    dft(resp, powerSpectrumComplex, DFT_COMPLEX_OUTPUT);
    cv::Mat spl[2];
    cv::split(powerSpectrumComplex, spl);


    //total number of samples
    const int total = resp.rows;

    // band limit
    const int low = total * 6./60./currentFps + 1;
    const int high = total * 54./60./currentFps + 1;
    spl[0].rowRange(0, min((size_t)low, (size_t)total)) = ZERO;
    spl[0].pop_back(min((size_t)(total - high), (size_t)total));
    spl[1].rowRange(0, min((size_t)low, (size_t)total)) = ZERO;
    spl[1].pop_back(min((size_t)(total - high), (size_t)total));

    pow(spl[0], 2, spl[0]);
    pow(spl[1], 2, spl[1]);
    cv::add (spl[0], spl[1], powerSpectrum);


    int idx[5];
    if (!powerSpectrum.empty()) {
        // grab index of max power spectrum


        rpm = 0;
        while( rpm   < 5  || rpm > 50)
        {
            minMaxIdx(powerSpectrum, 0, 0, 0, &idx[0]);
            rpm = idx[0] * currentFps * 60. / total;
            powerSpectrum.at<double>(idx[0] ) = 0;
            // qDebug() << idx[0] << "checking loop" << rpm;
        }


        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[1]); powerSpectrum.at<double>(idx[1]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[2]); powerSpectrum.at<double>(idx[2]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[3]); powerSpectrum.at<double>(idx[3]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[4]); powerSpectrum.at<double>(idx[4]) = 0;



        // calculate RPM
        rpm = idx[0] * currentFps * 60. / total; // constant 30 = 60 BPM / 2

    }

//    qDebug() << "rpm is: " << rpm << "with ID:" <<  idx[0] << "second opinions are:" << idx[1] << idx[2] << idx[3] << idx[4];

    // average calculated BPMs since last time
    Frpm = rpm;

    //qDebug() << "Final BPM reported is: " << Fbpm;

    return Frpm;

}


void spectrumF(Mat1d pulse, Mat &powerSpectrum)
{
    Mat powerSpectrumComplex; //Auxiliary matrix

    // Hamming window multiplication + subtraction of the mean (or normalization)
    // Avoids spectral leakage and 0 component;
    normalization(pulse, pulse);
    for (int i = 0; i< pulse.rows; i++)
    {
        double multiplier = 0.5* ( 1 - cos(2 * 3.14159265*i/(pulse.rows-1) ) );
        pulse.at<double>(i) =  multiplier * (pulse.at<double>(i));
    }

    //Zero padding
    cv::copyMakeBorder(pulse, pulse, pulse.rows*2, pulse.rows*2, 0, 0, BORDER_REPLICATE, 0);

    //FFT
    dft(pulse, powerSpectrumComplex, DFT_COMPLEX_OUTPUT);

    cv::Mat spl[2];
    cv::split(powerSpectrumComplex, spl);

    pow(spl[0], 2, spl[0]);
    pow(spl[1], 2, spl[1]);
    cv::add (spl[0], spl[1], powerSpectrum);

}
void spectrumB(Mat1d pulse, Mat &powerSpectrum)
{
    Mat powerSpectrumComplex; //Auxiliary matrix

    // Hamming window multiplication + subtraction of the mean (or normalization)
    // Avoids spectral leakage and 0 component;
    normalization(pulse, pulse);
    for (int i = 0; i< pulse.rows; i++)
    {
        double multiplier = 0.5* ( 1 - cos(2 * 3.14159265*i/(pulse.rows-1) ) );
        pulse.at<double>(i) =  multiplier * (pulse.at<double>(i));
    }

    //Zero padding
    cv::copyMakeBorder(pulse, pulse, pulse.rows*4, pulse.rows*4, 0, 0, BORDER_REPLICATE, 0);

    //FFT
    dft(pulse, powerSpectrumComplex, DFT_COMPLEX_OUTPUT);

    cv::Mat spl[2];
    cv::split(powerSpectrumComplex, spl);

    pow(spl[0], 2, spl[0]);
    pow(spl[1], 2, spl[1]);
    cv::add (spl[0], spl[1], powerSpectrum);

}


double bpmFromSpectrum(Mat1d powerSpectrum, double currentFps)
{
    //total number of samples
    const int total = powerSpectrum.rows;

    double bpm=0; double Fbpm=0;

    // band limit
    const int low = total * 40./60./currentFps + 1;
    const int high = total * 150./60./currentFps + 1;
    powerSpectrum.rowRange(0, min((size_t)low, (size_t)total)) = ZERO;
    powerSpectrum.pop_back(min((size_t)(total - high), (size_t)total));

    int idx[5];
    if (!powerSpectrum.empty()) {
        // grab index of max power spectrum
        bpm = 0;
        while( bpm   < 40  || bpm > 150)
        {
            minMaxIdx(powerSpectrum, 0, 0, 0, &idx[0]);
            bpm = idx[0] * currentFps * 60. / total;           //qDebug() << idx[0] << "checking loop" << bpm << powerSpectrum.at<double>(idx[0] );
            powerSpectrum.at<double>(idx[0] ) = 0;

        }

        //Second opinions
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[1]); powerSpectrum.at<double>(idx[1]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[2]); powerSpectrum.at<double>(idx[2]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[3]); powerSpectrum.at<double>(idx[3]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[4]); powerSpectrum.at<double>(idx[4]) = 0;


        // calculate BPM
        bpm = idx[0] * currentFps * 60. / total; // constant 30 = 60 BPM / 2

    }

    //qDebug() << "bpm is: " << bpm << "with ID:" <<  idx[0] << "second opinions are:" << idx[1] << idx[2] << idx[3] << idx[4]
    //         << "avg" << ((idx[1]+idx[2]+idx[3]+idx[4])/4);

    // Final BPMs after possible post-processing
    Fbpm = bpm;


    return Fbpm;

}

double rpmFromSpectrum(Mat1d powerSpectrum, double currentFps)
{
    //total number of samples
    const int total = powerSpectrum.rows;

    double rpm=0; double Frpm=0; double Lrpm;

    // band limit
    const int low = total * 8./60./currentFps + 1;
    const int high = total * 42./60./currentFps + 1;
    powerSpectrum.rowRange(0, min((size_t)low, (size_t)total)) = ZERO;
    powerSpectrum.pop_back(min((size_t)(total - high), (size_t)total));

    double accum=0.0, totalval=0.0;
    for (int i = low; i<high; i++)
    {
        int index = i;
        float val = powerSpectrum.at<double>(index);

        accum += val;
        totalval += (val*index);
    }

    Lrpm = (totalval/accum) * currentFps * 60 /total;


/*
    int idx[5];
    if (!powerSpectrum.empty()) {
        // grab index of max power spectrum
        rpm = 0;
        while( rpm   < 8  || rpm > 42)
        {
            minMaxIdx(powerSpectrum, 0, 0, 0, &idx[0]);
            rpm = idx[0] * currentFps * 60. / total;           //qDebug() << idx[0] << "checking loop" << bpm << powerSpectrum.at<double>(idx[0] );
            powerSpectrum.at<double>(idx[0] ) = 0;

        }

        //Second opinions
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[1]); powerSpectrum.at<double>(idx[1]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[2]); powerSpectrum.at<double>(idx[2]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[3]); powerSpectrum.at<double>(idx[3]) = 0;
        minMaxIdx(powerSpectrum, 0, 0, 0, &idx[4]); powerSpectrum.at<double>(idx[4]) = 0;


        // calculate *RPM
        rpm = idx[0] * currentFps * 60. / total; // constant 30 = 60 BPM / 2

    }


//    qDebug() << "Max rpm" << rpm << "Lavg rpm" << Lrpm;

    */

   // qDebug() << "bpm is: " << bpm << "with ID:" <<  idx[0] << "second opinions are:" << idx[1] << idx[2] << idx[3] << idx[4]
   //          << "avg" << ((idx[1]+idx[2]+idx[3]+idx[4])/4);

    // Final BPMs after possible post-processing

    Frpm = Lrpm;

    return Frpm;

}


}

