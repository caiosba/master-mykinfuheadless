// TCCKinFu.cpp : Defines the entry point for the console application.
//

#include <iostream>
#include <pcl/console/parse.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include <pcl/gpu/containers/initialization.h>
#include "Reconstruction.h"
#include "VolumetricData.h"
#include "Viewers/MyGLImageViewer.h"
#include "Viewers/MyGLCloudViewer.h"
#include "Viewers/shader.h"
#include "Mediators/MeshGenerationMediator.h"
#include "Mediators/ColoredReconstructionMediator.h"
#include "Mediators/HeadPoseEstimationMediator.h"
#include "Kinect.h"
#include "FaceDetection.h"
#include <iostream>
#include <boost/thread/thread.hpp>
#include <pcl/common/common_headers.h>
#include <pcl/io/pcd_io.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <vtkCamera.h>
#include "opencv2/core/core.hpp"
#include <opencv2/highgui/highgui.hpp>
#include "opencv2/calib3d/calib3d.hpp"
#include <opencv2/imgproc/imgproc.hpp>
#include "opencv2/contrib/contrib.hpp"
#include <unistd.h>
#include "Glasses.h"

using namespace std;
using namespace pcl;
using namespace pcl::gpu;
using namespace Eigen;
using namespace cv;

//Window's size

int windowWidth = 1280;
int windowHeight = 960;
int socketPort = 6001;
int skipframes = 0;

//Our Objects
Kinect *kinect;
Reconstruction *reconstruction;
MyGLImageViewer *myGLImageViewer;
MyGLCloudViewer *myGLCloudViewer;
VolumetricData *volumetricData;
ColoredReconstructionMediator *coloredReconstructionMediator;
HeadPoseEstimationMediator *headPoseEstimationMediator;
FaceDetection *faceDetector;
	
//VBOs
GLuint texVBO[10]; 
GLuint spt[2];
GLuint meshVBO[4];
/* texVBO
0 -- Depth
1 -- Real RGB
2 -- Raycast
3 -- ARFromVolume (Grid)
4 -- Virtual DepthBuffer
5 -- Virtual FrameBuffer
6 -- Real DepthBuffer
7 -- ARFromVolume (Volume Rendering)
*/

enum
{
	REAL_DEPTH_FROM_DEPTHMAP_BO = 0,
	REAL_RGB_BO = 1,
	RAYCAST_BO = 2,
	AR_FROM_VOLUME_KINECTFUSION_BO = 3,
	VIRTUAL_DEPTH_BO = 4,
	VIRTUAL_RGB_BO = 5,
	REAL_DEPTH_FROM_DEPTHBUFFER_BO = 6,
	AR_FROM_VOLUME_RENDERING_BO = 7
};

int indices[640 * 480 * 6];
float pointCloud[640 * 480 * 3];
float normalVector[640 * 480 * 3];

// Use a stream from an .ONI file instead of using live stream from Kinect (default)
bool live = true;
char *onifile = "stream.oni";

//AR (General attributes)
int vel = 8;
float scale = 1;
float translationVector[3];
float rotationAngles[3];
bool translationOn = false;
bool rotationOn = false;
bool AR = false;
bool ARConfiguration = false;

//AR Configuration (Polygonal model)
bool ARPolygonal = false;
char ARObjectFileName[1000];

//AR Configuration (Volumetric model)
char volumetricPath[1000];
int firstSlice, lastSlice;
char volumetricPathExtension[3];
bool ARVolumetric = false;

bool ARKinectFusionVolume = true;

bool integrateColors = false;
bool isHeadPoseEstimationEnabled = false;
bool hasFaceDetection = false;
bool faceDetected = false;
bool shader=true;

bool showCloud = false;
bool showRaycasting = true;
bool showDepthMap = false;
bool showRGBMap = true;

//
// Global handles for the currently active program object, with its two shader objects
//
GLuint ProgramObject = 0;
GLuint VertexShaderObject = 0;
GLuint FragmentShaderObject = 0;

GLuint shaderVS, shaderFS, shaderProg[3];   // handles to objects
GLint  linked;

int w1 = 1, w2 = 0, w3 = 120; 
int workAround = 0;

