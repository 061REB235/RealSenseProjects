﻿// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
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
//#define CV_WINDOW

// color filter and blob detection defaults
int threshold_LAB_L = 50;
int threshold_LAB_AB = 15;
int dilate_size = 2;

int Circularity_min = 50;
int Convexity_min = 70; 
int Inertia_min = 60; 

cv::Ptr<cv::SimpleBlobDetector> blobDetector;
cv::SimpleBlobDetector::Params blobParams;


using pixel = std::pair<int, int>;

// Application state shared between the main-thread and GLFW events
struct state {
    bool new_click = false;
    bool track = false;
    pixel last_click;
    pixel mouse_position; // Add this to track the mouse position continuously
    float last_point[3] = { 0, 0, 0 }; // To store the last 3D point
    cv::Scalar trackLABmin{ 0, 0, 0 };
    cv::Scalar trackLABmax{ 0, 0, 0 };
    cv::Vec3b trackColorLab{ 0, 0, 0 };

};

state app_state;

// Helper function to register to UI events
void register_glfw_callbacks(window& app, state& app_state);

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

            // perform folor separation
            cv::Mat maskLAB, invMaskLAB, cvBlob, mat_with_keypoints;
            cv::inRange(cvColor, app_state.trackLABmin, app_state.trackLABmax, maskLAB);

            cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT,
                cv::Size(2 * dilate_size + 1, 2 * dilate_size + 1),
                cv::Point(dilate_size, dilate_size));
            cv::dilate(maskLAB, maskLAB, element);

            cv::bitwise_and(cvColor, cvColor, cvBlob, maskLAB);

            invMaskLAB = 255 - maskLAB;

            std::vector<cv::KeyPoint> keypoints;
            blobDetector->detect(invMaskLAB, keypoints);
            cv::drawKeypoints(invMaskLAB, keypoints, mat_with_keypoints, cv::Scalar(0, 0, 255), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
           
            
            
            //cv::threshold(grey, grey, 127, 255, cv::THRESH_BINARY);

            //cvtColor(grey, grey, cv::COLOR_BGR2GRAY);
            //std::vector<cv::Mat> cvColorChannels(3);
           // cv::split(cvBlob, cvColorChannels); // Split into L, a, b channels
            //cv::Mat grey = cvColorChannels[0]; // The L channel is the lightness and can be used directly as a grayscale image
            //grey = 255-grey;
            // Detect blobs
           // std::vector<cv::KeyPoint> keypoints;
            //blobDetector->detect(grey, keypoints);
           // cv::Mat im_with_keypoints;
            //cv::drawKeypoints(grey, keypoints, im_with_keypoints, cv::Scalar(0, 0, 255), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);


#ifdef CV_WINDOW
            cv::imshow(window_name, mat_with_keypoints);
#endif
            

            if (app_state.new_click)
            {
                float pixel[2] = { float(app_state.last_click.first), float(app_state.last_click.second) };
                float point[3];

                // openCV get pixel color
                app_state.trackColorLab = cvColor.at<cv::Vec3b>(app_state.last_click.second, app_state.last_click.first);
                // set color range and enable tracking
                app_state.trackLABmin = cv::Scalar(app_state.trackColorLab[0] - threshold_LAB_L, app_state.trackColorLab[1] - threshold_LAB_AB, app_state.trackColorLab[2] - threshold_LAB_AB);
                app_state.trackLABmax = cv::Scalar(app_state.trackColorLab[0] + threshold_LAB_L, app_state.trackColorLab[1] + threshold_LAB_AB, app_state.trackColorLab[2] + threshold_LAB_AB);
                app_state.track = true;

                auto intr = depth.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
                // Get distance at pixel coordinates
                float distance = depth.get_distance(app_state.last_click.first, app_state.last_click.second);

                if (distance > 0) {
                    rs2_deproject_pixel_to_point(point, &intr, pixel, distance);

                    std::cout << "2D [" << app_state.last_click.first << ", " << app_state.last_click.second << "], ";
                    std::cout << std::fixed << std::setprecision(4) << "3D [" << point[0] << ", " << point[1] << ", " << point[2] << "]";
                    // Calculate the distance from the current point to the last point
                    if (!(app_state.last_point[0] == 0 && app_state.last_point[1] == 0 && app_state.last_point[2] == 0)) {
                        float dx = point[0] - app_state.last_point[0];
                        float dy = point[1] - app_state.last_point[1];
                        float dz = point[2] - app_state.last_point[2];
                        float distance_to_last_point = sqrt(dx * dx + dy * dy + dz * dz);
                        std::cout << ", distance to last point: " << distance_to_last_point << "m";
                    }
                    app_state.last_point[0] = point[0];
                    app_state.last_point[1] = point[1];
                    app_state.last_point[2] = point[2];

                    std::cout << std::endl;

                }
                else {
                    std::cout << "Invalid depth value." << std::endl;
                }
                std::cout << std::endl;
                app_state.new_click = false; // Ensure the message is printed once per click
            }
            

            glEnable(GL_BLEND);
            // Use the Alpha channel for blending
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // First render the colorized depth image
            depth_image.render(colorized_depth, { 0, 0, app.width(), app.height() });
            
            // Render the color frame (since we have selected RGBA format
            // pixels out of FOV will appear transparent)
            color_image.render(color, { 0, 0, app.width(), app.height() });

            // Render opencv




            // Show stream resolutions
            std::string depth_res = "Depth: " + std::to_string(depth.get_width()) + "x" + std::to_string(depth.get_height());
            std::string color_res = "Color: " + std::to_string(color.get_width()) + "x" + std::to_string(color.get_height());
            std::string str_roll = "Roll: " + std::to_string(roll_deg);
            std::string str_yaw = "Yaw: " + std::to_string(yaw_deg);

            glColor3f(1.f, 1.f, 1.f);
            draw_text(10, 10, depth_res.c_str());
            draw_text(10, 20, color_res.c_str());
            glColor3f(1.f, 0.f, 1.f);
            draw_text(10, 40, str_roll.c_str());
            glColor3f(0.f, 1.f, 1.f);
            draw_text(10, 50, str_yaw.c_str());

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
