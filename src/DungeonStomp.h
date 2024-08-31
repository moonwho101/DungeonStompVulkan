#include "../Common/VulkApp.h"
#include "../Common/VulkUtil.h"
#include "../Common/VulkanEx.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/MathHelper.h"
#include "../Common/Colors.h"
#include "../Common/TextureLoader.h"
#include "../Common/Camera.h"
#include "Waves.h"
#include <memory>
#include <fstream>
#include <iostream>
#include <sstream>
#include <future>
#include "FrameResource.h"
#include "ShadowMap.h"
#include "world.hpp"
#include "Missle.hpp"

const int gNumFrameResources = 3;

struct RenderItem {
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;
	//World matrix of the shape that descripes the object's local space
	//relative to the world space,
	glm::mat4 World = glm::mat4(1.0f);

	glm::mat4 TexTransform = glm::mat4(1.0f);

	// Used for GPU waves render items.
	glm::vec2 DisplacementMapTexelSize = { 1.0f, 1.0f };
	float GridSpatialStep = 1.0f;

	int NumFramesDirty = gNumFrameResources;

	uint32_t ObjCBIndex = -1;

	Material* Mat{ nullptr };
	MeshGeometry* Geo{ nullptr };


	uint32_t IndexCount{ 0 };
	uint32_t StartIndexLocation{ 0 };
	uint32_t BaseVertexLocation{ 0 };
	uint32_t TextureIndex{ 0 };
	uint32_t TextureNormalIndex{ 0 };
};

enum class RenderLayer : int
{
	Opaque = 0,
	Debug,
	Sky,
	Count
};

enum class RenderDungeon : int
{
	NormalMap = 0,
	Flat,
	Sky,
	Shadow,
	Quad,
	Torch
};

enum class ProgState : int {
	Init = 0,
	Draw,
	Exit
};

class DungeonStompApp : public VulkApp
{
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	std::unique_ptr<DescriptorSetLayoutCache> descriptorSetLayoutCache;
	std::unique_ptr<DescriptorSetPoolCache> descriptorSetPoolCache;
	std::unique_ptr<VulkanUniformBuffer> uniformBuffer;
	std::unique_ptr<VulkanImageList> textures;
	std::unique_ptr<VulkanImage> cubeMapTexture;
	std::unique_ptr<VulkanUniformBuffer> storageBuffer;
	std::unique_ptr<VulkanDescriptorList> uniformDescriptors;
	std::unique_ptr<VulkanDescriptorList> textureDescriptors;
	std::unique_ptr<VulkanDescriptorList> storageDescriptors;
	std::unique_ptr<VulkanDescriptorList> shadowDescriptors;
	std::unique_ptr<VulkanSampler> sampler;
	std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
	std::unique_ptr<VulkanPipelineLayout> cubeMapPipelineLayout;
	std::unique_ptr<VulkanPipelineLayout> shadowPipelineLayout;
	std::unique_ptr<VulkanPipelineLayout> debugPipelineLayout;
	std::unique_ptr<VulkanPipeline> opaquePipeline;
	std::unique_ptr<VulkanPipeline> torchPipeline;
	std::unique_ptr<VulkanPipeline> wireframePipeline;
	std::unique_ptr<VulkanPipeline> opaqueFlatPipeline;
	std::unique_ptr<VulkanPipeline> cubeMapPipeline;
	std::unique_ptr<VulkanPipeline> shadowPipeline;
	std::unique_ptr<VulkanPipeline> debugPipeline;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map < std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture> > mTextures;
	std::unordered_map<std::string, VkPipeline> mPSOs;

	RenderItem* mWavesRitem{ nullptr };

	bool mIsWireframe{ false };
	bool mIsFlatShader{ false };

	ProgState state{ ProgState::Init };
	std::unique_ptr<std::future<void>> futureRet;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

	Vulkan::Buffer WavesIndexBuffer;
	std::vector<Vulkan::Buffer> WaveVertexBuffers;
	std::vector<void*> WaveVertexPtrs;

	PassConstants mMainPassCB;  // index 0 of pass cbuffer.
	PassConstants mShadowPassCB;// index 1 of pass cbuffer.

	Light LightContainer[MaxLights];
	Camera mCamera;

	std::unique_ptr<ShadowMap> mShadowMap;

	Sphere mSceneBounds;

	std::unique_ptr<RenderItem> dungeonRitem = std::make_unique<RenderItem>();

	float mLightNearZ = 0.0f;
	float mLightFarZ = 0.0f;
	glm::vec3 mLightPosW;
	glm::mat4 mLightView = MathHelper::Identity4x4();
	glm::mat4 mLightProj = MathHelper::Identity4x4();
	glm::mat4 mShadowTransform = MathHelper::Identity4x4();

	float mLightRotationAngle = 0.0f;
	glm::vec3 mBaseLightDirections[3] = {
		glm::vec3(0.57735f, -0.57735f, 0.57735f),
		glm::vec3(-0.57735f, -0.57735f, 0.57735f),
		glm::vec3(0.0f, -0.707f, -0.707f)
	};
	glm::vec3 mRotatedLightDirections[3];

	// Depth bias (and slope) are used to avoid shadowing artifacts
	// Constant depth bias factor (always applied)
	float depthBiasConstant = 1.25f;
	// Slope depth bias factor, applied depending on polygon's slope
	float depthBiasSlope = 1.75f;

	POINT mLastMousePos;

	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;
	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);

	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateMaterialsBuffer(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	static void asyncInitStatic(DungeonStompApp* pThis);
	void asyncInit();

	void LoadTextures();
	void BuildWavesGeometry();
	void BuildBuffers();
	void BuildDescriptors();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void DrawRenderItems(VkCommandBuffer, VkPipelineLayout layout, const std::vector<RenderItem*>& ritems, RenderDungeon item = RenderDungeon::NormalMap);

	BOOL LoadRRTextures11(const char* filename);
	void SetTextureNormalMap();
	void SetTextureNormalMapEmpty();
	void ProcessLights11();
	void  ToggleFullscreen(bool isFullscreen);

public:

	DungeonStompApp(HINSTANCE hInstance);
	DungeonStompApp(const DungeonStompApp& rhs) = delete;
	DungeonStompApp& operator=(const DungeonStompApp& rhs) = delete;
	~DungeonStompApp();

	virtual bool Initialize()override;
};
#pragma once
