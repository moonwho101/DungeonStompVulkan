#include "DungeonStomp.h"

void ShutDownSound();
CameraBob bobY;
CameraBob bobX;

DungeonStompApp::DungeonStompApp(HINSTANCE hInstance) :VulkApp(hInstance) {
	mAllowWireframe = true;
	mClearValues[0].color = Colors::LightSteelBlue;
	mMSAA = false;
	mDepthBuffer = true;

	// Estimate the scene bounding sphere manually since we know how the scene was constructed.
	// The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
	// the world space origin.  In general, you need to loop over every world space vertex
	// position and compute the bounding sphere.
	mSceneBounds.Center = glm::vec3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);

	float scale = 1415.0f;
	mSceneBounds.Radius = sqrtf((10.0f * 10.0f) * scale + (15.0f * 15.0f) * scale);
}
DungeonStompApp::~DungeonStompApp() {
	vkDeviceWaitIdle(mDevice);

	for (auto& buffer : WaveVertexBuffers) {
		Vulkan::unmapBuffer(mDevice, buffer);
		Vulkan::cleanupBuffer(mDevice, buffer);
	}

	for (auto& pair : mGeometries) {
		free(pair.second->indexBufferCPU);
		if (pair.second->vertexBufferGPU.buffer != mWavesRitem->Geo->vertexBufferGPU.buffer) {
			free(pair.second->vertexBufferCPU);
			cleanupBuffer(mDevice, pair.second->vertexBufferGPU);
		}
		cleanupBuffer(mDevice, pair.second->indexBufferGPU);
	}

	ShutDownSound();
}

int SoundInit();
HRESULT CreateDInput(HWND hWnd);
extern void InitDS();

bool DungeonStompApp::Initialize() {
	if (!VulkApp::Initialize())
		return false;

	//Startup Dungeon Stomp and load textures.
	srand((unsigned int)time(NULL));
	SoundInit();
	CreateDInput(mhMainWnd);
	LoadRRTextures11("textures.dat");
	InitDS();

	//Set headbob
	bobX.SinWave(4.0f, 2.0f, 2.0f);
	bobY.SinWave(4.0f, 2.0f, 4.0f);

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);
#define __USE__THREAD__
#ifdef __USE__THREAD__
	futureRet = std::make_unique<std::future<void>>(std::async(std::launch::async, &DungeonStompApp::asyncInit, this));
#else
	asyncInit();
#endif
	return true;
}

void DungeonStompApp::asyncInitStatic(DungeonStompApp* pThis) {
	pThis->asyncInit();
}

void DungeonStompApp::asyncInit() {

	mShadowMap = std::make_unique<ShadowMap>(mDevice, mMemoryProperties, mBackQueue, mCommandBuffer, 2048, 2048);

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	LoadTextures();
	BuildShapeGeometry();
	BuildSkullGeometry();
	BuildWavesGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildBuffers();
	BuildDescriptors();
	BuildPSOs();
	BuildFrameResources();

	state = ProgState::Draw;
}

extern int number_of_tex_aliases;

void DungeonStompApp::LoadTextures() {

	auto grassTex = std::make_unique<Texture>();

	auto imageLoader = ImageLoader::begin(mDevice, mCommandBuffer, mBackQueue, mMemoryProperties);

	std::vector<Vulkan::Image> texturesList;
	imageLoader.begin(mDevice, mCommandBuffer, mBackQueue, mMemoryProperties)
		.addImage("../Textures/bricks2.png")
		.addImage("../Textures/bricks2_nmap.png")
		.addImage("../Textures/tile.png")
		.addImage("../Textures/tile_nmap.png")
		.addImage("../Textures/white1x1.png")
		.addImage("../Textures/default_nmap.png"); //.addImage("../Textures/WoodCrate01.png");


	for (int i = 0; i < number_of_tex_aliases; i++) {
		imageLoader.addImage(TexMap[i].texpath);
	}

	imageLoader.load(texturesList);

	textures = std::make_unique<VulkanImageList>(mDevice, texturesList);
	ImageLoader::begin(mDevice, mCommandBuffer, mBackQueue, mMemoryProperties)
		.addImage("../Textures/desertcube1024-posx.png")
		.addImage("../Textures/desertcube1024-negx.png")
		.addImage("../Textures/desertcube1024-posy.png")
		.addImage("../Textures/desertcube1024-negy.png")
		.addImage("../Textures/desertcube1024-posz.png")
		.addImage("../Textures/desertcube1024-negz.png")
		.setIsCube(true)
		.load(texturesList);
	cubeMapTexture = std::make_unique <VulkanImage>(mDevice, texturesList[0]);

	Vulkan::SamplerProperties sampProps;
	sampProps.addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler = std::make_unique<VulkanSampler>(mDevice, Vulkan::initSampler(mDevice, sampProps));
}

void DungeonStompApp::BuildBuffers() {
	Vulkan::Buffer dynamicBuffer;
	//pass and object constants are dynamic uniform buffers
	std::vector<UniformBufferInfo> bufferInfo{};
	UniformBufferBuilder::begin(mDevice, mDeviceProperties, mMemoryProperties, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, true)
		.AddBuffer(sizeof(PassConstants), 2, gNumFrameResources)//one for main, one for shadow
		.AddBuffer(sizeof(ObjectConstants), mAllRitems.size(), gNumFrameResources)
		.build(dynamicBuffer, bufferInfo);
	uniformBuffer = std::make_unique<VulkanUniformBuffer>(mDevice, dynamicBuffer, bufferInfo);

	UniformBufferBuilder::begin(mDevice, mDeviceProperties, mMemoryProperties, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, true)
		.AddBuffer(sizeof(MaterialData), mMaterials.size(), gNumFrameResources)
		.build(dynamicBuffer, bufferInfo);
	storageBuffer = std::make_unique<VulkanUniformBuffer>(mDevice, dynamicBuffer, bufferInfo);
}

void DungeonStompApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		quad.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TangentU = box.Vertices[i].TangentU;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;


	}
	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TexC = quad.Vertices[i].TexC;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}

	std::vector<std::uint32_t> indices;
	indices.insert(indices.end(), std::begin(box.Indices32), std::end(box.Indices32));
	indices.insert(indices.end(), std::begin(grid.Indices32), std::end(grid.Indices32));
	indices.insert(indices.end(), std::begin(sphere.Indices32), std::end(sphere.Indices32));
	indices.insert(indices.end(), std::begin(cylinder.Indices32), std::end(cylinder.Indices32));
	indices.insert(indices.end(), std::begin(quad.Indices32), std::end(quad.Indices32));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	geo->vertexBufferCPU = malloc(vbByteSize);
	memcpy(geo->vertexBufferCPU, vertices.data(), vbByteSize);

	geo->indexBufferCPU = malloc(ibByteSize);
	memcpy(geo->indexBufferCPU, indices.data(), ibByteSize);

	std::vector<uint32_t> vertexLocations;
	VertexBufferBuilder::begin(mDevice, mBackQueue, mCommandBuffer, mMemoryProperties)
		.AddVertices(vbByteSize, (float*)vertices.data())
		.build(geo->vertexBufferGPU, vertexLocations);

	std::vector<uint32_t> indexLocations;
	IndexBufferBuilder::begin(mDevice, mBackQueue, mCommandBuffer, mMemoryProperties)
		.AddIndices(ibByteSize, indices.data())
		.build(geo->indexBufferGPU, indexLocations);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;

	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void DungeonStompApp::BuildSkullGeometry() {
	std::ifstream fin("../Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"../Models/skull.txt not found.", 0, 0);
		return;
	}

	uint32_t vcount = 0;
	uint32_t tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	glm::vec3 vMin(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	glm::vec3 vMax(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

		glm::vec3 p = vertices[i].Pos;
		glm::vec3 spherePos = glm::normalize(p);

		float theta = atan2f(spherePos.z, spherePos.x);

		//Put in [0, 2pi].
		if (theta < 0.0f)
			theta += MathHelper::Pi;

		float phi = acosf(spherePos.y);

		float u = theta / (2.0f * MathHelper::Pi);
		float v = phi / MathHelper::Pi;

		vertices[i].TexC = { u,v };

		// Generate a tangent vector so normal mapping works.  We aren't applying
		// a texture map to the skull, so we just need any tangent vector so that
		// the math works out to give us the original interpolated vertex normal.
		glm::vec3 up = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		if (fabsf(glm::dot(vertices[i].Normal, up)) < 1.0f - 0.001f)
		{

			vertices[i].TangentU = glm::normalize(glm::cross(up, vertices[i].Normal));
		}
		else
		{
			up = glm::vec3(0.0f, 0.0f, 1.0f);
			vertices[i].TangentU = glm::normalize(glm::cross(vertices[i].Normal, up));
		}

		vMin = MathHelper::vectorMin(vMin, p);
		vMax = MathHelper::vectorMax(vMax, p);
	}

	/*BoundingBox bounds;
	bounds.Center = 0.5f * (vMin + vMax);
	bounds.Extents = 0.5f * (vMax - vMin);*/
	AABB bounds(vMin, vMax);

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::uint32_t> indices(3 * tcount);
	for (uint32_t i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	geo->vertexBufferCPU = malloc(vbByteSize);
	memcpy(geo->vertexBufferCPU, vertices.data(), vbByteSize);

	geo->indexBufferCPU = malloc(ibByteSize);
	memcpy(geo->indexBufferCPU, indices.data(), ibByteSize);

	std::vector<uint32_t> vertexLocations;
	VertexBufferBuilder::begin(mDevice, mBackQueue, mCommandBuffer, mMemoryProperties)
		.AddVertices(vbByteSize, (float*)vertices.data())
		.build(geo->vertexBufferGPU, vertexLocations);

	std::vector<uint32_t> indexLocations;
	IndexBufferBuilder::begin(mDevice, mBackQueue, mCommandBuffer, mMemoryProperties)
		.AddIndices(ibByteSize, indices.data())
		.build(geo->indexBufferGPU, indexLocations);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;

	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (uint32_t)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	submesh.Bounds = bounds;

	geo->DrawArgs["skull"] = submesh;
	mGeometries[geo->Name] = std::move(geo);

}

void DungeonStompApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->NumFramesDirty = gNumFrameResources;
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->NormalSrvHeapIndex = 1;
	bricks0->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = glm::vec3(0.1f, 0.1f, 0.1f);
	bricks0->Roughness = 0.3f;
	bricks0->Metal = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->NumFramesDirty = gNumFrameResources;
	tile0->Name = "tile0";
	tile0->MatCBIndex = 1;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->NormalSrvHeapIndex = 3;
	tile0->DiffuseAlbedo = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
	tile0->FresnelR0 = glm::vec3(0.2f, 0.2f, 0.2f);
	tile0->Roughness = 0.1f;
	tile0->Metal = 0.3f;

	auto mirror0 = std::make_unique<Material>();
	mirror0->NumFramesDirty = gNumFrameResources;
	mirror0->Name = "mirror0";
	mirror0->MatCBIndex = 2;
	mirror0->DiffuseSrvHeapIndex = 4;
	mirror0->NormalSrvHeapIndex = 5;
	mirror0->DiffuseAlbedo = glm::vec4(0.0f, 0.0f, 0.1f, 1.0f);
	mirror0->FresnelR0 = glm::vec3(0.98f, 0.97f, 0.95f);
	mirror0->Roughness = 0.1f;
	mirror0->Metal = 0.3f;

	auto skullMat = std::make_unique<Material>();
	skullMat->NumFramesDirty = gNumFrameResources;
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 3;
	skullMat->DiffuseSrvHeapIndex = 4;
	skullMat->NormalSrvHeapIndex = 5;
	skullMat->DiffuseAlbedo = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
	skullMat->FresnelR0 = glm::vec3(0.6f, 0.6f, 0.6f);
	skullMat->Roughness = 0.2f;
	skullMat->Metal = 0.3f;

	auto sky = std::make_unique<Material>();
	sky->NumFramesDirty = gNumFrameResources;
	sky->Name = "sky";
	sky->MatCBIndex = 4;
	sky->DiffuseSrvHeapIndex = 3;
	sky->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = glm::vec3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;
	sky->Metal = 0.3f;

	auto wall0 = std::make_unique<Material>();
	wall0->NumFramesDirty = gNumFrameResources;
	wall0->Name = "wall0";
	wall0->MatCBIndex = 5;
	wall0->DiffuseSrvHeapIndex = 8;
	wall0->NormalSrvHeapIndex = 3;
	wall0->DiffuseAlbedo = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
	wall0->FresnelR0 = glm::vec3(0.2f, 0.2f, 0.2f);
	wall0->Roughness = 0.1f;
	wall0->Metal = 0.3f;

	auto default2 = std::make_unique<Material>();
	default2->Name = "default";
	default2->NumFramesDirty = gNumFrameResources;
	default2->MatCBIndex = 6;
	default2->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	default2->FresnelR0 = glm::vec3(0.05f, 0.05f, 0.05f);
	default2->Roughness = 0.815f;
	default2->Metal = 0.1f;

	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->NumFramesDirty = gNumFrameResources;
	grass->MatCBIndex = 7;
	grass->DiffuseAlbedo = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
	grass->FresnelR0 = glm::vec3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.925f;
	grass->Metal = 0.1f;

	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->NumFramesDirty = gNumFrameResources;
	water->MatCBIndex = 8;
	water->DiffuseSrvHeapIndex = 0;
	water->DiffuseAlbedo = glm::vec4(0.5f, 0.5f, 1.0f, 0.5f);
	//Water (0.02f, 0.02f, 0.02f);
	water->FresnelR0 = glm::vec3(0.02f, 0.02f, 0.02f);
	water->Roughness = 0.326f;
	water->Metal = 0.2f;

	auto brick = std::make_unique<Material>();
	brick->Name = "brick";
	brick->NumFramesDirty = gNumFrameResources;
	brick->MatCBIndex = 9;
	brick->DiffuseSrvHeapIndex = 0;
	brick->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	brick->FresnelR0 = glm::vec3(0.02f, 0.02f, 0.02f);
	brick->Roughness = 0.825f;
	brick->Metal = 0.12f;

	auto stone = std::make_unique<Material>();
	stone->Name = "stone";
	stone->NumFramesDirty = gNumFrameResources;
	stone->MatCBIndex = 10;
	stone->DiffuseSrvHeapIndex = 0;
	stone->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	stone->FresnelR0 = glm::vec3(0.03f, 0.03f, 0.03f);
	stone->Roughness = 0.743f;
	stone->Metal = 0.22f;

	auto tile = std::make_unique<Material>();
	tile->Name = "tile";
	tile->NumFramesDirty = gNumFrameResources;
	tile->MatCBIndex = 11;
	tile->DiffuseSrvHeapIndex = 0;
	tile->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	tile->FresnelR0 = glm::vec3(0.02f, 0.02f, 0.02f);
	tile->Roughness = 0.32f;
	tile->Metal = 0.20f;

	auto crate = std::make_unique<Material>();
	crate->Name = "crate";
	crate->NumFramesDirty = gNumFrameResources;
	crate->MatCBIndex = 12;
	crate->DiffuseSrvHeapIndex = 0;
	crate->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	crate->FresnelR0 = glm::vec3(0.03f, 0.03f, 0.03f);
	crate->Roughness = 0.796f;
	crate->Metal = 0.160f;

	auto ice = std::make_unique<Material>();
	ice->Name = "ice";
	ice->NumFramesDirty = gNumFrameResources;
	ice->MatCBIndex = 13;
	ice->DiffuseSrvHeapIndex = 0;
	ice->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	ice->FresnelR0 = glm::vec3(0.1f, 0.1f, 0.1f);
	ice->Roughness = 0.415f;
	ice->Metal = 0.40f;

	auto bone = std::make_unique<Material>();
	bone->Name = "bone";
	bone->NumFramesDirty = gNumFrameResources;
	bone->MatCBIndex = 14;
	bone->DiffuseSrvHeapIndex = 0;
	bone->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	bone->FresnelR0 = glm::vec3(0.09f, 0.09f, 0.09f);
	bone->Roughness = 0.438f;
	bone->Metal = 0.41f;


	auto metal = std::make_unique<Material>();
	metal->Name = "metal";
	metal->NumFramesDirty = gNumFrameResources;
	metal->MatCBIndex = 15;
	metal->DiffuseSrvHeapIndex = 0;
	metal->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	metal->FresnelR0 = glm::vec3(0.12f, 0.12f, 0.12f);
	metal->Roughness = 0.514f;
	metal->Metal = 0.5f;

	auto glass = std::make_unique<Material>();
	glass->Name = "glass";
	glass->NumFramesDirty = gNumFrameResources;
	glass->MatCBIndex = 16;
	glass->DiffuseSrvHeapIndex = 0;
	glass->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	//Glass (0.08f, 0.08f, 0.08f);
	glass->FresnelR0 = glm::vec3(0.08f, 0.08f, 0.08f);
	glass->Roughness = 0.224f;
	glass->Metal = 0.3f;

	auto wood = std::make_unique<Material>();
	wood->Name = "wood";
	wood->NumFramesDirty = gNumFrameResources;
	wood->MatCBIndex = 17;
	wood->DiffuseSrvHeapIndex = 0;
	wood->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	wood->FresnelR0 = glm::vec3(0.04f, 0.04f, 0.04f);
	wood->Roughness = 0.838f;
	wood->Metal = 0.17f;

	auto flat = std::make_unique<Material>();
	flat->Name = "flat";
	flat->NumFramesDirty = gNumFrameResources;
	flat->MatCBIndex = 18;
	flat->DiffuseSrvHeapIndex = 0;
	flat->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	flat->FresnelR0 = glm::vec3(0.01f, 0.01f, 0.01f);
	flat->Roughness = 0.932f;
	flat->Metal = 0.1f;

	auto tilebrown = std::make_unique<Material>();
	tilebrown->Name = "tilebrown";
	tilebrown->NumFramesDirty = gNumFrameResources;
	tilebrown->MatCBIndex = 19;
	tilebrown->DiffuseSrvHeapIndex = 0;
	tilebrown->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	tilebrown->FresnelR0 = glm::vec3(0.02f, 0.02f, 0.02f);
	tilebrown->Roughness = 0.324f;
	tilebrown->Metal = 0.424f;

	auto monster = std::make_unique<Material>();
	monster->Name = "monster";
	monster->NumFramesDirty = gNumFrameResources;
	monster->MatCBIndex = 20;
	monster->DiffuseSrvHeapIndex = 0;
	monster->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	monster->FresnelR0 = glm::vec3(0.05f, 0.05f, 0.05f);
	monster->Roughness = 0.833f;
	monster->Metal = 0.143f;


	auto monsterweapon = std::make_unique<Material>();
	monsterweapon->Name = "monsterweapon";
	monsterweapon->NumFramesDirty = gNumFrameResources;
	monsterweapon->MatCBIndex = 21;
	monsterweapon->DiffuseSrvHeapIndex = 0;
	monsterweapon->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	monsterweapon->FresnelR0 = glm::vec3(0.06f, 0.06f, 0.06f);
	monsterweapon->Roughness = 0.702f;
	monsterweapon->Metal = 0.33f;

	auto playerweapon = std::make_unique<Material>();
	playerweapon->Name = "playerweapon";
	playerweapon->NumFramesDirty = gNumFrameResources;
	playerweapon->MatCBIndex = 22;
	playerweapon->DiffuseSrvHeapIndex = 0;
	playerweapon->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	playerweapon->FresnelR0 = glm::vec3(0.07f, 0.07f, 0.07f);
	playerweapon->Roughness = 0.742f;
	playerweapon->Metal = 0.34f;

	auto coin = std::make_unique<Material>();
	coin->Name = "coin";
	coin->NumFramesDirty = gNumFrameResources;
	coin->MatCBIndex = 23;
	coin->DiffuseSrvHeapIndex = 0;
	coin->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	//Gold(1.0f, 0.71f, 0.29f);
	coin->FresnelR0 = glm::vec3(1.0f, 0.71f, 0.29f);
	coin->Roughness = 0.314f;
	coin->Metal = 0.31f;

	auto torchholder = std::make_unique<Material>();
	torchholder->Name = "torchholder";
	torchholder->NumFramesDirty = gNumFrameResources;
	torchholder->MatCBIndex = 24;
	torchholder->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	torchholder->FresnelR0 = glm::vec3(0.05f, 0.05f, 0.05f);
	torchholder->Roughness = 0.615f;
	torchholder->Metal = 0.391f;

	auto chestwood = std::make_unique<Material>();
	chestwood->Name = "chestwood";
	chestwood->NumFramesDirty = gNumFrameResources;
	chestwood->MatCBIndex = 25;
	chestwood->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	chestwood->FresnelR0 = glm::vec3(0.04f, 0.04f, 0.04f);
	chestwood->Roughness = 0.938f;
	chestwood->Metal = 0.256f;

	auto chestmetal = std::make_unique<Material>();
	chestmetal->Name = "chestmetal";
	chestmetal->NumFramesDirty = gNumFrameResources;
	chestmetal->MatCBIndex = 26;
	chestmetal->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	chestmetal->FresnelR0 = glm::vec3(0.12f, 0.12f, 0.12f);
	chestmetal->Roughness = 0.914f;
	chestmetal->Metal = 0.426f;

	auto stonemain = std::make_unique<Material>();
	stonemain->Name = "stonemain";
	stonemain->NumFramesDirty = gNumFrameResources;
	stonemain->MatCBIndex = 27;
	stonemain->DiffuseSrvHeapIndex = 0;
	stonemain->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	stonemain->FresnelR0 = glm::vec3(0.03f, 0.03f, 0.03f);
	stonemain->Roughness = 0.743f;
	stonemain->Metal = 0.46f;

	auto doorwood = std::make_unique<Material>();
	doorwood->Name = "doorwood";
	doorwood->NumFramesDirty = gNumFrameResources;
	doorwood->MatCBIndex = 28;
	doorwood->DiffuseSrvHeapIndex = 0;
	doorwood->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	doorwood->FresnelR0 = glm::vec3(0.04f, 0.04f, 0.04f);
	doorwood->Roughness = 0.87938f;
	doorwood->Metal = 0.28f;

	auto doormetal = std::make_unique<Material>();
	doormetal->Name = "doormetal";
	doormetal->NumFramesDirty = gNumFrameResources;
	doormetal->MatCBIndex = 29;
	doormetal->DiffuseSrvHeapIndex = 0;
	doormetal->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	doormetal->FresnelR0 = glm::vec3(0.02f, 0.02f, 0.02f);
	doormetal->Roughness = 0.7382f;
	doormetal->Metal = 0.38f;

	auto button = std::make_unique<Material>();
	button->Name = "button";
	button->NumFramesDirty = gNumFrameResources;
	button->MatCBIndex = 30;
	button->DiffuseSrvHeapIndex = 0;
	button->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	button->FresnelR0 = glm::vec3(0.06f, 0.06f, 0.06f);
	button->Roughness = 0.124f;
	button->Metal = 0.23f;

	auto ceilingmain = std::make_unique<Material>();
	ceilingmain->Name = "ceilingmain";
	ceilingmain->NumFramesDirty = gNumFrameResources;
	ceilingmain->MatCBIndex = 31;
	ceilingmain->DiffuseSrvHeapIndex = 0;
	ceilingmain->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	ceilingmain->FresnelR0 = glm::vec3(0.031f, 0.031f, 0.031f);
	ceilingmain->Roughness = 0.762f;
	ceilingmain->Metal = 0.18f;

	auto pillar = std::make_unique<Material>();
	pillar->Name = "pillar";
	pillar->NumFramesDirty = gNumFrameResources;
	pillar->MatCBIndex = 32;
	pillar->DiffuseSrvHeapIndex = 0;
	pillar->DiffuseAlbedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	pillar->FresnelR0 = glm::vec3(0.022f, 0.022f, 0.022f);
	pillar->Roughness = 0.725f;
	pillar->Metal = 0.11f;

	//new material - increment MatCBIndex 

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["mirror0"] = std::move(mirror0);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["sky"] = std::move(sky);
	mMaterials["wall0"] = std::move(wall0);

	mMaterials["default"] = std::move(default2);
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);

	mMaterials["brick"] = std::move(brick);
	mMaterials["stone"] = std::move(stone);
	mMaterials["tile"] = std::move(tile);

	mMaterials["crate"] = std::move(crate);
	mMaterials["ice"] = std::move(ice);
	mMaterials["bone"] = std::move(bone);
	mMaterials["metal"] = std::move(metal);

	mMaterials["glass"] = std::move(glass);
	mMaterials["wood"] = std::move(wood);
	mMaterials["flat"] = std::move(flat);

	mMaterials["tilebrown"] = std::move(tilebrown);
	mMaterials["monster"] = std::move(monster);
	mMaterials["monsterweapon"] = std::move(monsterweapon);

	mMaterials["playerweapon"] = std::move(playerweapon);
	mMaterials["coin"] = std::move(coin);
	mMaterials["torchholder"] = std::move(torchholder);

	mMaterials["chestwood"] = std::move(chestwood);
	mMaterials["chestmetal"] = std::move(chestmetal);
	mMaterials["stonemain"] = std::move(stonemain);

	mMaterials["doorwood"] = std::move(doorwood);
	mMaterials["doormetal"] = std::move(doormetal);
	mMaterials["button"] = std::move(button);

	mMaterials["ceilingmain"] = std::move(ceilingmain);
	mMaterials["pillar"] = std::move(pillar);
}

