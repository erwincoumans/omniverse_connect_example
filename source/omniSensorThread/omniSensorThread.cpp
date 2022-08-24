/*###############################################################################
#
# Copyright 2022 NVIDIA Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
###############################################################################*/

/*###############################################################################
#
# The Omniverse Sensor Thread is a command line program that continously pushes updates from an external
# source into an existing USD on the Nucleus Server. This is to demonstrate a simulated sensor sync
# path with a model in USD. This app simulates one sensor connected to a mesh in an existing USD created
# by Omniverse Simple Sensor.
#	* Two arguments, 
#       1. The path to where to place the USD stage 
#		   * Acceptable forms:
#			   * omniverse://localhost/Users/test
#			   * C:\USD
#			* A relative path based on the CWD of the program (helloworld.usda)
#       2. The thread number
#		   * Acceptable forms:
#              * 1
#              * 2, etc.
#	* Initialize Omniverse
#		* Set the Omniverse Client log callback (using a lambda)
#		* Set the Omniverse Client log level
#		* Initialize the Omniverse Client library
#		* Register a connection status callback (using a lambda)
#   * Attach to an existing USD stage
#   * Associate sensors to a mesh in the stage
#	* Set the USD stage URL as live
#	* Start a thread that loops, taking simulated sensor input from a randomnized seed
#		* Update the color characteristic of a portion of the USD stage
#	* Destroy the stage object
#	* Shutdown the Omniverse Client library
#
# eg. omniSensorThread.exe omniverse://localhost/Users/test  4
#
###############################################################################*/

#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <thread>
#include <string>
#include "OmniClient.h"
#include "OmniUsdLive.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include <pxr/base/gf/matrix4f.h>
#include "pxr/base/gf/vec2f.h"
#include "pxr/usd/usdUtils/pipeline.h"
#include "pxr/usd/usdUtils/sparseValueWriter.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/primvar.h"
#include "pxr/usd/usdShade/input.h"
#include "pxr/usd/usdShade/output.h"
#include <pxr/usd/usdGeom/xform.h>
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/modelAPI.h>
#ifdef _WIN32
#include <conio.h>
#endif
#include <mutex>

PXR_NAMESPACE_USING_DIRECTIVE

// Globals for Omniverse Connection and base Stage
static UsdStageRefPtr gStage;

// Global for making the logging reasonable
static std::mutex gLogMutex;

// Multiplatform array size
#define HW_ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))

// Private tokens for building up SdfPaths. We recommend
// constructing SdfPaths via tokens, as there is a performance
// cost to constructing them directly via strings (effectively,
// a table lookup per path element). Similarly, any API which
// takes a token as input should use a predefined token
// rather than one created on the fly from a string.
TF_DEFINE_PRIVATE_TOKENS(
	_tokens,
	(box)
	(DistantLight)
	(DomeLight)
	(Looks)
	(Root)
	(Shader)
	(st)

	// These tokens will be reworked or replaced by the official MDL schema for USD.
	// https://developer.nvidia.com/usd/MDLschema
	(Material)
	((_module, "module"))
	(name)
	(out)
	((shaderId, "mdlMaterial"))
	(mdl)

	// Tokens used for USD Preview Surface
	(diffuseColor)
	(normal)
	(file)
	(result)
	(varname)
	(rgb)
	(RAW)
	(sRGB)
	(surface)
	(PrimST)
	(UsdPreviewSurface)
	((UsdShaderId, "UsdPreviewSurface"))
	((PrimStShaderId, "UsdPrimvarReader_float2"))
	(UsdUVTexture)
);

// Startup Omniverse 
static bool startOmniverse()
{
	// Register a function to be called whenever the library wants to print something to a log
	omniClientSetLogCallback(
		[](char const* threadName, char const* component, OmniClientLogLevel level, char const* message) noexcept
		{
			std::cout << "[" << omniClientGetLogLevelString(level) << "] " << message << std::endl;
		});

	// The default log level is "Info", set it to "Debug" to see all messages
	omniClientSetLogLevel(eOmniClientLogLevel_Warning);

	// Initialize the library and pass it the version constant defined in OmniClient.h
	// This allows the library to verify it was built with a compatible version. It will
	// return false if there is a version mismatch.
	if (!omniClientInitialize(kOmniClientVersion))
	{
		return false;
	}

	omniClientRegisterConnectionStatusCallback(nullptr, 
		[](void* userData, const char* url, OmniClientConnectionStatus status) noexcept
		{
			std::cout << "Connection Status: " << omniClientGetConnectionStatusString(status) << " [" << url << "]" << std::endl;
			if (status == eOmniClientConnectionStatus_ConnectError)
			{
				// We shouldn't just exit here - we should clean up a bit, but we're going to do it anyway
				std::cout << "[ERROR] Failed connection, exiting." << std::endl;
				exit(-1);
			}
		});

	return true;
}

