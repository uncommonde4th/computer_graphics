#include "application.hpp"
#include "graphics_internal.hpp"
#include "vulkan/vulkan_core.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <imgui.h>

namespace application {

struct Vertex {
  float position[3];
  float color[3];
};

struct PushConstants {
  float position[4];
  float rotation[4];
  float scale[4];
  float tint[4];
  float camera[4];
};

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

bool perspective;
float fovDeg;
float orthoSize;
float cameraDistance;

float position[3];
float rotationDeg[3];
float scale[3];
float tint[3];

bool playing;
float animTime;
float animSpeed;
float orbitRadius;
float verticalAmplitude;
float spinDegPerSec[3];

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkShaderModule vertexShader = VK_NULL_HANDLE;
VkShaderModule fragmentShader = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexBufferAllocation = nullptr;
VkBuffer indexBuffer = VK_NULL_HANDLE;
VmaAllocation indexBufferAllocation = nullptr;

VkShaderModule loadShaderModule(const char path[]) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return VK_NULL_HANDLE;
  }

  const size_t size = file.tellg();

  std::vector<uint32_t> buffer(size / sizeof(uint32_t));

  file.seekg(0);
  file.read(reinterpret_cast<char *>(buffer.data()), size);
  file.close();

  VkShaderModuleCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = size,
      .pCode = buffer.data(),
  };

  VkShaderModule result = VK_NULL_HANDLE;
  vkCreateShaderModule(graphics::internal::context.device, &info, nullptr,
                       &result);
  return result;
}