void DungeonStompApp::BuildRenderItems()
{
	auto skyRitem = std::make_unique<RenderItem>();
	skyRitem->World = glm::scale(glm::mat4(1.0f), glm::vec3(20000.0f, 30000.0f, 20000.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 0;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	auto quadRitem = std::make_unique<RenderItem>();
	quadRitem->World = MathHelper::Identity4x4();
	quadRitem->TexTransform = MathHelper::Identity4x4();
	quadRitem->ObjCBIndex = 1;
	quadRitem->Mat = mMaterials["bricks0"].get();
	quadRitem->Geo = mGeometries["shapeGeo"].get();

	quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quad"].IndexCount;
	quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Debug].push_back(quadRitem.get());
	mAllRitems.push_back(std::move(quadRitem));


	auto boxRitem = std::make_unique<RenderItem>();
	boxRitem->World = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 2.0f)), glm::vec3(0.0f, 0.5f, 0.0f));
	boxRitem->TexTransform = glm::mat4(1.0f);
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["bricks0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	auto skullRitem = std::make_unique<RenderItem>();
	skullRitem->World = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 0.4f, 0.4f)), glm::vec3(0.0f, 2.0f, 0.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 3;
	skullRitem->Mat = mMaterials["skullMat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
	mAllRitems.push_back(std::move(skullRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->TexTransform = glm::scale(glm::mat4(1.0f), glm::vec3(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 4;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	glm::mat4 brickTexTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 2.0f, 1.0f));
	UINT objCBIndex = 5;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		glm::mat4 leftCylWorld = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 1.5f, -10.0f + i * 5.0f));
		glm::mat4 rightCylWorld = glm::translate(glm::mat4(1.0f), glm::vec3(+5.0f, 1.5f, -10.0f + i * 5.0f));

		glm::mat4 leftSphereWorld = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 3.5f, -10.0f + i * 5.0f));
		glm::mat4 rightSphereWorld = glm::translate(glm::mat4(1.0f), glm::vec3(+5.0f, 3.5f, -10.0f + i * 5.0f));

		leftCylRitem->World = rightCylWorld;
		leftCylRitem->TexTransform = brickTexTransform;
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		rightCylRitem->World = leftCylWorld;
		rightCylRitem->TexTransform = brickTexTransform;
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		leftSphereRitem->World = leftSphereWorld;
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["mirror0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		rightSphereRitem->World = rightSphereWorld;
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["mirror0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	wavesRitem->TexTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 1.0f));// , XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = objCBIndex++;
	wavesRitem->Mat = mMaterials["wall0"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	///wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());
	mAllRitems.push_back(std::move(wavesRitem));

	for (int i = 0; i < number_of_tex_aliases; i++) {

		auto wavesRitem = std::make_unique<RenderItem>();
		wavesRitem->World = MathHelper::Identity4x4();
		wavesRitem->TexTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 1.0f));// , XMMatrixScaling(5.0f, 5.0f, 1.0f));
		wavesRitem->ObjCBIndex = objCBIndex++;
		wavesRitem->TextureIndex = TexMap[i].texture;
		wavesRitem->TextureNormalIndex = TexMap[i].normalmaptextureid;
		wavesRitem->Mat = mMaterials[TexMap[i].material].get();
		//wavesRitem->Mat = mMaterials["flat"].get();
		wavesRitem->Geo = mGeometries["waterGeo"].get();
		///wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
		wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
		wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

		mWavesRitem = wavesRitem.get();

		mRitemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());
		mAllRitems.push_back(std::move(wavesRitem));
	}
}

void DungeonStompApp::BuildWavesGeometry() {
	std::vector<uint32_t> indices(3 * mWaves->TriangleCount());//3 indices per face
	assert(mWaves->VertexCount() < 0x0000FFFFF);

	//Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;

	for (int i = 0; i < m - 1; ++i) {
		for (int j = 0; j < n - 1; ++j) {
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;
			k += 6;//next quad
		}
	}

	//uint32_t vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	uint32_t vbByteSize = MAX_NUM_QUADS * sizeof(Vertex);
	uint32_t ibByteSize = (uint32_t)indices.size() * sizeof(uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	//Set dynamically.
	geo->vertexBufferCPU = nullptr;


	geo->indexBufferCPU = malloc(ibByteSize);
	memcpy(geo->indexBufferCPU, indices.data(), ibByteSize);

	Vulkan::BufferProperties props;
#ifdef __USE__VMA__
	props.usage = VMA_MEMORY_USAGE_CPU_ONLY;
#else
	props.memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
#endif
	props.bufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	props.size = vbByteSize;
	WaveVertexBuffers.resize(gNumFrameResources);
	WaveVertexPtrs.resize(gNumFrameResources);
	for (size_t i = 0; i < gNumFrameResources; i++) {
		Vulkan::initBuffer(mDevice, mMemoryProperties, props, WaveVertexBuffers[i]);
		WaveVertexPtrs[i] = Vulkan::mapBuffer(mDevice, WaveVertexBuffers[i]);
	}
#ifdef __USE__VMA__
	props.usage = VMA_MEMORY_USAGE_GPU_ONLY;
#else
	props.memoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
#endif
	props.bufferUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	props.size = ibByteSize;
	Vulkan::initBuffer(mDevice, mMemoryProperties, props, WavesIndexBuffer);



	VkDeviceSize maxSize = std::max(vbByteSize, ibByteSize);
	Vulkan::Buffer stagingBuffer;

#ifdef __USE__VMA__
	props.usage = VMA_MEMORY_USAGE_CPU_ONLY;
#else
	props.memoryProps = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
#endif
	props.bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	props.size = maxSize;
	initBuffer(mDevice, mMemoryProperties, props, stagingBuffer);
	void* ptr = mapBuffer(mDevice, stagingBuffer);
	//copy vertex data

	memcpy(ptr, indices.data(), ibByteSize);
	CopyBufferTo(mDevice, mGraphicsQueue, mCommandBuffer, stagingBuffer, WavesIndexBuffer, ibByteSize);
	unmapBuffer(mDevice, stagingBuffer);
	cleanupBuffer(mDevice, stagingBuffer);

	//initBuffer(mDevice, mMemoryProperties, ibByteSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, geo->indexBufferGPU);

	//VkDeviceSize maxSize = std::max(vbByteSize, ibByteSize);
	//Buffer stagingBuffer;
	//initBuffer(mDevice, mMemoryProperties, maxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer);
	//void* ptr = mapBuffer(mDevice, stagingBuffer);
	////copy vertex data

	//memcpy(ptr, indices.data(), ibByteSize);
	//CopyBufferTo(mDevice, mGraphicsQueue, mCommandBuffer, stagingBuffer, geo->indexBufferGPU, ibByteSize);
	//unmapBuffer(mDevice, stagingBuffer);
	//cleanupBuffer(mDevice, stagingBuffer);

	geo->indexBufferGPU = WavesIndexBuffer;

	geo->VertexBufferByteSize = vbByteSize;
	geo->VertexByteStride = sizeof(Vertex);
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (uint32_t)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);

}

