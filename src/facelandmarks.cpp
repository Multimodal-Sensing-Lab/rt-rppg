#include "facelandmarks.h"
#include "QtDebug"


int myErrorHandler(int status, const char* func_name, const char* err_msg,
                   const char* file_name, int line, void*)
{
    // Silences the OpenCV error messages (the errors are handled where they occur)
    // qDebug() << "Bad positioning"; // Do NOTHING
    return 0;
}

FaceLandmarks::FaceLandmarks()
{
 badPositioning = 0;
}

FaceLandmarks::~FaceLandmarks()
{

}

// FOR THE FACE-ALIGNMENT
float canonical_face_x[68] = {0,0.0031491,0.017858,0.04622,0.098961,0.17921,0.27684,0.38235,0.5,0.61765,0.72316,0.82079,0.90104,0.95378,0.98214,0.99685,1,0.088736,0.15089,0.24244,0.33716,0.4245,0.5755,0.66284,0.75756,0.84911,0.91126,0.5,0.5,0.5,0.5,0.39488,0.44559,0.5,0.55441,0.60512,0.19216,0.24772,0.31339,0.37289,0.31022,0.24375,0.62711,0.68661,0.75228,0.80784,0.75625,0.68978,0.30369,0.37641,0.4498,0.5,0.5502,0.62359,0.69631,0.62539,0.55461,0.5,0.44539,0.37461,0.33321,0.44905,0.5,0.55095,0.66679,0.55199,0.5,0.44801};
float canonical_face_y[68] = {0.18154,0.3199,0.4571,0.59077,0.71438,0.82062,0.90863,0.97893,1,0.97893,0.90863,0.82062,0.71438,0.59077,0.4571,0.3199,0.18154,0.083538,0.019965,0,0.013752,0.052983,0.052983,0.013752,0,0.019965,0.083538,0.15964,0.24807,0.33543,0.4259,0.48942,0.50773,0.52316,0.50773,0.48942,0.17046,0.13783,0.13864,0.18552,0.19548,0.19453,0.18552,0.13864,0.13783,0.17046,0.19453,0.19548,0.64961,0.62165,0.60536,0.61907,0.60536,0.62165,0.64961,0.72347,0.7562,0.76236,0.7562,0.72347,0.6535,0.64844,0.6545,0.64844,0.6535,0.68948,0.69621,0.68948};

// FOR THE SWAPPING AND SYMMETRY MEASUREMENT
static std::vector<std::vector<int>> indexes17triangles= {{1, 19, 27},{1, 27, 31}, {27, 31, 33},{1, 31, 48},{1, 4, 48},{4, 8, 48},{31, 33, 48},{8, 33, 48},\
                                                          {15, 24, 27}, {15, 27, 35},{27, 33, 35},{15, 35, 54},{12, 15, 54},{8, 12, 54},{33, 35, 54},{8, 33, 54}, \
                                                          {19, 24, 27}};

static std::vector<std::vector<int>> indexes122triangles = {{0, 1, 36},  {0, 17, 36}, {1, 36, 68}, {1, 2, 68}, {2, 3, 68}, {3, 48, 68}, {3, 4, 48}, {4, 5, 48}, {5, 6, 48},\
                                                            {6, 48, 59}, {6, 7, 59}, {7, 58, 59}, {7, 8, 58}, {8, 57, 58}, {8, 9, 57}, {36, 41, 68}, {41, 68, 69}, {48, 68, 69}, {48, 31, 69},\
                                                            {29, 31, 69}, {28, 29, 69}, {27, 28, 69},{27, 39, 69}, {39, 40, 69}, {40, 41, 69}, {27, 39, 21},{20, 21, 39}, {20, 39, 38},{20, 19, 38}, \
                                                            {19, 37, 38}, {18, 19, 37},  {18, 36, 37}, {17, 18, 36}, {29, 30, 31}, {30, 31, 32}, {30, 32, 33}, {48, 49, 31}, {49, 50, 31}, {31, 32, 50},\
                                                            {32, 50, 51}, {32, 33, 51}, {48, 49, 60}, {48, 59, 60}, {49, 60, 61}, {49, 50, 61}, {50, 61, 62}, {50, 51, 62},  {59, 60, 67}, {58, 59, 67},\
                                                            {58, 66, 67}, {58, 57, 66}, {9, 56, 57},  {9, 10, 56}, {10, 55, 56}, {10, 11, 55}, {11, 54, 55}, {11, 12, 54},{12, 13, 54}, {13, 54, 70},\
                                                            {54, 70, 71}, {54, 71, 35}, {54, 53, 35}, {53, 52, 35}, {53, 52, 35}, {52, 34, 35}, {51, 52, 34}, {33, 34, 51}, {56, 57, 66}, {56, 65, 66},\
                                                            {55, 56, 65}, {55, 64, 65}, {55, 64, 54}, {54, 53, 64}, {53, 63, 64}, {52, 53, 63}, {52, 62, 63},{51, 52, 62}, {30, 33, 34},{30, 34, 35}, \
                                                            {30, 29, 35},{35, 71, 29}, {71, 29, 28}, {71, 28, 27}, {71, 27, 42}, {71, 47, 42}, {71, 46, 47},{71, 70, 46}, {70, 45, 46},{70, 45, 15}, \
                                                            {70, 15, 14} , {70, 14, 13}, {21, 22, 27}, {42, 22, 27}, {42, 22, 23}, {42, 43, 23}, {43, 23, 24}, {24, 43, 44}, {24, 25, 44}, {25, 44, 45}, \
                                                            {25, 26, 45}, {26, 16, 45}, {15, 16, 45}, {0, 17, 72}, {17, 18, 72}, {18, 72, 73},  {18, 19, 73},{19, 73, 74}, {19, 74, 75},{19, 20, 75}, \
                                                            {20, 21, 75},{21, 75, 76}, {21, 76, 77}, {21, 22, 77}, {22, 77, 78}, {22, 78, 79}, {22, 23, 79}, {23, 24, 79}, {24, 79, 80},{24, 25, 80}, \
                                                            {25, 80, 81}, {25, 26, 81}, {16, 26, 81}};

