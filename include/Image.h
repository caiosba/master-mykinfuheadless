#ifndef IMAGE_H
#define IMAGE_H

#include <iostream>
#include <pcl/gpu/containers/initialization.h>
#include <pcl/pcl_macros.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/gpu/containers/device_array.h>
#include <Eigen/Core>
#include <vector>
#include <pcl/gpu/containers/kernel_containers.h>
#include "internal.h"
#include "MyPointCloud.h"
//#include "../pcl-trunk/gpu/kinfu/tools/openni_capture.h"
#include "../tools/openni_capture.h"
#include <pcl/io/pcd_io.h>

using namespace std;
using namespace pcl;
using namespace pcl::gpu;

typedef DeviceArray2D<RGB> View;

class Image
{
public:
	Image(int cols, int rows);

	PtrStepSz<const unsigned short> getDepthMap() { return depthMap_; };
	PtrStepSz<const CaptureOpenNI::RGB> getRGBMap() { return rgbMap_; };
	device::DepthMap getDepthDevice() { return depthDevice_; };
	KinfuTracker::View getRgbDevice() { return rgbDevice_; };
	void getRaycastImage(KinfuTracker::View &viewDevice, Eigen::Vector3f volumeSize, MyPointCloud *globalPreviousPointCloud);
	void getRaycastImageFromPose(KinfuTracker::View &viewDevice, Eigen::Vector3f volumeSize, MyPointCloud *globalPreviousPointCloud);
	device::Intr& getIntrinsics() { return intrinsics; }
	float getTrancationDistance() { return trancationDistance_; }
	DeviceArray2D<float>& getDepthRawScaled() { return depthRawScaled_; }
	
	void setDepthDevice(device::DepthMap depthDevice) { depthDevice_ = depthDevice; };
	void setRgbDevice(KinfuTracker::View &rgbDevice) { rgbDevice_ = rgbDevice; }
	void setDepthMap(PtrStepSz<const unsigned short> depthMap) { depthMap_ = depthMap; }
	void setDepthIntrinsics(float fx, float fy, float cx = -1, float cy = -1);
	void setTrancationDistance(Eigen::Vector3f volumeSize);

	void applyBilateralFilter();
	void applyDepthTruncation(float truncValue);
	void applyDepthTruncation(device::DepthMap& depthMap, float truncValue);
	void applyPyrDown();
	void convertToPointCloud(MyPointCloud *currentPointCloud);
	void allocateBuffers(int cols, int rows);

private:
	PtrStepSz<const unsigned short> depthMap_;
	PtrStepSz<const CaptureOpenNI::RGB> rgbMap_;
	device::DepthMap depthDevice_;
	KinfuTracker::View rgbDevice_;
	DeviceArray2D<float> depthRawScaled_; 
	float trancationDistance_;
	device::Intr intrinsics;
	int cols_, rows_;
	std::vector<device::DepthMap> depths_curr_;
};

#endif