void DungeonStompApp::BuildDescriptors() {
	descriptorSetPoolCache = std::make_unique<DescriptorSetPoolCache>(mDevice);
	descriptorSetLayoutCache = std::make_unique<DescriptorSetLayoutCache>(mDevice);

	VkDescriptorSet descriptor0 = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout0;
	DescriptorSetBuilder::begin(descriptorSetPoolCache.get(), descriptorSetLayoutCache.get())
		.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(descriptor0, descriptorLayout0);

	VkDescriptorSet descriptor1 = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout1;
	DescriptorSetBuilder::begin(descriptorSetPoolCache.get(), descriptorSetLayoutCache.get())
		.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(descriptor1, descriptorLayout1);

	std::vector<VkDescriptorSet> descriptors{ descriptor0,descriptor1 };
	uniformDescriptors = std::make_unique<VulkanDescriptorList>(mDevice, descriptors);

	VkDescriptorSet descriptor2 = VK_NULL_HANDLE;

	VkDescriptorSetLayout descriptorLayout2;
	DescriptorSetBuilder::begin(descriptorSetPoolCache.get(), descriptorSetLayoutCache.get())
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(descriptor2, descriptorLayout2);

	descriptors = { descriptor2 };
	storageDescriptors = std::make_unique<VulkanDescriptorList>(mDevice, descriptors);

	VkDescriptorSet descriptor3 = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout3 = VK_NULL_HANDLE;
	DescriptorSetBuilder::begin(descriptorSetPoolCache.get(), descriptorSetLayoutCache.get())
		.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, (uint32_t)*textures)
		.build(descriptor3, descriptorLayout3);


	//Build stuff for CubeMap
	VkDescriptorSet descriptor4 = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout4 = VK_NULL_HANDLE;
	DescriptorSetBuilder::begin(descriptorSetPoolCache.get(), descriptorSetLayoutCache.get())
		.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, (uint32_t)1)
		.build(descriptor4, descriptorLayout4);
	descriptors = { descriptor3,descriptor4 };
	textureDescriptors = std::make_unique<VulkanDescriptorList>(mDevice, descriptors);

	//build stuff for shadow
	VkDescriptorSet descriptor5 = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout5 = VK_NULL_HANDLE;
	DescriptorSetBuilder::begin(descriptorSetPoolCache.get(), descriptorSetLayoutCache.get())
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, (uint32_t)1)
		.build(descriptor5, descriptorLayout5);
	descriptors = { descriptor5 };
	shadowDescriptors = std::make_unique<VulkanDescriptorList>(mDevice, descriptors);


	VkDeviceSize offset = 0;
	descriptors = { descriptor0,descriptor1 };
	std::vector<VkDescriptorSetLayout> descriptorLayouts = { descriptorLayout0,descriptorLayout1 };
	auto& ub = *uniformBuffer;
	for (int i = 0; i < (int)descriptors.size(); ++i) {
		//descriptorBufferInfo.clear();
		VkDeviceSize range = ub[i].objectSize;// bufferInfo[i].objectCount* bufferInfo[i].objectSize* bufferInfo[i].repeatCount;
		VkDeviceSize bufferSize = ub[i].objectCount * ub[i].objectSize * ub[i].repeatCount;
		VkDescriptorBufferInfo descrInfo{};
		descrInfo.buffer = ub;
		descrInfo.offset = offset;
		descrInfo.range = ub[i].objectSize;
		//descriptorBufferInfo.push_back(descrInfo);
		offset += bufferSize;
		VkDescriptorSetLayout layout = descriptorLayouts[i];
		DescriptorSetUpdater::begin(descriptorSetLayoutCache.get(), layout, descriptors[i])
			.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, &descrInfo)
			.update();
	}
	{
		auto& sb = *storageBuffer;
		VkDescriptorBufferInfo descrInfo{};
		descrInfo.buffer = sb;
		descrInfo.range = sb[0].objectCount * sb[0].objectSize * sb[0].repeatCount;
		DescriptorSetUpdater::begin(descriptorSetLayoutCache.get(), descriptorLayout2, descriptor2)
			.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &descrInfo)
			.update();
	}
	{
		auto& samp = *sampler;
		VkDescriptorImageInfo sampInfo;
		sampInfo.sampler = samp;
		auto& tl = *textures;
		std::vector<VkDescriptorImageInfo> imageInfos(tl);
		for (int i = 0; i < (int)tl; ++i) {
			imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfos[i].imageView = tl[i].imageView;

		}
		DescriptorSetUpdater::begin(descriptorSetLayoutCache.get(), descriptorLayout3, descriptor3)
			.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, &sampInfo)
			.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, imageInfos.data(), (uint32_t)tl)
			.update();
	}
	{
		VkDescriptorImageInfo imageInfo{};

		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = *cubeMapTexture;


		DescriptorSetUpdater::begin(descriptorSetLayoutCache.get(), descriptorLayout4, descriptor4)
			.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &imageInfo, (uint32_t)1)
			.update();
	}
	{
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = mShadowMap->getRenderTargetView();
		imageInfo.sampler = mShadowMap->getRenderTargetSampler();
		DescriptorSetUpdater::begin(descriptorSetLayoutCache.get(), descriptorLayout5, descriptor5)
			.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, (uint32_t)1)
			.update();
	}
	VkPipelineLayout layout{ VK_NULL_HANDLE };
	PipelineLayoutBuilder::begin(mDevice)
		.AddDescriptorSetLayout(descriptorLayout0)
		.AddDescriptorSetLayout(descriptorLayout1)
		.AddDescriptorSetLayout(descriptorLayout2)
		.AddDescriptorSetLayout(descriptorLayout3)
		.AddDescriptorSetLayout(descriptorLayout4)
		.build(layout);
	shadowPipelineLayout = std::make_unique<VulkanPipelineLayout>(mDevice, layout);


	PipelineLayoutBuilder::begin(mDevice)
		.AddDescriptorSetLayout(descriptorLayout0)
		.AddDescriptorSetLayout(descriptorLayout1)
		.AddDescriptorSetLayout(descriptorLayout2)
		.AddDescriptorSetLayout(descriptorLayout3)
		.AddDescriptorSetLayout(descriptorLayout4)
		.build(layout);
	cubeMapPipelineLayout = std::make_unique<VulkanPipelineLayout>(mDevice, layout);

	PipelineLayoutBuilder::begin(mDevice)
		.AddDescriptorSetLayout(descriptorLayout0)
		.AddDescriptorSetLayout(descriptorLayout1)
		.AddDescriptorSetLayout(descriptorLayout2)
		.AddDescriptorSetLayout(descriptorLayout3)
		.AddDescriptorSetLayout(descriptorLayout4)
		.AddDescriptorSetLayout(descriptorLayout5)
		.build(layout);
	pipelineLayout = std::make_unique <VulkanPipelineLayout>(mDevice, layout);

	PipelineLayoutBuilder::begin(mDevice)
		.AddDescriptorSetLayout(descriptorLayout0)
		.AddDescriptorSetLayout(descriptorLayout1)
		.AddDescriptorSetLayout(descriptorLayout2)
		.AddDescriptorSetLayout(descriptorLayout3)
		.AddDescriptorSetLayout(descriptorLayout4)
		.AddDescriptorSetLayout(descriptorLayout5)
		.build(layout);
	debugPipelineLayout = std::make_unique<VulkanPipelineLayout>(mDevice, layout);
}

void DungeonStompApp::BuildPSOs() {
	{
		std::vector<Vulkan::ShaderModule> shaders;
		VkVertexInputBindingDescription vertexInputDescription = {};
		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
		ShaderProgramLoader::begin(mDevice)
			.AddShaderPath("..//Shaders/default.vert.spv")
			.AddShaderPath("..//Shaders/default.frag.spv")
			.load(shaders, vertexInputDescription, vertexAttributeDescriptions);


		VkPipeline pipeline{ VK_NULL_HANDLE };
		PipelineBuilder::begin(mDevice, *pipelineLayout, mRenderPass, shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_FRONT_BIT)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setDepthTest(VK_TRUE)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 3)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 0)
			.build(pipeline);
		opaquePipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);
		mPSOs["opaque"] = *opaquePipeline;


		PipelineBuilder::begin(mDevice, *pipelineLayout, mRenderPass, shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_FRONT_BIT)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setDepthTest(VK_TRUE)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 3)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			.build(pipeline);
		torchPipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);
		mPSOs["torch"] = *torchPipeline;

		PipelineBuilder::begin(mDevice, *pipelineLayout, mRenderPass, shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_FRONT_BIT)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setDepthTest(VK_TRUE)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 3)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 0)
			.setSpecializationConstant(VK_SHADER_STAGE_FRAGMENT_BIT, 0)
			.build(pipeline);
		opaqueFlatPipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);
		mPSOs["opaqueFlat"] = *opaqueFlatPipeline;
		PipelineBuilder::begin(mDevice, *pipelineLayout, mRenderPass, shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_FRONT_BIT)
			.setPolygonMode(VK_POLYGON_MODE_LINE)
			.setDepthTest(VK_TRUE)
			.build(pipeline);
		wireframePipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);
		mPSOs["opaque_wireframe"] = *wireframePipeline;
		for (auto& shader : shaders) {
			Vulkan::cleanupShaderModule(mDevice, shader.shaderModule);
		}
	}
	{
		std::vector<Vulkan::ShaderModule> shaders;
		VkVertexInputBindingDescription vertexInputDescription = {};
		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
		ShaderProgramLoader::begin(mDevice)
			.AddShaderPath("..//Shaders/sky.vert.spv")
			.AddShaderPath("..//Shaders/sky.frag.spv")
			.load(shaders, vertexInputDescription, vertexAttributeDescriptions);
		VkPipeline pipeline = VK_NULL_HANDLE;
		PipelineBuilder::begin(mDevice, *pipelineLayout, mRenderPass, shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_BACK_BIT)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setDepthTest(VK_TRUE)
			.setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL)
			.build(pipeline);
		cubeMapPipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);

		mPSOs["sky"] = *cubeMapPipeline;

		for (auto& shader : shaders) {
			Vulkan::cleanupShaderModule(mDevice, shader.shaderModule);
		}
	}
	{
		std::vector<Vulkan::ShaderModule> shaders;
		VkVertexInputBindingDescription vertexInputDescription = {};
		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
		ShaderProgramLoader::begin(mDevice)
			.AddShaderPath("..//Shaders/shadow.vert.spv")
			.AddShaderPath("..//Shaders/shadow.frag.spv")
			.load(shaders, vertexInputDescription, vertexAttributeDescriptions);
		VkPipeline pipeline = VK_NULL_HANDLE;

		PipelineBuilder::begin(mDevice, *shadowPipelineLayout, mShadowMap->getRenderPass(), shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_BACK_BIT)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setDepthTest(VK_TRUE)
			.setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL)
			.build(pipeline);
		shadowPipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);

		mPSOs["shadow_opaque"] = *shadowPipeline;

		for (auto& shader : shaders) {
			Vulkan::cleanupShaderModule(mDevice, shader.shaderModule);
		}
	}
	{
		std::vector<Vulkan::ShaderModule> shaders;
		VkVertexInputBindingDescription vertexInputDescription = {};
		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
		ShaderProgramLoader::begin(mDevice)
			.AddShaderPath("..//Shaders/debug.vert.spv")
			.AddShaderPath("..//Shaders/debug.frag.spv")
			.load(shaders, vertexInputDescription, vertexAttributeDescriptions);
		VkPipeline pipeline = VK_NULL_HANDLE;
		PipelineBuilder::begin(mDevice, *debugPipelineLayout, mRenderPass, shaders, vertexInputDescription, vertexAttributeDescriptions)
			.setCullMode(VK_CULL_MODE_FRONT_BIT)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setDepthTest(VK_TRUE)
			.setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL)
			.build(pipeline);
		debugPipeline = std::make_unique<VulkanPipeline>(mDevice, pipeline);
		mPSOs["debug"] = *debugPipeline;

		for (auto& shader : shaders) {
			Vulkan::cleanupShaderModule(mDevice, shader.shaderModule);
		}
	}
}

