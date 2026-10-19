#include "healthsignals.h"
#include <iostream>
#include "QtDebug"

#define f_s 30

healthsignals::healthsignals()
{

}

healthsignals::~healthsignals()
{
    reset();
}

void healthsignals::initSignals()
{



    //Heart
    filterN   = new Filter(BPF, 61, f_s, 0.7, 2.5); //
    filterDB  = new Filter(BPF, 61, f_s, 0.75, 1.75);  //Heart_Extended 45 to 240 bpm
    filterDG  = new Filter(BPF, 61, f_s, 0.75, 1.75);  //
    filterDR  = new Filter(BPF, 61, f_s, 0.75, 1.75);   //
    filterNowHR = new Filter(BPF, 61, f_s, 0.8, 2);  //For real-time display, assume a heart from 60bpm to 120bpm


    //Breathing
    //filterBreath = new Filter(BPF, 61, f_s, 0.3, 0.8);
    filterBreath = new Filter(LPF, 9, f_s, 0.7);

    filterNowBR = new Filter(LPF, 9, f_s, 0.7);

    
    seconds = 0;
    sigStable = 0;
    signalCounter = 0;
    userReset = 1;
    total_time_fseg = 0;
    total_time_filt = 0;
    total_time_rPPG = 0;
    total_time_Spect = 0;

    resetValues();
}

void healthsignals::resetValues()
{
    normalFace = cv::Mat::zeros(cv::Size(384,288), CV_8UC3);
    eulerian = cv::Mat::zeros(cv::Size(200,150), CV_8UC3);

    //Last values
    last_pulse = 0;
    last_breath = 0;

    //Breathing
    breathingRate = 0;

    //Heart PPG
    heartRate = 0.0;
    SpO2 = 0.0;
}


