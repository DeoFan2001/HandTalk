#pragma once
#include <opencv2/opencv.hpp>
#include <optional>
#include <string>

namespace HandTracker {

	struct Region { int x, y, w, h; };

	std::optional<Region> selectRegion();
	bool setupOverlayWindow(const std::string& winName, int x, int y, int w, int h, int alpha = 200);

} // namespace HandTracker