void DungeonStompApp::BuildFrameResources() {
	for (int i = 0; i < gNumFrameResources; i++) {
		auto& ub = *uniformBuffer;
		auto& sb = *storageBuffer;
		PassConstants* pc = (PassConstants*)((uint8_t*)ub[0].ptr + ub[0].objectSize * ub[0].objectCount * i);// ((uint8_t*)pPassCB + passSize * i);
		ObjectConstants* oc = (ObjectConstants*)((uint8_t*)ub[1].ptr + ub[1].objectSize * ub[1].objectCount * i);// ((uint8_t*)pObjectCB + objectSize * mAllRitems.size() * i);
		//MaterialConstants* mc = (MaterialConstants*)((uint8_t*)ub[2].ptr + ub[2].objectSize * ub[2].objectCount * i);// ((uint8_t*)pMatCB + matSize * mMaterials.size() * i);
		MaterialData* md = (MaterialData*)((uint8_t*)sb[0].ptr + sb[0].objectSize * sb[0].objectCount * i);
		Vertex* pWv = (Vertex*)WaveVertexPtrs[i];
		mFrameResources.push_back(std::make_unique<FrameResource>(pc, oc, md, pWv));// , pWv));
	}
}
void DungeonStompApp::OnResize()
{
	VulkApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void DungeonStompApp::OnMouseMove(WPARAM btnState, int x, int y) {
	if ((btnState & MK_LBUTTON) != 0) {
		// Make each pixel correspond to a quarter of a degree.
		float dx = glm::radians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = glm::radians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void DungeonStompApp::OnMouseDown(WPARAM btnState, int x, int y) {
	//mLastMousePos.x = x;
	//mLastMousePos.y = y;
	//SetCapture(mhMainWnd);
}

void DungeonStompApp::OnMouseUp(WPARAM btnState, int x, int y) {
	//ReleaseCapture();
}

extern HRESULT FrameMove(double fTime, FLOAT fTimeKey);
extern void UpdateWorld(float fElapsedTime);
extern VOID UpdateControls();
extern void UpdateCamera(const GameTimer& gt, Camera& mCamera);

void DungeonStompApp::Update(const GameTimer& gt) {
	VulkApp::Update(gt);

	if (state == ProgState::Draw) {

		//Cycle through the circular frame resource array
		mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
		mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

		float t = gt.DeltaTime();
		UpdateControls();
		FrameMove(0.0f, t);
		UpdateWorld(t);

		bobY.update(t);
		bobX.update(t);

		UpdateCamera(gt, mCamera);

		mSceneBounds.Center = glm::vec3(m_vEyePt.x, m_vEyePt.y, m_vEyePt.z);

		OnKeyboardInput(gt);

		// Animate the lights (and hence shadows).
		//mLightRotationAngle += 0.9f * gt.DeltaTime();
		glm::mat4 R = glm::rotate(glm::mat4(1.0f), mLightRotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
		for (int i = 0; i < 3; ++i) {
			glm::vec3 lightDir = R * glm::vec4(mBaseLightDirections[i], 0.0f);
			mRotatedLightDirections[i] = lightDir;

		}

		UpdateObjectCBs(gt);
		UpdateMaterialsBuffer(gt);
		UpdateShadowTransform(gt);
		UpdateMainPassCB(gt);
		UpdateShadowPassCB(gt);
		UpdateWaves(gt);
	}
}

extern int cutoff;

void DungeonStompApp::UpdateWaves(const GameTimer& gt)
{
	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	Vertex* pWaves = mCurrFrameResource->pWavesVB;

	// Update the dungeon vertex buffer with the new solution.
	for (int j = 0; j < cutoff; j++)
	{
		pWaves[j].Pos.x = src_v[j].x;
		pWaves[j].Pos.y = src_v[j].y;
		pWaves[j].Pos.z = src_v[j].z;

		pWaves[j].Normal.x = src_v[j].nx;
		pWaves[j].Normal.y = src_v[j].ny;
		pWaves[j].Normal.z = src_v[j].nz;

		pWaves[j].TexC.x = src_v[j].tu;
		pWaves[j].TexC.y = src_v[j].tv;

		pWaves[j].TangentU.x = src_v[j].nmx;
		pWaves[j].TangentU.y = src_v[j].nmy;
		pWaves[j].TangentU.z = src_v[j].nmz;
	}

	mWavesRitem->Geo->vertexBufferGPU = WaveVertexBuffers[mCurrFrameResourceIndex];
}

bool isFullscreen = false;
bool enableWindowKey = false;
bool enableNormalmap = true;
bool enableNormalmapKey = false;
bool enableCameraBob = true;
bool enableCameraBobKey = false;

WINDOWPLACEMENT wpc{};// window placement information

void DungeonStompApp::OnKeyboardInput(const GameTimer& gt)
{
	//rise from the dead
	if (player_list[trueplayernum].bIsPlayerAlive == FALSE) {
		if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
			player_list[trueplayernum].bIsPlayerAlive = TRUE;
			player_list[trueplayernum].health = player_list[trueplayernum].hp;
		}
	}

	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;

	if (GetAsyncKeyState('F') && !enableWindowKey) {

		if (!isFullscreen) {
			isFullscreen = true;
			ToggleFullscreen(isFullscreen);
		}
		else {
			isFullscreen = false;
			ToggleFullscreen(isFullscreen);
		}
	}

	if (GetAsyncKeyState('F')) {
		enableWindowKey = 1;
	}
	else {
		enableWindowKey = 0;
	}

	if (GetAsyncKeyState('N') && !enableNormalmapKey) {

		if (enableNormalmap) {
			enableNormalmap = false;
			SetTextureNormalMapEmpty();
		}
		else {
			enableNormalmap = true;
			SetTextureNormalMap();
		}
	}

	if (GetAsyncKeyState('N')) {
		enableNormalmapKey = 1;
	}
	else {
		enableNormalmapKey = 0;
	}

	if (GetAsyncKeyState('B') && !enableCameraBobKey) {

		if (enableCameraBob) {
			enableCameraBob = false;
		}
		else {
			enableCameraBob = true;
		}
	}

	if (GetAsyncKeyState('B')) {
		enableCameraBobKey = 1;
	}
	else {
		enableCameraBobKey = 0;
	}


	//float speed = 100.0f;

	//if (GetAsyncKeyState('W') & 0x8000)
	//	mCamera.Walk(speed * dt);

	//if (GetAsyncKeyState('S') & 0x8000)
	//	mCamera.Walk(-1.0f * speed * dt);

	//if (GetAsyncKeyState('A') & 0x8000)
	//	mCamera.Strafe(-1.0f * speed * dt);

	//if (GetAsyncKeyState('D') & 0x8000)
	//	mCamera.Strafe(speed * dt);

	//mCamera.UpdateViewMatrix();
}

void  DungeonStompApp::ToggleFullscreen(bool isFullscreen) {

	LONG HWND_style = 0;// current Hwnd style
	LONG HWND_extended_style = 0;// previous Hwnd style

	if (HWND_style == 0)
		HWND_style = GetWindowLong(mhMainWnd, GWL_STYLE);
	if (HWND_extended_style == 0)
		HWND_extended_style = GetWindowLong(mhMainWnd, GWL_EXSTYLE);

	if (isFullscreen) {
		long HWND_newStyle = HWND_style & (~WS_BORDER) & (~WS_DLGFRAME) & (~WS_THICKFRAME);
		long HWND_extended_newStyle = HWND_extended_style & (~WS_EX_WINDOWEDGE);

		SetWindowLong(mhMainWnd, GWL_STYLE, HWND_newStyle | static_cast<long>(WS_POPUP));
		SetWindowLong(mhMainWnd, GWL_EXSTYLE, HWND_extended_newStyle | WS_EX_TOPMOST);

		ShowWindow(mhMainWnd, SW_SHOWMAXIMIZED);

		ShowCursor(false);
	}
	else {

		SetWindowLong(mhMainWnd, GWL_STYLE, HWND_style);
		SetWindowLong(mhMainWnd, GWL_EXSTYLE, HWND_extended_style);
		ShowWindow(mhMainWnd, SW_SHOWNORMAL);
		SetWindowPlacement(mhMainWnd, &wpc);

		ShowCursor(true);
	}
}

void  DungeonStompApp::UpdateObjectCBs(const GameTimer& gt) {

	uint8_t* pObjConsts = (uint8_t*)mCurrFrameResource->pOCs;
	auto& ub = *uniformBuffer;
	VkDeviceSize objSize = ub[1].objectSize;

	for (auto& e : mAllRitems) {
		//Only update the cbuffer data if the constants have changed.
		//This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0) {
			glm::mat4 world = e->World;
			ObjectConstants objConstants;
			objConstants.World = world;
			objConstants.TexTransform = e->TexTransform;
			objConstants.MaterialIndex = e->Mat->MatCBIndex;
			objConstants.TextureIndex = e->TextureIndex;
			objConstants.TextureNormalIndex = e->TextureNormalIndex;
			memcpy((pObjConsts + (objSize * e->ObjCBIndex)), &objConstants, sizeof(objConstants));
			e->NumFramesDirty--;
		}
	}
}

void DungeonStompApp::UpdateMaterialsBuffer(const GameTimer& gt) {
	uint8_t* pMatConsts = (uint8_t*)mCurrFrameResource->pMats;
	auto& sb = *storageBuffer;
	VkDeviceSize objSize = sb[0].objectSize;
	for (auto& e : mMaterials) {
		Material* mat = e.second.get();

		if (mat->NumFramesDirty > 0) {
			glm::mat4 matTransform = mat->MatTransform;
			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			matData.Metal = mat->Metal;
			matData.MatTransform = matTransform;
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;
			memcpy((pMatConsts + (objSize * mat->MatCBIndex)), &matData, sizeof(MaterialData));
			mat->NumFramesDirty--;
		}
	}
}

void DungeonStompApp::UpdateShadowTransform(const GameTimer& gt)
{
	// Only the first "main" light casts a shadow.
	glm::vec3 lightDir = mRotatedLightDirections[0];
	glm::vec3 lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	glm::vec3 targetPos = mSceneBounds.Center;
	glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::mat4 lightView = glm::lookAtLH(lightPos, targetPos, lightUp);

	mLightPosW = lightPos;

	// Transform bounding sphere to light space.
	glm::vec3 sphereCenterLS = lightView * glm::vec4(targetPos, 1.0f);//needs to be 1.0f?
	//XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	//Adjust shadowmap depending on which direction you are facing.
	if ((angy >= 0.00 && angy <= 90.0f) || (angy >= 270.0f && angy <= 360.0f)) {
		l = sphereCenterLS.x - (mSceneBounds.Radius * 1.645f);
	}
	else {
		r = sphereCenterLS.x + (mSceneBounds.Radius * 1.645f);
	}


	mLightNearZ = n;
	mLightFarZ = f;
	//XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);
	glm::mat4 lightProj = glm::orthoLH_ZO(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	glm::mat4 T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	glm::mat4 S = T * lightProj * lightView;// lightView* lightProj* T;
	//XMStoreFloat4x4(&mLightView, lightView);
	mLightView = lightView;
	//XMStoreFloat4x4(&mLightProj, lightProj);
	mLightProj = lightProj;
	//XMStoreFloat4x4(&mShadowTransform, S);
	mShadowTransform = S;
}
void DungeonStompApp::UpdateMainPassCB(const GameTimer& gt) {
	glm::mat4 view = mCamera.GetView();
	glm::mat4 proj = mCamera.GetProj();
	proj[1][1] *= -1;
	glm::mat4 viewProj = proj * view;//reverse for column major matrices view * proj
	glm::mat4 invView = glm::inverse(view);
	glm::mat4 invProj = glm::inverse(proj);
	glm::mat4 invViewProj = glm::inverse(viewProj);

	PassConstants* pPassConstants = mCurrFrameResource->pPCs;
	mMainPassCB.View = view;
	mMainPassCB.Proj = proj;
	mMainPassCB.ViewProj = viewProj;
	mMainPassCB.InvView = invView;
	mMainPassCB.InvProj = invProj;
	mMainPassCB.InvViewProj = invViewProj;
	mMainPassCB.ShadowTransform = mShadowTransform;
	mMainPassCB.EyePosW = mCamera.GetPosition();
	mMainPassCB.RenderTargetSize = glm::vec2(mClientWidth, mClientHeight);
	mMainPassCB.InvRenderTargetSize = glm::vec2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.55f, 0.55f, 0.55f, 1.0f };
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	//XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
	//XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
	//mMainPassCB.Lights[0].Strength = { 1.0f, 1.0f, 0.9f };

	for (int i = 0; i < MaxLights; i++) {
		mMainPassCB.Lights[i + 1].Direction = LightContainer[i].Direction;
		mMainPassCB.Lights[i + 1].Strength = LightContainer[i].Strength;
		mMainPassCB.Lights[i + 1].Position = LightContainer[i].Position;
		mMainPassCB.Lights[i + 1].FalloffEnd = LightContainer[i].FalloffEnd;
		mMainPassCB.Lights[i + 1].FalloffStart = LightContainer[i].FalloffStart;
		mMainPassCB.Lights[i + 1].SpotPower = LightContainer[i].SpotPower;
	}

	mMainPassCB.Lights[0].Strength = { 0.20f, 0.20f, 0.20f };
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].FalloffEnd = 0.0f;
	mMainPassCB.Lights[0].SpotPower = 0.0f;


	memcpy(pPassConstants, &mMainPassCB, sizeof(PassConstants));
}

void DungeonStompApp::UpdateShadowPassCB(const GameTimer& gt) {
	glm::mat4 view = mLightView;
	glm::mat4 proj = mLightProj;
	proj[1][1] *= -1;
	glm::mat4 viewProj = proj * view;//reverse for column major matrices view * proj
	glm::mat4 invView = glm::inverse(view);
	glm::mat4 invProj = glm::inverse(proj);
	glm::mat4 invViewProj = glm::inverse(viewProj);

	uint32_t w = mShadowMap->Width();
	uint32_t h = mShadowMap->Height();

	PassConstants* pPassConstants = mCurrFrameResource->pPCs;
	mShadowPassCB.View = view;
	mShadowPassCB.Proj = proj;
	mShadowPassCB.ViewProj = viewProj;
	mShadowPassCB.InvView = invView;
	mShadowPassCB.InvProj = invProj;
	mShadowPassCB.InvViewProj = invViewProj;
	mShadowPassCB.ShadowTransform = mShadowTransform;
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = glm::vec2(w, h);
	mShadowPassCB.InvRenderTargetSize = glm::vec2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;
	mShadowPassCB.TotalTime = gt.TotalTime();
	mShadowPassCB.DeltaTime = gt.DeltaTime();
	mShadowPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mShadowPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mShadowPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	mShadowPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mShadowPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mShadowPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mShadowPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto& ub = *uniformBuffer;
	auto size = ub[0].objectSize;
	uint8_t* ptr = (uint8_t*)pPassConstants + size;
	memcpy(ptr, &mShadowPassCB, sizeof(PassConstants));
}

void ScanMod(float fElapsedTime);

void DungeonStompApp::Draw(const GameTimer& gt) {
	uint32_t index = 0;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if (ProgState::Init == state) {
		cmd = BeginRender(true);
		EndRender(cmd);
	}
	else if (ProgState::Draw == state) {

		auto& ub = *uniformBuffer;
		VkDeviceSize passSize = ub[0].objectSize;
		VkDeviceSize passCount = ub[0].objectCount;
		auto& ud = *uniformDescriptors;
		//bind descriptors that don't change during pass
		VkDescriptorSet descriptor0 = ud[0];//pass constant buffer
		uint32_t dynamicOffsets[1] = { mCurrFrame * (uint32_t)passSize * (uint32_t)passCount };

		//bind storage buffer
		auto& sb = *storageBuffer;
		VkDeviceSize matSize = sb[0].objectSize * sb[0].objectCount;
		auto& sd = *storageDescriptors;
		VkDescriptorSet descriptor2 = sd[0];
		auto& td = *textureDescriptors;
		VkDescriptorSet descriptor3 = td[0];
		VkDescriptorSet descriptor4 = td[1];
		auto& sh = *shadowDescriptors;
		VkDescriptorSet descriptor5 = sh[0];

		VkCommandBuffer cmd = BeginRender(false);//don't want to start main render pass
		{
			dynamicOffsets[0] = { mCurrFrame * (uint32_t)passSize * (uint32_t)passCount + (uint32_t)passSize };
			//shadow pass
			uint32_t w = mShadowMap->Width();
			uint32_t h = mShadowMap->Height();
			VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
			VkClearValue clearValue[1] = { {1.0f,0.0f } };
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValue;
			renderPassBeginInfo.renderPass = mShadowMap->getRenderPass();
			renderPassBeginInfo.framebuffer = mShadowMap->getFramebuffer();
			renderPassBeginInfo.renderArea = { 0,0,(uint32_t)w,(uint32_t)h };
			VkViewport viewport = mShadowMap->Viewport();
			pvkCmdSetViewport(cmd, 0, 1, &viewport);
			VkRect2D scissor = mShadowMap->ScissorRect();
			pvkCmdSetScissor(cmd, 0, 1, &scissor);
			//vkCmdSetDepthBias(cmd, depthBiasConstant, 0.0f, depthBiasSlope);
			pvkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["shadow_opaque"]);
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *shadowPipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *shadowPipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *shadowPipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *shadowPipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once
			DrawRenderItems(cmd, *pipelineLayout, mRitemLayer[(int)RenderLayer::Opaque], RenderDungeon::Shadow);
			pvkCmdEndRenderPass(cmd);
			//Vulkan::transitionImage(mDevice,mGraphicsQueue,cmd,mShadowMap->getRenderTargetView(),VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT)
		}
		//start main render pass now
		pvkCmdBeginRenderPass(cmd, &mRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		dynamicOffsets[0] = (uint32_t)(mCurrFrame * passSize * passCount);


		VkViewport viewport = { 0.0f,0.0f,(float)mClientWidth,(float)mClientHeight,0.0f,1.0f };
		pvkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { {0,0},{(uint32_t)mClientWidth,(uint32_t)mClientHeight} };
		pvkCmdSetScissor(cmd, 0, 1, &scissor);

		if (mIsWireframe) {
			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["opaque_wireframe"]);
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);//bind PC data
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once		
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 5, 1, &descriptor5, 0, 0);//bind PC data once
			DrawRenderItems(cmd, *pipelineLayout, mRitemLayer[(int)RenderLayer::Opaque], RenderDungeon::NormalMap);
			//pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["debug"]);
			//DrawRenderItems(cmd, *debugPipelineLayout, mRitemLayer[(int)RenderLayer::Debug]);
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);//bind PC data
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once
			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["sky"]);
			DrawRenderItems(cmd, *cubeMapPipelineLayout, mRitemLayer[(int)RenderLayer::Sky], RenderDungeon::Sky);
		}
		else {

			//Draw flat shading
			//pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["opaqueFlat"]);

			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);//bind PC data
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once		
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 5, 1, &descriptor5, 0, 0);//bind PC data once

			//DrawRenderItems(cmd, *pipelineLayout, mRitemLayer[(int)RenderLayer::Opaque]);
			//pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["debug"]);
			//DrawRenderItems(cmd, *debugPipelineLayout, mRitemLayer[(int)RenderLayer::Debug]);
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);//bind PC data
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once

			//pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["sky"]);
			//DrawRenderItems(cmd, *cubeMapPipelineLayout, mRitemLayer[(int)RenderLayer::Sky]);

			//Draw normalmap
			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["opaque"]);

			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);//bind PC data
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once		
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 5, 1, &descriptor5, 0, 0);//bind PC data once

			//Finally, draw the dungeon.
			DrawRenderItems(cmd, *pipelineLayout, mRitemLayer[(int)RenderLayer::Opaque], RenderDungeon::NormalMap);
			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["opaqueFlat"]);
			DrawRenderItems(cmd, *pipelineLayout, mRitemLayer[(int)RenderLayer::Opaque], RenderDungeon::Flat);

			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["torch"]);
			DrawRenderItems(cmd, *pipelineLayout, mRitemLayer[(int)RenderLayer::Opaque], RenderDungeon::Torch);

			//pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["debug"]);
			//DrawRenderItems(cmd, *debugPipelineLayout, mRitemLayer[(int)RenderLayer::Debug], RenderDungeon::Quad);

			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, 1, &descriptor0, 1, dynamicOffsets);//bind PC data
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 2, 1, &descriptor2, 0, 0);//bind PC data once
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 3, 1, &descriptor3, 0, 0);//bind PC data once
			//pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 4, 1, &descriptor4, 0, 0);//bind PC data once

			pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPSOs["sky"]);
			DrawRenderItems(cmd, *cubeMapPipelineLayout, mRitemLayer[(int)RenderLayer::Sky], RenderDungeon::Sky);
		}
		EndRender(cmd);
	}

	ScanMod(gt.DeltaTime());
}

