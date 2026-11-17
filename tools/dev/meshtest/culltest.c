// Native reproduction of vkd3d-proton's mesh "culling" subtest on MoltenVK (no Wine).
// Uses the exact dxil-spirv SPIR-V: mesh shader reads an SRV through a buffer device address (push constants),
// pixel shader InterlockedOr's into a UAV through a buffer device address.
// Usage: culltest <mesh.spv> <frag.spv|-> ; env: CULL_GROUPS (default 4), CULL_RT (0/1, default 0), CREATE_ONLY
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(1); } } while (0)

static VkShaderModule module(VkDevice dev, const char* path) {
	FILE* f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
	fseek(f, 0, SEEK_END); size_t sz = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	uint32_t* code = malloc(sz); fread(code, 1, sz, f); fclose(f);
	VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sz, code };
	VkShaderModule m; CHECK(vkCreateShaderModule(dev, &ci, NULL, &m)); free(code); return m;
}

static uint32_t findMem(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want) {
	VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
	fprintf(stderr, "no memory type\n"); exit(1);
}

typedef struct { VkBuffer buf; VkDeviceMemory mem; VkDeviceAddress va; void* map; } Buf;

static Buf makeBuf(VkPhysicalDevice pd, VkDevice dev, VkDeviceSize size, VkBufferUsageFlags usage) {
	Buf b = {0};
	VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_SHARING_MODE_EXCLUSIVE };
	CHECK(vkCreateBuffer(dev, &bci, NULL, &b.buf));
	VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, b.buf, &mr);
	VkMemoryAllocateFlagsInfo fi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0 };
	VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &fi, mr.size,
		findMem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
	CHECK(vkAllocateMemory(dev, &mai, NULL, &b.mem)); CHECK(vkBindBufferMemory(dev, b.buf, b.mem, 0));
	CHECK(vkMapMemory(dev, b.mem, 0, size, 0, &b.map));
	VkBufferDeviceAddressInfo ai = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, b.buf };
	b.va = vkGetBufferDeviceAddress(dev, &ai);
	return b;
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: %s mesh.spv frag.spv|-\n", argv[0]); return 2; }
	const char* fragPath = strcmp(argv[2], "-") ? argv[2] : NULL;
	uint32_t groups = getenv("CULL_GROUPS") ? (uint32_t)atoi(getenv("CULL_GROUPS")) : 4;
	int useRT = getenv("CULL_RT") ? atoi(getenv("CULL_RT")) : 0;
	printf("config: groups=%u rt=%d frag=%s\n", groups, useRT, fragPath ? "yes" : "no");

	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "culltest", 1, NULL, 0, VK_API_VERSION_1_3 };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app };
	VkInstance inst; CHECK(vkCreateInstance(&ici, NULL, &inst));
	uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst, &n, &pd);

	// Enable everything the device supports (dxil-spirv needs BDA, Vulkan memory model, int64, etc.).
	VkPhysicalDeviceMeshShaderFeaturesEXT mesh = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	VkPhysicalDeviceVulkan13Features v13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &mesh };
	VkPhysicalDeviceVulkan12Features v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &v13 };
	VkPhysicalDeviceVulkan11Features v11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &v12 };
	VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v11 };
	vkGetPhysicalDeviceFeatures2(pd, &f2);
	mesh.multiviewMeshShader = VK_FALSE; mesh.primitiveFragmentShadingRateMeshShader = VK_FALSE; mesh.meshShaderQueries = VK_FALSE;
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
	const char* dexts[] = { VK_EXT_MESH_SHADER_EXTENSION_NAME, "VK_KHR_portability_subset" };
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qci, 0, NULL, 2, dexts, NULL };
	VkDevice dev; CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
	VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
	PFN_vkCmdDrawMeshTasksEXT drawMesh = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(dev, "vkCmdDrawMeshTasksEXT");

	static const uint32_t cull[4] = { 0xdeadbeef, 0x12345678, 0x00000000, 0xffffffff };
	Buf srv = makeBuf(pd, dev, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	Buf uav = makeBuf(pd, dev, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	memcpy(srv.map, cull, sizeof(cull)); memset(uav.map, 0, 256);

	// Optional 32x32 render target.
	VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM; VkImageView view = VK_NULL_HANDLE; VkImage img = VK_NULL_HANDLE;
	if (useRT) {
		VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, fmt, {32, 32, 1}, 1, 1,
			VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
		CHECK(vkCreateImage(dev, &imci, NULL, &img));
		VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, findMem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
		VkDeviceMemory im; CHECK(vkAllocateMemory(dev, &mai, NULL, &im)); CHECK(vkBindImageMemory(dev, img, im, 0));
		VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img, VK_IMAGE_VIEW_TYPE_2D, fmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
		CHECK(vkCreateImageView(dev, &ivci, NULL, &view));
	}

	// CULL_SETS=n adds n descriptor set layouts (one storage buffer each, VK_SHADER_STAGE_ALL) like vkd3d's
	// bindless layout, which moves push constants to a higher Metal buffer index. CULL_PC_ALL=1 uses STAGE_ALL.
	uint32_t nsets = getenv("CULL_SETS") ? (uint32_t)atoi(getenv("CULL_SETS")) : (getenv("CULL_VKD3D") ? 7 : 0);
	VkDescriptorSetLayout setLayouts[8];
	for (uint32_t i = 0; i < nsets && i < 8; i++) {
		VkDescriptorSetLayoutBinding lb = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, NULL };
		VkDescriptorSetLayoutCreateInfo slci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &lb };
		CHECK(vkCreateDescriptorSetLayout(dev, &slci, NULL, &setLayouts[i]));
	}
	VkShaderStageFlags pcStages = getenv("CULL_PC_ALL") ? VK_SHADER_STAGE_ALL : (VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT);
	VkPushConstantRange pcr = { pcStages, 0, getenv("CULL_VKD3D") ? 32u : 20u };
	VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, nsets, setLayouts, 1, &pcr };
	VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));
	VkPipelineShaderStageCreateInfo stages[2]; uint32_t sc = 0;
	stages[sc++] = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_MESH_BIT_EXT, module(dev, argv[1]), "main", NULL };
	if (fragPath) stages[sc++] = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module(dev, fragPath), "main", NULL };
	VkViewport vp = { 0, 0, 32, 32, 0, 1 }; VkRect2D sci = { {0, 0}, {32, 32} };
	VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, &vp, 1, &sci };
	VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState cba = { 0 }; cba.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, useRT ? 1u : 0u, &cba };
	VkPipelineRenderingCreateInfo rci = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, useRT ? 1u : 0u, &fmt };
	// VK3D_* toggles mirror vkd3d-proton's culling pipeline (all on with CULL_VKD3D=1).
	int all = getenv("CULL_VKD3D") != NULL;
	#define T(name) (all ? !getenv("NO_" name) : getenv(name) != NULL)
	VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineRasterizationDepthClipStateCreateInfoEXT dclip = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT, NULL, 0, VK_TRUE };
	VkSampleMask smask = 0xffffffff;
	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE };
	VkPipelineDynamicStateCreateInfo dsi = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 3, dyn };
	int useDyn = T("V_DYN");
	if (T("V_SUBGROUP")) stages[0].flags = VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
	if (T("V_CLAMP")) { rs.depthClampEnable = VK_TRUE; rs.pNext = &dclip; }
	if (T("V_MASK")) ms.pSampleMask = &smask;
	if (useDyn) { vps.viewportCount = 0; vps.pViewports = NULL; vps.scissorCount = 0; vps.pScissors = NULL; }
	printf("vkd3d-state: dyn=%d subgroup=%d clamp=%d mask=%d dss=%d twice=%d\n", useDyn, T("V_SUBGROUP"), T("V_CLAMP"), T("V_MASK"), T("V_DSS"), T("V_TWICE"));
	VkGraphicsPipelineCreateInfo gci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rci, 0, sc, stages, NULL, NULL, NULL, &vps, &rs, &ms,
		T("V_DSS") ? &dss : NULL, &cb, useDyn ? &dsi : NULL, layout };
	VkPipeline pipe; CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, NULL, &pipe));
	if (T("V_TWICE")) { VkPipeline pipe2; CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, NULL, &pipe2)); }
	printf("pipeline created\n"); fflush(stdout);
	if (getenv("CREATE_ONLY")) { printf("create-only: skipping GPU draw\n"); return 0; }

	VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
	VkCommandPool pool; CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
	VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
	if (getenv("CULL_COPY_PROBE")) {
		// Mirror the failing vkd3d submit: a plain buffer copy right after pipeline creation, no draw.
		VkBufferCopy rg = { 0, 0, 16 };
		VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		CHECK(vkBeginCommandBuffer(cmd, &bi));
		vkCmdCopyBuffer(cmd, srv.buf, uav.buf, 1, &rg);
		CHECK(vkEndCommandBuffer(cmd));
		VkSubmitInfo s0 = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL };
		CHECK(vkQueueSubmit(q, 1, &s0, VK_NULL_HANDLE));
		VkResult pr = vkQueueWaitIdle(q);
		printf("copy-probe: wait=%d uav[0]=%08x (expect deadbeef)\n", pr, ((uint32_t*)uav.map)[0]);
		memset(uav.map, 0, 256);
		CHECK(vkResetCommandPool(dev, pool, 0));
	}
	VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(cmd, &cbbi));
	if (useRT) {
		VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, img, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &b);
	}
	VkRenderingAttachmentInfo att = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0, NULL, 0, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {{{0, 0, 0, 0}}} };
	// CULL_HUGE=1: vkd3d-proton's no-attachment pass uses a 16384x16384 render area with 2048 layers.
	int huge = getenv("CULL_HUGE") && !useRT;
	uint32_t ext = getenv("CULL_EXT") ? (uint32_t)atoi(getenv("CULL_EXT")) : (huge ? 16384u : 32u);
	uint32_t lay = getenv("CULL_LAYERS") ? (uint32_t)atoi(getenv("CULL_LAYERS")) : (huge ? 2048u : 1u);
	VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, {{0, 0}, {ext, ext}}, lay, 0,
		useRT ? 1u : 0u, useRT ? &att : NULL, NULL, NULL };
	vkCmdBeginRendering(cmd, &ri);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
	if (useDyn) { vkCmdSetViewportWithCount(cmd, 1, &vp); vkCmdSetScissorWithCount(cmd, 1, &sci); vkCmdSetDepthWriteEnable(cmd, VK_FALSE); }
	if (getenv("CULL_BIND_SETS") && nsets) {
		// Bind real descriptor sets (each: one storage buffer = srv) like vkd3d's bindless sets, so MoltenVK binds
		// argument buffers to the Metal mesh/fragment stages.
		VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nsets };
		VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, nsets, 1, &ps };
		VkDescriptorPool dpool; CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
		VkDescriptorSet sets[8];
		VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, nsets, setLayouts };
		CHECK(vkAllocateDescriptorSets(dev, &dsai, sets));
		for (uint32_t i = 0; i < nsets; i++) {
			VkDescriptorBufferInfo bi = { srv.buf, 0, VK_WHOLE_SIZE };
			VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi, NULL };
			vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
		}
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, nsets, sets, 0, NULL);
		printf("bound %u descriptor sets\n", nsets);
	}
	struct { uint64_t srv, uav; uint32_t pad; } __attribute__((packed)) pc = { srv.va, uav.va, 0 };
	vkCmdPushConstants(cmd, layout, pcStages, 0, 20, &pc);
	drawMesh(cmd, groups, 1, 1);
	vkCmdEndRendering(cmd);
	CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL };
	CHECK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
	VkResult wr = vkQueueWaitIdle(q);
	uint32_t* r = (uint32_t*)uav.map;
	printf("wait=%d uav = %08x %08x %08x %08x  (expect %08x %08x %08x %08x for groups=4)\n", wr, r[0], r[1], r[2], r[3], cull[0], cull[1], cull[2], cull[3]);
	return 0;
}