void printHelp() {

	std::cout << "Help " << std::endl;
	std::cout << "--cloud: Show Cloud " << std::endl;
	std::cout << "--mesh: Show Mesh extracted from MC " << std::endl;
	std::cout << "--color: Enable color integration " << std::endl;
	std::cout << "--hpefile config.txt: Head Pose Estimation Config File " << std::endl; 
	std::cout << "--threshold value: Depth Threshold for depth map truncation " << std::endl;
	std::cout << "--face cascades.txt: Enable face detection " << std::endl;
	
	std::cout << "On the fly.. " << std::endl;
	std::cout << "Press 'h' to enable head pose tracking " << std::endl;
	std::cout << "Press 'p' to stop the head pose tracking " << std::endl;
	std::cout << "Press 'c' to continue the head pose tracking " << std::endl;
	std::cout << "Press 's' to save point cloud or mesh (if --mesh is enabled) " << std::endl;
	std::cout << "Press 'a' to enable AR application " << std::endl;

}

void saveModel()
{

	int op;
	std::cout << "Saving Model..." << std::endl;
	std::cout << "Do you want to save a point cloud (.pcd) or a mesh (.ply)? (0: point cloud; 1: mesh)" << std::endl;
	std::cin >> op;
	if(op == 0) {
		if(integrateColors)
			coloredReconstructionMediator->savePointCloud(reconstruction->getTsdfVolume());
		else
			reconstruction->savePointCloud();
	} else {
		MeshGenerationMediator mgm;
		mgm.saveMesh(reconstruction->getTsdfVolume());
	}
}

void setScale(unsigned char key)
{
	
	if(ARPolygonal)
		myGLCloudViewer->setOBJScale(1.f/scale);
	if(key == 's')
		if(scale < 2)
			scale *= 2;
		else
			scale++;
	else 
		if(scale < 2)
			scale /= 2;
		else
			scale--;
	if(ARPolygonal)
		myGLCloudViewer->setOBJScale(scale);

}

void positionVirtualObject(int x, int y)
{
	//We compute the translationVector necessary to move the virtual object
	float virtualCentroid[3];
	if (ARPolygonal)
		myGLCloudViewer->computeARModelCentroid(virtualCentroid);
	if (ARVolumetric) {
		virtualCentroid[0] = 0.5f; virtualCentroid[1] = 0.5f; virtualCentroid[2] = 0.5f;
	}

	//We need to update the centroid
	virtualCentroid[0] += translationVector[0];
	virtualCentroid[1] += translationVector[1];
	virtualCentroid[2] += translationVector[2];

	y = windowHeight/2 - (y - windowHeight/2);
	int pixel = y * windowWidth/2 + x;
	
	float cx = reconstruction->getIntrinsics().cx;
	float cy = reconstruction->getIntrinsics().cy;
	float fx = reconstruction->getIntrinsics().fx;
	float fy = reconstruction->getIntrinsics().fy;
	
	// If the chosen point is visible
	if(reconstruction->getCurrentDepthMap()[pixel] != 0) {
	
		float xp = (float)(x - cx) * reconstruction->getCurrentDepthMap()[pixel]/fx;
		float yp = (float)(y - cy) * reconstruction->getCurrentDepthMap()[pixel]/fy;
		float zp = reconstruction->getCurrentDepthMap()[pixel];
					
		yp *= -1;
				
		translationVector[0] += xp - virtualCentroid[0];
		translationVector[1] += yp - virtualCentroid[1];
		translationVector[2] += zp - virtualCentroid[2];
				
	}
}

void loadArguments(int argc, char **argv, Reconstruction *reconstruction)
{
	//Default arguments
	char fileName[100];
	char hpeConfigFileName[100];
	char cascadeFileName[100];
	char aux[5];
	int begin = 0;
	int end = 0;
	int threshold = 700; // Decrease this value to remove the background
	
  if(pcl::console::find_argument(argc, argv, "--cloud") >= 0) {
	showCloud = true;
  }
  if(pcl::console::find_argument(argc, argv, "--color") >= 0) {
	integrateColors = true;
	coloredReconstructionMediator = new ColoredReconstructionMediator(reconstruction->getVolumeSize());
  }
  if(pcl::console::find_argument(argc, argv, "-h") >= 0) {
	printHelp();
  }
  if(pcl::console::parse(argc, argv, "--threshold", aux) >= 0) {
	threshold = atoi(aux);
  }
  if(pcl::console::parse(argc, argv, "--hpefile", hpeConfigFileName) >= 0) {
	isHeadPoseEstimationEnabled = true;
	headPoseEstimationMediator = new HeadPoseEstimationMediator(hpeConfigFileName);
	reconstruction->setHeadPoseEstimationMediatorPointer((void*)headPoseEstimationMediator);
  }
  if(pcl::console::parse(argc, argv, "--face", cascadeFileName) >= 0) {
	hasFaceDetection = true;
	faceDetector = new FaceDetection(cascadeFileName);
  }
  if(pcl::console::parse(argc, argv, "--arobject", ARObjectFileName) >= 0) {
	  ARPolygonal = true;
	  ARKinectFusionVolume = false;
  }
  if(pcl::console::parse(argc, argv, "--arvolumetif", volumetricPath) >= 0) {
	  sprintf(volumetricPathExtension, "%s", "tif");
	  ARVolumetric = true;
	  ARKinectFusionVolume = false;
  }
  if(pcl::console::parse_2x_arguments(argc, argv, "--arvolumeslices", firstSlice, lastSlice) >= 0) {
  }

#if (!USE_KINECT)
  if(argc > 1) {
	  for(int args = 1; args < argc; args++) {
		if(argv[args][1] == 'f') {
		  sprintf(fileName, argv[args + 1]); 
		  begin = atoi((const char*)argv[args + 2]);
		  end = atoi((const char*)argv[args + 3]);
		}
	  }
  }
#endif

	//Initialize reconstruction with arguments
	reconstruction->setThreshold(threshold);

}