extern int playerObjectStart;
extern int playerObjectEnd;
extern int  playerGunObjectStart;
bool ObjectHasShadow(int object_id);
int lastTexture = -1;

void DungeonStompApp::DrawRenderItems(VkCommandBuffer cmd, VkPipelineLayout layout, const std::vector<RenderItem*>& ritems, RenderDungeon item) {
	auto& ub = *uniformBuffer;
	VkDeviceSize objectSize = ub[1].objectSize;
	auto& ud = *uniformDescriptors;
	VkDescriptorSet descriptor1 = ud[1];

	ProcessLights11();

	//for (size_t i = 0; i < ritems.size(); i++) {
	{
		int i = 23;

		if (item == RenderDungeon::Sky)
			i = 0;

		if (item == RenderDungeon::Quad)
			i = 0;

		auto ri = ritems[i];

		if (ri->ObjCBIndex == 0 || ri->ObjCBIndex == 1) {
			//Draw the sky or the debug quad.
			uint32_t indexOffset = ri->StartIndexLocation;

			const auto vbv = ri->Geo->vertexBufferGPU;
			const auto ibv = ri->Geo->indexBufferGPU;
			pvkCmdBindVertexBuffers(cmd, 0, 1, &vbv.buffer, mOffsets);

			pvkCmdBindIndexBuffer(cmd, ibv.buffer, indexOffset * sizeof(uint32_t), VK_INDEX_TYPE_UINT32);
			uint32_t cbvIndex = ri->ObjCBIndex;

			uint32_t dyoffsets[2] = { (uint32_t)(cbvIndex * objectSize) };
			pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &descriptor1, 1, dyoffsets);
			pvkCmdDrawIndexed(cmd, ri->IndexCount, 1, 0, ri->BaseVertexLocation, 0);
		}

		if (ri->ObjCBIndex == 25) {
			//Draw the dungeon 
			uint32_t indexOffset = ri->StartIndexLocation;

			const auto vbv = ri->Geo->vertexBufferGPU;
			const auto ibv = ri->Geo->indexBufferGPU;
			pvkCmdBindVertexBuffers(cmd, 0, 1, &vbv.buffer, mOffsets);

			int texture_alias_number = texture_list_buffer[ri->TextureIndex];
			int texture_number = TexMap[texture_alias_number].texture;

			if (texture_alias_number == 112) {
				int a = 1;
			}

			pvkCmdBindIndexBuffer(cmd, ibv.buffer, indexOffset * sizeof(uint32_t), VK_INDEX_TYPE_UINT32);
			uint32_t cbvIndex = ri->ObjCBIndex;

			cbvIndex = texture_alias_number;

			bool draw = true;
			int v = 0;
			int vert_index = 0;
			int texture = 153;

			for (int currentObject = 0; currentObject < number_of_polys_per_frame; currentObject++)
			{
				int i = ObjectsToDraw[currentObject].vert_index;
				int vert_index = ObjectsToDraw[currentObject].srcstart;
				int fperpoly = ObjectsToDraw[currentObject].srcfstart;
				int face_index = ObjectsToDraw[currentObject].srcfstart;

				int texture_alias_number = texture_list_buffer[i];
				int texture_number = TexMap[texture_alias_number].texture;
				int oid = 0;
				int normal_map_texture = TexMap[texture_alias_number].normalmaptextureid;

				draw = false;

				if (item == RenderDungeon::NormalMap && normal_map_texture == -1) {
					draw = false;
				}
				if (item == RenderDungeon::NormalMap && normal_map_texture != -1) {
					draw = true;
				}


				if (item == RenderDungeon::Flat && normal_map_texture == -1) {
					draw = true;
				}
				if (item == RenderDungeon::Flat && normal_map_texture != -1) {
					draw = false;
				}

				if (item == RenderDungeon::Shadow) {
					oid = ObjectsToDraw[currentObject].objectId;

					//Don't draw player captions
					if (oid == -99) {
						draw = false;
					}
				}

				if (item == RenderDungeon::Shadow) {
					if (oid == -1) {
						//Draw 3DS and MD2 Shadows
						draw = true;
					}
					else {
						draw = false;
					}

					if (oid > 0) {
						//Draw objects.dat that have SHADOW attribute set to 1
						if (ObjectHasShadow(oid)) {
							draw = true;
						}
					}

					if (ObjectsToDraw[currentObject].castshaddow == 0) {
						draw = false;
					}
				}

				if (currentObject >= playerGunObjectStart && currentObject < playerObjectStart && item == RenderDungeon::Shadow) {
					//don't draw the onscreen player weapon
					draw = false;
				}


				if (currentObject >= playerObjectStart && currentObject < playerObjectEnd && item != RenderDungeon::Shadow) {
					draw = false;
				}

				if (currentObject >= playerObjectStart && currentObject < playerObjectEnd && item == RenderDungeon::Shadow) {
					draw = true;
				}

				if (texture_number >= 94 && texture_number <= 101 ||
					texture_number >= 289 - 1 && texture_number <= 296 - 1 ||
					texture_number >= 279 - 1 && texture_number <= 288 - 1 ||
					texture_number >= 206 - 1 && texture_number <= 210 - 1 ||
					texture_number == 378) {

					draw = false;

					if (item == RenderDungeon::Torch) {
						draw = true;
					}
				}

				if (draw) {
					if (dp_command_index_mode[i] == 1) {//USE_NON_INDEXED_DP

						v = verts_per_poly[currentObject];
						vert_index = ObjectsToDraw[currentObject].srcstart;

						texture = texture_number + 26;

						if (lastTexture != texture) {
							uint32_t dyoffsets[2] = { (uint32_t)(texture * objectSize) };
							pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &descriptor1, 1, dyoffsets);
							lastTexture = texture;
						}

						//Draw the dungeon, monsters and items.
						vkCmdDraw(cmd, v, 1, vert_index, 0);
					}
				}
			}
		}
	}
}

