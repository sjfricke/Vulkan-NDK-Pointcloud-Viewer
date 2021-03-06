/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "VulkanDevice.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>

#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

#define MAX_WEIGHTS 8

namespace vkglTF
{
	/*
		glTF texture loading class
	*/
	struct Texture {
		vks::VulkanDevice *device;
		VkImage image;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;

		void updateDescriptor()
		{
			descriptor.sampler = sampler;
			descriptor.imageView = view;
			descriptor.imageLayout = imageLayout;
		}

		void destroy()
		{
			vkDestroyImageView(device->logicalDevice, view, nullptr);
			vkDestroyImage(device->logicalDevice, image, nullptr);
			vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
			vkDestroySampler(device->logicalDevice, sampler, nullptr);
		}

		/*
			Load a texture from a glTF image (stored as vector of chars loaded via stb_image)
			Also generates the mip chain as glTF images are stored as jpg or png without any mips
		*/
		void fromglTfImage(tinygltf::Image &gltfimage, vks::VulkanDevice *device, VkQueue copyQueue)
		{
			this->device = device;

			unsigned char* buffer = nullptr;
			VkDeviceSize bufferSize = 0;
			bool deleteBuffer = false;
			if (gltfimage.component == 3) {
				// Most devices don't support RGB only on Vulkan so convert if necessary
				// TODO: Check actual format support and transform only if required
				bufferSize = gltfimage.width * gltfimage.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &gltfimage.image[0];
				for (size_t i = 0; i< gltfimage.width * gltfimage.height; ++i) {
					for (int32_t j = 0; j < 3; ++j) {
						rgba[j] = rgb[j];
					}
					rgba += 4;
					rgb += 3;
				}
				deleteBuffer = true;
			}
			else {
				buffer = &gltfimage.image[0];
				bufferSize = gltfimage.image.size();
			}

			VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

			VkFormatProperties formatProperties;

			width = gltfimage.width;
			height = gltfimage.height;
			mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

			vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);
			assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
			assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements memReqs{};

			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo{};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = bufferSize;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
			vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, buffer, bufferSize);
			vkUnmapMemory(device->logicalDevice, stagingMemory);

			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
			vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

			VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageExtent.depth = 1;

			vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			device->flushCommandBuffer(copyCmd, copyQueue, true);

			vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

			// Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
			VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			for (uint32_t i = 1; i < mipLevels; i++) {
				VkImageBlit imageBlit{};

				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
				imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
				imageBlit.srcOffsets[1].z = 1;

				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstOffsets[1].x = int32_t(width >> i);
				imageBlit.dstOffsets[1].y = int32_t(height >> i);
				imageBlit.dstOffsets[1].z = 1;

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = 0;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.image = image;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}

				vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.image = image;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}
			}

			subresourceRange.levelCount = mipLevels;
			imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			device->flushCommandBuffer(blitCmd, copyQueue, true);

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			samplerInfo.maxAnisotropy = 1.0;
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxLod = (float)mipLevels;
			samplerInfo.maxAnisotropy = 8.0f;
			samplerInfo.anisotropyEnable = VK_TRUE;
			VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.layerCount = 1;
			viewInfo.subresourceRange.levelCount = mipLevels;
			VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

			descriptor.sampler = sampler;
			descriptor.imageView = view;
			descriptor.imageLayout = imageLayout;
		}
	};

	/*
		glTF material class
	*/
	struct Material {
		enum AlphaMode{ ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		vkglTF::Texture *baseColorTexture;
		vkglTF::Texture *metallicRoughnessTexture;
		vkglTF::Texture *normalTexture;
		vkglTF::Texture *occlusionTexture;
		vkglTF::Texture *emissiveTexture;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	/*
		glTF primitive class
	*/
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		Material &material;
	};

	struct MorphPushConst{
		uint32_t bufferOffset;
		uint32_t normalOffset;
		uint32_t tangentOffset;
		uint32_t vertexStride;
		float    weights[MAX_WEIGHTS];
	};

	/*
		glTF Mesh class
	*/
	struct Mesh {
		enum MorphInterpolation {LINEAR, STEP, CUBICSPLINE};
		bool isMorphTarget;
		size_t  sampler;
		size_t  input;
		size_t  output;
		MorphInterpolation interpolation;
		std::vector<float> weightsInit;
		std::vector<float> weightsTime;
		std::vector<float> weightsData;
		uint32_t morphVertexOffset;
		MorphPushConst morphPushConst;

		std::vector<Primitive> primitives;

		// for keeping state of mesh's animation
		uint32_t currentIndex = 0;
	};

	/*
		glTF model loading and rendering class
	*/
	struct Model {

		struct Vertex {
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec3 tangent;
		};

		struct Vertices {
			VkBuffer buffer{VK_NULL_HANDLE};
			VkDeviceMemory memory;
		};

		struct Indices {
			uint32_t count;
			VkBuffer buffer{VK_NULL_HANDLE};
			VkDeviceMemory memory;
		};

		Vertices verticesMorph;
		Indices indicesMorph;
		Vertices verticesNormal;
		Indices indicesNormal;

		std::vector<Mesh> meshesMorph;
		std::vector<Mesh> meshesNormal;
		std::vector<Texture> textures;
		std::vector<Material> materials;

		// In order [POS_0, POS_1... NORMAL_0, NORMAL_1... TANGENT_0, TANGENT_1..]
		std::vector<float> morphVertexData; // TODO clear after device transfer
		float animationMaxTime = 0.0f;
		float currentTime = 0.0f;

		void destroy(VkDevice device)
		{
			if (verticesMorph.buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, verticesMorph.buffer, nullptr);
				vkFreeMemory(device, verticesMorph.memory, nullptr);
			}
			if (indicesMorph.buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, indicesMorph.buffer, nullptr);
				vkFreeMemory(device, indicesMorph.memory, nullptr);
			}
			if (verticesNormal.buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, verticesNormal.buffer, nullptr);
				vkFreeMemory(device, verticesNormal.memory, nullptr);
			}
			if (indicesNormal.buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, indicesNormal.buffer, nullptr);
				vkFreeMemory(device, indicesNormal.memory, nullptr);
			}
			for (auto texture : textures) {
				texture.destroy();
			}
		};

		void loadNode(const tinygltf::Node &node, size_t nodeIndex, const glm::mat4 &parentMatrix, const tinygltf::Model &model,
					  std::vector<Vertex>& vertexBufferMorph, std::vector<uint32_t>& indexBufferMorph,
					  std::vector<Vertex>& vertexBufferNormal, std::vector<uint32_t >& indexBufferNormal,
					  float globalscale)
		{

			// Generate local node matrix
			glm::vec3 translation = glm::vec3(0.0f);
			if (node.translation.size() == 3) {
				translation = glm::make_vec3(node.translation.data());
			}
			glm::mat4 rotation = glm::mat4(1.0f);
			if (node.rotation.size() == 4) {
				glm::quat q = glm::make_quat(node.rotation.data());
				rotation = glm::mat4(q);
			}
			glm::vec3 scale = glm::vec3(1.0f);
			if (node.scale.size() == 3) {
				scale = glm::make_vec3(node.scale.data());
			}
			glm::mat4 localNodeTRSMatrix;
			glm::mat4 localNodeRSMatrix; // need only rotate/scale for morph changes
			if (node.matrix.size() == 16) {
				localNodeTRSMatrix = glm::make_mat4x4(node.matrix.data());
				localNodeRSMatrix = glm::make_mat4x4(node.matrix.data());
			} else {
				// T * R * S
				localNodeTRSMatrix = glm::translate(glm::mat4(1.0f), translation) * rotation * glm::scale(glm::mat4(1.0f), scale);
				localNodeRSMatrix = glm::mat4(1.0f) * rotation * glm::scale(glm::mat4(1.0f), scale);
			}
			localNodeTRSMatrix = parentMatrix * localNodeTRSMatrix;
			// TODO send in RS Matrix from Parent

			// Parent node with children
			// TODO support children testing
			if (node.children.size() > 0) {
				for (auto i = 0; i < node.children.size(); i++) {
					loadNode(model.nodes[node.children[i]], node.children[i], localNodeTRSMatrix, model, vertexBufferMorph, indexBufferMorph, vertexBufferNormal, indexBufferNormal, globalscale);
				}
			}

			if (node.mesh < 0) {
				return; // non mesh node
			}

			// Node contains mesh data
			const tinygltf::Mesh mesh = model.meshes[node.mesh];

			// determine if the mesh is morph or not
			if (mesh.weights.empty()) {
				meshesNormal.push_back(Mesh{}); // normal meshes
			} else {
				meshesMorph.push_back(Mesh{}); // morph meshes
			}
			Mesh &pMesh = (mesh.weights.empty()) ? meshesNormal.back() : meshesMorph.back();
			pMesh.isMorphTarget = mesh.weights.empty() ? false : true;

			if (pMesh.isMorphTarget) {
				// find glTF sampler to node's mesh
				bool foundSampler = false;
				for (auto& animation : model.animations) {
					for (auto& channel : animation.channels) {
						if (channel.target_node == nodeIndex &&	channel.target_path == "weights") {
							pMesh.sampler = channel.sampler;
							pMesh.input = animation.samplers[pMesh.sampler].input;
							pMesh.output = animation.samplers[pMesh.sampler].output;
							if (animation.samplers[pMesh.sampler].interpolation == "STEP") {
								pMesh.interpolation = Mesh::STEP;
							} else if (animation.samplers[pMesh.sampler].interpolation == "CUBICSPLINE") {
								pMesh.interpolation = Mesh::CUBICSPLINE;
							} else { // LINEAR as default from glTF spec
								pMesh.interpolation = Mesh::LINEAR;
							}

							foundSampler = true;
							break;
						}
					}
					if (foundSampler) { break; }
				}

				// set init weights of mesh
				for (size_t i = 0; i < mesh.weights.size() && i < MAX_WEIGHTS; i++) {
					pMesh.weightsInit.push_back(static_cast<float>(mesh.weights[i]));
				}

				if (!foundSampler) {
					// No animation assigned to the mesh morph target weights.

					// Just for safety
					pMesh.weightsTime.clear();
					pMesh.weightsData.clear();
				} else {

					// get weight input (times)
					const tinygltf::Accessor &inputAccessor = model.accessors[pMesh.input];
					const tinygltf::BufferView &inputView = model.bufferViews[inputAccessor.bufferView];
					const float* weightTimeBuffer = reinterpret_cast<const float *>(&(model.buffers[inputView.buffer].data[inputAccessor.byteOffset + inputView.byteOffset]));
					pMesh.weightsTime.resize(inputAccessor.count);

					// We need to copy morph weight data for CPU to calculate during looping
					// Also trying to avoid C memcpy for safty and true C++ container use
					for (size_t i = 0; i < pMesh.weightsTime.size(); i++) {
						pMesh.weightsTime[i] = weightTimeBuffer[i];
					}

					// looking for animation time in whole model
					animationMaxTime = std::max(animationMaxTime, pMesh.weightsTime.back());

					// now the output (weight data)
					const tinygltf::Accessor &outputAccessor = model.accessors[pMesh.output];
					const tinygltf::BufferView &outputView = model.bufferViews[outputAccessor.bufferView];
					const float* weightDataBuffer = reinterpret_cast<const float *>(&(model.buffers[outputView.buffer].data[outputAccessor.byteOffset + outputView.byteOffset]));
					pMesh.weightsData.resize(outputAccessor.count);

					for (size_t i = 0; i < pMesh.weightsData.size(); i++) {
						pMesh.weightsData[i] = weightDataBuffer[i];
					}
				}

			} else {
				// Non-morph targets

				// zero out push constants for shaders to skip over
				pMesh.morphPushConst.bufferOffset = 0;
				pMesh.morphPushConst.normalOffset = 0;
				pMesh.morphPushConst.tangentOffset = 0;
				pMesh.morphPushConst.vertexStride = 0;
			}

			for (auto& primitive : mesh.primitives) {

				if (primitive.indices < 0) {
					continue;
				}

				pMesh.primitives.push_back(vkglTF::Primitive{
					.firstIndex = (pMesh.isMorphTarget) ? static_cast<uint32_t>(indexBufferMorph.size()) : static_cast<uint32_t>(indexBufferNormal.size()),
					.indexCount = 0,
					.material = materials[primitive.material],
				});
				Primitive &pPrimitive = pMesh.primitives.back();

				uint32_t vertexStart = (pMesh.isMorphTarget) ? static_cast<uint32_t>(vertexBufferMorph.size()) : static_cast<uint32_t>(vertexBufferNormal.size());
				pMesh.morphVertexOffset = vertexStart * sizeof(Vertex);

				// Vertices
				{
					const float *bufferPos = nullptr;
					const float *bufferNormals = nullptr;
					const float *bufferTexCoords = nullptr;

					// Position attribute is required
					assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

					const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
					bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));

					if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
						const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
						bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
					}

					if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
						const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoords = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
					}

					if (pMesh.isMorphTarget) {
						std::vector<const float*> morphBuffer;
						uint32_t morphVertexCount = 0;
						// loop for each type to pack data given for morphVertexData
						for (size_t t = 0; t < primitive.targets.size(); t++) {
							if(primitive.targets[t].find("POSITION") != primitive.targets[t].end()) {
								const tinygltf::Accessor &posWeightAccessor = model.accessors[primitive.targets[t].find("POSITION")->second];
								const tinygltf::BufferView &posWeightView = model.bufferViews[posWeightAccessor.bufferView];
								morphBuffer.push_back(reinterpret_cast<const float*>(&(model.buffers[posWeightView.buffer].data[posWeightAccessor.byteOffset + posWeightView.byteOffset])));
								morphVertexCount = posWeightAccessor.count; // TODO https://github.com/KhronosGroup/glTF/issues/1339
							}
						}

						pMesh.morphPushConst.normalOffset = static_cast<uint32_t>(morphBuffer.size());
						for (size_t t = 0; t < primitive.targets.size(); t++) {
							if(primitive.targets[t].find("NORMAL") != primitive.targets[t].end()) {
								const tinygltf::Accessor &normalWeightAccessor = model.accessors[primitive.targets[t].find("NORMAL")->second];
								const tinygltf::BufferView &normalWeightView = model.bufferViews[normalWeightAccessor.bufferView];
								morphBuffer.push_back(reinterpret_cast<const float*>(&(model.buffers[normalWeightView.buffer].data[normalWeightAccessor.byteOffset + normalWeightView.byteOffset])));
							}
						}

						pMesh.morphPushConst.tangentOffset = static_cast<uint32_t>(morphBuffer.size());
						for (size_t t = 0; t < primitive.targets.size(); t++) {
							if(primitive.targets[t].find("TANGENT") != primitive.targets[t].end()) {
								const tinygltf::Accessor &tangentWeightAccessor = model.accessors[primitive.targets[t].find("TANGENT")->second];
								const tinygltf::BufferView &tangentWeightView = model.bufferViews[tangentWeightAccessor.bufferView];
								morphBuffer.push_back(reinterpret_cast<const float*>(&(model.buffers[tangentWeightView.buffer].data[tangentWeightAccessor.byteOffset + tangentWeightView.byteOffset])));
							}
						}

						pMesh.morphPushConst.vertexStride = static_cast<uint32_t>(morphBuffer.size());
						pMesh.morphPushConst.bufferOffset = static_cast<uint32_t>(morphVertexData.size());

						// Pack data in VAO style
						// Can assume all vec3 from spec
						for (size_t i = 0; i < morphVertexCount; i++) {
							// Position data inserted first
							for (size_t j = 0; j <  morphBuffer.size(); j++) {
								glm::vec3 temp = localNodeRSMatrix * glm::vec4(glm::make_vec3(&(morphBuffer[j])[i * 3]), 1.0f);

								if (j < pMesh.morphPushConst.normalOffset) {
									// only position get global scaled up
									temp *= globalscale;
								} else if (temp.x != 0 || temp.y != 0 ||  temp.z != 0) { // glm::normalize() causes "nan" TODO figure that out
									// need to normalize normal/tangent vectors
									temp = glm::normalize(temp);
								}
								temp.y *= -1.0f;
								morphVertexData.push_back(temp.x);
								morphVertexData.push_back(temp.y);
								morphVertexData.push_back(temp.z);
							}
						}
					}

					for (size_t v = 0; v < posAccessor.count; v++) {
						Vertex vert{};
						vert.pos = localNodeTRSMatrix * glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
						vert.pos *= globalscale;

						// glm::normalize() causes "nan" TODO figure that out
						vert.normal = glm::normalize(glm::mat3(localNodeTRSMatrix) * glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));

						//vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
						vert.tangent = glm::vec3(0.0f);

						// Vulkan coordinate system
						vert.pos.y *= -1.0f;
						vert.normal.y *= -1.0f;

						if (pMesh.isMorphTarget) {
							vertexBufferMorph.push_back(vert);
						} else {
							vertexBufferNormal.push_back(vert);
						}
					}
				}

				// Indices
				{
					const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
					const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
					const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

					pPrimitive.indexCount = static_cast<uint32_t>(accessor.count);

					// each morph has own gl_VertexIndex start at 0 so index is at zero_
					switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						uint32_t *buf = new uint32_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
						for (size_t index = 0; index < accessor.count; index++) {
							if (pMesh.isMorphTarget) {
								indexBufferMorph.push_back(buf[index]);
							} else {
								indexBufferNormal.push_back(buf[index] + vertexStart);
							}
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						uint16_t *buf = new uint16_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
						for (size_t index = 0; index < accessor.count; index++) {
							if (pMesh.isMorphTarget) {
								indexBufferMorph.push_back(buf[index]);
							} else {
								indexBufferNormal.push_back(buf[index] + vertexStart);
							}
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						uint8_t *buf = new uint8_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
						for (size_t index = 0; index < accessor.count; index++) {
							if (pMesh.isMorphTarget) {
								indexBufferMorph.push_back(buf[index]);
							} else {
								indexBufferNormal.push_back(buf[index] + vertexStart);
							}
						}
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}
			}
		}

		void loadImages(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue)
		{
			for (tinygltf::Image &image : gltfModel.images) {
				vkglTF::Texture texture;
				texture.fromglTfImage(image, device, transferQueue);
				textures.push_back(texture);
			}
		}

		void loadMaterials(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue)
		{
			for (tinygltf::Material &mat : gltfModel.materials) {
				vkglTF::Material material{};
				if (mat.values.find("baseColorTexture") != mat.values.end()) {
					material.baseColorTexture = &textures[gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source];
				}
				if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
					material.metallicRoughnessTexture = &textures[gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source];
				}
				if (mat.values.find("roughnessFactor") != mat.values.end()) {
					material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
				}
				if (mat.values.find("metallicFactor") != mat.values.end()) {
					material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
				}
				if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
					material.normalTexture = &textures[gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source];
				}
				if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
					material.emissiveTexture = &textures[gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source];
				}
				if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
					material.occlusionTexture = &textures[gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source];
				}
				if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
					tinygltf::Parameter param = mat.additionalValues["alphaMode"];
					if (param.string_value == "BLEND") {
						material.alphaMode = Material::ALPHAMODE_BLEND;
					}
					if (param.string_value == "MASK") {
						material.alphaMode = Material::ALPHAMODE_MASK;
					}
				}
				if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
					material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
				}
				materials.push_back(material);
			}
		}

		void loadFromFile(std::string filename, vks::VulkanDevice *device, VkQueue transferQueue, float scale = 1.0f)
		{
			tinygltf::Model gltfModel;
			tinygltf::TinyGLTF gltfContext;
			std::string error;

#if defined(__ANDROID__)
			AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);
			char* fileData = new char[size];
			AAsset_read(asset, fileData, size);
			AAsset_close(asset);
			std::string baseDir;
			bool fileLoaded = gltfContext.LoadASCIIFromString(&gltfModel, &error, fileData, size, baseDir);
			free(fileData);
