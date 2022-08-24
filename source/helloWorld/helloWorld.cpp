/*###############################################################################
#
# Copyright 2020 NVIDIA Corporation
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
# This "helloworld" sample demonstrates how to:
#  * connect to an Omniverse server
#  * create a USD stage
#  * create a physics scene to define simulation parameters
#  * create a polygonal box and add it to the stage and make it a dynamic rigid
#  * create a cube and add it to the stage and make it a dynamic rigid
#  * create a quad and add it to the stage and make it a collider
#  * upload an MDL material and its textures to an Omniverse server
#  * bind an MDL and USD Preview Surface material to the box
#  * add a light to the stage
#  * move and rotate the box with live updates
#  * disconnect from an Omniverse server
#  * 
#  * optional stuff:
#  *  print verbose Omniverse logs
#  *  open an existing stage and find a mesh to do live edits
#
###############################################################################*/

#include "pch.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#ifdef _WIN32
#include <filesystem>
#include <conio.h>
#else
#include <experimental/filesystem>
#endif
#include <mutex>
#include <memory>
#include <map>
#include <condition_variable>

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
#include <pxr/usd/usdGeom/cube.h>
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/modelAPI.h>

// Physics includes, note that usdPhysics is now part of USD itself, 
// in newer USD versions these includes will be replaced by pxr official usdPhysics schema
#include <usdPhysics/scene.h>
#include <usdPhysics/rigidBodyAPI.h>
#include <usdPhysics/collisionAPI.h>
#include <usdPhysics/meshCollisionAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Globals for Omniverse Connection and base Stage
static UsdStageRefPtr gStage;

// Omniverse logging is noisy, only enable it if verbose mode (-v)
static bool gOmniverseLoggingEnabled = false;

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

static void OmniClientConnectionStatusCallbackImpl(void* userData, const char* url, OmniClientConnectionStatus status) noexcept
{
	// Let's just print this regardless
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Connection Status: " << omniClientGetConnectionStatusString(status) << " [" << url << "]" << std::endl;
	}
	if (status == eOmniClientConnectionStatus_ConnectError)
	{
		// We shouldn't just exit here - we should clean up a bit, but we're going to do it anyway
		std::cout << "[ERROR] Failed connection, exiting." << std::endl;
		exit(-1);
	}
}

static void failNotify(const char *msg, const char *detail = nullptr, ...)
{
	std::unique_lock<std::mutex> lk(gLogMutex);

	fprintf(stderr, "%s\n", msg);
	if (detail != nullptr)
	{
		fprintf(stderr, "%s\n", detail);
	}
}

// Shut down Omniverse connection
static void shutdownOmniverse()
{
	// Calling this prior to shutdown ensures that all pending live updates complete.
	omniUsdLiveWaitForPendingUpdates();

	// The stage is a sophisticated object that needs to be destroyed properly.  
	// Since gStage is a smart pointer we can just reset it
	gStage.Reset();

	//omniClientTick(1000);
	omniClientShutdown();
}

// Omniverse Log callback
static void logCallback(const char* threadName, const char* component, OmniClientLogLevel level, const char* message) noexcept
{
	std::unique_lock<std::mutex> lk(gLogMutex);
	if (gOmniverseLoggingEnabled)
	{
		puts(message);
	}
}

// Startup Omniverse 
static bool startOmniverse(bool doLiveEdit)
{
	// Register a function to be called whenever the library wants to print something to a log
	omniClientSetLogCallback(logCallback);

	// The default log level is "Info", set it to "Debug" to see all messages
	omniClientSetLogLevel(eOmniClientLogLevel_Debug);

	// Initialize the library and pass it the version constant defined in OmniClient.h
	// This allows the library to verify it was built with a compatible version. It will
	// return false if there is a version mismatch.
	if (!omniClientInitialize(kOmniClientVersion))
	{
		return false;
	}

	omniClientRegisterConnectionStatusCallback(nullptr, OmniClientConnectionStatusCallbackImpl);

	// Enable live updates
	omniUsdLiveSetDefaultEnabled(doLiveEdit);
	
	return true;
}

