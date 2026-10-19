#ifndef EXT_OPENCV_HPP
#define	EXT_OPENCV_HPP

#include <iostream>
#include <opencv2/core/core.hpp>


using std::cout;
using std::endl;

namespace cv {

const Scalar BLACK    (  0,   0,   0);
const Scalar RED      (255,   0,   0);
const Scalar GREEN    (  0, 255,   0);
const Scalar BLUE     (  0,   0, 255);
const Scalar WHITE    (255, 255, 255);

const Scalar ZERO     (0);

void normalization(InputArray _a, OutputArray _b);

void meanFilter(InputArray _a, OutputArray _b, size_t n = 3, Size s = Size(5, 5));

void interpolate(const Rect& a, const Rect& b, Rect& c, double p);

void detrend(InputArray _z, OutputArray _r, int lambda = 10);

int countZeros(InputArray _a);

double bpm(Mat1d pulse, double current_Fps);
double rpm(Mat1d resp, double current_Fps);

double bpmMultiple(Mat1d pulse0, Mat1d pulse1, Mat1d pulse2, Mat1d pulse3, double current_Fps);

void spectrumF(Mat1d pulse, Mat &powerSpectrum);
void spectrumB(Mat1d pulse, Mat &powerSpectrum);

double bpmFromSpectrum(Mat1d powerSpectrum, double currentFps);

double rpmFromSpectrum(Mat1d powerSpectrum, double currentFps);



}

#endif	/* EXT_OPENCV_HPP */

