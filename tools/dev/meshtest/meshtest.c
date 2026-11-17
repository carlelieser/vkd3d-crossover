// Native mesh-pipeline harness for MoltenVK (no Wine). Creates a mesh (+optional task, +optional
// fragment) pipeline from SPIR-V files, renders one draw into an offscreen image, and reads back pixel (0,0).
// Usage: meshtest <mesh.spv> [task.spv|-] [frag.spv|-] [groupsX]
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(1); } } while (0)

static uint32_t* load(const char* path, size_t* size) {
	FILE* f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
	fseek(f, 0, SEEK_END); *size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	uint32_t* d = malloc(*size); fread(d, 1, *size, f); fclose(f);
	return d;
}

static VkShaderModule module(VkDevice dev, const char* path) {
	size_t sz; uint32_t* code = load(path, &sz);
	VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sz, code };
	VkShaderModule m; CHECK(vkCreateShaderModule(dev, &ci, NULL, &m));
	free(code);
	return m;
}

static uint32_t findMem(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want) {
	VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
	fprintf(stderr, "no memory type\n"); exit(1);
}

int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "usage: %s mesh.spv [task.spv|-] [frag.spv|-] [groupsX]\n", argv[0]); return 2; }
	const char* meshPath = argv[1];
	const char* taskPath = argc > 2 && strcmp(argv[2], "-") ? argv[2] : NULL;
	const char* fragPath = argc > 3 && strcmp(argv[3], "-") ? argv[3] : NULL;
	uint32_t groups = argc > 4 ? (uint32_t)atoi(argv[4]) : 1;

	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "meshtest", 1, NULL, 0, VK_API_VERSION_1_3 };
	const char* iexts[] = { VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, iexts };
	VkInstance inst; CHECK(vkCreateInstance(&ici, NULL, &inst));
	uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst, &n, &pd);

	VkPhysicalDeviceMeshShaderFeaturesEXT mesh = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	VkPhysicalDeviceVulkan13Features v13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &mesh };
	VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v13 };
	vkGetPhysicalDeviceFeatures2(pd, &f2);
	printf("taskShader=%u meshShader=%u dynamicRendering=%u\n", mesh.taskShader, mesh.meshShader, v13.dynamicRendering);

	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
	const char* dexts[] = { VK_EXT_MESH_SHADER_EXTENSION_NAME, "VK_KHR_portability_subset" };
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qci, 0, NULL, 2, dexts, NULL };
	VkDevice dev; CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
	VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
	PFN_vkCmdDrawMeshTasksEXT drawMesh = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(dev, "vkCmdDrawMeshTasksEXT");
	if (!drawMesh) { fprintf(stderr, "vkCmdDrawMeshTasksEXT missing\n"); return 1; }

	// 4x4 RGBA8 render target + readback buffer.
	VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
	VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, fmt, {4, 4, 1}, 1, 1,
		VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
	VkImage img; CHECK(vkCreateImage(dev, &imci, NULL, &img));
	VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
	VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, findMem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
	VkDeviceMemory imem; CHECK(vkAllocateMemory(dev, &mai, NULL, &imem)); CHECK(vkBindImageMemory(dev, img, imem, 0));
	VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img, VK_IMAGE_VIEW_TYPE_2D, fmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
	VkImageView view; CHECK(vkCreateImageView(dev, &ivci, NULL, &view));
	VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE };
	VkBuffer buf; CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
	vkGetBufferMemoryRequirements(dev, buf, &mr);
	mai.allocationSize = mr.size; mai.memoryTypeIndex = findMem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory bmem; CHECK(vkAllocateMemory(dev, &mai, NULL, &bmem)); CHECK(vkBindBufferMemory(dev, buf, bmem, 0));

	// Pipeline.
	VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));
	VkPipelineShaderStageCreateInfo stages[3]; uint32_t sc = 0;
	if (taskPath) stages[sc++] = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_TASK_BIT_EXT, module(dev, taskPath), "main", NULL };
	stages[sc++] = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_MESH_BIT_EXT, module(dev, meshPath), "main", NULL };
	if (fragPath) stages[sc++] = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module(dev, fragPath), "main", NULL };
	VkViewport vp = { 0, 0, 4, 4, 0, 1 }; VkRect2D sci = { {0, 0}, {4, 4} };
	VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, &vp, 1, &sci };
	VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState cba = { 0 }; cba.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, 1, &cba };
	VkPipelineRenderingCreateInfo rci = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &fmt };
	VkGraphicsPipelineCreateInfo gci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rci, 0, sc, stages, NULL, NULL, NULL, &vps, &rs, &ms, NULL, &cb, NULL, layout };
	VkPipeline pipe;
	printf("creating pipeline (%u stages)...\n", sc); fflush(stdout);
	CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, NULL, &pipe));
	printf("pipeline created\n"); fflush(stdout);
	if (getenv("MESHTEST_CREATE_ONLY")) { printf("create-only: skipping GPU draw\n"); return 0; }

	// Record: clear to black, draw, copy to buffer.
	VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0 };
	VkCommandPool pool; CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
	VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
	VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(cmd, &cbbi));
	VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, img, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &b);
	VkRenderingAttachmentInfo att = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0, NULL, 0, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {{{0, 0, 0, 0}}} };
	VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, {{0, 0}, {4, 4}}, 1, 0, 1, &att, NULL, NULL };
	vkCmdBeginRendering(cmd, &ri);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
	drawMesh(cmd, groups, 1, 1);
	vkCmdEndRendering(cmd);
	b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
	VkBufferImageCopy reg = { 0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {4, 4, 1} };
	vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &reg);
	CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL };
	CHECK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(q));
	uint8_t* px; CHECK(vkMapMemory(dev, bmem, 0, 64, 0, (void**)&px));
	printf("pixel(0,0) = %u %u %u %u   pixel(3,3) = %u %u %u %u\n", px[0], px[1], px[2], px[3], px[60], px[61], px[62], px[63]);
	return 0;
}