// Create a new connection for this model in Omniverse, returns the created stage URL
static std::string createOmniverseModel(const std::string& destinationPath)
{
	std::string stageUrl = destinationPath + "/helloworld.usd";

	// Delete the old version of this file on Omniverse and wait for the operation to complete
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Waiting for " << stageUrl << " to delete... " << std::endl;
	}
	omniClientWait(omniClientDelete(stageUrl.c_str(), nullptr, nullptr));
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "finished" << std::endl;
	}

	// Create this file in Omniverse cleanly
	gStage = UsdStage::CreateNew(stageUrl);
	if (!gStage)
	{
		failNotify("Failure to create model in Omniverse", stageUrl.c_str());
		return std::string();
	}

	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "New stage created: " << stageUrl << std::endl;
	}

	// Always a good idea to declare your up-ness
	UsdGeomSetStageUpAxis(gStage, UsdGeomTokens->y);

	// For physics its important to set units!
	UsdGeomSetStageMetersPerUnit(gStage, 0.01);

	return stageUrl;
}

// This function will add a commented checkpoint to a file on Nucleus if:
//   Live mode is disabled (live checkpoints are ill-supported)
//   The Nucleus server supports checkpoints
static void checkpointFile(const std::string& stageUrl, const char* comment)
{
	if (omniUsdLiveGetDefaultEnabled())
	{
		return;
	}

	bool bCheckpointsSupported = false;
	omniClientWait(omniClientGetServerInfo(stageUrl.c_str(), &bCheckpointsSupported, 
		[](void* UserData, OmniClientResult Result, OmniClientServerInfo const * Info) noexcept
		{
			if (Result == eOmniClientResult_Ok && Info && UserData)
			{
				bool* bCheckpointsSupported = static_cast<bool*>(UserData);
				*bCheckpointsSupported = Info->checkpointsEnabled;
			}
		}));

	if (bCheckpointsSupported)
	{
		const bool bForceCheckpoint = true;
		omniClientWait(omniClientCreateCheckpoint(stageUrl.c_str(), comment, bForceCheckpoint, nullptr, 
		[](void* userData, OmniClientResult result, char const * checkpointQuery) noexcept
		{}));

		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Adding checkpoint comment <" << comment << "> to stage <" << stageUrl <<">" << std::endl;
	}
}

// Stage URL really only needs to contain the server in the URL.  eg. omniverse://ov-prod
static void printConnectedUsername(const std::string& stageUrl)
{
	// Get the username for the connection
	std::string userName("_none_");
	omniClientWait(omniClientGetServerInfo(stageUrl.c_str(), &userName, [](void* userData, OmniClientResult result, struct OmniClientServerInfo const * info) noexcept
		{
			std::string* userName = static_cast<std::string*>(userData);
			if (userData && userName && info && info->username)
			{
				userName->assign(info->username);
			}
		}));
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Connected username: " << userName << std::endl;
	}
}

static void createPhysicsScene(const SdfPath& rootPrimPath)
{
	std::string sceneName("physicsScene");
	const SdfPath scenePrimPath = rootPrimPath.AppendChild(TfToken(sceneName));

	// Create physics scene, note that we dont have to specify gravity
	// the default value is derived based on the scene up Axis and meters per unit.
	// Hence in this case the gravity would be (0.0, -981.0, 0.0) since we have
	// defined the Y up-axis and we are having a scene in centimeters.
	UsdPhysicsScene::Define(gStage, scenePrimPath);
}

static void enablePhysics(const UsdPrim& prim, bool dynamic)
{
	if (dynamic)
	{
		// Make the cube a physics rigid body dynamic
		UsdPhysicsRigidBodyAPI::Apply(prim);
	}

	// Add collision
	UsdPhysicsCollisionAPI::Apply(prim);

	if (prim.IsA<UsdGeomMesh>())
	{
		UsdPhysicsMeshCollisionAPI meshCollisionAPI = UsdPhysicsMeshCollisionAPI::Apply(prim);
		if (dynamic)
		{
			// set mesh approximation to convexHull for dynamic meshes
			meshCollisionAPI.GetApproximationAttr().Set(UsdPhysicsTokens->convexHull);
		}
		else
		{
			// set mesh approximation to none - triangle mesh as is will be used
			meshCollisionAPI.GetApproximationAttr().Set(UsdPhysicsTokens->none);
		}
	}
}

static void createDynamicCube(const SdfPath& rootPrimPath, double size)
{
	// Create the geometry inside of "Root"
	std::string cubeName("cube");
	const SdfPath cubePrimPath = rootPrimPath.AppendChild(TfToken(cubeName));
	UsdGeomCube cube = UsdGeomCube::Define(gStage, cubePrimPath);

	if (!cube)
		return;

	// Move it up
	cube.AddTranslateOp(UsdGeomXformOp::PrecisionFloat).Set(GfVec3f(65.0f, 300.0f, 65.0f));

	cube.GetSizeAttr().Set(size);

	enablePhysics(cube.GetPrim(), true);

	// Commit the changes to the USD
	gStage->Save();
	omniUsdLiveProcess();
}