int main() {
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		DungeonStompApp theApp(GetModuleHandle(NULL));
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (std::exception& e)
	{

		MessageBoxA(nullptr, e.what(), "Failed", MB_OK);
		return 0;
	}
}

extern int number_of_tex_aliases;

BOOL DungeonStompApp::LoadRRTextures11(const char* filename)
{
	FILE* fp;
	char s[256];
	char p[256];
	char f[256];
	char t[256];

	int y_count = 30;
	int done = 0;
	int object_count = 0;
	int vert_count = 0;
	int pv_count = 0;
	int poly_count = 0;
	int tex_alias_counter = 0;
	int tex_counter = 0;
	int i;
	BOOL start_flag = TRUE;
	BOOL found;

	if (fopen_s(&fp, filename, "r") != 0)
	{
		//PrintMessage(hwnd, "ERROR can't open ", filename, SCN_AND_FILE);
		//MessageBox(hwnd, filename, "Error can't open", MB_OK);
		//return FALSE;
	}

	int flip = 0;

	auto grassTex = std::make_unique<Texture>();

	while (done == 0)
	{
		found = FALSE;
		fscanf_s(fp, "%s", &s, 256);

		if (strcmp(s, "AddTexture") == 0)
		{
			fscanf_s(fp, "%s", &p, 256);
			//remember the file
			strcpy_s(f, 256, p);
			tex_counter++;
		}

		if (strcmp(s, "Alias") == 0)
		{
			fscanf_s(fp, "%s", &p, 256);
			fscanf_s(fp, "%s", &p, 256);
			strcpy_s((char*)TexMap[tex_alias_counter].tex_alias_name, 100, (char*)&p);

			TexMap[tex_alias_counter].texture = tex_counter - 1;

			bool exists = true;
			FILE* fp4 = NULL;
			fopen_s(&fp4, f, "rb");
			if (fp4 == NULL)
			{
				exists = false;
			}

			if (exists) {
				strcpy_s(t, f);
			}
			else {
				strcpy_s(t, "../Textures/bluetile1.png");
			}

			strcpy_s(TexMap[tex_alias_counter].texpath, t);

			grassTex->Name = t;
			grassTex->FileName = t;
			//loadTexture(mDevice, mCommandBuffer, mGraphicsQueue, mMemoryProperties, grassTex->FileName.c_str(), *grassTex);

			fscanf_s(fp, "%s", &p, 256);
			if (strcmp(p, "AlphaTransparent") == 0)
				TexMap[tex_alias_counter].is_alpha_texture = TRUE;

			i = tex_alias_counter;

			fscanf_s(fp, "%s", &p, 256);

			if (strcmp(p, "WHOLE") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)1.0;
				TexMap[i].tu[1] = (float)0.0;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)1.0;
				TexMap[i].tv[2] = (float)1.0;
				TexMap[i].tu[3] = (float)1.0;
				TexMap[i].tv[3] = (float)0.0;
			}

			if (strcmp(p, "TL_QUAD") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)0.5;
				TexMap[i].tu[1] = (float)0.0;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)0.5;
				TexMap[i].tv[2] = (float)0.5;
				TexMap[i].tu[3] = (float)0.5;
				TexMap[i].tv[3] = (float)0.0;
			}

			if (strcmp(p, "TR_QUAD") == 0)
			{
				TexMap[i].tu[0] = (float)0.5;
				TexMap[i].tv[0] = (float)0.5;
				TexMap[i].tu[1] = (float)0.5;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)1.0;
				TexMap[i].tv[2] = (float)0.5;
				TexMap[i].tu[3] = (float)1.0;
				TexMap[i].tv[3] = (float)0.0;
			}
			if (strcmp(p, "LL_QUAD") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)1.0;
				TexMap[i].tu[1] = (float)0.0;
				TexMap[i].tv[1] = (float)0.5;
				TexMap[i].tu[2] = (float)0.5;
				TexMap[i].tv[2] = (float)1.0;
				TexMap[i].tu[3] = (float)0.5;
				TexMap[i].tv[3] = (float)0.5;
			}
			if (strcmp(p, "LR_QUAD") == 0)
			{
				TexMap[i].tu[0] = (float)0.5;
				TexMap[i].tv[0] = (float)1.0;
				TexMap[i].tu[1] = (float)0.5;
				TexMap[i].tv[1] = (float)0.5;
				TexMap[i].tu[2] = (float)1.0;
				TexMap[i].tv[2] = (float)1.0;
				TexMap[i].tu[3] = (float)1.0;
				TexMap[i].tv[3] = (float)0.5;
			}
			if (strcmp(p, "TOP_HALF") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)0.5;
				TexMap[i].tu[1] = (float)0.0;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)1.0;
				TexMap[i].tv[2] = (float)0.5;
				TexMap[i].tu[3] = (float)1.0;
				TexMap[i].tv[3] = (float)0.0;
			}
			if (strcmp(p, "BOT_HALF") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)1.0;
				TexMap[i].tu[1] = (float)0.0;
				TexMap[i].tv[1] = (float)0.5;
				TexMap[i].tu[2] = (float)1.0;
				TexMap[i].tv[2] = (float)1.0;
				TexMap[i].tu[3] = (float)1.0;
				TexMap[i].tv[3] = (float)0.5;
			}
			if (strcmp(p, "LEFT_HALF") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)1.0;
				TexMap[i].tu[1] = (float)0.0;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)0.5;
				TexMap[i].tv[2] = (float)1.0;
				TexMap[i].tu[3] = (float)0.5;
				TexMap[i].tv[3] = (float)0.0;
			}
			if (strcmp(p, "RIGHT_HALF") == 0)
			{
				TexMap[i].tu[0] = (float)0.5;
				TexMap[i].tv[0] = (float)1.0;
				TexMap[i].tu[1] = (float)0.5;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)1.0;
				TexMap[i].tv[2] = (float)1.0;
				TexMap[i].tu[3] = (float)1.0;
				TexMap[i].tv[3] = (float)0.0;
			}
			if (strcmp(p, "TL_TRI") == 0)
			{
				TexMap[i].tu[0] = (float)0.0;
				TexMap[i].tv[0] = (float)0.0;
				TexMap[i].tu[1] = (float)1.0;
				TexMap[i].tv[1] = (float)0.0;
				TexMap[i].tu[2] = (float)0.0;
				TexMap[i].tv[2] = (float)1.0;
			}
			if (strcmp(p, "BR_TRI") == 0)
			{
			}

			fscanf_s(fp, "%s", &p, 256);
			strcpy_s((char*)TexMap[tex_alias_counter].material, 100, (char*)&p);

			tex_alias_counter++;
			found = TRUE;
		}

		if (strcmp(s, "END_FILE") == 0)
		{
			//PrintMessage(hwnd, "\n", NULL, LOGFILE_ONLY);
			number_of_tex_aliases = tex_alias_counter;
			found = TRUE;
			done = 1;
		}

		if (found == FALSE)
		{
			//PrintMessage(hwnd, "File Error: Syntax problem :", p, SCN_AND_FILE);
			//MessageBox(hwnd, "p", "File Error: Syntax problem ", MB_OK);
			//return FALSE;
		}
	}
	fclose(fp);

	SetTextureNormalMap();

	return TRUE;
}

