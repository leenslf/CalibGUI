#ifndef CALIBTOOLS_H
#define CALIBTOOLS_H

#include <string>
#include <vector>

#include <QString>

#include <opencv2/core.hpp>

#include "config.h"

struct CalibrationResult {
  bool    success = false;
  QString errorMessage;
  float   fx = 0.0f;
  float   fy = 0.0f;
  float   cx = 0.0f;
  float   cy = 0.0f;
  cv::Mat distCoeffs;
  std::vector<float> perViewErrors;
};

class CalibTools {
public:
  static constexpr int kFrameCount = 30;

  // Runs the full ChArUco calibration pipeline 
  static CalibrationResult RunCalibration(std::string path, CalibSettings settings);

  // Appends every *.png/*.jpg/*.jpeg found directly in dirPath to *filenames.
  static void GlobFrameFiles(const std::string &dirPath, std::vector<cv::String> *filenames);

  static std::string SanitizeFolderName(const std::string &name);
};

#endif