// Create a simple quad in USD with normals and add a collider
static void createQuad(const SdfPath& rootPrimPath, double size)
{
	// Create the geometry inside of "Root"
	std::string quadName("quad");
	const SdfPath quadPrimPath = rootPrimPath.AppendChild(TfToken(quadName));
	UsdGeomMesh mesh = UsdGeomMesh::Define(gStage, quadPrimPath);

	if (!mesh)
		return;

	// Set orientation
	mesh.CreateOrientationAttr(VtValue(UsdGeomTokens->rightHanded));

	// Add all of the vertices
	VtArray<GfVec3f> points = { 
		GfVec3f(-size, 0.0, -size),
		GfVec3f(-size, 0.0, size),
		GfVec3f(size, 0.0, size), 
		GfVec3f(size, 0.0, -size), 
	};
	mesh.CreatePointsAttr(VtValue(points));

	// Calculate indices for each triangle
	VtArray<int> vecIndices = { 0, 1, 2, 3 };
	mesh.CreateFaceVertexIndicesAttr(VtValue(vecIndices));

	// Add vertex normals	
	VtArray<GfVec3f> meshNormals = { 
		GfVec3f(0.0, 0.0, 1.0),
		GfVec3f(0.0, 0.0, 1.0),
		GfVec3f(0.0, 0.0, 1.0), 
		GfVec3f(0.0, 0.0, 1.0) 
	};
	mesh.CreateNormalsAttr(VtValue(meshNormals));

	// Add face vertex count
	VtArray<int> faceVertexCounts = { 4 };
	mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));

	// set is as a static triangle mesh
	enablePhysics(mesh.GetPrim(), false);

	// Commit the changes to the USD
	gStage->Save();
	omniUsdLiveProcess();
}

// Create a simple box in USD with normals and UV information
double h = 50.0;
int gBoxVertexIndices[] = { 0, 1, 2, 1, 3, 2, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23 };
double gBoxNormals[][3] = { {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0} };
double gBoxPoints[][3] = { {h, -h, -h}, {-h, -h, -h}, {h, h, -h}, {-h, h, -h}, {h, h, h}, {-h, h, h}, {-h, -h, h}, {h, -h, h}, {h, -h, h}, {-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, h, h}, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h} };
float gBoxUV[][2] = { {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0} };

static UsdGeomMesh createBox(const SdfPath& rootPrimPath, int boxNumber=0)
{
	// Create the geometry inside of "Root"
	std::string boxName("box_");
	boxName.append(std::to_string(boxNumber));
	SdfPath boxPrimPath = rootPrimPath.AppendChild(TfToken(boxName));//_tokens->box);
	UsdGeomMesh mesh = UsdGeomMesh::Define(gStage, boxPrimPath);

	if (!mesh)
		return mesh;

	// Set orientation
	mesh.CreateOrientationAttr(VtValue(UsdGeomTokens->rightHanded));

	// Add all of the vertices
	int num_vertices = HW_ARRAY_COUNT(gBoxPoints);
	VtArray<GfVec3f> points;
	points.resize(num_vertices);
	for (int i = 0; i < num_vertices; i++)
	{
		points[i] = GfVec3f(gBoxPoints[i][0], gBoxPoints[i][1], gBoxPoints[i][2]);
	}
	mesh.CreatePointsAttr(VtValue(points));

	// Calculate indices for each triangle
	int num_indices = HW_ARRAY_COUNT(gBoxVertexIndices); // 2 Triangles per face * 3 Vertices per Triangle * 6 Faces
	VtArray<int> vecIndices;
	vecIndices.resize(num_indices);
	for (int i = 0; i < num_indices; i++)
	{
		vecIndices[i] = gBoxVertexIndices[i];
	}
	mesh.CreateFaceVertexIndicesAttr(VtValue(vecIndices));

	// Add vertex normals
	int num_normals = HW_ARRAY_COUNT(gBoxNormals);
	VtArray<GfVec3f> meshNormals;
	meshNormals.resize(num_vertices);
	for (int i = 0; i < num_vertices; i++)
	{
		meshNormals[i] = GfVec3f((float)gBoxNormals[i][0], (float)gBoxNormals[i][1], (float)gBoxNormals[i][2]);
	}
	mesh.CreateNormalsAttr(VtValue(meshNormals));

	// Add face vertex count
	VtArray<int> faceVertexCounts;
	faceVertexCounts.resize(12); // 2 Triangles per face * 6 faces
	std::fill(faceVertexCounts.begin(), faceVertexCounts.end(), 3);
	mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));

	// Set the color on the mesh
	UsdPrim meshPrim = mesh.GetPrim();
	UsdAttribute displayColorAttr = mesh.CreateDisplayColorAttr();
	{
		VtVec3fArray valueArray;
		GfVec3f rgbFace(0.463f, 0.725f, 0.0f);
		valueArray.push_back(rgbFace);
		displayColorAttr.Set(valueArray);
	}

	// Set the UV (st) values for this mesh
	UsdGeomPrimvar attr2 = mesh.CreatePrimvar(_tokens->st, SdfValueTypeNames->TexCoord2fArray);
	{
		int uv_count = HW_ARRAY_COUNT(gBoxUV);
		VtVec2fArray valueArray;
		valueArray.resize(uv_count);
		for (int i = 0; i < uv_count; ++i)
		{
			valueArray[i].Set(gBoxUV[i]);
		}

		bool status = attr2.Set(valueArray);
	}
	attr2.SetInterpolation(UsdGeomTokens->vertex);

	// Move it up
	mesh.AddTranslateOp(UsdGeomXformOp::PrecisionDouble).Set(GfVec3d(0.0f, 100.0f, 0.0f));
	mesh.AddRotateXYZOp(UsdGeomXformOp::PrecisionDouble).Set(GfVec3d(20.0, 0.0, 20.0));

	// Make the cube a physics rigid body dynamic
	enablePhysics(mesh.GetPrim(), true);

	// Commit the changes to the USD
	gStage->Save();
	omniUsdLiveProcess();

	return mesh;
}