void reshape(int w, int h)
{
	windowWidth = w;
	windowHeight = h;

	glViewport( 0, 0, windowWidth, windowHeight );
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	gluOrtho2D( 0, windowWidth, 0, windowHeight );
	glMatrixMode( GL_MODELVIEW );

}

void displayDepthData()
{
	glViewport(windowWidth/2, 0, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity();    

	myGLImageViewer->loadDepthTexture(reconstruction->getCurrentDepthMap(), texVBO, REAL_DEPTH_FROM_DEPTHMAP_BO, reconstruction->getThreshold(), windowWidth, windowHeight);
	myGLImageViewer->drawRGBTexture(texVBO, REAL_DEPTH_FROM_DEPTHMAP_BO, windowWidth, windowHeight);

}

void displayRGBData()
{
	glViewport(windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 
  // if (reconstruction->poseChanged()) {
  //   Mat img;
  //   VideoCapture cap = VideoCapture(1); // Change ID in order to change camera
  //   cap >> img;
  //   cvtColor(img, img, CV_BGR2RGB);
  //   myGLImageViewer->loadRGBTexture((const unsigned char*)img.data, texVBO, REAL_RGB_BO, windowWidth, windowHeight);
  // }
  // else {
	  myGLImageViewer->loadRGBTexture(reconstruction->getRGBMap(), texVBO, REAL_RGB_BO, windowWidth, windowHeight);
	// }
	myGLImageViewer->drawRGBTexture(texVBO, REAL_RGB_BO, windowWidth, windowHeight);
}

void displayRaycastedData()
{
	glViewport(0, windowHeight/2, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity();
  if (reconstruction->poseChanged()) {
	  if (reconstruction->getEnableGlassesBackground()) {
      Mat bg = reconstruction->getGlassesFrame();
		  Mat image = Mat(480, 640, CV_8UC3, reconstruction->getRaycastImageFromPose());
			if (bg.data && image.data) {
        cvtColor(bg, bg, CV_BGR2RGB);
		    for (int i=0; i < image.rows; i++) {
		    	for (int j=0; j < image.cols; j++) {
		    		if (image.at<cv::Vec3b>(i,j)[0] == 0 && image.at<cv::Vec3b>(i,j)[1] == 0 && image.at<cv::Vec3b>(i,j)[2] == 0) {
		    	    image.at<cv::Vec3b>(i,j)[0] = bg.at<cv::Vec3b>(i,j)[0];
		    	    image.at<cv::Vec3b>(i,j)[1] = bg.at<cv::Vec3b>(i,j)[1];
		    	    image.at<cv::Vec3b>(i,j)[2] = bg.at<cv::Vec3b>(i,j)[2];
		    		}
          }
		    }
		    myGLImageViewer->loadRGBTexture((const unsigned char*)image.data, texVBO, RAYCAST_BO, windowWidth, windowHeight);
			}
		}
		else {
	    myGLImageViewer->loadRGBTexture(reconstruction->getRaycastImageFromPose(), texVBO, RAYCAST_BO, windowWidth, windowHeight);
		}
  }
  else {
	  myGLImageViewer->loadRGBTexture(reconstruction->getRaycastImage(), texVBO, RAYCAST_BO, windowWidth, windowHeight);
  }
	myGLImageViewer->drawRGBTexture(texVBO, RAYCAST_BO, windowWidth, windowHeight);
}

void displayARDataFromVolume()
{
	/*
	glViewport(0, 0, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 
	myGLImageViewer->loadARTexture(reconstruction->getRGBMap(), reconstruction->getRaycastImage(), texVBO, AR_FROM_VOLUME_KINECTFUSION_BO, windowWidth, windowHeight);
	myGLImageViewer->drawRGBTexture(texVBO, AR_FROM_VOLUME_KINECTFUSION_BO, windowWidth, windowHeight);
	*/

	//Second Viewport: Virtual Object
	glViewport(windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 

	//myGLImageViewer->loadARTexture(reconstruction->getRGBMap(), reconstruction->getRaycastImage(), texVBO, AR_FROM_VOLUME_KINECTFUSION_BO, windowWidth, windowHeight);
	myGLImageViewer->loadRGBTexture(reconstruction->getRaycastImage(), texVBO, RAYCAST_BO, windowWidth, windowHeight);
	//myGLImageViewer->setProgram(shaderProg[2]);
	myGLImageViewer->drawRGBTexture(texVBO, RAYCAST_BO, windowWidth, windowHeight);

	//Fourth Viewport: Virtual + Real Object
	glViewport(windowWidth/2, 0, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 

	myGLImageViewer->loadRGBTexture(reconstruction->getRGBMap(), texVBO, REAL_RGB_BO, windowWidth, windowHeight);

	myGLImageViewer->setProgram(shaderProg[1]);
	myGLImageViewer->drawARTextureWithOcclusion(texVBO, REAL_RGB_BO, REAL_DEPTH_FROM_DEPTHBUFFER_BO, RAYCAST_BO, VIRTUAL_DEPTH_BO, windowWidth, windowHeight);
	
}

void displayARDataFromOBJFile()
{
	
	//First Viewport: Only the virtual object
	glViewport(windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 
	myGLCloudViewer->configureOBJAmbient(reconstruction->getThreshold());
	myGLCloudViewer->drawOBJ(translationVector, rotationAngles, reconstruction->getCurrentTranslation(), reconstruction->getCurrentRotation(), 
		reconstruction->getInitialTranslation());

	myGLImageViewer->loadRGBTexture(reconstruction->getRGBMap(), texVBO, REAL_RGB_BO, windowWidth, windowHeight);
	myGLImageViewer->loadDepthBufferTexture(texVBO, VIRTUAL_DEPTH_BO, windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);
	myGLImageViewer->loadFrameBufferTexture(texVBO, VIRTUAL_RGB_BO, windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);
	myGLImageViewer->loadDepthBufferTexture(texVBO, REAL_DEPTH_FROM_DEPTHBUFFER_BO, 0, 0, windowWidth/2, windowHeight/2);

	//Third Viewport: Virtual + Real Object
	
	glViewport(windowWidth/2, 0, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity();   
	
	myGLImageViewer->drawARTextureWithOcclusion(texVBO, REAL_RGB_BO, REAL_DEPTH_FROM_DEPTHBUFFER_BO, VIRTUAL_RGB_BO, VIRTUAL_DEPTH_BO, windowWidth, windowHeight);
	
}

void displayARDataFromVolumeRendering()
{

	glDisable(GL_DEPTH_TEST);
	//Second Viewport: Virtual Object
	glViewport(windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity();  
	//myGLImageViewer->drawARVolumetricWithOcclusionPartI(texVBO, 5, 9, 6, windowWidth, windowHeight);
	
	glEnable(GL_TEXTURE_3D);
	myGLCloudViewer->configureARAmbientWithBlending(reconstruction->getThreshold());
	myGLCloudViewer->updateModelViewMatrix(translationVector, rotationAngles, reconstruction->getCurrentTranslation(), 
		reconstruction->getCurrentRotation(), reconstruction->getInitialTranslation(), true, volumetricData->getWidth(), 
		volumetricData->getHeight(), volumetricData->getDepth());
	glScalef(scale, scale, scale);
	myGLImageViewer->setProgram(shaderProg[2]);
	myGLImageViewer->draw3DTexture(texVBO, AR_FROM_VOLUME_RENDERING_BO);
	glPopMatrix();
	
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_DEPTH_TEST);

	//myGLImageViewer->drawARVolumetricWithOcclusionPartII(texVBO, 7);
	
	//Fourth Viewport: Virtual + Real Object
	glViewport(windowWidth/2, 0, windowWidth/2, windowHeight/2);
	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 

	myGLImageViewer->loadRGBTexture(reconstruction->getRGBMap(), texVBO, REAL_RGB_BO, windowWidth, windowHeight);
	myGLImageViewer->loadFrameBufferTexture(texVBO, VIRTUAL_RGB_BO, windowWidth/2, windowHeight/2, windowWidth/2, windowHeight/2);

	myGLImageViewer->setProgram(shaderProg[1]);
	myGLImageViewer->drawARTextureWithOcclusion(texVBO, REAL_RGB_BO, REAL_DEPTH_FROM_DEPTHBUFFER_BO, VIRTUAL_RGB_BO, VIRTUAL_DEPTH_BO, windowWidth, windowHeight);

	//myGLImageViewer->drawRGBTexture(texVBO, 5, windowWidth/2, windowHeight/2);
	
}

void displayCloud(bool globalCoordinates = true)
{
	
	reconstruction->getPointCloud(pointCloud, globalCoordinates);
	reconstruction->getNormalVector(normalVector, globalCoordinates);

	myGLCloudViewer->loadIndices(indices, pointCloud);
	myGLCloudViewer->loadVBOs(meshVBO, indices, pointCloud, normalVector);
	
	glViewport(0, windowHeight/2, windowWidth/2, windowHeight/2);

	glMatrixMode(GL_PROJECTION);          
	glLoadIdentity(); 
	
	myGLCloudViewer->configureAmbient(reconstruction->getThreshold(), pointCloud);
	if(reconstruction->getGlobalTime() > 1)
		myGLCloudViewer->drawMesh(meshVBO, reconstruction->getCurrentTranslation(), reconstruction->getCurrentRotation(), reconstruction->getInitialTranslation(), 
			rotationAngles, shader, globalCoordinates);
	if(workAround == 1) {
		myGLCloudViewer->drawMesh(meshVBO, Eigen::Vector3f::Zero(), Eigen::Matrix3f::Identity(), reconstruction->getInitialTranslation(), rotationAngles, shader, 
			globalCoordinates);
		workAround = 2;
	}
}

void display()
{
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

	if(!AR)
	{
		if(workAround == 1)
			displayCloud();
		if(showDepthMap)
			displayDepthData();
		if(showRGBMap)
			displayRGBData();
		if(showRaycasting && reconstruction->hasImage())
			displayRaycastedData();
		//if(reconstruction->isOnlyTrackingOn())
			//displayARDataFromVolume();
		if(showCloud && reconstruction->hasImage())
			if(ARPolygonal)
				displayCloud(!reconstruction->isOnlyTrackingOn());
			else
				displayCloud();
	} else {
		if(showCloud)
			displayCloud(ARConfiguration);
		if(ARPolygonal)
			displayARDataFromOBJFile();
		if(ARVolumetric) {
			//Occlusion support
			glEnable(GL_DEPTH_TEST);
			displayCloud(true);
			myGLImageViewer->loadDepthBufferTexture(texVBO, VIRTUAL_DEPTH_BO, 0, 0, windowWidth/2, windowHeight/2);
			displayCloud(false);
			myGLImageViewer->loadDepthBufferTexture(texVBO, REAL_DEPTH_FROM_DEPTHBUFFER_BO, 0, windowHeight/2, windowWidth/2, windowHeight/2);
			displayARDataFromVolumeRendering();
		}
		if(ARKinectFusionVolume) {
			displayCloud(true);
			myGLImageViewer->loadDepthBufferTexture(texVBO, VIRTUAL_DEPTH_BO, 0, 0, windowWidth/2, windowHeight/2);
			displayCloud(false);
			myGLImageViewer->loadDepthBufferTexture(texVBO, REAL_DEPTH_FROM_DEPTHBUFFER_BO, 0, windowHeight/2, windowWidth/2, windowHeight/2);
			displayARDataFromVolume();
		}
	}
	
	glutSwapBuffers();
	glutPostRedisplay();

}

int framecount = 0;
void idle()
{
	
	if(kinect->grabFrame()) {
	  framecount++;
		if ((skipframes > 0) && (framecount % skipframes != 0)) {
		  printf("Skipping frame %d\n", framecount);
		}
		else {
		  printf("Processing frame %d\n", framecount);
		  if(hasFaceDetection) {
		  	if(reconstruction->getGlobalTime() == 0)
		  		faceDetected = false;
		  	if(faceDetected) {
		  		reconstruction->stopTracking(false);
		  		faceDetector->segmentFace(kinect->getRGBImage(), kinect->getDepthImage());
		  	} else {
		  		faceDetected = faceDetector->run(kinect->getRGBImage(), kinect->getDepthImage());
		  		reconstruction->stopTracking(!faceDetected);
		  	}
		  }
		  if(!ARConfiguration)
		  	reconstruction->run(kinect->getRGBImage(), kinect->getDepthImage());
		  if(!ARConfiguration && !AR && integrateColors)
		  	coloredReconstructionMediator->updateColorVolume(reconstruction);
		  if(workAround != 2)
		  	workAround = 1;
		}
	}

}

void keyboard(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		exit(0);
		break;
	case (int)'x' : case (int)'X':
		std::cout << "Saving initial transformation..." << std::endl;
    reconstruction->saveInitialTransformation();
		break;
	case (int)'z' : case (int)'Z':
		std::cout << "Changing camera pose..." << std::endl;
    reconstruction->changePose();
		break;
	case (int)'i' : case (int)'I':
		std::cout << "Head Pose Tracking Activated..." << std::endl;
		reconstruction->enableOnlyTracking();
		hasFaceDetection = false;
		break;
	case (int)'p' : case (int)'P':
		std::cout << "Pause..." << std::endl;
		reconstruction->stopTracking(true);
		if(isHeadPoseEstimationEnabled)
			headPoseEstimationMediator->stopTracking(true, reconstruction);
		break;
	case (int)'c' : case (int)'C':
		std::cout << "Continue..." << std::endl;
		reconstruction->stopTracking(false);
		if(isHeadPoseEstimationEnabled)
			headPoseEstimationMediator->stopTracking(false, reconstruction);
		break;
	case (int)'s' : case (int)'S':
		if(AR) 
		{
			setScale(key);
			std::cout << "Scale: " << scale << std::endl;
		}
		else
			saveModel();
		break;
	case (int)'a' : case (int)'A':
		if(!AR) {
			translationVector[0] = -(reconstruction->getInitialTranslation()(0) - reconstruction->getCurrentTranslation()(0));
			translationVector[1] = -(reconstruction->getInitialTranslation()(1) - reconstruction->getCurrentTranslation()(1));
			translationVector[2] = -(reconstruction->getInitialTranslation()(2) - reconstruction->getCurrentTranslation()(2));
			AR = true;
			ARConfiguration = true;
			
			if(ARPolygonal || ARKinectFusionVolume)
				glEnable(GL_DEPTH_TEST);
			
			reconstruction->enableOnlyTracking();
			hasFaceDetection = false;
			std::cout << "Enabling AR" << std::endl;
			std::cout << "AR Enabled: Click the window to position the object (if necessary, use the scale factor (s)" << std::endl;
			myGLCloudViewer->setEyePosition(1, 0, 120);
		} else if(ARConfiguration) {
			ARConfiguration = false;
			std::cout << "AR Enabled: Configuration finished" << std::endl;
			myGLCloudViewer->setEyePosition(1, 0, 170);
		} else if(AR && !ARConfiguration){
			AR = false;
		}
		break;
	case (int)'t' : case (int)'T':
		translationOn = !translationOn;
		std::cout << "Translation On? ";
		if(translationOn)
			std::cout << "Yes" << std::endl;
		else
			std::cout << "No" << std::endl;
		break;
	case (int)'r' : case (int)'R':
		if(AR) {
			rotationOn = !rotationOn;
			std::cout << "Rotation On? ";
			if(rotationOn)
				std::cout << "Yes" << std::endl;
			else
				std::cout << "No" << std::endl;
		} else {
			reconstruction->reset();
		}
		break;
	case (int)'u':
		shader = !shader;
		break;
	case (int)'q':
		w1 += 10;
		myGLCloudViewer->setEyePosition(w1, w2, w3);
		break;
	case (int)'Q':
		w1 -= 10;
		myGLCloudViewer->setEyePosition(w1, w2, w3);
		break;
	case (int)'w':
		w2 += 10;
		myGLCloudViewer->setEyePosition(w1, w2, w3);
		break;
	case (int)'W':
		w2 -= 10;
		myGLCloudViewer->setEyePosition(w1, w2, w3);
		break;
	case (int)'e':
		w3 += 10;
		myGLCloudViewer->setEyePosition(w1, w2, w3);
		break;
	case (int)'E':
		w3 -= 10;
		myGLCloudViewer->setEyePosition(w1, w2, w3);
		break;
	case (int)'b':
		std::cout << w1 << " " << w2 << " " << w3 << std::endl;
		break;
	default:
		break;
	}
	glutPostRedisplay();
}

void specialKeyboard(int key, int x, int y)
{
	switch (key)
	{
	case GLUT_KEY_UP:
		if(translationOn)
			translationVector[1] += vel;
		if(rotationOn)
			rotationAngles[1] += vel;
		break;
	case GLUT_KEY_DOWN:
		if(translationOn)
			translationVector[1] -= vel;
		if(rotationOn)
			rotationAngles[1] -= vel;
		break;
	case GLUT_KEY_LEFT:
		if(translationOn)
			translationVector[0] -= vel;
		if(rotationOn)
			rotationAngles[0] -= vel;
		break;
	case GLUT_KEY_RIGHT:
		if(translationOn)
			translationVector[0] += vel;
		if(rotationOn)
			rotationAngles[0] += vel;
		break;
	case GLUT_KEY_PAGE_UP:
		if(translationOn)
			translationVector[2] += vel;
		if(rotationOn)
			rotationAngles[2] += vel;
		break;
	case GLUT_KEY_PAGE_DOWN:
		if(translationOn)
			translationVector[2] -= vel;
		if(rotationOn)
			rotationAngles[2] -= vel;
		break;
	default:
		break;
	}
	glutPostRedisplay();
}

void mouse(int button, int state, int x, int y) 
{
	if (button == GLUT_LEFT_BUTTON)
		if (state == GLUT_UP)
			if(ARConfiguration)
				positionVirtualObject(x, y);

	glutPostRedisplay();
}

void initGL()
{
	
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0 );
	glShadeModel(GL_SMOOTH);
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1);  

	if(texVBO[0] == 0)
		glGenTextures(10, texVBO);
	if(meshVBO[0] == 0)
		glGenBuffers(4, meshVBO);

	myGLImageViewer = new MyGLImageViewer();
	myGLCloudViewer = new MyGLCloudViewer();
	volumetricData = new VolumetricData();

	myGLCloudViewer->setEyePosition(1, 0, 120);
	if(ARPolygonal)
		myGLCloudViewer->loadARModel(ARObjectFileName);
	if(ARVolumetric && !strcmp(volumetricPathExtension, "tif")) {
		volumetricData->loadTIFData(volumetricPath, firstSlice, lastSlice);
		myGLImageViewer->load3DTexture(volumetricData->getData(), texVBO, AR_FROM_VOLUME_RENDERING_BO, volumetricData->getWidth(), 
			volumetricData->getHeight(), volumetricData->getDepth());
	}
	//AR Configuration
	translationVector[0] = 0;
	translationVector[1] = 0;
	translationVector[2] = 0;
	rotationAngles[0] = 0;
	rotationAngles[1] = 0;
	rotationAngles[2] = 0;

}

void* pcl_viewer_thread(void* param) {
  boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer (new pcl::visualization::PCLVisualizer ("Cloud Viewer"));

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>& cloud = *cloud_ptr;
    
  viewer->setBackgroundColor(0, 0, 0.15);
  viewer->setPointCloudRenderingProperties(visualization::PCL_VISUALIZER_POINT_SIZE, 1);
  viewer->initCameraParameters();
  viewer->setCameraClipDistances(0.01, 10.01);
  viewer->setShowFPS(false);
  pcl::visualization::Camera camera;
  bool gotCamera = false;

  while (!viewer->wasStopped())
  {
    boost::this_thread::sleep (boost::posix_time::microseconds (100000));

    if (reconstruction->poseChanged()) {
      Eigen::Affine3f pose;
      pose = viewer->getViewerPose();
      Matrix3frm rotation = pose.linear();
      Matrix3f axis_reorder;
      // Rotation of 180° on Z 
      axis_reorder << -1,  0, 0,
                       0, -1, 0,
                       0,  0, 1;
      rotation = rotation * axis_reorder;
      reconstruction->setPoseR(rotation);
      reconstruction->setPoseT(pose.translation());
			cout << rotation << endl;
			cout << pose.translation() << endl;
    }
    else {
      viewer->removeAllPointClouds();
      cloud_ptr = reconstruction->getPCLPointCloud();
      viewer->addPointCloud<pcl::PointXYZ> (cloud_ptr);
      if (!gotCamera) {
        viewer->resetCamera();
        std::vector<pcl::visualization::Camera> cameras;
        viewer->getCameras(cameras);
        camera = cameras[0];
        camera.view[1] *= -1;
        gotCamera = true;
      }
      viewer->setCameraParameters(camera);
    }

    viewer->spinOnce(100);
  }
}

void* pcl_viewer_thread_virtual_object(void* param) {
  boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer (new pcl::visualization::PCLVisualizer ("Virtual Object Cloud Viewer"));

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
	if (pcl::io::loadPCDFile<pcl::PointXYZ> ("hat.pcd", *cloud_ptr) == -1) {
	  printf("Could not load virtual object from PCD file.\n");
	  return (-1);
	}
  pcl::PointCloud<pcl::PointXYZ>& cloud = *cloud_ptr;

  viewer->setBackgroundColor(0, 0, 0.15);
  viewer->setPointCloudRenderingProperties(visualization::PCL_VISUALIZER_POINT_SIZE, 1);
  viewer->initCameraParameters();
  viewer->setCameraClipDistances(0.01, 10.01);
  viewer->setShowFPS(false);
  pcl::visualization::Camera camera;
  bool gotCamera = false;

  while (!viewer->wasStopped())
  {
    boost::this_thread::sleep (boost::posix_time::microseconds (100000));

    if (reconstruction->poseChanged()) {
      Eigen::Affine3f pose;
      pose = viewer->getViewerPose();
      Matrix3frm rotation = pose.linear();
      Matrix3f axis_reorder;
      // Rotation of 180° on Z 
      axis_reorder << -1,  0, 0,
                       0, -1, 0,
                       0,  0, 1;
      rotation = rotation * axis_reorder;
      reconstruction->setPoseR(rotation);
      reconstruction->setPoseT(pose.translation());
			cout << rotation << endl;
			cout << pose.translation() << endl;
    }
    else {
      viewer->removeAllPointClouds();
      viewer->addPointCloud<pcl::PointXYZ> (cloud_ptr);
      if (!gotCamera) {
        viewer->resetCamera();
        std::vector<pcl::visualization::Camera> cameras;
        viewer->getCameras(cameras);
        camera = cameras[0];
        camera.view[1] *= -1;
        gotCamera = true;
      }
      viewer->setCameraParameters(camera);
    }

    viewer->spinOnce(100);
  }   
}

int main(int argc, char **argv) {

  if (live) {
    pcl::gpu::setDevice(0);
    pcl::gpu::printShortCudaDeviceInfo(0);
	}
  
  //This argument is an exception. It is loaded first because it is necessary to instantiate the Reconstruction object
  Eigen::Vector3i volumeSize(700, 700, 1400); //mm
  if (pcl::console::parse_3x_arguments(argc, argv, "--volumesize", volumeSize(0), volumeSize(1), volumeSize(2)) >= 0) {
  }
  
  try
  {
	//Initialize the Reconstruction object
	reconstruction = new Reconstruction(volumeSize);
	kinect = new Kinect(live, onifile);
	loadArguments(argc, argv, reconstruction);
  
  // Initialize a thread for cloud visualization, which needs access to the cloud
	// Uncomment here and comment line 239 on src/Reconstruction.cpp in order to enable independent camera instead of pose from file
	/*
  pthread_t thread_id;
  pthread_create(&thread_id, NULL, pcl_viewer_thread, (void *)&reconstruction);
	*/

  // Cloud AR
  // pthread_t thread_id2;
  // pthread_create(&thread_id2, NULL, pcl_viewer_thread_virtual_object, (void *)&reconstruction);

	// Glasses integration

	//Initialize the GL window
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_ALPHA);
	glutInitWindowSize(windowWidth, windowHeight);
	glutInit(&argc, argv);
	glutCreateWindow("KinFu");
  printf("LEVELS: %d\n", LEVELS);

	//Initialize glew
	GLenum err = glewInit();
	if (GLEW_OK != err) {
		std::cout << "Error: " << glewGetErrorString(err) << std::endl;
		exit(0);
	}
	initGL();

	glutReshapeFunc(reshape);
	glutDisplayFunc(display);
	glutIdleFunc(idle);
	glutKeyboardFunc(keyboard);
	glutMouseFunc(mouse);
	glutSpecialFunc(specialKeyboard);

	initShader("Shaders/Phong", 0);
	initShader("Shaders/Occlusion", 1);
	initShader("Shaders/VRBlend", 2);

	myGLCloudViewer->setProgram(shaderProg[0]);
	myGLImageViewer->setProgram(shaderProg[1]);
	
	glutMainLoop();

  } 
  catch (const std::bad_alloc& /*e*/)
  {
    cout << "Bad alloc" << endl;
  }
  catch (const std::exception& /*e*/)
  {
    cout << "Exception" << endl;
  }

  delete kinect;
  delete reconstruction;
  delete myGLImageViewer;
  delete myGLCloudViewer;
  delete volumetricData;
  if(integrateColors)
	  delete coloredReconstructionMediator;
  if(isHeadPoseEstimationEnabled)
	  delete headPoseEstimationMediator;
  if(hasFaceDetection)
	  delete faceDetector;
  return 0;

}
