#pragma once
#include <opencv2/opencv.hpp>
#include <optional>
#include <string>

namespace HandTracker {

	struct Region { int x, y, w, h; };

	std::optional<Region> selectRegion();

} // namespace HandTracker