// Opens an existing stage and finds the first UsdGeomMesh
static UsdGeomMesh findGeomMesh(const std::string& existingStage)
{
	// Open this file from Omniverse
	gStage = UsdStage::Open(existingStage);
	if (!gStage)
	{
		failNotify("Failure to open stage in Omniverse:", existingStage.c_str());
		return UsdGeomMesh();
	}

	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Existing stage opened: " << existingStage << std::endl;
	}

	if (UsdGeomTokens->y != UsdGeomGetStageUpAxis(gStage))
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Stage is not Y-up so live xform edits will be incorrect.  Stage is " << UsdGeomGetStageUpAxis(gStage) << "-up" << std::endl;
	}

	// Traverse the stage and return the first UsdGeomMesh we find
	auto range = gStage->Traverse();
	for (const auto& node : range)
	{
		if (node.IsA<UsdGeomMesh>())
		{
			{
				std::unique_lock<std::mutex> lk(gLogMutex);
				std::cout << "Found UsdGeomMesh: " << node.GetName() << std::endl;
			}
			return UsdGeomMesh(node);
		}
	}

	// No UsdGeomMesh found in stage (what kind of stage is this anyway!?)
	std::cout << "ERROR: No UsdGeomMesh found in stage: " << existingStage << std::endl;
	return UsdGeomMesh();
}

// Upload a material and its textures to the Omniverse Server
static void uploadMaterial(const std::string& destinationPath)
{
	std::string uriPath = destinationPath + "/Materials";

	// Delete the old version of this folder on Omniverse and wait for the operation to complete
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Waiting for " << uriPath << " to delete... ";
	}
	omniClientWait(omniClientDelete(uriPath.c_str(), nullptr, nullptr));
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "finished" << std::endl;
	}

	// Upload the material folder (MDL and textures)
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Waiting for the resources/Materials folder to upload to " << uriPath << " ... ";
	}
	omniClientWait(omniClientCopy("resources/Materials", uriPath.c_str(), nullptr, nullptr));
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "finished" << std::endl;
	}
}

