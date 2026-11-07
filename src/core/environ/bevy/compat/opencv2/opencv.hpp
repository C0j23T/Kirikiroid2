// bevy port: RenderManager.cpp includes the OpenCV umbrella header but only
// uses boxFilter/resize/warpAffine/warpPerspective (imgproc) and
// getAffineTransform/getPerspectiveTransform. OpenCV 5 moved the latter two to
// the geometry module. Include just these modules; link opencv_core,
// opencv_imgproc and opencv_geometry.
#pragma once
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#if __has_include(<opencv2/geometry/2d.hpp>)
#include <opencv2/geometry/2d.hpp>
#endif
