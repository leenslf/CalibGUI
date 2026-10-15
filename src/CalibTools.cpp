#include "CalibTools.h"

#include <cctype>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>

#undef DEBUG_LOCAL
#include "debuglog.h"

static const char *kFrameExtensions[] = { "*.png", "*.jpg", "*.jpeg" };

void CalibTools::GlobFrameFiles(const std::string &dirPath, std::vector<cv::String> *filenames) {
  filenames->clear();
  for (const char *ext : kFrameExtensions) {
    std::vector<cv::String> matches;
    cv::glob(dirPath + ext, matches, false);
    filenames->insert(filenames->end(), matches.begin(), matches.end());
  }
}

std::string CalibTools::SanitizeFolderName(const std::string &name) {
  std::string out;
  for (char c : name) {
    out += (isalnum((unsigned char) c) || c == '-' || c == '_') ? c : '_';
  }
  return out.empty() ? "kamera" : out;
}

// Runs entirely on a QtConcurrent worker thread.
CalibrationResult CalibTools::RunCalibration(std::string path, CalibSettings settings) {
  CalibrationResult result;

  std::vector<cv::String> filenames;
  GlobFrameFiles(path, &filenames);

  if ((int) filenames.size() < kMinFrameCount) {
    result.errorMessage = QString("HATA: Kalibrasyon için yeterli kare yok (%1). En az %2 kare gereklidir.")
      .arg((int) filenames.size()).arg(kMinFrameCount);
    return result;
  }

  cv::Ptr<cv::aruco::Dictionary> dictionary =
    cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
  cv::Ptr<cv::aruco::CharucoBoard> board = cv::aruco::CharucoBoard::create(
    settings.squaresX, settings.squaresY,
    settings.squareLengthM, settings.markerLengthM, dictionary);

  std::vector<std::vector<cv::Point2f>> allCharucoCorners;
  std::vector<std::vector<int>> allCharucoIds;
  cv::Size imageSize;

  for (const cv::String &fn : filenames) {
    cv::Mat image = cv::imread(fn);
    if (image.empty()) continue;
    imageSize = image.size();

    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> markerCorners;
    cv::aruco::detectMarkers(image, dictionary, markerCorners, markerIds);
    if (markerIds.empty()) continue;

    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;
    cv::aruco::interpolateCornersCharuco(markerCorners, markerIds, image, board,
                                          charucoCorners, charucoIds);
    if ((int) charucoIds.size() < settings.minValidFrames) continue;

    allCharucoCorners.push_back(charucoCorners);
    allCharucoIds.push_back(charucoIds);
  }

  if ((int) allCharucoCorners.size() < settings.minValidFrames) {
    result.errorMessage = "HATA: Kalibrasyon tahtası yeterli sayıda karede tespit edilemedi.";
    return result;
  }

  std::vector<cv::Mat> rvecs, tvecs;
  cv::Mat cameraMatrix;
  cv::Mat stdDeviationsIntrinsics, stdDeviationsExtrinsics, perViewErrors;
  try {
    cv::aruco::calibrateCameraCharuco(
      allCharucoCorners, allCharucoIds, board, imageSize,
      cameraMatrix, result.distCoeffs, rvecs, tvecs,
      stdDeviationsIntrinsics, stdDeviationsExtrinsics, perViewErrors);
  } catch (const cv::Exception &e) {
    ERRPRINTF("[EE] CalibTools::RunCalibration: %s\n", e.what());
    result.errorMessage = "HATA: Kalibrasyon başarısız oldu.";
    return result;
  }
  result.fx = (float) cameraMatrix.at<double>(0, 0);
  result.fy = (float) cameraMatrix.at<double>(1, 1);
  result.cx = (float) cameraMatrix.at<double>(0, 2);
  result.cy = (float) cameraMatrix.at<double>(1, 2);

  result.perViewErrors.reserve(perViewErrors.total());
  for (int i = 0; i < (int) perViewErrors.total(); ++i) {
    result.perViewErrors.push_back((float) perViewErrors.at<double>(i));
  }

  result.success = true;
  return result;
}