bool createBuffer(const void *data, size_t size, VkBufferUsageFlags usage,
                  VkBuffer &buffer, VmaAllocation &allocation) {
  auto &context = graphics::internal::context;

  VkBufferCreateInfo bufferInfo {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo allocationInfo {
    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO,
  };

  if (vmaCreateBuffer(context.allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
    return false;
  }

  void* mapped = nullptr;
  if (vmaMapMemory(context.allocator, allocation, &mapped) != VK_SUCCESS) {
    return false;
  }
  memcpy(mapped, data, size);
  vmaFlushAllocation(context.allocator, allocation, 0, VK_WHOLE_SIZE);
  vmaUnmapMemory(context.allocator, allocation);
  return true;
}

void generateBoxGeometry() {
	vertices.clear();
	indices.clear();
 
	vertices.reserve(8 + 2);
	indices.reserve(4 * 12);
 
	constexpr uint32_t corners = 4;
	constexpr float halfX = 0.75f;
	constexpr float halfY = 0.5f;
	constexpr float halfZ = 0.4f;
 
	for (uint32_t i = 0; i < corners; ++i)
	{
		const float x = (i == 1 || i == 2) ? halfX : -halfX;
		const float z = (i >= 2) ? halfZ : -halfZ;
 
		vertices.push_back({
			.position = {x, -halfY, z},
			.color = {0.6f + 0.4f * x / halfX, 0.2f, 0.6f + 0.4f * z / halfZ},
		});
 
		vertices.push_back({
			.position = {x, halfY, z},
			.color = {0.6f + 0.4f * x / halfX, 1.0f, 0.6f + 0.4f * z / halfZ},
		});
	}
 
	vertices.push_back({
		.position = {0.0f, -halfY, 0.0f},
		.color = {0.6f, 0.2f, 0.6f},
	});
 
	vertices.push_back({
		.position = {0.0f, halfY, 0.0f},
		.color = {0.6f, 1.0f, 0.6f},
	});
 
	const uint32_t bottomCenter = corners * 2;
	const uint32_t topCenter = bottomCenter + 1;
 
	for (uint32_t i = 0; i < corners; ++i)
	{
		const uint32_t next = (i + 1) % corners;
 
		const uint32_t bottom = i * 2;
		const uint32_t top = bottom + 1;
 
		const uint32_t nextBottom = next * 2;
		const uint32_t nextTop = nextBottom + 1;
 
		indices.insert(indices.end(), {
			bottom, top, nextBottom,
			nextBottom, top, nextTop,
			bottomCenter, bottom, nextBottom,
			topCenter, nextTop, top,
		});
	}
}

bool initialize() { 
  auto& context = graphics::internal::context;

  perspective = true;
  fovDeg = 60.0f;
  orthoSize = 3.0f;
  cameraDistance = 7.0f;

  position[0] = position[1] = position[2] = 0.0f;
  rotationDeg[0] = 20.0f;
  rotationDeg[1] = 30.0f;
  rotationDeg[2] = 0.0f;
  scale[0] = scale[1] = scale[2] = 1.0f;
  tint[0] = tint[1] = tint[2] = 1.0f;

  playing = true;
  animTime = 0.0f;
  animSpeed = 1.0f;
  orbitRadius = 2.0f;
  verticalAmplitude = 0.8f;
  spinDegPerSec[0] = 30.0f;
  spinDegPerSec[1] = 45.0f;
  spinDegPerSec[2] = 15.0f;

  generateBoxGeometry();

  if (!createBuffer(vertices.data(), vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexBufferAllocation)) {
    std::cerr << "Не удалось создать vertex buffer\n";
    return false;
  }

  if (!createBuffer(indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexBufferAllocation)) {
    std::cerr << "Не удалось создать index buffer\n";
    return false;
  }

  VkPushConstantRange pushRange {
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .offset = 0,
    .size = sizeof(PushConstants),
  };

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;

  if (vkCreatePipelineLayout(context.device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    std::cerr << "Не удалось создать pipeline layout \n";
    return false;
  }

  vertexShader = loadShaderModule("shaders/basic.vert.spv");
  fragmentShader = loadShaderModule("shaders/basic.frag.spv");

  if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
    std::cerr << "Не удалось загрузить шейдеры \n";
    return false;
  }

  VkPipelineShaderStageCreateInfo shaderStages[2]{};

  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertexShader;
  shaderStages[0].pName = "main";

  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragmentShader;
  shaderStages[1].pName = "main";

  VkVertexInputBindingDescription vertexBinding {
    .binding = 0,
    .stride = sizeof(Vertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };

  VkVertexInputAttributeDescription vertexAttributes[2]{
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
	};


	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
	vertexInputInfo.vertexAttributeDescriptionCount = 2;
	vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;
 
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE;
	rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterInfo.lineWidth = 1.0f;

	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_TRUE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineMultisampleStateCreateInfo sampleInfo{};
	sampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	sampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorAttachment{};
	colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorAttachment.blendEnable = VK_FALSE;
 
	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = &colorAttachment;

  const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
 
	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineInfo.pViewportState = &viewportInfo;
	pipelineInfo.pRasterizationState = &rasterInfo;
	pipelineInfo.pDepthStencilState = &depthInfo;
	pipelineInfo.pMultisampleState = &sampleInfo;
	pipelineInfo.pColorBlendState = &blendInfo;
	pipelineInfo.pDynamicState = &dynamicInfo;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = context.render_pass;
	pipelineInfo.subpass = 0;
 
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
		std::cerr << "Не удалось создать графический pipeline \n";
		return false;
	}
 
	return true;

}


void shutdown() {
  auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
 
	vmaDestroyBuffer(context.allocator, indexBuffer, indexBufferAllocation);
	vmaDestroyBuffer(context.allocator, vertexBuffer, vertexBufferAllocation);
 
	vkDestroyPipeline(context.device, graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
 
	vkDestroyShaderModule(context.device, vertexShader, nullptr);
	vkDestroyShaderModule(context.device, fragmentShader, nullptr);

}

void update([[maybe_unused]] double time) {
  	if (playing) {
		animTime += ImGui::GetIO().DeltaTime * animSpeed;
	}
 
	ImGui::Begin("Parallelepiped");
 
	// Проекция
	ImGui::Text("Projection");
	if (ImGui::RadioButton("Perspective", perspective)) perspective = true;
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !perspective)) perspective = false;
	if (perspective) {
		ImGui::SliderFloat("FOV (deg)", &fovDeg, 20.0f, 120.0f);
	} else {
		ImGui::SliderFloat("Ortho size", &orthoSize, 0.5f, 10.0f);
	}
	ImGui::SliderFloat("Camera distance", &cameraDistance, 2.0f, 30.0f);
 
	// Трансформация
	ImGui::Separator();
	ImGui::DragFloat3("Position", position, 0.01f);
	ImGui::DragFloat3("Rotation (deg)", rotationDeg, 0.5f);
	ImGui::DragFloat3("Scale", scale, 0.01f, 0.1f, 5.0f);
 
	// Анимация
	ImGui::Separator();
	if (ImGui::Button(playing ? "Pause" : "Play")) playing = !playing;
	ImGui::SliderFloat("Speed", &animSpeed, 0.0f, 5.0f);
	ImGui::SliderFloat("Orbit radius", &orbitRadius, 0.0f, 5.0f);
	ImGui::SliderFloat("Vertical amplitude", &verticalAmplitude, 0.0f, 3.0f);
	ImGui::SliderFloat3("Spin (deg/s)", spinDegPerSec, -180.0f, 180.0f);
 
	// Цвет
	ImGui::Separator();
	ImGui::ColorEdit3("Tint", tint);
 
	ImGui::End();

}

void render(const graphics::internal::FrameData &fd) {
  
	// Очистка и заполнение черным
	auto& context = graphics::internal::context;
 
	vkResetCommandBuffer(fd.command_buffer, 0);
 
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
 
	vkBeginCommandBuffer(fd.command_buffer, &beginInfo);
 
	VkClearValue clearValues[2]{};
	clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}};
	clearValues[1].depthStencil = {1.0f, 0};
 
	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = context.render_pass;
	renderPassInfo.framebuffer = fd.framebuffer;
	renderPassInfo.renderArea.extent = context.swapchain_extent;
	renderPassInfo.clearValueCount = 2;
	renderPassInfo.pClearValues = clearValues;
 
	// Main
 
	vkCmdBeginRenderPass(fd.command_buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
 
	const float width = float(context.swapchain_extent.width);
	const float height = float(context.swapchain_extent.height);
 
	const VkViewport viewport{
		.x = 0, .y = 0,
		.width = width,
		.height = height,
		.minDepth = 0.0f, .maxDepth = 1.0f
	};
 
	const VkRect2D scissor{
		.extent = context.swapchain_extent,
	};
 
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
 
	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
 
	constexpr float degToRad = 3.14159265f / 180.0f;
 
	// Траектория: окружность в XZ + колебания по Y (частота вдвое выше) + постоянное вращение
	PushConstants pc{};
 
	pc.position[0] = position[0] + orbitRadius * std::cos(animTime);
	pc.position[1] = position[1] + verticalAmplitude * std::sin(2.0f * animTime);
	pc.position[2] = position[2] + orbitRadius * std::sin(animTime);
 
	for (int i = 0; i < 3; ++i) {
		pc.rotation[i] = (rotationDeg[i] + spinDegPerSec[i] * animTime) * degToRad;
		pc.scale[i] = scale[i];
		pc.tint[i] = tint[i];
	}
 
	pc.camera[0] = perspective ? 1.0f : 0.0f;
	pc.camera[1] = perspective ? fovDeg * degToRad : orthoSize;
	pc.camera[2] = cameraDistance;
	pc.camera[3] = width / height;
 
	vkCmdPushConstants(fd.command_buffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
 
	const VkDeviceSize vertexBufferOffset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer, &vertexBufferOffset);
	vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
 
	vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
 
	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application