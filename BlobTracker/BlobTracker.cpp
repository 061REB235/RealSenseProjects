﻿#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include "example.hpp"          // Include short list of convenience functions for rendering

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>

// This example will require several standard data-structures and algorithms:
#define _USE_MATH_DEFINES
#include <math.h>
#include <queue>
#include <unordered_set>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>


// uncoment to enable opencv blob prieview window with sliders
#define CV_WINDOW

// color filter and blob detection defaults
int threshold_LAB_L = 50;
int threshold_LAB_AB = 15;
int dilate_size = 2;

int Circularity_min = 50;
int Convexity_min = 70; 
int Inertia_min = 60;

int maxDistancePixels = 30;
int maxHoldFrames = 15;

cv::Ptr<cv::SimpleBlobDetector> blobDetector;
cv::SimpleBlobDetector::Params blobParams;


using pixel = std::pair<int, int>;

// Application state shared between the main-thread and GLFW events
struct state {
    bool new_click = false;
    bool start_tracking = false;
    bool tracking = false;
    pixel last_click;
    pixel mouse_position; // Add this to track the mouse position continuously
    float last_point[3] = { 0, 0, 0 }; // To store the last 3D point
    cv::Scalar trackLABmin{ 0, 0, 0 };
    cv::Scalar trackLABmax{ 0, 0, 0 };
    cv::Vec3b trackColorLab{ 0, 0, 0 };
    cv::KeyPoint lastBlobCenter;
    int blobHoldFrames;
};

state app_state;

// Helper function to register to UI events
void register_glfw_callbacks(window& app, state& app_state);

// draws openGL cross
void drawCross(int centerX, int centerY);

// transforms from sensor coordinate frame to robot coordinate frame
void transformPoint(const float sourcePoint[3], float destPoint[3]);

// Find closest blob keypoint
cv::KeyPoint findClosestKeypoint(const std::vector<cv::KeyPoint>& keypoints, const pixel& pixel, int maxDistance);
cv::KeyPoint findClosestKeypoint(const std::vector<cv::KeyPoint>& keypoints, const cv::KeyPoint& referenceKeypoint, int maxDistance);
// Converts Keypoint coordinates to pixel
pixel keypointToPixel(const cv::KeyPoint& keypoint);

#ifdef CV_WINDOW
// openCV slider callbacks
void cv_slider_1(int value, void* userdata) {
    threshold_LAB_L = value;
    app_state.trackLABmin = cv::Scalar(app_state.trackColorLab[0] - threshold_LAB_L, app_state.trackColorLab[1] - threshold_LAB_AB, app_state.trackColorLab[2] - threshold_LAB_AB);
    app_state.trackLABmax = cv::Scalar(app_state.trackColorLab[0] + threshold_LAB_L, app_state.trackColorLab[1] + threshold_LAB_AB, app_state.trackColorLab[2] + threshold_LAB_AB);
}
void cv_slider_2(int value, void* userdata) {
    threshold_LAB_AB = value;
    app_state.trackLABmin = cv::Scalar(app_state.trackColorLab[0] - threshold_LAB_L, app_state.trackColorLab[1] - threshold_LAB_AB, app_state.trackColorLab[2] - threshold_LAB_AB);
    app_state.trackLABmax = cv::Scalar(app_state.trackColorLab[0] + threshold_LAB_L, app_state.trackColorLab[1] + threshold_LAB_AB, app_state.trackColorLab[2] + threshold_LAB_AB);
}
void cv_dilate_dilate_slider(int value, void* userdata) {
    dilate_size = value;
}

void cv_blob_slider_circ_min(int value, void* userdata) {
    if (value == 0) value = 1;
    blobParams.minCircularity = value / 100.0f;
    blobDetector = cv::SimpleBlobDetector::create(blobParams);
}

void cv_blob_slider_convex_min(int value, void* userdata) {
    if (value == 0) value = 1;
    blobParams.minConvexity = value / 100.0f;
    blobDetector = cv::SimpleBlobDetector::create(blobParams);
}

void cv_blob_slider_inertia_min(int value, void* userdata) {
    if (value == 0) value = 1;
    blobParams.minInertiaRatio = value / 100.0f;
    blobDetector = cv::SimpleBlobDetector::create(blobParams);
}
#endif