// Bind a material to this geometry
static void createMaterial(UsdGeomMesh meshIn)
{

	std::string materialName = "Fieldstone";

	// Create a material instance for this in USD
	TfToken materialNameToken(materialName);
	// Make path for "/Root/Looks/Fieldstone";
	SdfPath matPath = SdfPath::AbsoluteRootPath()
		.AppendChild(_tokens->Root)
		.AppendChild(_tokens->Looks)
		.AppendChild(materialNameToken);
	UsdShadeMaterial newMat = UsdShadeMaterial::Define(gStage, matPath);

	// MDL Shader
	{
		// Create the MDL shader to bind to the material
		SdfAssetPath mdlShaderModule = SdfAssetPath("./Materials/Fieldstone.mdl");
		SdfPath shaderPath = matPath.AppendChild(materialNameToken);
		UsdShadeShader mdlShader = UsdShadeShader::Define(gStage, shaderPath);
		mdlShader.CreateIdAttr(VtValue(_tokens->shaderId));

		// These attributes will be reworked or replaced in the official MDL schema for USD.
		// https://developer.nvidia.com/usd/MDLschema
		mdlShader.SetSourceAsset(mdlShaderModule, _tokens->mdl);
		mdlShader.GetPrim().CreateAttribute(TfToken("info:mdl:sourceAsset:subIdentifier"), SdfValueTypeNames->Token, false, SdfVariabilityUniform).Set(materialNameToken);

		// Set the linkage between material and MDL shader
		UsdShadeOutput mdlOutput = newMat.CreateSurfaceOutput(_tokens->mdl);
		mdlOutput.ConnectToSource(mdlShader, _tokens->out);
	}

	
	// USD Preview Surface Shaders
	{
		// Create the "USD Primvar reader for float2" shader
		SdfPath shaderPath = matPath.AppendChild(_tokens->PrimST);
		UsdShadeShader primStShader = UsdShadeShader::Define(gStage, shaderPath);
		primStShader.CreateIdAttr(VtValue(_tokens->PrimStShaderId));
		primStShader.CreateOutput(_tokens->result, SdfValueTypeNames->Float2);
		primStShader.CreateInput(_tokens->varname, SdfValueTypeNames->Token).Set(_tokens->st);

		// Create the "Diffuse Color Tex" shader
		std::string diffuseColorShaderName = materialName + "DiffuseColorTex";
		std::string diffuseFilePath = "./Materials/Fieldstone/Fieldstone_BaseColor.png";
		shaderPath = matPath.AppendChild(TfToken(diffuseColorShaderName));
		UsdShadeShader diffuseColorShader = UsdShadeShader::Define(gStage, shaderPath);
		diffuseColorShader.CreateIdAttr(VtValue(_tokens->UsdUVTexture));
		UsdShadeInput texInput = diffuseColorShader.CreateInput(_tokens->file, SdfValueTypeNames->Asset);
		texInput.Set(SdfAssetPath(diffuseFilePath));
		texInput.GetAttr().SetColorSpace(_tokens->sRGB);
		diffuseColorShader.CreateInput(_tokens->st, SdfValueTypeNames->Float2).ConnectToSource(primStShader.CreateOutput(_tokens->result, SdfValueTypeNames->Float2));
		UsdShadeOutput diffuseColorShaderOutput = diffuseColorShader.CreateOutput(_tokens->rgb, SdfValueTypeNames->Float3);

		// Create the "Normal Tex" shader
		std::string normalShaderName = materialName + "NormalTex";
		std::string normalFilePath = "./Materials/Fieldstone/Fieldstone_N.png";
		shaderPath = matPath.AppendChild(TfToken(normalShaderName));
		UsdShadeShader normalShader = UsdShadeShader::Define(gStage, shaderPath);
		normalShader.CreateIdAttr(VtValue(_tokens->UsdUVTexture));
		UsdShadeInput normalTexInput = normalShader.CreateInput(_tokens->file, SdfValueTypeNames->Asset);
		normalTexInput.Set(SdfAssetPath(normalFilePath));
		normalTexInput.GetAttr().SetColorSpace(_tokens->RAW);
		normalShader.CreateInput(_tokens->st, SdfValueTypeNames->Float2).ConnectToSource(primStShader.CreateOutput(_tokens->result, SdfValueTypeNames->Float2));
		UsdShadeOutput normalShaderOutput = normalShader.CreateOutput(_tokens->rgb, SdfValueTypeNames->Float3);

		// Create the USD Preview Surface shader
		std::string usdPreviewSurfaceShaderName = materialName + "PreviewSurface";
		shaderPath = matPath.AppendChild(TfToken(usdPreviewSurfaceShaderName));
		UsdShadeShader usdPreviewSurfaceShader = UsdShadeShader::Define(gStage, shaderPath);
		usdPreviewSurfaceShader.CreateIdAttr(VtValue(_tokens->UsdPreviewSurface));
		UsdShadeInput diffuseColorInput = usdPreviewSurfaceShader.CreateInput(_tokens->diffuseColor, SdfValueTypeNames->Color3f);
		diffuseColorInput.ConnectToSource(diffuseColorShaderOutput);
		UsdShadeInput normalInput = usdPreviewSurfaceShader.CreateInput(_tokens->normal, SdfValueTypeNames->Normal3f);
		normalInput.ConnectToSource(normalShaderOutput);

		// Set the linkage between material and USD Preview surface shader
		UsdShadeOutput usdPreviewSurfaceOutput = newMat.CreateSurfaceOutput();
		usdPreviewSurfaceOutput.ConnectToSource(usdPreviewSurfaceShader, _tokens->surface);
	}

	// Final step, associate the material with the face
	UsdShadeMaterialBindingAPI usdMaterialBinding(meshIn);
	usdMaterialBinding.Bind(newMat);

	// Commit the changes to the USD
	gStage->Save();
	omniUsdLiveProcess();
}