// Create a new connection for this model in Omniverse, returns the created stage URL
static std::string openOmniverseModel(const std::string& destinationPath)
{
	std::string stageUrl = destinationPath;

	// Open the live stage
	std::cout << "    Opening the stage : " << stageUrl.c_str() << std::endl;
	gStage = pxr::UsdStage::Open(stageUrl);
	if (!gStage)
	{
		std::cout << "    Failure to open model in Omniverse: " << stageUrl.c_str() << std::endl;
		return std::string();
	}

	omniUsdLiveSetModeForUrl(stageUrl.c_str(), OmniUsdLiveMode::eOmniUsdLiveModeEnabled);

	std::cout << "       Success in opening the stage" << std::endl;
	return stageUrl;
}

// Find the mesh we will change the color of in the current stage
UsdGeomMesh attachToZoneGeometry(int zone)
{
	std::string path = "/World/box_" + std::to_string(zone);
	std::cout << "    Opening prim at path: " << path << std::endl;
	UsdPrim prim = gStage->GetPrimAtPath(SdfPath(path));
	UsdGeomMesh meshPrim = pxr::UsdGeomMesh(prim);
	if (!meshPrim)
	{
		std::cout << "    Failure opening prim" << std::endl;
	}

	return meshPrim;
}

// This class contains a doWork method that's use as a thread's function
//	and members that allow for synchronization between the the file update
//  callbacks and a main thread that takes keyboard input
// It busy-loops using `omniUsdLiveProcess` to recv updates from the server
//  to the USD stage.  If there are updates it will export the USDA file
//  within a second of receiving them.
class DataStageWriterWorker
{
public:
	DataStageWriterWorker() : stopped(false), variance(1.0f), step(0), runLimit(-1) {};
	void doWork() {
		std::time_t currentTime = std::time(0);
		while (!stopped)
		{
			using namespace std::chrono_literals;
			double randomn = std::rand();

			// Setting a frequency of 300ms as a starting point for updates
			std::this_thread::sleep_for(300ms);

			omniUsdLiveWaitForPendingUpdates();

			// Update the color this zone in the model
			{
				// Make a color change for the cube
				UsdAttribute displayColorAttr = mesh.GetDisplayColorAttr();
				VtVec3fArray valueArray;
				GfVec3f rgbFace(0.463f * variance, 0.725f * variance, 0.0f);
				valueArray.push_back(rgbFace);

				// Use the mutex lock since we are making a change to the same layer from multiple threads
				{
					std::unique_lock<std::mutex> lk(gLogMutex);
					displayColorAttr.Set(valueArray);
					stage->Save();
				}
			}

			// Update the value of the variance - simulates the change in sensor reading
			step++;
			if (step >= 360)
				step = 0;
			variance = cos((double)step);
		}
	}
	std::atomic<bool> stopped;
	pxr::UsdStageRefPtr stage;
	int zone;
	UsdGeomMesh mesh;
	float variance;
	int step;
	int runLimit;
};

// The program expects two arguments, input and output paths to a USD file
int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cout << "Please provide a path where to keep the USD model and thread number." << std::endl;
		std::cout << "   Arguments:" << std::endl;
		std::cout << "       Path to USD model" << std::endl;
		std::cout << "       Number of boxes / processes" << std::endl;
		std::cout << "       Timeout in seconds (-1 for infinity)" << std::endl;
		std::cout << "Example - omniSensorThread.exe omniverse://localhost/Users/test 4 25" << std::endl;

		return -1;
    }

    std::cout << "Omniverse Sensor Thread: " << argv[1] << " " << argv[2] << std::endl;
	
	// Create the final model string URL
	std::string stageUrl(argv[1]);
	std::string baseUrl(argv[1]);

	// Which sensor are we attaching?
	int threadNumber = std::atoi(argv[2]);

	// How long to run for?
	int timeout = -1;
	timeout = std::atoi(argv[3]);

	stageUrl += "/SimpleSensorExample.usd";

	// Initialize Omniverse via the Omni Client Lib
	startOmniverse();

	// Create the model in Omniverse
	std::string newStageUrl = openOmniverseModel(stageUrl);
	if (newStageUrl.length() == 0)
	{
		exit(1);
	}

	// Create the worker thread object
	DataStageWriterWorker *w = new DataStageWriterWorker;
	w->zone = threadNumber;

	// Add zones of data to the model
	std::cout << "    Attach to the zone geometry" << std::endl;
	w->mesh = attachToZoneGeometry(threadNumber);
	w->stage = gStage;
	w->runLimit = timeout;

	// Start Live Edit with Omni Client Library
	omniUsdLiveProcess();

	// Create a running thread
	std::cout << "    Worker thread started" << std::endl;
	std::thread *workerThread = new std::thread(&DataStageWriterWorker::doWork, w);

	std::time_t startTime = std::time(0);
	int elapsedTime = 0;
	while (timeout == -1 || elapsedTime < timeout)
	{
		// Add a slight pause so that the main thread is not operating at a high rate
		// Right now, checks the time every 5 seconds. Increase this if the timeout is very short.
		std::chrono::seconds duration(5);
		std::this_thread::sleep_for(duration);

		std::time_t newTime = std::time(0);
		elapsedTime = (newTime - startTime);
	}

	// Stop the threads
	w->stopped = true;

	// Wait for the threads to go away
	workerThread->join();

	// The stage is a sophisticated object that needs to be destroyed properly.  
	// Since stage is a smart pointer we can just reset it
	gStage.Reset();

	// Shutdown the connection to Omniverse
	omniClientShutdown();
}