void healthsignals::reset(){

    //clear deques;
    timestamps.clear();
    raw_breathing.clear();
    filtered_resp.clear();

    raw_ppgN.clear();
    raw_ppgR.clear();
    raw_ppgG.clear();
    raw_ppgB.clear();

    filtered_pulseN.clear();
    filtered_ppgR.clear();
    filtered_ppgG.clear();



    seconds = 0;

    signalCounter = 0;

    sigStable = 0;

    userReset = 1;

    resetValues();

}
void healthsignals::addSignals()
{
    userReset = 0;

    cv::Mat eulerianinput = normalFace.clone();

    time0 = cv::getTickCount();
    //Copy normalFace for checking breathing ---> a better area should be selected
    //Crop central part of the face to avoid noisy borders
    cv::Mat center = normalFace(cv::Rect(normalFace.cols/8, 0, 6*normalFace.cols/8,2*normalFace.rows/3));

    //cv::Mat breathArea = normalFace.clone();
    cv::Mat breathArea = center;

    //Eliminate eye region
    cv::Mat eyeROI = normalFace(cv::Rect(normalFace.cols/7, 2*normalFace.rows/12, 5*normalFace.cols/7,2*normalFace.rows/12));
    eyeROI.setTo(cv::Scalar(0, 0, 0));
    //Eliminate moustache
    cv::Mat moustachROI = normalFace(cv::Rect(normalFace.cols/4, 7*normalFace.rows/12, 2*normalFace.cols/4,1*normalFace.rows/12));
    moustachROI.setTo(cv::Scalar(0, 0, 0));

    time1 = cv::getTickCount();
    total_time_fseg += (time1 - time0) / cv::getTickFrequency();


    time0 = cv::getTickCount();
    //Lab colorspace
    cv::Mat centerLab;
    cv::cvtColor(center, centerLab, cv::COLOR_BGR2Lab);
    time1 = cv::getTickCount();
    total_time_rPPG += (time1 - time0) / cv::getTickFrequency();

    cv::Mat breathAreaLab;
    cv::cvtColor(breathArea, breathAreaLab, cv::COLOR_BGR2Lab);

    //Show images to be sure they are right (debug)
    //cv::imshow("centerLab", centerLab);
    //cv::imshow("Normal", normalFace);
    //cv::imshow("center", center );
    //cv::imshow("eyes", eyeROI);
    //cv::imshow("moustache", moustachROI);
    //cv::imshow("breathArea", breathArea);
    //cv::imshow("euleriansignal", eulerian);
    //cv::waitKey(10);

    //RawSignals
    cv::Scalar rawNormal = cv::mean(center);     //Raw green
    cv::Scalar rawsignalN  = cv::mean(centerLab); //Raw a-channel from Lab
    cv::Scalar rawbreath  = cv::mean(breathArea);
    cv::Scalar rawbreathLab  = cv::mean(breathAreaLab);   //Raw green

    //RGB colorspace, used for SpO2 calculations
    double red   = - (rawNormal[2]);
    double green = - (rawNormal[1]);
    double blue  = - (rawNormal[0]);

    //Lab colorspace, used for heartRate calculations
    double Lab = - (rawsignalN[1]);  //Channel a from Lab --> more robust to illumination changes than green

    //For the moment in RGB colorspace, but something better could be done
    double breath =  - (rawbreath[1]);
    double breathLab = - (rawbreathLab[0]); //

    //Timestamps
    double time = (double) cv::getTickCount(); // timestamp



    if (signalCounter >0)
    {
        //Values for the instantaneous signals. Something better could be done here

        //double now_pulse  = filterNowHR->do_sample(Lab);
        //double now_breath = filterNowBR->do_sample(breathLab);

        time0 = cv::getTickCount();

        double filt_pulseN = filterN->do_sample(Lab);

        double filt_resp  = filterBreath->do_sample(breathLab);

        double filt_red   = filterDR->do_sample(red);    //SpO2: pulsatile components of red and green
        double filt_green = filterDG->do_sample(green);

        //start filling the signals
        timestamps.push_back(time); //qDebug() << "time" << time;

        raw_ppgB.push_back(blue);
        raw_ppgG.push_back(green);
        raw_ppgR.push_back(red);

        raw_ppgN.push_back(Lab);

        raw_breathing.push_back(breathLab);

        time1 = cv::getTickCount();
        total_time_filt += (time1 - time0) / cv::getTickFrequency();


        time0 = cv::getTickCount();
        if (signalCounter % 30 == 0)   //Every Second -> Calculate the signals again
        {
            calculateSignals();
        }

        time1 = cv::getTickCount();
        total_time_Spect += (time1 - time0) / cv::getTickFrequency();


        if (signalCounter >60)  //Two seconds, filters filled
        {

            filtered_pulseN.push_back(filt_pulseN);

            filtered_resp.push_back(filt_resp);

            filtered_ppgR.push_back(filt_red);
            filtered_ppgG.push_back(filt_green);

            //Start updating the signal plots
            //last_pulse = now_pulse;
            //last_breath = now_breath;

            double maxHR =* std::max_element(filtered_pulseN.begin(), filtered_pulseN.end());
            double minHR =* std::min_element(filtered_pulseN.begin(), filtered_pulseN.end());
            double maxBR =* std::max_element(filtered_resp.begin(), filtered_resp.end());
            double minBR =* std::min_element(filtered_resp.begin(), filtered_resp.end());

            double denom;
            denom = maxHR-minHR;
            if (denom < 0.001) denom = 0.001;
            last_pulse = (200*(filt_pulseN - minHR) / denom) - 100;
            denom = maxBR-minBR;
            if (denom < 0.001) denom = 0.001;
            last_breath = (200*(filt_resp - minBR) / denom) - 100;

            eulerian = EulerianMagnify(eulerianinput);

        }

    }


    //heart & breathing
    if (signalCounter >= 360)  //10 (+2) seconds have passed, eliminate one sample each time. Probably more efficient if done in SECONDS-30-SAMPLES and not in SINGLE-SAMPLES
    {
        timestamps.pop_front();

        raw_ppgN.pop_front();
        raw_ppgB.pop_front();
        raw_ppgG.pop_front();
        raw_ppgR.pop_front();

        raw_breathing.pop_front();
        filtered_resp.pop_front();

        filtered_pulseN.pop_front();
        filtered_ppgR.pop_front();
        filtered_ppgG.pop_front();
    }

    signalCounter++;

    qDebug() << "Times at frame: " << signalCounter << " - [FsEG]:" << total_time_fseg;
    qDebug() << "Times at frame: " << signalCounter << " - [rPPG]:" << total_time_rPPG;
    qDebug() << "Times at frame: " << signalCounter << " - [FILT]:" << total_time_filt;
    qDebug() << "Times at frame: " << signalCounter << " - [SPECT]:" << total_time_Spect;


}