// FOR THE MESH GENERATION
static int mesh_index_a[24] = {19, 1, 4, 8, 12, 15, 24, 19, 27, 1, 31, 27, 33, 31, 48, 33, 54, 35, 33, 27, 35, 15, 27, 24};
static int mesh_index_b[4]  = {33, 8, 54, 15};


//*******************************************//
//      LANDMARK PROCESSING FUNCTIONS        //
//*******************************************//
std::vector<cv::Point> FaceLandmarks::get68LandMarks( dlib::full_object_detection &shape)
{
    std::vector<cv::Point> LandMarks;
    cv::Point LLPoint;
    for (int i = 0; i < LANDMARKPOINTS; i++)
    {
        LLPoint.x = shape.part(i).x();
        LLPoint.y = shape.part(i).y();
        LandMarks.push_back(LLPoint);
    }
    return LandMarks;
}


void FaceLandmarks::get82Landmarks2(cv::Mat &image, std::vector<cv::Point> *LLpoints_wExtras)
{

    // Computing strategic points.
    cv::LineIterator it(image, LLpoints_wExtras->at(48), LLpoints_wExtras->at(39), 8);
    cv::Point central_left;
    for(int i = 0; i < (it.count/2)+1; i++, ++it)
        if(i == (it.count/2)) central_left = it.pos();

    // Mouth --> eyes [Right side]
    // Computing strategic points.
    cv::LineIterator ot(image, LLpoints_wExtras->at(54), LLpoints_wExtras->at(42), 8);
    cv::Point central_right;
    for(int i = 0; i < (ot.count/2)+1; i++, ++ot)
        if(i == (ot.count/2)) central_right = ot.pos();

    // Nose --> face [Left side]
    // Computing strategic points.
    it = cv::LineIterator(image, central_left, LLpoints_wExtras->at(2), 8);
    cv::Point cheek_left;
    for(int i = 0; i < (it.count/2)+1; i++, ++it)
        if(i == (it.count/2)) cheek_left = it.pos();

    // Noose --> face [Rigth side]
    // Computing strategic points.
    it = cv::LineIterator(image, central_right, LLpoints_wExtras->at(14), 8);
    cv::Point cheek_right;
    for(int i = 0; i < (it.count/2)+1; i++, ++it)
        if(i == (it.count/2)) cheek_right = it.pos();


    // Add new points to the shape face
    LLpoints_wExtras->push_back(cheek_left);
    LLpoints_wExtras->push_back(central_left);
    LLpoints_wExtras->push_back(cheek_right);
    LLpoints_wExtras->push_back(central_right);

    // Add extra points forehead head
    int distanceNosePointsY = LLpoints_wExtras->at(29).y - LLpoints_wExtras->at(27).y;
    LLpoints_wExtras->resize(82);

    for(int i = 17; i < 27; i++)
    {
        cv::Point temp;
        temp.x = LLpoints_wExtras->at(i).x;
        temp.y = LLpoints_wExtras->at(i).y - distanceNosePointsY;
        LLpoints_wExtras->at(55+i) = temp;
        if (i == 21)
        {
            temp.x = LLpoints_wExtras->at(27).x;
            temp.y = LLpoints_wExtras->at(27).y - distanceNosePointsY - distanceNosePointsY/2;
            LLpoints_wExtras->at(55+i+1) = temp;
        }

    }

}