// Create a light source in the scene.
static void createDistantLight()
{
	// Construct /Root/Light path
	SdfPath lightPath = SdfPath::AbsoluteRootPath()
		.AppendChild(_tokens->Root)
		.AppendChild(_tokens->DistantLight);
	UsdLuxDistantLight newLight = UsdLuxDistantLight::Define(gStage, lightPath);

	// Set the attributes
	newLight.CreateAngleAttr(VtValue(0.53f));
	GfVec3f color(1.0f, 1.0f, 0.745f);
	newLight.CreateColorAttr(VtValue(color));
	newLight.CreateIntensityAttr(VtValue(5000.0f));

	// Commit the changes to the USD
	gStage->Save();
	omniUsdLiveProcess();
}

// Create a light source in the scene.
static void createDomeLight(const std::string& texturePath)
{
	// Construct /Root/Light path
	SdfPath lightPath = SdfPath::AbsoluteRootPath()
		.AppendChild(_tokens->Root)
		.AppendChild(_tokens->DomeLight);
	UsdLuxDomeLight newLight = UsdLuxDomeLight::Define(gStage, lightPath);

	// Set the attributes
	newLight.CreateIntensityAttr(VtValue(1000.0f));
	newLight.CreateTextureFileAttr(VtValue(SdfAssetPath(texturePath)));
	newLight.CreateTextureFormatAttr(VtValue(UsdLuxTokens->latlong));

	// Set rotation on domelight
	UsdGeomXformable xForm = newLight;
	UsdGeomXformOp rotateOp;
	GfVec3d rotXYZ(270, 0, 0);
	rotateOp = xForm.AddXformOp(UsdGeomXformOp::TypeRotateXYZ, UsdGeomXformOp::Precision::PrecisionDouble);
	rotateOp.Set(rotXYZ);

	// Commit the changes to the USD
	gStage->Save();
	omniUsdLiveProcess();
}

// Create an empty folder, just as an example.
static void createEmptyFolder(const std::string& emptyFolderPath)
{
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Waiting to create a new folder: " << emptyFolderPath << " ... ";
	}

	OmniClientResult localResult;
	localResult = Count_eOmniClientResult;

	omniClientWait(omniClientCreateFolder(emptyFolderPath.c_str(), &localResult, [](void* userData, OmniClientResult result) noexcept
		{
			auto returnResult = static_cast<OmniClientResult*>(userData);
			*returnResult = result;
		}));

	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "finished [" << omniClientGetResultString(localResult) << "]" << std::endl;
	}
}