void healthsignals::calculateSignals()
{

    time();
    spo2();

    heart();


    breath();

    seconds ++;

//    qDebug () << "fps" << currentFps << "HeartRate" << heartRate << "Breathing" << breathingRate << "SpO2" << SpO2 << "Seconds" << seconds << "    Stability" << sigStable;

     // Number of signals that have been calculated
}



void healthsignals::time()
{

    const double diff = 1000 * (timestamps.back() - timestamps.front()) / (cv::getTickFrequency()*(timestamps.size()-1));
    currentFps = 1000 / (diff);
    //qDebug() << "diff" << diff << "Current fps:" << currentFps;

    //currentFps = 30.0;

}


void healthsignals::heart()
{
    cv::Mat1d pulseN;
    if (seconds < 8) {
        for (unsigned int i = 0; i < raw_ppgN.size(); i++) { pulseN.push_back(raw_ppgN.at(i));}
        sigStable = 0;
    } else {
        for (unsigned int i = 0; i < filtered_pulseN.size(); i++) { pulseN.push_back(filtered_pulseN.at(i));}
        sigStable = 1;
    }

    cv::Mat1d powerSpectrumN;
    spectrumF(pulseN, powerSpectrumN);

    double testBPMN = bpmFromSpectrum(powerSpectrumN, currentFps);


    heartRate = testBPMN;

}

void healthsignals::breath()
{

    cv::Mat1d resp;
    for (unsigned int i = 0; i < raw_breathing.size(); i++) { resp.push_back(raw_breathing.at(i));}
    //for (unsigned int i = 0; i < filtered_resp.size(); i++) { resp.push_back(filtered_resp.at(i));}


    cv::Mat1d powerSpectrumResp;
    spectrumB(resp, powerSpectrumResp);

    double testRPM = rpmFromSpectrum(powerSpectrumResp, currentFps);


    breathingRate = testRPM;


}

//Simplest possible version of the computation
void healthsignals::heart0()
{

    cv::Mat1d pulse;
    for (unsigned int i = 0; i < raw_ppgG.size(); i++) { pulse.push_back(raw_ppgG.at(i));}

    double testBPM = bpm(pulse, currentFps);

    heartRate = testBPM;
}

//Simplest possible version of the computation
void healthsignals::breath0()
{
    cv::Mat1d resp;
    for (unsigned int i = 0; i < filtered_resp.size(); i++) { resp.push_back(filtered_resp.at(i));}

    double testRPM = rpm(resp, currentFps);

    breathingRate = testRPM;
}