cv::Mat FaceLandmarks::swapfacesfunc(cv::Mat &image_a, cv::Mat &image_b, std::vector<cv::Point> *LLpoints_a, std::vector<cv::Point> *LLpoints_b, std::vector<int> points)
{


    //Handling the error. Instead of a message I should stop adding signals and show a warning, maybe
    cvSetErrMode(CV_ErrModeParent);
    cv::redirectError(myErrorHandler);

    // Given a pair of triangles, find the affine transform.
    std::vector<cv::Point2f> triangleA, triangleB;
    std::vector<cv::Point2f> triangleAA, triangleBB;
    std::vector<cv::Point> triangleAAInt;
    triangleA.push_back(LLpoints_a->at(points[0]));
    triangleA.push_back(LLpoints_a->at(points[1]));
    triangleA.push_back(LLpoints_a->at(points[2]));
    triangleB.push_back(LLpoints_b->at(points[0]));
    triangleB.push_back(LLpoints_b->at(points[1]));
    triangleB.push_back(LLpoints_b->at(points[2]));

    // Bounding box around triangles. There is a bug in the OpenCV function that does this
    cv::Rect rectA = getRectCheck(triangleA, image_a);
    cv::Rect rectB = getRectCheck(triangleB, image_b);

    // Triangle refactor
    triangleAA.push_back(cv::Point2f(LLpoints_a->at(points[0]).x - rectA.x,  LLpoints_a->at(points[0]).y - rectA.y));
    triangleAA.push_back(cv::Point2f(LLpoints_a->at(points[1]).x - rectA.x, LLpoints_a->at(points[1]).y - rectA.y));
    triangleAA.push_back(cv::Point2f(LLpoints_a->at(points[2]).x - rectA.x, LLpoints_a->at(points[2]).y - rectA.y));
    triangleBB.push_back(cv::Point2f(LLpoints_b->at(points[0]).x - rectB.x,  LLpoints_b->at(points[0]).y - rectB.y));
    triangleBB.push_back(cv::Point2f(LLpoints_b->at(points[1]).x - rectB.x,  LLpoints_b->at(points[1]).y - rectB.y));
    triangleBB.push_back(cv::Point2f(LLpoints_b->at(points[2]).x - rectB.x,  LLpoints_b->at(points[2]).y - rectB.y));

    // Triangle refactor INT for Convex Poly
    triangleAAInt.push_back(cv::Point((int)(LLpoints_a->at(points[0]).x - rectA.x),  (int)(LLpoints_a->at(points[0]).y - rectA.y)));
    triangleAAInt.push_back(cv::Point((int)(LLpoints_a->at(points[1]).x - rectA.x), (int)(LLpoints_a->at(points[1]).y - rectA.y)));
    triangleAAInt.push_back(cv::Point((int)(LLpoints_a->at(points[2]).x - rectA.x), (int)(LLpoints_a->at(points[2]).y - rectA.y)));

    // Apply warpImage to small rectangular patches
    cv::Mat img1Cropped;
    try {image_a(rectA).copyTo(img1Cropped);} catch (cv::Exception& e){  badPositioning = 1;//qDebug() << "Bad Triangle!";
    }
    cv::Mat img2Cropped;
    try {image_b(rectB).copyTo(img2Cropped);} catch (cv::Exception& e){ badPositioning = 1;//qDebug() << "Bad Triangle!";
    }

    // Given a pair of triangles, find the affine transform.
    cv::Mat warpMat;
    try {warpMat = cv::getAffineTransform( triangleBB, triangleAA );} catch (cv::Exception& e){ badPositioning = 1;//qDebug() << "Skewed triangle transforming points.";
    }

    // Apply the Affine Transform just found to the src image
    try { cv::warpAffine( img2Cropped, img1Cropped, warpMat, img1Cropped.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE); } catch (cv::Exception& e){ badPositioning = 1;//qDebug() << "Affine transform failed because of Skewed triangle.";
    }

    // Get mask by filling triangle
    cv::Mat mask = cv::Mat::zeros(rectA.height, rectA.width, CV_8UC1);
    cv::fillConvexPoly(mask, triangleAAInt, cv::Scalar(255));

    cv::Mat srcROI = image_a( rectA );
    img1Cropped.copyTo(srcROI, mask);

    cv::redirectError(cv::ErrorCallback());

    return image_a;
}


