#include <iostream>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <deque>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

#include <Eigen/StdVector>
#include <Eigen/Dense>

#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>

#include <pcl/registration/ndt.h>
#include <pcl/filters/approximate_voxel_grid.h>

#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "utility.h"
#include "kitti.h"
#include "costfunctions.h"
#include "velo.h"

//#define VISUALIZE

int main(int argc, char** argv) {
    cv::setUseOptimized(true); 
    if(argc < 2) {
        std::cout << "Usage: velo kittidatasetnumber. e.g. velo 00" << std::endl;
        return 1;
    }
    std::string dataset = argv[1];
    std::ofstream output;
    output.open(("results/" + std::string(argv[1]) + ".txt").c_str());
    loadImage(dataset, 0, 0); // to set width and height
    loadCalibration(dataset);
    loadTimes(dataset);

    for(int i=0; i<num_cams; i++) {
        std::cerr << cam_mat[i] << std::endl;
        std::cerr << cam_intrinsic[i] << std::endl;
        //std::cerr << cam_trans[i] << std::endl;
        //std::cerr << min_x[i] << " " << min_y[i] << " " << max_x[i] << " " << max_y[i] << " " << std::endl;
    }

    //std::cerr << velo_to_cam << std::endl;

    //std::cerr << times.size() << std::endl;

    int num_frames = times.size();

    cv::Ptr<cv::xfeatures2d::FREAK> freak = cv::xfeatures2d::FREAK::create();
    cv::Ptr<cv::GFTTDetector> gftt = cv::GFTTDetector::create(corner_count, 0.01, 3);

    std::vector<std::vector<std::vector<cv::Point2f>>> keypoints(num_cams,
            std::vector<std::vector<cv::Point2f>>(num_frames));
    std::vector<std::vector<cv::Mat>> descriptors(num_cams,
            std::vector<cv::Mat>(num_frames));
    std::vector<std::vector<std::vector<int>>> has_depth(num_cams,
            std::vector<std::vector<int>>(num_frames));
    std::vector<std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr>> kp_with_depth(
            num_cams,
            std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr>(num_frames));

#ifdef VISUALIZE
    char video[] = "video";
    cvNamedWindow(video);
#endif

    Eigen::Matrix4d pose = Eigen::Matrix4d::Identity();
    double transform[6] = {0, 0, 0, 0, 0, 0.5};

    for(int frame = 0; frame < num_frames; frame++) {
        auto start = clock()/double(CLOCKS_PER_SEC);

        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(
                new pcl::PointCloud<pcl::PointXYZ>);
        loadPoints(cloud, dataset, frame);

        std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> scans;
        segmentPoints(cloud, scans);
        if(scans.size() != 64) {
            std::cerr << "Scan " << frame << " has " << scans.size() << std::endl;
        }
        for(int cam = 0; cam<num_cams; cam++) {
            cv::Mat img = loadImage(dataset, cam, frame);

            //auto a = clock()/double(CLOCKS_PER_SEC);
            detectFeatures(keypoints[cam][frame],
                    descriptors[cam][frame],
                    gftt,
                    freak,
                    img,
                    cam);

            //auto b = clock()/double(CLOCKS_PER_SEC);
            std::vector<std::vector<cv::Point2f>> projection;
            std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> scans_valid;
            projectLidarToCamera(scans, projection, scans_valid, cam);

            //auto c = clock()/double(CLOCKS_PER_SEC);
            kp_with_depth[cam][frame] = 
                pcl::PointCloud<pcl::PointXYZ>::Ptr(
                        new pcl::PointCloud<pcl::PointXYZ>);
            has_depth[cam][frame] = 
                featureDepthAssociation(scans_valid,
                    projection,
                    keypoints[cam][frame],
                    kp_with_depth[cam][frame]);
            //auto d = clock()/double(CLOCKS_PER_SEC);
            //std::cerr << "clock (" << cam << "): " << a << " " << b << " " << c << " " << d << std::endl;
#ifdef VISUALIZE
            if(cam == 0 && frame > 0) {
                cv::Mat draw;
                cvtColor(img, draw, cv::COLOR_GRAY2BGR);
                //cv::drawKeypoints(img, keypoints[frame], draw);
                for(int k=0; k<keypoints[cam][frame].size(); k++) {
                    auto p = keypoints[cam][frame][k];
                    Eigen::Vector3f pe;
                    pe << p.x, p.y, 1;
                    pe = cam_intrinsic[cam] * pe;
                    cv::Point2f pp(pe(0)/pe(2), pe(1)/pe(2));
                    if(has_depth[cam][frame][k] != -1) {
                        cv::circle(draw, pp, 3, cv::Scalar(0, 0, 255), -1, 8, 0);
                    } else {
                        cv::circle(draw, pp, 3, cv::Scalar(255, 200, 0), -1, 8, 0);
                    }
                }
                for(int s=0, _s = projection.size(); s<_s; s++) {
                    auto P = projection[s];
                    for(auto p : P) {
                        Eigen::Vector3f pe;
                        pe << p.x, p.y, 1;
                        pe = cam_intrinsic[cam] * pe;
                        cv::Point2f pp(pe(0)/pe(2), pe(1)/pe(2));
                        cv::circle(draw, pp, 1, 
                                cv::Scalar(0, 128, 0), -1, 8, 0);
                    }
                }
                cv::imshow(video, draw);
                cvWaitKey(1);
                //cvWaitKey();
            }
#endif
        }

        if(frame > 0) {
            pose *= frameToFrame(
                    descriptors,
                    keypoints,
                    kp_with_depth,
                    has_depth,
                    frame,
                    frame-1,
                    transform);
            double end = clock()/double(CLOCKS_PER_SEC);
            std::cerr << "Frame (" << dataset << "):" 
                << std::setw(5) << frame+1 << "/" << num_frames << ", "
                << std::fixed << std::setprecision(3) <<  end-start << " |";
            for(int j=0; j<6; j++) {
                std::cerr << std::setfill(' ') << std::setw(8)
                    << std::fixed << std::setprecision(4)
                    << transform[j];
            }
            std::cerr << std::endl;
        }
        output_line(pose, output);

    }
    return 0;
}