void healthsignals::spo2()
{

    std::vector<double> deriv_ppgB, deriv_ppgG, deriv_ppgR;
    for (unsigned int i = 1; i < raw_ppgB.size(); i++) { deriv_ppgB.push_back(abs(raw_ppgB.at(i))); }
    for (unsigned int i = 1; i < raw_ppgG.size(); i++) { deriv_ppgG.push_back(abs(raw_ppgG.at(i))); }
    for (unsigned int i = 1; i < raw_ppgR.size(); i++) { deriv_ppgR.push_back(abs(raw_ppgR.at(i))); }

    cv::Mat1d EnerR, EnerG, EnerB;

    for (unsigned int i = 0; i < deriv_ppgR.size(); i++) { EnerR.push_back(deriv_ppgR.at(i)); }
    for (unsigned int i = 0; i < deriv_ppgG.size(); i++) { EnerG.push_back(deriv_ppgG.at(i)); }
    for (unsigned int i = 0; i < deriv_ppgB.size(); i++) { EnerB.push_back(deriv_ppgB.at(i)); }


    cv::Scalar meanR, meanG, meanB, stdR, stdG, stdB;
    cv::meanStdDev(EnerR, meanR, stdR);
    cv::meanStdDev(EnerG, meanG, stdG);
    cv::meanStdDev(EnerB, meanB, stdB);

    //AC components: standard deviation of the red and green values filtered in the heart band (filterDR, filterDG),
    //i.e. their pulsatile components. DC components: mean of the raw values (above).
    if (filtered_ppgR.size() < 2 || filtered_ppgG.size() < 2) return;
    cv::Mat1d pulsatileR, pulsatileG;
    for (unsigned int i = 0; i < filtered_ppgR.size(); i++) { pulsatileR.push_back(filtered_ppgR.at(i)); }
    for (unsigned int i = 0; i < filtered_ppgG.size(); i++) { pulsatileG.push_back(filtered_ppgG.at(i)); }
    cv::Scalar meanPR, meanPG, acR, acG;
    cv::meanStdDev(pulsatileR, meanPR, acR);
    cv::meanStdDev(pulsatileG, meanPG, acG);


    double RG, RB;
    RG = ( (550*(acG[0]*meanR[0])) / (850*(acR[0]*meanG[0])) );
    RB = ( (380*(stdB[0]*meanR[0])) / (850*(stdR[0]*meanB[0])) );

    //Ratio of ratios with empirical constants (weights 550 and 850, A = 100, B = 2), not calibrated against reference
    //SpO2 measurements (see the paper): SpO2 = A - B * (Lambda Green * Gac/Gdc) / (Lambda Red * Rac/Rdc)
    double Acalib = 100; double Bcalib = 2.0;
    SpO2 = Acalib - (Bcalib*RG);

    //qDebug() << "Spo2" << SpO2 << "RG/RB" << RG << RB << "Values" << meanG[0] << stdG[0] << meanR[0] << stdR[0] << meanB[0] << stdB[0];
}


cv::Mat healthsignals::EulerianMagnify(cv::Mat frameinput)
{
    cv::Mat frame;
    // Separate luminance and chrominance components
    cv::Size s(200,150);
    cv::resize(frameinput, frame, s);
    frame.convertTo(frame, CV_32F, 1./255);
    cv::cvtColor(frame, frame, cv::COLOR_BGR2Lab);

    float val = (-last_pulse+100)/30;

        // Filter
    std::vector<cv::Mat> out_channels(3);
    out_channels[0] = cv::Mat::zeros(frame.size(), CV_32F);
    out_channels[1] = cv::Mat::zeros(frame.size(), CV_32F);
    out_channels[2] = cv::Mat::zeros(frame.size(), CV_32F);

     std::vector<cv::Mat> in_channels(3);
     cv::split(frame, in_channels);

        out_channels[0] =  in_channels[0];
        out_channels[1] =  in_channels[1]*0.6 + (in_channels[1]*val)*0.4;
        out_channels[2] =  in_channels[2]*0.8 + (in_channels[2]*val)*0.2;

    // Merge L, a, and bb channels into single frame
    cv::merge(out_channels, frame);

    cv::cvtColor(frame, frame, cv::COLOR_Lab2BGR);
    frame.convertTo(frame, CV_8U, 255.);

    return frame;

}



//EXTRA CODE
//CvPlot::clear("Pulse");
//CvPlot::plot("Pulse", (double*) powerSpectrumN.data, powerSpectrumN.rows/15, 1, 0, 0 , 200);     CvPlot::label("raw_Breath");
//
