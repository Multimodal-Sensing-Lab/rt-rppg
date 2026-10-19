#include "process.h"
#include <iostream>
#include "QtDebug"
#include <math.h>
#include <cstdlib>
#include <string>


process::process()
{

}

process::~process()
{

}



void process::initProcess()
{

    countRGB = 0; control_init=true;
    total_time_fd = 0;
    total_time_fl = 0;
    total_time_fn = 0;

    // DLIB FACE DETECTOR
//    detector = dlib::get_frontal_face_detector();
//    detector = cv::FaceDetectorYN::create("models/face_detection_yunet_2022mar_int8.onnx", "",  cv::Size(320, 320),  0.7f,  0.3f, 5000, 0, 0);
    // YuNet runs with the DNN module of OpenCV, on the CPU by default (as in the measurements of the paper).
    // Optionally (README, "GPU"): RT_RPPG_DNN=opencl|opencl_fp16|cuda|cuda_fp16 selects another backend and target,
    // and RT_RPPG_FACE_MODEL another model file (the CUDA backend needs the fp32 model, not the int8 one).
    const char *model = std::getenv("RT_RPPG_FACE_MODEL");
    const char *dnn = std::getenv("RT_RPPG_DNN");
    std::string device = dnn ? dnn : "cpu";
    int backend = cv::dnn::DNN_BACKEND_DEFAULT, target = cv::dnn::DNN_TARGET_CPU;
    if (device == "opencl") { backend = cv::dnn::DNN_BACKEND_OPENCV; target = cv::dnn::DNN_TARGET_OPENCL; }
    else if (device == "opencl_fp16") { backend = cv::dnn::DNN_BACKEND_OPENCV; target = cv::dnn::DNN_TARGET_OPENCL_FP16; }
    else if (device == "cuda") { backend = cv::dnn::DNN_BACKEND_CUDA; target = cv::dnn::DNN_TARGET_CUDA; }
    else if (device == "cuda_fp16") { backend = cv::dnn::DNN_BACKEND_CUDA; target = cv::dnn::DNN_TARGET_CUDA_FP16; }
    else if (device != "cpu") qDebug() << "Unknown RT_RPPG_DNN" << dnn << "- using the CPU";
    if (device != "cpu" || (model && *model))
        qDebug() << "Face detector:" << (model && *model ? model : "default model") << "on" << device.c_str();
    detector = cv::FaceDetectorYN::create(model && *model ? model : "models/face_detection_yunet_2022mar_int8.onnx", "",
                                          cv::Size(320, 320), 0.7f, 0.3f, 5000, backend, target);
    detector->setInputSize(cv::Size(192, 144));

    // LANDMARK MODEL LOADING
//    dlib::deserialize("models/shape_predictor_68_face_landmarks.dat") >> pose_model;
    dlib::deserialize("models/shape_predictor_68_face_landmarks_gtx.dat") >> pose_model;



    //Initialize Health Signals
    sig.initSignals();

}

void process::detectFacesRGB(cv::Mat frame)
{

//        qDebug() << "Tino9a:" <<  frame.cols << "," << frame.rows << "  ch=" << frame.channels() << " Type=" << frame.type();
        time0 = cv::getTickCount();
        dlib::cv_image<dlib::bgr_pixel> cimg_big(frame);


        if ( countRGB % SKIP_FRAMES == 0 )
        {
            //Face detection
            cv::Mat frame_small;
            cv::resize(frame, frame_small, cv::Size(), 1.0/FACE_DOWNSAMPLE_RATIO, 1.0/FACE_DOWNSAMPLE_RATIO);
//            qDebug() << "Tino9b:" << frame_small.cols << "," << frame_small.rows << "  ch=" << frame_small.channels() << " Type=" << frame_small.type();
//            dlib::cv_image<dlib::bgr_pixel> cimg(frame_small);
//            faces = detector(cimg);
            detector->detect(frame_small, faces_temp);
            faces.clear();
            for (int i = 0; i < faces_temp.rows; ++i)
            {
                    // Draw bounding boxes
                    int x1 = static_cast<int>(faces_temp.at<float>(i, 0));
                    int y1 = static_cast<int>(faces_temp.at<float>(i, 1));
                    int w = static_cast<int>(faces_temp.at<float>(i, 2));
                    int h = static_cast<int>(faces_temp.at<float>(i, 3));
                    y1 = (y1 + h/2) - w/2;
                    h = w;
                    faces.push_back(dlib::rectangle(int(x1),int(y1), int((x1 + w)),int((y1 + h))));
             }

        }

         time1 = cv::getTickCount();
         total_time_fd += (time1 - time0) / cv::getTickFrequency();

//        qDebug() << "Tino9c:" << faces.size();


        if (faces.size() > 0 && countRGB >= 0)
        {
            time0 = cv::getTickCount();
            FaceActive = 1;
            shapeIn = pose_model(cimg_big, dlib::rectangle(faces[0].left()*FACE_DOWNSAMPLE_RATIO,faces[0].top()*FACE_DOWNSAMPLE_RATIO, faces[0].right()* FACE_DOWNSAMPLE_RATIO,faces[0].bottom()* FACE_DOWNSAMPLE_RATIO));
//            shapeIn = pose_model(cimg_big, dlib::rectangle(int(faces[0].x*FACE_DOWNSAMPLE_RATIO),int(faces[0].y*FACE_DOWNSAMPLE_RATIO), int((faces[0].width + faces[0].x)* FACE_DOWNSAMPLE_RATIO),int((faces[0].height + faces[0].y)* FACE_DOWNSAMPLE_RATIO)));
            RGBlandmarks = lan.get68LandMarks( shapeIn );
            time1 = cv::getTickCount();
            total_time_fl += (time1 - time0) / cv::getTickFrequency();

//            qDebug() << "Tino9c:" << faces.size();

            time0 = cv::getTickCount();
            //Forward Normal Face to Health
            selectNormalFaceRGB(frame);
            time1 = cv::getTickCount();
            total_time_fn += (time1 - time0) / cv::getTickFrequency();
//            qDebug() << "Tino9d:" << faces.size();
            //Adding signals
            sig.addSignals();

        } else { FaceActive = 0; }


        countRGB++;
        qDebug() << "Times at frame: " << countRGB << " - [FD]:" << total_time_fd;
        qDebug() << "Times at frame: " << countRGB << " - [FL]:" << total_time_fl;
        qDebug() << "Times at frame: " << countRGB << " - [FN]:" << total_time_fn;

//        qDebug() << "Tino9f:" << faces.size();

}


void process:: selectNormalFaceRGB(cv::Mat img)
{
    //Trying face normalization


    if(control_init){
        estabilizer.InitializeABSetOfParameters(&RGBlandmarks, 4, LANDMARKPOINTS, 0.6, 0.001);
        control_init = false;
    }else{
        estabilizer.PredictParameterSet(&RGBlandmarks, &RGBlandmarks);

    }


    cv::Mat normalFace = lan.getNormalizedFace122(img , &RGBlandmarks);
    normalFace.copyTo(sig.normalFace);


    // Drawing the landmark points
    for (int i = 0; i < LANDMARKPOINTS; i++){
        cv::circle(img, RGBlandmarks.at(i), 2, cv::Scalar(0, 255, 0), -1);
    }

    if(lan.badPositioning)
    {
        // cv::rectangle(img, cv::Point(0,0) , cv::Point(img.cols-1, img.rows-1), cv::Scalar(0,0,255), 5);
    }


}

