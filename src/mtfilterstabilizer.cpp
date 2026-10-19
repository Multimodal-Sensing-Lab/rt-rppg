#include "mtfilterstabilizer.h"

MTFilterStabilizer::MTFilterStabilizer(){}

void MTFilterStabilizer::InitializeABSetOfParameters(std::vector<cv::Point> * parameters, float maxMotion, int nbrOfParameters, float alfa, float beta)
{
    int i;
    data_points.parameters.clear();
    data_points.parameters.resize(nbrOfParameters);
    data_points.nbrOfParameters = nbrOfParameters;
    data_points.alfa = alfa;
    data_points.beta = beta;
    for (i = 0; i < nbrOfParameters; i++)
    {
        data_points.parameters[i].vkX_1 = 0;
        data_points.parameters[i].xkX_1 = parameters->at(i).x;
        data_points.parameters[i].lastX = parameters->at(i).x;
        data_points.parameters[i].maxMotion = maxMotion; // We can send a vector for having a particular maxMotion for each point.

        data_points.parameters[i].vkY_1 = 0;
        data_points.parameters[i].xkY_1 = parameters->at(i).y;
        data_points.parameters[i].lastY = parameters->at(i).y;
        //data_points.parameters[i].maxMotionY = maxMotion; // We can send a vector for having a particular maxMotion for each point.
    }
}


float GetOnePredictionStabAB(float * axk, float * avk, float input, float alfa, float beta)
{
    float a = alfa;
    float b = beta;
    float xk;
    float dt = 1.0f;
    float rk;


    // prediction
    xk = *axk + *avk*dt;

   // error
    rk = input - xk;

    // new value=prediction+a*error
    *axk = xk + a*rk;
    *avk += (b*rk) / dt;

    //*avk *= 0.9;
    return *axk;
}



void MTFilterStabilizer::PredictParameterSet(std::vector<cv::Point> * measurementsIn, std::vector<cv::Point> * predictionsOut)
{
    int i;
    cv::Point predictiondist;
    cv::Point input;
    float a;
    cv::Point prediction, correction, error;


    for (i = 0; i < data_points.nbrOfParameters; i++)
    {
        // difference to prediction
        input.x = measurementsIn->at(i).x;
        input.y = measurementsIn->at(i).y;
        predictiondist.x = measurementsIn->at(i).x - (data_points.parameters[i].xkX_1 + data_points.parameters[i].vkX_1);
        predictiondist.y = measurementsIn->at(i).y - (data_points.parameters[i].xkY_1 + data_points.parameters[i].vkY_1);

        // new position would be
        a = data_points.alfa;
        prediction.x = (data_points.parameters[i].xkX_1 + data_points.parameters[i].vkX_1);
        correction.x = (1.0f - a)*prediction.x + (a - 1.0f)*input.x; //(correction=prediction+measurement)
        prediction.y = (data_points.parameters[i].xkY_1 + data_points.parameters[i].vkY_1);
        correction.y = (1.0f - a)*prediction.y + (a - 1.0f)*input.y; //(correction=prediction+measurement)

        // X correction
        if (correction.x > data_points.parameters[i].maxMotion)
        {
            error.x = correction.x - data_points.parameters[i].maxMotion;
            data_points.parameters[i].xkX_1 = data_points.parameters[i].xkX_1 - error.x / (1.0f - a);
            prediction.x = (data_points.parameters[i].xkX_1 + data_points.parameters[i].vkX_1);
            correction.x = (1.0f - a)*prediction.x + (a - 1.0f)*input.x;
        }

        if (correction.x < -data_points.parameters[i].maxMotion)
        {
            error.x = - data_points.parameters[i].maxMotion - correction.x;
            data_points.parameters[i].xkX_1 = data_points.parameters[i].xkX_1 + error.x / (1.0f - a);
            prediction.x = (data_points.parameters[i].xkX_1 + data_points.parameters[i].vkX_1);
            correction.x = (1.0f - a)*prediction.x + (a - 1.0f)*input.x;
        }

        // Y Correction
        if (correction.y > data_points.parameters[i].maxMotion)
        {
            error.y = correction.y - data_points.parameters[i].maxMotion;
            data_points.parameters[i].xkY_1 = data_points.parameters[i].xkY_1 - error.y / (1.0f - a);
            prediction.y = (data_points.parameters[i].xkY_1 + data_points.parameters[i].vkY_1);
            correction.y = (1.0f - a)*prediction.y + (a - 1.0f)*input.y;
        }

        if (correction.y < -data_points.parameters[i].maxMotion)
        {
            error.y = - data_points.parameters[i].maxMotion - correction.y;
            data_points.parameters[i].xkY_1 = data_points.parameters[i].xkY_1 + error.y / (1.0f - a);
            prediction.y = (data_points.parameters[i].xkY_1 + data_points.parameters[i].vkY_1);
            correction.y = (1.0f - a)*prediction.y + (a - 1.0f)*input.y;
        }


        // Prediction
        predictionsOut->at(i).x = GetOnePredictionStabAB(&data_points.parameters[i].xkX_1, &data_points.parameters[i].vkX_1, measurementsIn->at(i).x, data_points.alfa, data_points.beta);
        predictionsOut->at(i).y = GetOnePredictionStabAB(&data_points.parameters[i].xkY_1, &data_points.parameters[i].vkY_1, measurementsIn->at(i).y, data_points.alfa, data_points.beta);


        predictiondist.x = predictionsOut->at(i).x - input.x;
        predictiondist.y = predictionsOut->at(i).y - input.y;


        if (predictiondist.x >- data_points.parameters[i].maxMotion / 2.0 && predictiondist.x < data_points.parameters[i].maxMotion / 2.0)
        {
            data_points.parameters[i].vkX_1 *= 0.9f;
        }
        if (predictiondist.y >- data_points.parameters[i].maxMotion / 2.0 && predictiondist.y < data_points.parameters[i].maxMotion / 2.0)
        {
            data_points.parameters[i].vkY_1 *= 0.9f;
        }



        if (predictiondist.x > -data_points.parameters[i].maxMotion / 4.0 && predictiondist.x < data_points.parameters[i].maxMotion / 4.0)
        {
            data_points.parameters[i].vkX_1 *= 0.9f;
        }
        if (predictiondist.y > -data_points.parameters[i].maxMotion / 4.0 && predictiondist.y < data_points.parameters[i].maxMotion / 4.0)
        {
            data_points.parameters[i].vkY_1 *= 0.9f;
        }


        if (predictiondist.x > -data_points.parameters[i].maxMotion / 32.0 && predictiondist.x < data_points.parameters[i].maxMotion / 32.0)
        {
            data_points.parameters[i].vkX_1 *= 0.6f;
        }
        if (predictiondist.y > -data_points.parameters[i].maxMotion / 32.0 && predictiondist.y < data_points.parameters[i].maxMotion / 32.0)
        {
            data_points.parameters[i].vkY_1 *= 0.6f;
        }

        if (predictiondist.x*data_points.parameters[i].vkX_1>0)
        {
            //	data_points.parameters[i].vkX_1 =0;
        }


    }
}

void MTFilterStabilizer::ReleaseABSetOfParameters()
{
     data_points.parameters.clear();
}