void DungeonStompApp::SetTextureNormalMap() {

	char junk[255];

	for (int i = 0; i < number_of_tex_aliases; i++) {

		sprintf_s(junk, "%s_nm", TexMap[i].tex_alias_name);

		int normalmap = -1;

		for (int j = 0; j < number_of_tex_aliases; j++) {
			if (strstr(TexMap[j].tex_alias_name, "_nm") != 0) {

				if (strcmp(TexMap[j].tex_alias_name, junk) == 0) {
					TexMap[i].normalmaptextureid = j;
				}
			}
		}
	}
}

void DungeonStompApp::SetTextureNormalMapEmpty() {

	for (int i = 0; i < number_of_tex_aliases; i++) {
		TexMap[i].normalmaptextureid = -1;
	}
}

void DungeonStompApp::ProcessLights11()
{
	//P = pointlight, M = misslelight, C = sword light S = spotlight
	//12345678901234567890 
	//01234567890123456789
	//PPPPPPPPPPPMMMMCSSSS

	int sort[200];
	float dist[200];
	int obj[200];
	int temp;

	for (int i = 0; i < MaxLights; i++)
	{
		LightContainer[i].Strength = { 1.0f, 1.0f, 1.0f };
		LightContainer[i].FalloffStart = 80.0f;
		LightContainer[i].Direction = { 0.0f, -1.0f, 0.0f };
		LightContainer[i].FalloffEnd = 120.0f;
		LightContainer[i].Position = glm::vec3{ 0.0f,9000.0f,0.0f };
		LightContainer[i].SpotPower = 0.0f;
	}

	int dcount = 0;
	//Find lights
	for (int q = 0; q < oblist_length; q++)
	{
		int ob_type = oblist[q].type;
		float	qdist = FastDistance(m_vEyePt.x - oblist[q].x,
			m_vEyePt.y - oblist[q].y,
			m_vEyePt.z - oblist[q].z);
		//if (ob_type == 57)
		if (ob_type == 6 && oblist[q].light_source->command == 900)
			//if (ob_type == 6 && qdist < 2500 && oblist[q].light_source->command == 900)
		{
			dist[dcount] = qdist;
			sort[dcount] = dcount;
			obj[dcount] = q;
			dcount++;
		}
	}
	//sorting - ASCENDING ORDER
	for (int i = 0; i < dcount; i++)
	{
		for (int j = i + 1; j < dcount; j++)
		{
			if (dist[sort[i]] > dist[sort[j]])
			{
				temp = sort[i];
				sort[i] = sort[j];
				sort[j] = temp;
			}
		}
	}

	if (dcount > 16) {
		dcount = 16;
	}

	for (int i = 0; i < dcount; i++)
	{
		int q = obj[sort[i]];
		float dist2 = dist[sort[i]];

		int angle = (int)oblist[q].rot_angle;
		int ob_type = oblist[q].type;



		LightContainer[i].SpotPower = 0.0f;
		LightContainer[i].Strength = { 9.0f, 9.0f, 9.0f };
		LightContainer[i].Position = glm::vec3{ oblist[q].x,oblist[q].y + 50.0f, oblist[q].z };
	}

	int count = 0;

	for (int misslecount = 0; misslecount < MAX_MISSLE; misslecount++)
	{
		if (your_missle[misslecount].active == 1)
		{
			if (count < 4) {

				float r = MathHelper::RandF(10.0f, 100.0f);

				LightContainer[11 + count].Position = glm::vec3{ your_missle[misslecount].x, your_missle[misslecount].y, your_missle[misslecount].z };
				LightContainer[11 + count].Strength = glm::vec3{ 0.0f, 0.0f, 1.0f };
				LightContainer[11 + count].FalloffStart = 120.0f;
				LightContainer[11 + count].Direction = { 0.0f, -1.0f, 0.0f };
				LightContainer[11 + count].FalloffEnd = 170.0f;
				LightContainer[11 + count].SpotPower = 0.0f;


				if (your_missle[misslecount].model_id == 103) {
					LightContainer[11 + count].Strength = glm::vec3{ 0.0f, 4.0f, 3.843f };
				}
				else if (your_missle[misslecount].model_id == 104) {
					LightContainer[11 + count].Strength = glm::vec3{ 4.0f, 3.396f, 0.0f };
				}
				else if (your_missle[misslecount].model_id == 105) {
					LightContainer[11 + count].Strength = glm::vec3{ 3.91f, 4.0f, 0.0f };

				}
				count++;
			}
		}
	}

	bool flamesword = false;

	if (strstr(your_gun[current_gun].gunname, "FLAME") != NULL ||
		strstr(your_gun[current_gun].gunname, "ICE") != NULL ||
		strstr(your_gun[current_gun].gunname, "LIGHTNINGSWORD") != NULL)
	{
		flamesword = true;
	}

	if (flamesword) {

		int spot = 15;

		LightContainer[spot].Position = glm::vec3{ m_vEyePt.x, m_vEyePt.y, m_vEyePt.z };
		LightContainer[spot].Strength = glm::vec3{ 0.0f, 0.0f, 1.0f };
		LightContainer[spot].FalloffStart = 200.0f;
		LightContainer[spot].Direction = { 0.0f, -1.0f, 0.0f };
		LightContainer[spot].FalloffEnd = 300.0f;
		LightContainer[spot].SpotPower = 0.0f;

		if (strstr(your_gun[current_gun].gunname, "SUPERFLAME") != NULL) {
			LightContainer[spot].Strength = glm::vec3{ 7.0f, 0.867f, 0.0f };
		}
		else if (strstr(your_gun[current_gun].gunname, "FLAME") != NULL) {
			LightContainer[spot].Strength = glm::vec3{ 4.0f, 0.369f, 0.0f };
		}
		else if (strstr(your_gun[current_gun].gunname, "ICE") != NULL) {

			LightContainer[spot].Strength = glm::vec3{ 0.0f, 0.796f, 9.0f };
		}
		else if (strstr(your_gun[current_gun].gunname, "LIGHTNINGSWORD") != NULL) {
			LightContainer[spot].Strength = glm::vec3{ 5.0f, 5.0f, 5.0f };
		}
	}

	count = 0;
	dcount = 0;

	//Find lights SPOT
	for (int q = 0; q < oblist_length; q++)
	{
		int ob_type = oblist[q].type;
		float	qdist = FastDistance(m_vEyePt.x - oblist[q].x,
			m_vEyePt.y - oblist[q].y,
			m_vEyePt.z - oblist[q].z);
		//if (ob_type == 6)
		if (ob_type == 6 && qdist < 2500 && oblist[q].light_source->command == 1)
		{
			dist[dcount] = qdist;
			sort[dcount] = dcount;
			obj[dcount] = q;
			dcount++;
		}

	}

	//sorting - ASCENDING ORDER
	for (int i = 0; i < dcount; i++)
	{
		for (int j = i + 1; j < dcount; j++)
		{
			if (dist[sort[i]] > dist[sort[j]])
			{
				temp = sort[i];
				sort[i] = sort[j];
				sort[j] = temp;
			}
		}
	}

	if (dcount > 10) {
		dcount = 10;
	}

	for (int i = 0; i < dcount; i++)
	{
		int q = obj[sort[i]];
		float dist2 = dist[sort[i]];
		int angle = (int)oblist[q].rot_angle;
		int ob_type = oblist[q].type;
		//float adjust = 0.4f;
		float adjust = 0.0f;
		LightContainer[i + 16].Position = glm::vec3{ oblist[q].x,oblist[q].y + 0.0f, oblist[q].z };
		LightContainer[i + 16].Strength = glm::vec3{ (float)oblist[q].light_source->rcolour + adjust, (float)oblist[q].light_source->gcolour + adjust, (float)oblist[q].light_source->bcolour + adjust };
		LightContainer[i + 16].FalloffStart = 600.0f;
		LightContainer[i + 16].Direction = { oblist[q].light_source->direction_x, oblist[q].light_source->direction_y, oblist[q].light_source->direction_z };
		LightContainer[i + 16].FalloffEnd = 650.0f;
		LightContainer[i + 16].SpotPower = 1.9f;
	}
}


