#ifndef MTFILTERSTABILIZER_H
#define MTFILTERSTABILIZER_H
#include <stdlib.h>
#include <vector>

//#include <opencv/cv.h>
#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

typedef struct MTABOneParameter
{
    //for alphaBeta, X,y,size
    float vkX_1;
    float xkX_1;
    float lastX;
    float maxMotion;

    float vkY_1;
    float xkY_1;
    float lastY;
    //float maxMotionY; // We assume the same maxMotion temporally
} MTABOneParameter;

typedef struct MTABParameterSet
{
    std::vector<MTABOneParameter> parameters;
    int nbrOfParameters;
    float alfa;
    float beta;
} MTABParameterSet;


class MTFilterStabilizer
{
public:
    MTFilterStabilizer();

    // Variables
    MTABParameterSet data_points;


    // Methods
    void InitializeABSetOfParameters( std::vector<cv::Point> * parameters, float maxMotion, int nbrOfParameters, float alfa, float beta);
    void PredictParameterSet(std::vector<cv::Point> * measurementsIn, std::vector<cv::Point> * predictionsOut);
    void ReleaseABSetOfParameters();



};

#endif // MTFILTERSTABILIZER_H