// Perform a live edit on the box
static void liveEdit(UsdGeomMesh meshIn)
{
	double angle = 0;

	// Process any updates that may have happened to the stage from another client
	omniUsdLiveWaitForPendingUpdates();
	{
		std::unique_lock<std::mutex> lk(gLogMutex);
		std::cout << "Begin Live Edit on " << meshIn.GetPath() << " - ";
		std::cout << "Press 't' to move the box\nPress 'q' or escape to quit\n";
	}

	bool wait = true;
	while (wait)
	{
#ifdef _WIN32
		char nextCommand = _getch();
#else
		char nextCommand = getchar();
#endif
		// Process any updates that may have happened to the stage from another client
		omniUsdLiveWaitForPendingUpdates();

		switch (nextCommand) {
		case 't':
		{
			// Increase the angle
			angle += 15;
			if (angle >= 360)
				angle = 0;

			double radians = angle * 3.1415926 / 180.0;
			double x = sin(radians) * 100;
			double y = cos(radians) * 100;
			// Get the transform on the mesh
			UsdGeomXformable xForm = meshIn;

			// Define storage for the different xform ops that Omniverse Kit likes to use
			UsdGeomXformOp translateOp;
			UsdGeomXformOp rotateOp;
			UsdGeomXformOp scaleOp;
			GfVec3d position(0);
			GfVec3d rotXYZ(0);
			GfVec3d scale(1);

			// Get the xform ops stack
			bool resetXformStack = false;
			std::vector<UsdGeomXformOp> xFormOps = xForm.GetOrderedXformOps(&resetXformStack);

			// Get the current xform op values
			for (size_t i = 0; i < xFormOps.size(); i++)
			{
				switch (xFormOps[i].GetOpType()) {
				case UsdGeomXformOp::TypeTranslate:
					translateOp = xFormOps[i];
					translateOp.Get(&position);
					break;
				case UsdGeomXformOp::TypeRotateXYZ:
					rotateOp = xFormOps[i];
					rotateOp.Get(&rotXYZ);
					break;
				case UsdGeomXformOp::TypeScale:
					scaleOp = xFormOps[i];
					scaleOp.Get(&scale);
					break;
				}
			}

			// Move/Rotate the existing position/rotation - this works for Y-up stages
			position += GfVec3d(x, 0, y);
			rotXYZ = GfVec3d(rotXYZ[0], angle, rotXYZ[2]);

			// A utility class to set the position, rotation, or scale values
			class SetOp
			{
			public:
				SetOp(UsdGeomXformable& xForm, UsdGeomXformOp& op, UsdGeomXformOp::Type opType, const GfVec3d& value, const UsdGeomXformOp::Precision precision)
				{
					if (!op)
					{
						op = xForm.AddXformOp(opType, precision);
						std::unique_lock<std::mutex> lk(gLogMutex);
						std::cout << " Adding " << UsdGeomXformOp::GetOpTypeToken(opType) << std::endl;
					}

					if (op.GetPrecision() == UsdGeomXformOp::Precision::PrecisionFloat)
						op.Set(GfVec3d(value));
					else
						op.Set(value);

					std::unique_lock<std::mutex> lk(gLogMutex);
					std::cout << " Setting " << UsdGeomXformOp::GetOpTypeToken(opType) << std::endl;
				}
			};

			SetOp(xForm, translateOp, UsdGeomXformOp::TypeTranslate, position, UsdGeomXformOp::Precision::PrecisionDouble);
			SetOp(xForm, rotateOp, UsdGeomXformOp::TypeRotateXYZ, rotXYZ, UsdGeomXformOp::Precision::PrecisionDouble);
			SetOp(xForm, scaleOp, UsdGeomXformOp::TypeScale, scale, UsdGeomXformOp::Precision::PrecisionDouble);

			// Make sure the xform op order is correct (translate, rotate, scale)
			std::vector<UsdGeomXformOp> xFormOpsReordered;
			xFormOpsReordered.push_back(translateOp);
			xFormOpsReordered.push_back(rotateOp);
			xFormOpsReordered.push_back(scaleOp);
			xForm.SetXformOpOrder(xFormOpsReordered);

			// Commit the change to USD
			gStage->Save();
			break;
		}

		//escape or 'q'
		case 27:
		case 'q':
			wait = false;
			std::cout << "Live Edit complete\n";
			break;
		}
	}
}

// Returns true if the provided maybeURL contains a host and path
static bool isValidOmniURL(const std::string& maybeURL)
{
	bool isValidURL = false;
	OmniClientUrl* url = omniClientBreakUrl(maybeURL.c_str());
	if (url->host && url->path && 
		(std::string(url->scheme) == std::string("omniverse") ||
		 std::string(url->scheme) == std::string("omni")))
	{
		isValidURL = true;
	}
	omniClientFreeUrl(url);
	return isValidURL;
}

// Print the command line arguments help
static void printCmdLineArgHelp()
{
	std::cout << "Usage: samples [options]" << std::endl;
	std::cout << "  options:" << std::endl;
	std::cout << "    -h, --help                    Print this help" << std::endl;
	std::cout << "    -l, --live                    Allow the user to continue modifying the stage live after creating (with the 't' key)" << std::endl;
	std::cout << "    -p, --path dest_stage_folder  Alternate destination stage path folder [default: omniverse://localhost/Users/test]" << std::endl;
	std::cout << "    -e, --existing path_to_stage  Open an existing stage and perform live transform edits (full omniverse URL)" << std::endl;
	std::cout << "    -v, --verbose                 Show the verbose Omniverse logging" << std::endl;
	std::cout << "\n\nExamples:\n";
	std::cout << " * create a stage on the ov-prod server at /Projects/HelloWorld/helloworld.usd" << std::endl;
	std::cout << "    > samples -p omniverse://ov-prod/Projects/HelloWorld" << std::endl;
	std::cout << "\n * live edit a stage on the ov-prod server at /Projects/LiveEdit/livestage.usd" << std::endl;
	std::cout << "    > samples -e omniverse://ov-prod/Projects/LiveEdit/livestage.usd" << std::endl;
}


