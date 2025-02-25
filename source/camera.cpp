#include <unistd.h>
#include "opencv2/opencv.hpp"
#include "iostream"
#include "opencv2/videoio/videoio_c.h"
#include "nadjieb/mjpeg_streamer.hpp"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

using MJPEGStreamer = nadjieb::MJPEGStreamer;
static bool use_debug = false;

// If you don't want to allocate this much memory you can use a signal_t structure as well
// and read directly from a cv::Mat object, but on Linux this should be OK
static float features[EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT];

/**
 * Resize and crop to the set width/height from model_metadata.h
 */
void resize_and_crop(cv::Mat *in_frame, cv::Mat *out_frame) {
    // to resize... we first need to know the factor
    float factor_w = static_cast<float>(EI_CLASSIFIER_INPUT_WIDTH) / static_cast<float>(in_frame->cols);
    float factor_h = static_cast<float>(EI_CLASSIFIER_INPUT_HEIGHT) / static_cast<float>(in_frame->rows);

    float largest_factor = factor_w > factor_h ? factor_w : factor_h;

    cv::Size resize_size(static_cast<int>(largest_factor * static_cast<float>(in_frame->cols)),
        static_cast<int>(largest_factor * static_cast<float>(in_frame->rows)));

    cv::Mat resized;
    cv::resize(*in_frame, resized, resize_size);

    int crop_x = resize_size.width > resize_size.height ?
        (resize_size.width - resize_size.height) / 2 :
        0;
    int crop_y = resize_size.height > resize_size.width ?
        (resize_size.height - resize_size.width) / 2 :
        0;

    cv::Rect crop_region(crop_x, crop_y, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);

    if (use_debug) {
        printf("crop_region x=%d y=%d width=%d height=%d\n", crop_x, crop_y, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    }

    *out_frame = resized(crop_region);
}

int main(int argc, char** argv) {
    // If you see: OpenCV: not authorized to capture video (status 0), requesting... Abort trap: 6
    // This might be a permissions issue. Are you running this command from a simulated shell (like in Visual Studio Code)?
    // Try it from a real terminal.
    //
    printf("OPENCV VERSION: %d_%d\n", CV_MAJOR_VERSION, CV_MINOR_VERSION);

    if (argc < 2) {
        printf("Requires one parameter (ID of the webcam).\n");
        printf("You can find these via `v4l2-ctl --list-devices`.\n");
        printf("E.g. for:\n");
        printf("    C922 Pro Stream Webcam (usb-70090000.xusb-2.1):\n");
	    printf("            /dev/video0\n");
        printf("The ID of the webcam is 0\n");
        exit(1);
    }

    for (int ix = 2; ix < argc; ix++) {
        if (strcmp(argv[ix], "--debug") == 0) {
            printf("Enabling debug mode\n");
            use_debug = true;
        }
    }

    MJPEGStreamer streamer;
    streamer.start(80);
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 90};

    // open the camera...
    cv::VideoCapture camera(atoi(argv[1]));
    if (!camera.isOpened()) {
        std::cerr << "ERROR: Could not open camera" << std::endl;
        return 1;
    } 

    std::cout << "Resolution: " <<  camera.get(CV_CAP_PROP_FRAME_WIDTH) 
	    << "x" << camera.get(CV_CAP_PROP_FRAME_HEIGHT) << std::endl;
     std::cout   << "EI_CLASSIFIER_INPUT_WIDTH: " << EI_CLASSIFIER_INPUT_WIDTH
         << " EI_CLASSIFIER_INPUT_HEIGHT: " << EI_CLASSIFIER_INPUT_HEIGHT << std::endl;


    if (use_debug) {
        // create a window to display the images from the webcam
        cv::namedWindow("Webcam", cv::WINDOW_AUTOSIZE);
    }

    // this will contain the image from the webcam
    cv::Mat frame;

    // display the frame until you press a key
    while (1) {
        // 100ms. between inference
        int64_t next_frame = (int64_t)(ei_read_timer_ms() + 100);

        // capture the next frame from the webcam
        camera >> frame;

        cv::Mat cropped;
        resize_and_crop(&frame, &cropped);

        size_t feature_ix = 0;
        for (int rx = 0; rx < (int)cropped.rows; rx++) {
            for (int cx = 0; cx < (int)cropped.cols; cx++) {
                cv::Vec3b pixel = cropped.at<cv::Vec3b>(rx, cx);
                uint8_t b = pixel.val[0];
                uint8_t g = pixel.val[1];
                uint8_t r = pixel.val[2];
                features[feature_ix++] = (r << 16) + (g << 8) + b;
            }
        }

        ei_impulse_result_t result;

        // construct a signal from the features buffer
        signal_t signal;
        numpy::signal_from_buffer(features, EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT, &signal);

        // and run the classifier
        EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
        if (res != 0) {
            printf("ERR: Failed to run classifier (%d)\n", res);
            return 1;
        }

    #if EI_CLASSIFIER_OBJECT_DETECTION == 1
        printf("Classification result (%d ms.):\n", result.timing.dsp + result.timing.classification);
        bool found_bb = false;
        for (size_t ix = 0; ix < EI_CLASSIFIER_OBJECT_DETECTION_COUNT; ix++) {
            auto bb = result.bounding_boxes[ix];
            if (bb.value == 0) {
                continue;
            }

	    cv::rectangle(cropped, cv::Point(bb.x, bb.y), cv::Point(bb.x+bb.width, bb.y+bb.height), cv::Scalar(255,255,255), 2);
            cv::putText(cropped, bb.label, cv::Point(bb.x, bb.y-5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,0), 1);

            found_bb = true;
            printf("    %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\n", bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);
        }

        if (!found_bb) {
            printf("    no objects found\n");
        }
    #else
        printf("(DSP+Classification) %d ms.\n", result.timing.dsp + result.timing.classification);
	size_t ix_max = -1;
	float max_value = 0.f;
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
	    if (result.classification[ix].value > max_value) {
	        max_value = result.classification[ix].value;
		ix_max =  ix;
	    }
            //printf("%s: %.05f", result.classification[ix].label, result.classification[ix].value);
            //if (ix != EI_CLASSIFIER_LABEL_COUNT - 1) {
            //    printf(", ");
            //}
        }
        //printf("\n");
	char text[30];
	sprintf(text, "%s: %.2f", result.classification[ix_max].label, result.classification[ix_max].value);
    #endif

        // show the image on the window
        if (use_debug) {
            cv::imshow("Webcam", cropped);
            // wait (10ms) for a key to be pressed
            if (cv::waitKey(10) >= 0)
                break;
        }

        int64_t sleep_ms = next_frame > (int64_t)ei_read_timer_ms() ? next_frame - (int64_t)ei_read_timer_ms() : 0;
        if (sleep_ms > 0) {
            usleep(sleep_ms * 1000);
        }


	if (streamer.isAlive()) { 
            std::vector<uchar> buff_bgr;
            cv::imencode(".jpg", cropped, buff_bgr, params);
            streamer.publish("/stream", std::string(buff_bgr.begin(), buff_bgr.end()));
        } else {
            printf("streamer is mot alive!\n");
	}
	 
    }

    streamer.stop();

    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor."
#endif