#else
			bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, filename.c_str());
#endif
			// TODO better placement so not sending in 4 vectors to loadNode()
			std::vector<Vertex> vertexBufferMorph;
			std::vector<uint32_t> indexBufferMorph;
			std::vector<Vertex> vertexBufferNormal;
			std::vector<uint32_t> indexBufferNormal;

			if (fileLoaded) {
			//	loadImages(gltfModel, device, transferQueue);
			//	loadMaterials(gltfModel, device, transferQueue);
				const tinygltf::Scene &scene = gltfModel.scenes[gltfModel.defaultScene];
				for (size_t i = 0; i < scene.nodes.size(); i++) {
					const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
					loadNode(node, scene.nodes[i],  glm::mat4(1.0f), gltfModel, vertexBufferMorph, indexBufferMorph, vertexBufferNormal, indexBufferNormal, scale);
				}
			}
			else {
				// TODO: throw
				std::cerr << "Could not load gltf file: " << error << std::endl;
				exit(-1);
			}

			size_t vertexBufferSizeMorph = vertexBufferMorph.size() * sizeof(Vertex);
			size_t indexBufferSizeMorph = indexBufferMorph.size() * sizeof(uint32_t);
			indicesMorph.count = static_cast<uint32_t>(indexBufferMorph.size());

			size_t vertexBufferSizeNormal = vertexBufferNormal.size() * sizeof(Vertex);
			size_t indexBufferSizeNormal = indexBufferNormal.size() * sizeof(uint32_t);
			indicesNormal.count = static_cast<uint32_t>(indexBufferNormal.size());

			struct StagingBuffer {
				VkBuffer buffer;
				VkDeviceMemory memory;
			} vertexStagingMorph, indexStagingMorph, vertexStagingNormal, indexStagingNormal;


			// TODO this is ugly, but just for testing
			if ((vertexBufferSizeMorph > 0) && (indexBufferSizeMorph > 0)) {
				// Create staging buffers
				// Vertex data Morph
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					vertexBufferSizeMorph,
					&vertexStagingMorph.buffer,
					&vertexStagingMorph.memory,
					vertexBufferMorph.data()));

				// Index data Morph
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					indexBufferSizeMorph,
					&indexStagingMorph.buffer,
					&indexStagingMorph.memory,
					indexBufferMorph.data()));

				// Create device local buffers
				// Vertex buffer Morph
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					vertexBufferSizeMorph,
					&verticesMorph.buffer,
					&verticesMorph.memory));

				// Index buffer Morph
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					indexBufferSizeMorph,
					&indicesMorph.buffer,
					&indicesMorph.memory))

				// Copy from staging buffers
				VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

				VkBufferCopy copyRegion = {};

				copyRegion.size = vertexBufferSizeMorph;
				vkCmdCopyBuffer(copyCmd, vertexStagingMorph.buffer, verticesMorph.buffer, 1, &copyRegion);

				copyRegion.size = indexBufferSizeMorph;
				vkCmdCopyBuffer(copyCmd, indexStagingMorph.buffer, indicesMorph.buffer, 1, &copyRegion);

				device->flushCommandBuffer(copyCmd, transferQueue, true); // TODO Need to free compyCmd?

				vkDestroyBuffer(device->logicalDevice, vertexStagingMorph.buffer, nullptr);
				vkFreeMemory(device->logicalDevice, vertexStagingMorph.memory, nullptr);
				vkDestroyBuffer(device->logicalDevice, indexStagingMorph.buffer, nullptr);
				vkFreeMemory(device->logicalDevice, indexStagingMorph.memory, nullptr);
			}

			// TODO have one buffer allocated and make Normal and Morph buffers adjacent
			if ((vertexBufferSizeNormal > 0) && (indexBufferSizeNormal > 0)) {

				// Vertex data Normal
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					vertexBufferSizeNormal,
					&vertexStagingNormal.buffer,
					&vertexStagingNormal.memory,
					vertexBufferNormal.data()));

				// Index data Normal
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					indexBufferSizeNormal,
					&indexStagingNormal.buffer,
					&indexStagingNormal.memory,
					indexBufferNormal.data()));

				// Vertex buffer Normal
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					vertexBufferSizeNormal,
					&verticesNormal.buffer,
					&verticesNormal.memory));

				// Index buffer Normal
				VK_CHECK_RESULT(device->createBuffer(
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					indexBufferSizeNormal,
					&indicesNormal.buffer,
					&indicesNormal.memory));

				// Normal copy
				VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

				VkBufferCopy copyRegion = {};

				copyRegion.size = vertexBufferSizeNormal;
				vkCmdCopyBuffer(copyCmd, vertexStagingNormal.buffer, verticesNormal.buffer, 1, &copyRegion);

				copyRegion.size = indexBufferSizeNormal;
				vkCmdCopyBuffer(copyCmd, indexStagingNormal.buffer, indicesNormal.buffer, 1, &copyRegion);

				device->flushCommandBuffer(copyCmd, transferQueue, true);

				vkDestroyBuffer(device->logicalDevice, vertexStagingNormal.buffer, nullptr);
				vkFreeMemory(device->logicalDevice, vertexStagingNormal.memory, nullptr);
				vkDestroyBuffer(device->logicalDevice, indexStagingNormal.buffer, nullptr);
				vkFreeMemory(device->logicalDevice, indexStagingNormal.memory, nullptr);
			}
		}

		void drawMorph(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
		{
			// TODO have a static and full draw call
			for (auto mesh : meshesMorph) {
				// need offset since index buffer will be zero'ed for each mesh
				const VkDeviceSize offsets[1] = {mesh.morphVertexOffset};
				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vkglTF::Mesh::morphPushConst), &mesh.morphPushConst);
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &verticesMorph.buffer, offsets);
				vkCmdBindIndexBuffer(commandBuffer, indicesMorph.buffer, 0, VK_INDEX_TYPE_UINT32);
				for (auto primitive : mesh.primitives) {
					vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
				}
			}
		}

		void drawNormal(VkCommandBuffer commandBuffer)
		{
			for (auto mesh : meshesNormal) {
				const VkDeviceSize offsets[1] = {0};
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &verticesNormal.buffer, offsets);
				vkCmdBindIndexBuffer(commandBuffer, indicesNormal.buffer, 0, VK_INDEX_TYPE_UINT32);
				for (auto primitive : mesh.primitives) {
					vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
				}
			}
		}
	};
}
