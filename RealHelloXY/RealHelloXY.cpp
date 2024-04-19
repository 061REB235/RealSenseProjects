// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include "example.hpp"          // Include short list of convenience functions for rendering

// This example will require several standard data-structures and algorithms:
#define _USE_MATH_DEFINES
#include <math.h>
#include <queue>
#include <unordered_set>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

using pixel = std::pair<int, int>;

// Application state shared between the main-thread and GLFW events
struct state {
    bool new_click = false;
    pixel last_click;
    pixel mouse_position; // Add this to track the mouse position continuously
    float last_point[3] = { 0, 0, 0 }; // To store the last 3D point
};

// Helper function to register to UI events
void register_glfw_callbacks(window& app, state& app_state);

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
    rs2::align align_to(RS2_STREAM_DEPTH);

    // Declare RealSense pipeline, encapsulating the actual device and sensors
    rs2::pipeline pipe;

    rs2::config cfg;
    if (!serial.empty())
        cfg.enable_device(serial);

    cfg.enable_stream(RS2_STREAM_DEPTH, 1280, 720, RS2_FORMAT_Z16, 30);
    cfg.enable_stream(RS2_STREAM_COLOR, 1280, 720, RS2_FORMAT_RGBA8, 30);
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
    window app(stream.width(), stream.height(), "RealHelloXYZ");

    state app_state;

    register_glfw_callbacks(app, app_state);

    rs2::frame_queue postprocessed_frames;

    std::atomic_bool alive{ true };

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
                data = data.apply_filter(align_to);

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
            float yaw_deg = yaw * (180.0f / M_PI);
            float roll_deg = roll * (180.0f / M_PI);
            // Adjust roll by 90 degrees for horizontal alignment
            roll_deg += 90.0f;

            if (roll_deg > 180.0f) {
                roll_deg -= 360.0f;
            }
            if (app_state.new_click)
            {
                float pixel[2] = { float(app_state.last_click.first), float(app_state.last_click.second) };
                float point[3];
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