// Main Application 
int main(int argc, char*argv[])
{
	bool doLiveEdit = false;
	std::string existingStage;
	std::string destinationPath = "omniverse://localhost/Users/test";
	UsdGeomMesh boxMesh;

	// Process the arguments, if any
	for (int x = 1; x < argc; x++)
	{
		if (strcmp(argv[x], "-h") == 0 || strcmp(argv[x], "--help") == 0)
		{
			printCmdLineArgHelp();
			return 0;
		}
		else if (strcmp(argv[x], "-l") == 0 || strcmp(argv[x], "--live") == 0)
		{
			doLiveEdit = true;
		}
		else if ((strcmp(argv[x], "-p") == 0 || strcmp(argv[x], "--path") == 0) && argc > x)
		{
			if (x == argc-1)
			{
				std::cout << "ERROR: Missing an Omniverse folder URL to create the stage.\n" << std::endl;
				printCmdLineArgHelp();
				return -1;
			}
			destinationPath = std::string(argv[++x]);
			if (!isValidOmniURL(destinationPath))
			{
				std::cout << "This is not an Omniverse Nucleus URL: " << destinationPath << std::endl;
				std::cout << "Correct Omniverse URL format is: omniverse://server_name/Path/To/Example/Folder" << std::endl;
				std::cout << "Allowing program to continue because file paths may be provided in the form: C:/Path/To/Stage" << std::endl;
			}
		}
		else if (strcmp(argv[x], "-v") == 0 || strcmp(argv[x], "--verbose") == 0)
		{
			gOmniverseLoggingEnabled = true;
		}
		else if (strcmp(argv[x], "-e") == 0 || strcmp(argv[x], "--existing") == 0)
		{
			doLiveEdit = true;
			if (x == argc-1)
			{
				std::cout << "ERROR: Missing an Omniverse URL to the stage to edit.\n" << std::endl;
				printCmdLineArgHelp();
				return -1;
			}
			existingStage = std::string(argv[++x]);
			if (!isValidOmniURL(existingStage))
			{
				std::cout << "This is not an Omniverse Nucleus URL: " << destinationPath << std::endl;
				std::cout << "Correct Omniverse URL format is: omniverse://server_name/Path/To/Example/Folder" << std::endl;
				std::cout << "Allowing program to continue because file paths may be provided in the form: C:/Path/To/Stage" << std::endl;
			}
		}
		else
		{
			std::cout << "Unrecognized option: " << argv[x] << std::endl;
		}
	}

	// Startup Omniverse with the default login
	if (!startOmniverse(doLiveEdit))
		exit(1);

	if (existingStage.empty())
	{
		// Create the USD model in Omniverse
		const std::string stageUrl = createOmniverseModel(destinationPath);

		// Print the username for the server
		printConnectedUsername(stageUrl);

		// Keep the model contained inside of "Root", only need to do this once per model
		const SdfPath rootPrimPath = SdfPath::AbsoluteRootPath().AppendChild(_tokens->Root);
		UsdGeomXform rootPrim = UsdGeomXform::Define(gStage, rootPrimPath);

		// Define the defaultPrim as the /Root prim
		gStage->SetDefaultPrim(rootPrim.GetPrim());

		// Create physics scene
		createPhysicsScene(rootPrimPath);

		// Create box geometry in the model
		boxMesh = createBox(rootPrimPath);

		// Create dynamic cube
		createDynamicCube(rootPrimPath, 100.0);

		// Create quad - static tri mesh collision so that the box collides with it
		createQuad(rootPrimPath, 500.0);

		checkpointFile(stageUrl, "Add box and nothing else");

		// Create lights in the scene
		createDistantLight();
		createDomeLight("./Materials/kloofendal_48d_partly_cloudy.hdr");

		// Add a Nucleus Checkpoint to the stage
		checkpointFile(stageUrl, "Add lights to stage");

		// Upload a material and textures to the Omniverse server
		uploadMaterial(destinationPath);

		// Add a material to the box
		createMaterial(boxMesh);

		// Add a Nucleus Checkpoint to the stage
		checkpointFile(stageUrl, "Add material to the box");

		// Create an empty folder, just as an example
		createEmptyFolder(destinationPath + "/EmptyFolder");
	}
	else
	{
		// Find a UsdGeomMesh in the existing stage
		boxMesh = findGeomMesh(existingStage);
	}

	// Do a live edit session moving the box around, changing a material
	if (doLiveEdit && boxMesh)
		liveEdit(boxMesh);

	// All done, shut down our connection to Omniverse
	shutdownOmniverse();

	return 0;
}
