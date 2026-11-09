#include "HandTracker.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace HandTracker {

    // === БЫСТРОЕ ЗАПОЛНЕНИЕ ВХОДНОГО ТЕНЗОРА ===
    // BGR uint8 (HxWx3) -> RGB float [0..1], раскладка CHW.
    // Раньше это делалось тройным циклом с rgb.at<Vec3f>(y,x) на каждый пиксель - медленно.
    // Теперь cv::split сам раскладывает каналы прямо в нужный буфер.
    static void fillTensor(const cv::Mat& bgr, std::vector<float>& data, int size) {
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

        cv::Mat planes[3] = {
            cv::Mat(size, size, CV_32F, data.data()),
            cv::Mat(size, size, CV_32F, data.data() + size * size),
            cv::Mat(size, size, CV_32F, data.data() + 2 * size * size)
        };
        cv::split(rgb, planes);
    }

    // === HAND TRACK ===
    cv::Point2f HandTrack::center() const {
        cv::Point2f c(0, 0);
        for (int i : {0, 5, 9, 13, 17}) c += cv::Point2f(points[i].x, points[i].y);
        return c / 5.0f;
    }

    float HandTrack::size() const {
        float m = 0.0f;
        for (int i = 1; i < 21; ++i) {
            cv::Point2f d(points[i].x - points[0].x, points[i].y - points[0].y);
            m = std::max(m, (float)cv::norm(d));
        }
        return m;
    }

    void HandTrack::updatePoints(const std::array<cv::Point3f, 21>& newPts, float dt) {
        raw = newPts;
        for (int i = 0; i < 21; ++i)
            points[i] = filters[i].update(newPts[i], dt);
    }

    // === PALM DETECTOR ===
    bool PalmDetector::init(const std::string& modelPath) {
        try {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "palm");
            sessionOptions = std::make_unique<Ort::SessionOptions>();
            sessionOptions->SetIntraOpNumThreads(4);
            sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            std::wstring wpath(modelPath.begin(), modelPath.end());
            session = std::make_unique<Ort::Session>(*env, wpath.c_str(), *sessionOptions);

            inputName = session->GetInputNameAllocated(0, allocator).get();
            outRegressors = session->GetOutputNameAllocated(0, allocator).get();
            outClassificators = session->GetOutputNameAllocated(1, allocator).get();

            std::cout << "Palm: input=\"" << inputName << "\"\n";
            anchors = generateAnchors();
            std::cout << "Anchors: " << anchors.size() << "\n";

            inputData.resize(3 * PALM_INPUT_SIZE * PALM_INPUT_SIZE);
            return true;
        }
        catch (const Ort::Exception& e) {
            std::cerr << "Palm init error: " << e.what() << "\n";
            return false;
        }
    }

    std::vector<PalmDetector::Detection> PalmDetector::detect(const cv::Mat& frame, float threshold) {
        std::vector<Detection> result;
        if (frame.empty()) return result;

        int imgW = frame.cols, imgH = frame.rows;
        float scale = (float)PALM_INPUT_SIZE / std::max(imgW, imgH);
        float newW = imgW * scale, newH = imgH * scale;
        float padX = (PALM_INPUT_SIZE - newW) / 2.0f;
        float padY = (PALM_INPUT_SIZE - newH) / 2.0f;

        cv::Mat matrix = (cv::Mat_<double>(2, 3) << scale, 0, padX, 0, scale, padY);
        cv::Mat canvas;
        cv::warpAffine(frame, canvas, matrix,
            cv::Size(PALM_INPUT_SIZE, PALM_INPUT_SIZE),
            cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

        fillTensor(canvas, inputData, PALM_INPUT_SIZE);

        std::array<int64_t, 4> inputShape = { 1, 3, PALM_INPUT_SIZE, PALM_INPUT_SIZE };
        Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            mi, inputData.data(), inputData.size(),
            inputShape.data(), inputShape.size());

        const char* in[] = { inputName.c_str() };
        const char* out[] = { outRegressors.c_str(), outClassificators.c_str() };
        auto outputs = session->Run(Ort::RunOptions{ nullptr }, in, &inputTensor, 1, out, 2);

        auto regShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        int64_t N = regShape[1], regDim = regShape[2];

        float* regData = outputs[0].GetTensorMutableData<float>();
        float* clsData = outputs[1].GetTensorMutableData<float>();

        struct Candidate { float score, cx, cy, w, h; cv::Point2f wrist, mcp; };
        std::vector<Candidate> candidates;

        for (int64_t i = 0; i < N; ++i) {
            float score = 1.0f / (1.0f + std::exp(-clsData[i]));
            if (score < threshold) continue;

            float* reg = regData + i * regDim;
            const cv::Point2f& a = anchors[i];

            Candidate c;
            c.score = score;
            c.cx = reg[0] / PALM_INPUT_SIZE + a.x;
            c.cy = reg[1] / PALM_INPUT_SIZE + a.y;
            c.w = reg[2] / PALM_INPUT_SIZE;
            c.h = reg[3] / PALM_INPUT_SIZE;
            c.wrist = { reg[4] / PALM_INPUT_SIZE + a.x, reg[5] / PALM_INPUT_SIZE + a.y };
            c.mcp = { reg[8] / PALM_INPUT_SIZE + a.x, reg[9] / PALM_INPUT_SIZE + a.y };
            candidates.push_back(c);
        }

        if (candidates.empty()) return result;

        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        std::vector<Candidate> kept;
        std::vector<bool> suppressed(candidates.size(), false);

        for (size_t i = 0; i < candidates.size(); ++i) {
            if (suppressed[i]) continue;
            const auto& top = candidates[i];

            std::vector<size_t> overlapping = { i };
            float tx1 = top.cx - top.w / 2, ty1 = top.cy - top.h / 2;
            float tx2 = top.cx + top.w / 2, ty2 = top.cy + top.h / 2;

            for (size_t j = i + 1; j < candidates.size(); ++j) {
                if (suppressed[j]) continue;
                const auto& c = candidates[j];
                float x1 = c.cx - c.w / 2, y1 = c.cy - c.h / 2;
                float x2 = c.cx + c.w / 2, y2 = c.cy + c.h / 2;
                float inter = std::max(0.0f, std::min(x2, tx2) - std::max(x1, tx1)) *
                    std::max(0.0f, std::min(y2, ty2) - std::max(y1, ty1));
                float uni = (x2 - x1) * (y2 - y1) + (tx2 - tx1) * (ty2 - ty1) - inter;
                if (inter / std::max(uni, 1e-9f) > 0.3f) overlapping.push_back(j);
            }

            Candidate b = top;
            float sumW = 0, aCx = 0, aCy = 0, aW = 0, aH = 0;
            float aWx = 0, aWy = 0, aMx = 0, aMy = 0;
            for (size_t k : overlapping) {
                const auto& c = candidates[k];
                float w = c.score;
                sumW += w;
                aCx += c.cx * w; aCy += c.cy * w; aW += c.w * w; aH += c.h * w;
                aWx += c.wrist.x * w; aWy += c.wrist.y * w;
                aMx += c.mcp.x * w; aMy += c.mcp.y * w;
            }
            b.cx = aCx / sumW; b.cy = aCy / sumW; b.w = aW / sumW; b.h = aH / sumW;
            b.wrist = { aWx / sumW, aWy / sumW };
            b.mcp = { aMx / sumW, aMy / sumW };

            kept.push_back(b);
            for (size_t k : overlapping) suppressed[k] = true;
            if ((int)kept.size() >= MAX_HANDS * 2) break;
        }

        for (auto& c : kept) {
            Detection d;
            d.score = c.score;
            d.cx = (c.cx * PALM_INPUT_SIZE - padX) / scale;
            d.cy = (c.cy * PALM_INPUT_SIZE - padY) / scale;
            d.width = std::max(c.w, c.h) * PALM_INPUT_SIZE / scale;
            d.wrist.x = (c.wrist.x * PALM_INPUT_SIZE - padX) / scale;
            d.wrist.y = (c.wrist.y * PALM_INPUT_SIZE - padY) / scale;
            d.mcp.x = (c.mcp.x * PALM_INPUT_SIZE - padX) / scale;
            d.mcp.y = (c.mcp.y * PALM_INPUT_SIZE - padY) / scale;
            if (d.width < 20.0f) continue;
            if (d.cx < 0 || d.cx > imgW || d.cy < 0 || d.cy > imgH) continue;
            result.push_back(d);
        }
        return result;
    }

    // === ROI ПО ЛАДОНИ (как было раньше) ===
    Roi roiFromPalm(const PalmDetector::Detection& palm) {
        cv::Point2f dir = palm.mcp - palm.wrist;
        float norm = (float)cv::norm(dir);
        if (norm < 1e-6f) norm = 1.0f;
        cv::Point2f unit = dir / norm;

        Roi r;
        r.cx = palm.cx + ROI_SHIFT * palm.width * unit.x;
        r.cy = palm.cy + ROI_SHIFT * palm.width * unit.y;
        r.angleDeg = std::atan2(unit.y, unit.x) * 180.0f / (float)CV_PI + 90.0f;
        r.side = ROI_SCALE * palm.width;
        return r;
    }

    // === ROI ПО КЛЮЧЕВЫМ ТОЧКАМ ПРОШЛОГО КАДРА ===
    // Главная идея: детектор ладони нужен только чтобы НАЙТИ руку.
    // Дальше руку ведём по её же точкам: рамку строим вокруг точек прошлого кадра.
    // Поэтому кулак (который детектор ладони находит плохо) не теряется.
    Roi roiFromLandmarks(const std::array<cv::Point3f, 21>& pts) {
        cv::Point2f wrist(pts[0].x, pts[0].y);
        cv::Point2f dir = cv::Point2f(pts[9].x, pts[9].y) - wrist;   // запястье -> основание среднего пальца
        float angle = std::atan2(dir.y, dir.x) + (float)CV_PI / 2.0f; // тот же угол, что и в roiFromPalm
        float c = std::cos(angle), s = std::sin(angle);

        // Поворачиваем точки так, чтобы рука "стояла" пальцами вверх, и берём рамку вокруг них
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (const auto& p : pts) {
            float dx = p.x - wrist.x, dy = p.y - wrist.y;
            float qx = c * dx + s * dy;
            float qy = -s * dx + c * dy;
            minX = std::min(minX, qx); maxX = std::max(maxX, qx);
            minY = std::min(minY, qy); maxY = std::max(maxY, qy);
        }
        float w = maxX - minX, h = maxY - minY;
        float qcx = (minX + maxX) / 2.0f;
        float qcy = (minY + maxY) / 2.0f + ROI_LM_SHIFT_Y * h;   // чуть вверх, к пальцам

        // Центр рамки возвращаем в координаты кадра (обратный поворот)
        Roi r;
        r.cx = wrist.x + c * qcx - s * qcy;
        r.cy = wrist.y + s * qcx + c * qcy;
        r.angleDeg = angle * 180.0f / (float)CV_PI;
        r.side = ROI_LM_SCALE * std::max(w, h);
        return r;
    }

    // === HAND LANDMARKER ===
    bool HandLandmarker::init(const std::string& modelPath) {
        try {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "lm");
            sessionOptions = std::make_unique<Ort::SessionOptions>();
            sessionOptions->SetIntraOpNumThreads(4);
            sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            std::wstring wpath(modelPath.begin(), modelPath.end());
            session = std::make_unique<Ort::Session>(*env, wpath.c_str(), *sessionOptions);

            inputName = session->GetInputNameAllocated(0, allocator).get();
            outScreen = session->GetOutputNameAllocated(0, allocator).get();
            outPresence = session->GetOutputNameAllocated(1, allocator).get();
            outHandedness = session->GetOutputNameAllocated(2, allocator).get();
            outWorld = session->GetOutputNameAllocated(3, allocator).get();

            std::cout << "Landmark: input=\"" << inputName << "\"\n";

            inputData.resize(3 * HAND_INPUT_SIZE * HAND_INPUT_SIZE);
            return true;
        }
        catch (const Ort::Exception& e) {
            std::cerr << "Landmark init error: " << e.what() << "\n";
            return false;
        }
    }

    HandLandmarker::Result HandLandmarker::predict(const cv::Mat& frame, const Roi& roi,
        float presenceThreshold) {
        Result res;
        if (frame.empty() || roi.side < 20.0f) return res;

        cv::Mat matrix = cv::getRotationMatrix2D({ roi.cx, roi.cy },
            roi.angleDeg, (double)HAND_INPUT_SIZE / roi.side);
        matrix.at<double>(0, 2) += HAND_INPUT_SIZE / 2.0 - roi.cx;
        matrix.at<double>(1, 2) += HAND_INPUT_SIZE / 2.0 - roi.cy;

        cv::Mat crop;
        cv::warpAffine(frame, crop, matrix,
            cv::Size(HAND_INPUT_SIZE, HAND_INPUT_SIZE),
            cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        cv::Mat inverse;
        cv::invertAffineTransform(matrix, inverse);

        fillTensor(crop, inputData, HAND_INPUT_SIZE);

        std::array<int64_t, 4> inputShape = { 1, 3, HAND_INPUT_SIZE, HAND_INPUT_SIZE };
        Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            mi, inputData.data(), inputData.size(),
            inputShape.data(), inputShape.size());

        const char* in[] = { inputName.c_str() };
        const char* out[] = { outScreen.c_str(), outPresence.c_str(),
                              outHandedness.c_str(), outWorld.c_str() };
        auto outputs = session->Run(Ort::RunOptions{ nullptr }, in, &inputTensor, 1, out, 4);

        float* screen = outputs[0].GetTensorMutableData<float>();
        float* presence = outputs[1].GetTensorMutableData<float>();
        float* handedness = outputs[2].GetTensorMutableData<float>();

        res.presence = presence[0];
        res.handednessProb = handedness[0];
        res.handedness = handedness[0] > 0.5f ? 1 : 0;
        if (res.presence < presenceThreshold) return res;

        float z_scale = roi.side / (float)HAND_INPUT_SIZE;
        for (int i = 0; i < 21; ++i) {
            float x = screen[i * 3 + 0], y = screen[i * 3 + 1], z = screen[i * 3 + 2];
            float xImg = (float)(inverse.at<double>(0, 0) * x + inverse.at<double>(0, 1) * y + inverse.at<double>(0, 2));
            float yImg = (float)(inverse.at<double>(1, 0) * x + inverse.at<double>(1, 1) * y + inverse.at<double>(1, 2));
            res.points[i] = cv::Point3f(xImg, yImg, z * z_scale);
        }
        res.detected = true;
        return res;
    }

    // === ОТРИСОВКА ===
    void drawHand(cv::Mat& frame, const HandTrack& hand, cv::Scalar color, int id) {
        for (auto& c : CONNECTIONS) {
            cv::line(frame,
                cv::Point((int)hand.points[c.first].x, (int)hand.points[c.first].y),
                cv::Point((int)hand.points[c.second].x, (int)hand.points[c.second].y),
                color, 2, cv::LINE_AA);
        }
        for (int i = 0; i < 21; ++i)
            cv::circle(frame,
                cv::Point((int)hand.points[i].x, (int)hand.points[i].y),
                4, color, -1, cv::LINE_AA);

        for (int i : FINGERTIPS) {
            cv::Point p((int)hand.points[i].x, (int)hand.points[i].y);
            cv::circle(frame, p, 9, cv::Scalar(0, 0, 255), -1, cv::LINE_AA);
            cv::circle(frame, p, 11, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
        }

        cv::Point wrist((int)hand.points[0].x, (int)hand.points[0].y);
        std::string label = "Hand " + std::to_string(id);
        label += (hand.handedness == 1) ? " (L)" : " (R)";
        cv::putText(frame, label, cv::Point(wrist.x - 30, wrist.y + 25),
            cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2, cv::LINE_AA);
    }

} // namespace HandTracker