cv::Mat FaceLandmarks::getNormalizedFace122(cv::Mat &image, std::vector<cv::Point> * landmarks)
{
    badPositioning = 0;
    std::vector<cv::Point> canonical_landmarks;
    std::vector<cv::Point> transform_landmarks(*landmarks);
    cv::Point in;
    for (int i = 0; i < LANDMARKPOINTS; i++) {
        in.x = canonical_face_x[i]*image.cols;
        in.y = canonical_face_y[i]*image.rows*0.85 + 45;
        canonical_landmarks.push_back(in);
    }

    get82Landmarks2(image, &canonical_landmarks);
    get82Landmarks2(image, &transform_landmarks);

    cv::Mat targetMat(image.rows, image.cols, CV_8UC3, cv::Scalar(0,0,0));


    for (int i = 0; i < 122; i++) {
        targetMat = swapfacesfunc(targetMat, image, &canonical_landmarks, &transform_landmarks, indexes122triangles[i]);
    }

    return targetMat;
}

//Get a normalized face based on 17 triangles only
cv::Mat FaceLandmarks::getNormalizedFace17(cv::Mat &image, std::vector<cv::Point> * landmarks)
{
    std::vector<cv::Point> canonical_landmarks;
    cv::Point in;
    for (int i = 0; i < LANDMARKPOINTS; i++) {
        in.x = canonical_face_x[i]*image.cols;
        in.y = canonical_face_y[i]*image.rows;
        canonical_landmarks.push_back(in);
    }

    cv::Mat targetMat(image.rows, image.cols, CV_8UC3, cv::Scalar(0,0,0));

    for( int i = 0; i < 17; i++)
    {
        targetMat = swapfacesfunc(targetMat, image, &canonical_landmarks, landmarks, indexes17triangles[i]);
    }

    return targetMat;
}


//Draw a mesh with 17 triangles using 68 landmarks
cv::Mat FaceLandmarks::drawSimpleMesh17(cv::Mat image, std::vector<cv::Point> *landmarkpoints)
{
    // Variables
    cv::Mat canvas;
    canvas = image;

    for ( int i = 0; i < 23; i++)
        cv::line(canvas, landmarkpoints->at(mesh_index_a[i]), landmarkpoints->at(mesh_index_a[i+1]), cv::Scalar(0,0,255));

    for ( int i = 0; i < 3; i++)
        cv::line(canvas, landmarkpoints->at(mesh_index_b[i]), landmarkpoints->at(mesh_index_b[i+1]), cv::Scalar(0,0,255));

    cv::line(canvas, landmarkpoints->at(54), landmarkpoints->at(12), cv::Scalar(0,0,255));
    cv::line(canvas, landmarkpoints->at(4), landmarkpoints->at(48), cv::Scalar(0,0,255));
    cv::line(canvas, landmarkpoints->at(1), landmarkpoints->at(48), cv::Scalar(0,0,255));
    cv::line(canvas, landmarkpoints->at(8), landmarkpoints->at(48), cv::Scalar(0,0,255));
    cv::line(canvas, landmarkpoints->at(54), landmarkpoints->at(15), cv::Scalar(0,0,255));

    return canvas;
}

//There is a bug in OpenCV that makes the containing rects wrong
cv::Rect FaceLandmarks::getRectCheck(std::vector <cv::Point2f> tri, cv::Mat img)
{
    int sx1 = tri[0].x;
    int sx2 = tri[1].x;
    int sx3 = tri[2].x;
    int sy1 = tri[0].y;
    int sy2 = tri[1].y;
    int sy3 = tri[2].y;

    int xmax = sx1 > sx2 ? (sx1 > sx3 ? sx1 : sx3) : (sx2 > sx3 ? sx2 : sx3);
    int ymax = sy1 > sy2 ? (sy1 > sy3 ? sy1 : sy3) : (sy2 > sy3 ? sy2 : sy3);
    int xmin = sx1 < sx2 ? (sx1 < sx3 ? sx1 : sx3) : (sx2 < sx3 ? sx2 : sx3);
    int ymin = sy1 < sy2 ? (sy1 < sy3 ? sy1 : sy3) : (sy2 < sy3 ? sy2 : sy3);


    if (xmin < 0) {xmin = 0;}
    if (ymin < 0) {ymin = 0;}
    if (xmax > (img.cols-1) ) {xmax = img.cols-1;}
    if (ymax > (img.rows-1) ) {ymax = img.rows-1;}

    cv::Rect r(xmin, ymin, xmax - xmin, ymax - ymin);

    return r;
}