int main(int argc, char* argv[]) try
{
    std::string serial;
    if (!device_with_streams({ RS2_STREAM_COLOR,RS2_STREAM_DEPTH }, serial))
        return EXIT_SUCCESS;

    // OpenGL textures for the color and depth frames
    texture depth_image, color_image;

    // Colorizer is used to visualize depth data
    rs2::colorizer color_map;
    // Use black to white color map
    color_map.set_option(RS2_OPTION_COLOR_SCHEME, 2.f);
    // Decimation filter reduces the amount of data (while preserving best samples)
    rs2::decimation_filter dec;
    // If the demo is too slow, make sure you run in Release (-DCMAKE_BUILD_TYPE=Release)
    // but you can also increase the following parameter to decimate depth more (reducing quality)
    dec.set_option(RS2_OPTION_FILTER_MAGNITUDE, 2);
    // Define transformations from and to Disparity domain
    rs2::disparity_transform depth2disparity;
    rs2::disparity_transform disparity2depth(false);
    // Define spatial filter (edge-preserving)
    rs2::spatial_filter spat;
    // Enable hole-filling
    // Hole filling is an agressive heuristic and it gets the depth wrong many times
    // However, this demo is not built to handle holes
    // (the shortest-path will always prefer to "cut" through the holes since they have zero 3D distance)
    spat.set_option(RS2_OPTION_HOLES_FILL, 5); // 5 = fill all the zero pixels
    // Define temporal filter
    rs2::temporal_filter temp;
    // Spatially align all streams to depth viewport
    // We do this because:
    //   a. Usually depth has wider FOV, and we only really need depth for this demo
    //   b. We don't want to introduce new holes
    rs2::align align_to_color(RS2_STREAM_COLOR);

    // Declare RealSense pipeline, encapsulating the actual device and sensors
    rs2::pipeline pipe;

    rs2::config cfg;
    if (!serial.empty())
        cfg.enable_device(serial);

    cfg.enable_stream(RS2_STREAM_DEPTH, 1280, 720, RS2_FORMAT_Z16, 30);
    cfg.enable_stream(RS2_STREAM_COLOR, 1280, 720, RS2_FORMAT_RGB8, 30);
    cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F);

    auto profile = pipe.start(cfg);

    auto sensor = profile.get_device().first<rs2::depth_sensor>();

    // Set the device to High Accuracy preset of the D400 stereoscopic cameras
    if (sensor && sensor.is<rs2::depth_stereo_sensor>())
    {
        sensor.set_option(RS2_OPTION_VISUAL_PRESET, RS2_RS400_VISUAL_PRESET_HIGH_ACCURACY);
    }

    auto stream = profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();

    
    // Create a simple OpenGL window for rendering:
    window app(stream.width(), stream.height(), "BlobTracker");


    register_glfw_callbacks(app, app_state);

    rs2::frame_queue postprocessed_frames;

    std::atomic_bool alive{ true };

    // opencv blob tracker
    //blobParams.thresholdStep = 10;
    blobParams.minThreshold = 0.0f;
    blobParams.maxThreshold = 100.0f;
    //blobParams.minDistBetweenBlobs = 0.5;
    // Filter by Area
    blobParams.filterByArea = true;
    blobParams.minArea = 300;
    blobParams.maxArea = 600000;

    // Filter by Circularity
    blobParams.filterByCircularity = true;
    blobParams.minCircularity = Circularity_min/100.0f;
    blobParams.maxCircularity = 1.0f;

    // Filter by Convexity
    blobParams.filterByConvexity = true;
    blobParams.minConvexity = Convexity_min/100.0f;
    blobParams.maxConvexity = 1.0f;

    // Filter by Inertia
    blobParams.filterByInertia = true;
    blobParams.minInertiaRatio = Inertia_min/100.0f;
    blobParams.maxInertiaRatio = 1.0f;
    blobDetector = cv::SimpleBlobDetector::create(blobParams);

    //opencv window
 #ifdef CV_WINDOW
    const auto window_name = "OpenCV Image";
    cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
    cv::createTrackbar("L* Th", "OpenCV Image", &threshold_LAB_L, 255, cv_slider_1);
    cv::createTrackbar("a*, b* Th", "OpenCV Image", &threshold_LAB_AB, 255, cv_slider_2);
    cv::createTrackbar("dilate it", "OpenCV Image", &dilate_size, 21, cv_dilate_dilate_slider);
    cv::createTrackbar("minConvex", "OpenCV Image", &Convexity_min, 100, cv_blob_slider_convex_min);
    cv::createTrackbar("minCircle", "OpenCV Image", &Circularity_min, 100, cv_blob_slider_circ_min);
    cv::createTrackbar("minInertia", "OpenCV Image", &Inertia_min, 100, cv_blob_slider_inertia_min);
    
 #endif
    
        
    

    // Video-processing thread will fetch frames from the camera,
    // apply post-processing and send the result to the main thread for rendering
    // It recieves synchronized (but not spatially aligned) pairs
    // and outputs synchronized and aligned pairs
    std::thread video_processing_thread([&]() {
        while (alive)
        {
            // Fetch frames from the pipeline and send them for processing
            rs2::frameset data;
            if (pipe.poll_for_frames(&data))
            {
                // First make the frames spatially aligned
                data = data.apply_filter(align_to_color);

                // Decimation will reduce the resultion of the depth image,
                // closing small holes and speeding-up the algorithm
                //data = data.apply_filter(dec);

                // To make sure far-away objects are filtered proportionally
                // we try to switch to disparity domain
                data = data.apply_filter(depth2disparity);
                // Apply spatial filtering
                data = data.apply_filter(spat);

                // Apply temporal filtering
                data = data.apply_filter(temp);

                // If we are in disparity domain, switch back to depth
                data = data.apply_filter(disparity2depth);

                //// Apply color map for visualization of depth
                data = data.apply_filter(color_map);

                

                // Send resulting frames for visualization in the main thread
                postprocessed_frames.enqueue(data);
            }
        }
        });

    rs2::frameset current_frameset;
    cv::Mat cvColor;
    std::string str_tracked = "Not tracking";
    float trackedPixel[2];
    float trackedPoint[3];
    float outputPoint[3] = { 0,0,0 };
    // && cv::waitKey(1) < 0 && cv::getWindowProperty(window_name, cv::WND_PROP_AUTOSIZE) >= 0 - for openCV test window
    while (app) // Application still alive?
    {
        // Fetch the latest available post-processed frameset

        postprocessed_frames.poll_for_frame(&current_frameset);

        if (current_frameset)
        {
            auto depth = current_frameset.get_depth_frame();
            auto color = current_frameset.get_color_frame();
            auto colorized_depth = current_frameset.first(RS2_STREAM_DEPTH, RS2_FORMAT_RGB8);
            auto accel_frame = current_frameset.first_or_default(RS2_STREAM_ACCEL);
           
            rs2_vector accel_data = accel_frame.as<rs2::motion_frame>().get_motion_data();
            // Calculate pitch and roll in radians
            float yaw = atan2(-accel_data.x, sqrt(accel_data.y * accel_data.y + accel_data.z * accel_data.z));
            float roll = atan2(accel_data.y, accel_data.z);

            // Convert radians to degrees
            float yaw_deg = (float)(yaw * (180.0f / M_PI));
            float roll_deg = (float)(roll * (180.0f / M_PI));
            // Adjust roll by 90 degrees for horizontal alignment
            roll_deg += 90.0f;

            if (roll_deg > 180.0f) {
                roll_deg -= 360.0f;
            }

            // OpenCV

            // convert rs color frame to cv Lab colors
            cv::Mat r_rgb = cv::Mat(cv::Size(color.get_width(), color.get_height()), CV_8UC3, (void*)color.get_data(), cv::Mat::AUTO_STEP);
            cv::Mat cvColor;
            cvtColor(r_rgb, cvColor, cv::COLOR_RGB2Lab);
            

            if (app_state.new_click)
            {
                float pixel[2] = { float(app_state.last_click.first), float(app_state.last_click.second) };
                float point[3];

                // openCV get pixel color
                app_state.trackColorLab = cvColor.at<cv::Vec3b>(app_state.last_click.second, app_state.last_click.first);
                // set color range and enable tracking
                app_state.trackLABmin = cv::Scalar(app_state.trackColorLab[0] - threshold_LAB_L, app_state.trackColorLab[1] - threshold_LAB_AB, app_state.trackColorLab[2] - threshold_LAB_AB);
                app_state.trackLABmax = cv::Scalar(app_state.trackColorLab[0] + threshold_LAB_L, app_state.trackColorLab[1] + threshold_LAB_AB, app_state.trackColorLab[2] + threshold_LAB_AB);
                app_state.start_tracking = true;

                app_state.new_click = false; // Ensure the message is printed once per click
            }

            // OpenCV
            // perform color separation
            cv::Mat maskLAB;
            cv::inRange(cvColor, app_state.trackLABmin, app_state.trackLABmax, maskLAB);

            cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT,
                cv::Size(2 * dilate_size + 1, 2 * dilate_size + 1),
                cv::Point(dilate_size, dilate_size));
            cv::dilate(maskLAB, maskLAB, element);
            maskLAB = 255 - maskLAB;
            // perform blob detection
            std::vector<cv::KeyPoint> keypoints;
            blobDetector->detect(maskLAB, keypoints);


            pixel blobCenterPixel = keypointToPixel(app_state.lastBlobCenter);
            trackedPixel[0] = blobCenterPixel.first;
            trackedPixel[1] = blobCenterPixel.second;

            if (app_state.tracking) {
                try {
                    app_state.lastBlobCenter = findClosestKeypoint(keypoints, app_state.lastBlobCenter, maxDistancePixels);
                    app_state.blobHoldFrames = maxHoldFrames;
                    str_tracked = "Blob u: " + std::to_string(blobCenterPixel.first) + ", v: " + std::to_string(blobCenterPixel.second);
                    auto intr = depth.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
                    // Get distance at pixel coordinates
                    float distance = depth.get_distance(app_state.last_click.first, app_state.last_click.second);
                    if (distance > 0) {
                        rs2_deproject_pixel_to_point(trackedPoint, &intr, trackedPixel, distance);
                        str_tracked += ",\nx: " + std::to_string(trackedPoint[0]) + ",\ny: " + std::to_string(trackedPoint[1]) + ",\nz: " + std::to_string(trackedPoint[2]);
                        transformPoint(trackedPoint, outputPoint);
                        str_tracked += "\nTransformed:\nx: " + std::to_string(outputPoint[0]) + ",\ny: " + std::to_string(outputPoint[1]) + ",\nz: " + std::to_string(outputPoint[2]);

                    }
                    else {
                        str_tracked += "\n Invalid depth\n";
                    }

                } catch (const std::runtime_error& e) {
                    app_state.blobHoldFrames--;
                    if (app_state.blobHoldFrames <= 0) {
                        app_state.tracking = false;
                        str_tracked = "Blob dropped";
                    }
                    //std::cerr << "Error: " << e.what() << std::endl;
                }
            } else if (app_state.start_tracking) {

                try {
                    app_state.lastBlobCenter = findClosestKeypoint(keypoints, app_state.last_click, maxDistancePixels);
                    app_state.start_tracking = false;
                    app_state.tracking = true;
                    app_state.blobHoldFrames = maxHoldFrames;
                }
                catch (const std::runtime_error& e) {
                    app_state.start_tracking = false;
                    str_tracked = "Couldn`t start blob tracking";
                    //std::cerr << "Error: " << e.what() << std::endl;
                }
            }

            // display mask with keypoints
#ifdef CV_WINDOW
            cv::Mat maskLAB_with_keypoints;
            cv::drawKeypoints(maskLAB, keypoints, maskLAB_with_keypoints, cv::Scalar(0, 0, 255), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
            cv::imshow(window_name, maskLAB_with_keypoints);
#endif
            

            glEnable(GL_BLEND);
            // Use the Alpha channel for blending
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // First render the colorized depth image
            depth_image.render(colorized_depth, { 0, 0, app.width(), app.height() });
            
            // Render the color frame (since we have selected RGBA format
            // pixels out of FOV will appear transparent)
            color_image.render(color, { 0, 0, app.width(), app.height() });

            // Show stream resolutions
            std::string depth_res = "Depth: " + std::to_string(depth.get_width()) + "x" + std::to_string(depth.get_height());
            std::string color_res = "Color: " + std::to_string(color.get_width()) + "x" + std::to_string(color.get_height());
            std::string str_roll = "Roll: " + std::to_string(roll_deg);
            std::string str_yaw = "Yaw: " + std::to_string(yaw_deg);
            
            // Set the drawing color to black with 50% transparency
            glColor4f(0.0f, 0.0f, 0.0f, 0.5f); // RGBA

            // Draw a filled rectangle using triangle fan
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(0.0f, 0.0f); // Top-left corner
            glVertex2f(150.0f, 0.0f); // Top-right corner
            glVertex2f(150.0f, 200.0f); // Bottom-right corner
            glVertex2f(0.0f, 200.0f); // Bottom-left corner
            glEnd();

            if (app_state.tracking) drawCross(trackedPixel[0], trackedPixel[1]);

            glColor3f(1.f, 1.f, 1.f);
            draw_text(10, 10, depth_res.c_str());
            draw_text(10, 20, color_res.c_str());
            glColor3f(1.f, 0.f, 1.f);
            draw_text(10, 50, str_roll.c_str());
            glColor3f(0.f, 1.f, 1.f);
            draw_text(10, 60, str_yaw.c_str());
            glColor3f(1.f, 1.f, 0.f);
            draw_text(10, 80, str_tracked.c_str());
            // Draw intersecting lines
            glLineWidth(1.0f); // Set line width
            glBegin(GL_LINES);
            // Y axis
            glColor3f(0.0f, 1.0f, 0.0f);
            glVertex2f(app.width() / 2.0f, 0.0f);
            glVertex2f(app.width() / 2.0f, app.height());
            // X axis
            glColor3f(1.0f, 0.0f, 0.0f);
            glVertex2f(0.0f, app.height() / 2.0f);
            glVertex2f(app.width(), app.height() / 2.0f);
            glEnd();

            glColor3f(1.f, 1.f, 1.f);
            glDisable(GL_BLEND);
        }
    }

    // Signal threads to finish and wait until they do
    alive = false;
    video_processing_thread.join();

    return EXIT_SUCCESS;
}
catch (const rs2::error& e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

// Get mouse clicks
void register_glfw_callbacks(window& app, state& app_state)
{
    app.on_left_mouse = [&](bool pressed)
        {
            if (pressed)
            {
                // Use the last known mouse position as the click position
                app_state.last_click = app_state.mouse_position;
                app_state.new_click = true;
            }
        };

    app.on_mouse_move = [&](double x, double y)
        {
            // Continuously update the mouse position
            app_state.mouse_position = { static_cast<int>(x), static_cast<int>(y) };
        };
}

void drawCross(int centerX, int centerY) {
    int halfSize = 50; // Half the size of the cross to center it around (centerX, centerY)

    // Enable blending if you want anti-aliased lines
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set line width if desired
    glLineWidth(2.0f);

    // Set color to white
    glColor3f(1.0f, 1.0f, 1.0f);

    // Start drawing lines
    glBegin(GL_LINES);

    // Horizontal line (left to right)
    glVertex2f(centerX - halfSize, centerY);
    glVertex2f(centerX + halfSize, centerY);

    // Vertical line (bottom to top)
    glVertex2f(centerX, centerY - halfSize);
    glVertex2f(centerX, centerY + halfSize);

    glEnd();

    // Reset OpenGL states as necessary
    glDisable(GL_BLEND);
}

void transformPoint(const float sourcePoint[3], float destPoint[3]) {
    cv::Matx44f transformH
    (1, 0, 0, 0,
     0, 1, 0, -0.09,
     0, 0, 1, -0.15,
     0, 0, 0, 1);

    cv::Matx41f sourcePointH(sourcePoint[0], sourcePoint[1], sourcePoint[2], 1.0f);
    cv::Matx41f destPointH = transformH * sourcePointH;

    destPoint[0] = destPointH(0) / destPointH(3);
    destPoint[1] = destPointH(1) / destPointH(3);
    destPoint[2] = destPointH(2) / destPointH(3);
}

cv::KeyPoint findClosestKeypoint(const std::vector<cv::KeyPoint>& keypoints, const pixel& pixel, int maxDistance) {
    if (keypoints.empty()) {
        throw std::runtime_error("The keypoints vector is empty.");
    }

    double minDistance = std::numeric_limits<double>::max();
    cv::KeyPoint closestKeypoint;
    bool found = false;

    for (const auto& keypoint : keypoints) {
        double dx = keypoint.pt.x - pixel.first;
        double dy = keypoint.pt.y - pixel.second;
        double distance = std::sqrt(dx * dx + dy * dy);

        if (distance < minDistance) {
            minDistance = distance;
            closestKeypoint = keypoint;
            found = true;
        }
    }

    if (!found || minDistance > maxDistance) {
        throw std::runtime_error("No keypoints found within the maximum distance.");
    }

    return closestKeypoint;
}

cv::KeyPoint findClosestKeypoint(const std::vector<cv::KeyPoint>& keypoints, const cv::KeyPoint& referenceKeypoint, int maxDistance) {
    if (keypoints.empty()) {
        throw std::runtime_error("The keypoints vector is empty.");
    }

    double minDistance = std::numeric_limits<double>::max();
    cv::KeyPoint closestKeypoint;
    bool found = false;

    for (const auto& keypoint : keypoints) {
        double dx = keypoint.pt.x - referenceKeypoint.pt.x;
        double dy = keypoint.pt.y - referenceKeypoint.pt.y;
        double distance = std::sqrt(dx * dx + dy * dy);

        if (distance < minDistance) {
            minDistance = distance;
            closestKeypoint = keypoint;
            found = true;
        }
    }

    if (!found || minDistance > maxDistance) {
        throw std::runtime_error("No keypoints found within the maximum distance.");
    }

    return closestKeypoint;
}

pixel keypointToPixel(const cv::KeyPoint& keypoint) {
    // Convert float coordinates to integer coordinates
    int x = static_cast<int>(keypoint.pt.x + 0.5); // Adding 0.5 for rounding
    int y = static_cast<int>(keypoint.pt.y + 0.5); // Adding 0.5 for rounding
    return { x, y };
}

        