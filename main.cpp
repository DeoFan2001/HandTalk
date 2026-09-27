#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <vector>

#include "ScreenCapture.h"
#include "HandTracker.h"
#include "UI.h"
    
using namespace HandTracker;

int main() {
    SetProcessDPIAware();
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

    std::cout << "=== Dual Hand Tracker ===\n\n";

    PalmDetector palm;
    if (!palm.init("models\\palm_detection.onnx")) { std::cin.get(); return -1; }

    HandLandmarker lm;
    if (!lm.init("models\\hand_landmark.onnx")) { std::cin.get(); return -1; }

    std::optional<Region> region;
    while (!region) {
        if (GetAsyncKeyState('Q') & 0x8000) return 0;
        if (GetAsyncKeyState('G') & 0x8000) {
            region = selectRegion();
            while (GetAsyncKeyState('G') & 0x8000) Sleep(50);
        }
        Sleep(50);
    }

    ScreenCapture cap(region->x, region->y, region->w, region->h);

    std::string win = "Overlay";
    cv::namedWindow(win, cv::WINDOW_NORMAL);
    cv::resizeWindow(win, region->w, region->h);
    cv::moveWindow(win, region->x, region->y);

    cv::Mat& first = cap.grab();
    cv::imshow(win, first);
    cv::waitKey(50);
    setupOverlayWindow(win, region->x, region->y, region->w, region->h, 200);

    std::cout << "\nOverlay active. Press Q to quit.\n\n";

    int frameCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    int palmCountdown = 0;
    int nextHandId = 1;

    std::vector<HandTrack> tracks;
    std::vector<PalmDetector::Detection> lastPalms;

    const std::vector<cv::Scalar> colors = {
        cv::Scalar(0, 255, 0), cv::Scalar(255, 128, 0),
        cv::Scalar(0, 255, 255), cv::Scalar(255, 0, 255),
    };

    while (true) {
        cv::Mat& frame = cap.grab();

        if (palmCountdown <= 0) {
            lastPalms = palm.detect(frame, PALM_THRESHOLD);
            palmCountdown = PALM_SKIP;
        }
        else palmCountdown--;

        std::vector<HandLandmarker::Result> newDetections;
        for (auto& p : lastPalms) {
            auto res = lm.predict(frame, p);
            if (res.detected) newDetections.push_back(res);
        }

        std::vector<bool> detectionUsed(newDetections.size(), false);

        for (auto& track : tracks) {
            cv::Point2f trackCenter = track.center();
            float bestDist = TRACK_MAX_DIST;
            int bestIdx = -1;

            for (size_t di = 0; di < newDetections.size(); ++di) {
                if (detectionUsed[di]) continue;
                cv::Point2f detCenter(0, 0);
                for (int i : {0, 5, 9, 13, 17})
                    detCenter += cv::Point2f(newDetections[di].points[i].x, newDetections[di].points[i].y);
                detCenter /= 5.0f;
                float dist = cv::norm(trackCenter - detCenter);
                if (dist < bestDist) { bestDist = dist; bestIdx = (int)di; }
            }

            if (bestIdx >= 0) {
                const auto& det = newDetections[bestIdx];
                track.updatePoints(det.points);
                track.handedness = det.handedness;
                track.score = det.presence;
                track.missedFrames = 0;
                track.active = true;
                detectionUsed[bestIdx] = true;
            }
            else {
                track.missedFrames++;
                if (track.missedFrames > TRACK_MAX_MISS) track.active = false;
            }
        }

        for (size_t di = 0; di < newDetections.size(); ++di) {
            if (detectionUsed[di]) continue;
            if ((int)tracks.size() >= MAX_HANDS) break;
            HandTrack t;
            t.id = nextHandId++;
            t.handedness = newDetections[di].handedness;
            t.score = newDetections[di].presence;
            t.missedFrames = 0;
            t.active = true;
            t.updatePoints(newDetections[di].points);
            tracks.push_back(t);
        }

        tracks.erase(
            std::remove_if(tracks.begin(), tracks.end(),
                [](const HandTrack& t) { return !t.active; }),
            tracks.end());

        for (size_t i = 0; i < tracks.size(); ++i)
            drawHand(frame, tracks[i], colors[i % colors.size()], tracks[i].id);

        cv::putText(frame, "hands: " + std::to_string(tracks.size()),
            { 10, 30 }, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);

        cv::imshow(win, frame);

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (el >= 1000) {
            std::cout << "FPS: " << frameCount << " | tracks: " << tracks.size() << "\r" << std::flush;
            frameCount = 0; startTime = now;
        }

        if (GetAsyncKeyState('Q') & 0x8000) break;
        cv::waitKey(1);
    }
    return 0;
}