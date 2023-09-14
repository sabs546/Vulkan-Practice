#define VK_USE_PLATFORM_WIN32_KHR
#define no_init_all

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <vector>
#include <iostream>

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>
#include <lodepng.h>
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <fstream>

struct SVertexData
{
	float position[3];
	float color[3];
	float padding[2];
};

SVertexData particle_vertices[3] =
{
	{ {  0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
	{ {  0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -5.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 1.0f } }
};

struct SBuffer
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
	unsigned int buffer_size;
	VkBufferUsageFlags usage;
	VkSharingMode sharing_mode;
	VkMemoryPropertyFlags buffer_memory_properties;
	void* mapped_buffer_memory = nullptr;
};

struct STexture
{
	SBuffer transfer_buffer;
	unsigned int width;
	unsigned int height;
	VkFormat format;

	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkImageLayout layout;
	VkSampler sampler;
};

uint32_t indices[3] = {
	0,1,2
};

VkInstance instance = VK_NULL_HANDLE;

uint32_t queue_family_index = 0;
VkPhysicalDevice physical_device = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue graphics_queue = VK_NULL_HANDLE;
VkQueue present_queue = VK_NULL_HANDLE;

VkCommandPool command_pool = VK_NULL_HANDLE;
VkBuffer buffer = VK_NULL_HANDLE;
VkDeviceMemory buffer_memory = VK_NULL_HANDLE;

VkDeviceSize particle_vertex_buffer_size = sizeof(SVertexData) * 3;
VkDeviceSize index_buffer_size = sizeof(uint32_t) * 3;

VkPhysicalDeviceProperties chosen_physical_device_properties;
VkPhysicalDeviceFeatures chosen_physical_device_features;
VkPhysicalDeviceMemoryProperties chosen_physical_device_mem_properties;

VkDescriptorPool descriptor_pool;            // Allocation Pool
VkDescriptorSet descriptor_set;
VkDescriptorSetLayout descriptor_set_layout; // Describes the type of data that can be bound using the descriptor tool

const uint32_t physical_device_extension_count = 1;
const char* physical_device_extensions[physical_device_extension_count] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME /* Platform specificity */ };

bool window_open;
SDL_Window* window;
SDL_SysWMinfo window_info;
uint32_t screen_width;
uint32_t screen_height;
VkSurfaceKHR surface;

uint32_t current_frame_index;
std::unique_ptr<VkFence> fences;
VkSemaphore image_available_semaphore;
VkSemaphore render_finished_semaphore;

VkSurfaceFormatKHR surface_format;
VkPresentModeKHR present_mode;

VkSwapchainKHR swapchain;
uint32_t swapchain_image_count;
std::unique_ptr<VkImage> swapchain_images;
std::unique_ptr<VkImageView> swapchain_image_views;

struct FrameBufferAttachment
{
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkFormat format;
	VkSampler sampler;
};

struct VulkanAttachments
{
	FrameBufferAttachment color, depth;
};

const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

VkRenderPass renderpass = VK_NULL_HANDLE;
std::unique_ptr<VkFramebuffer> framebuffers = nullptr;
std::unique_ptr<VulkanAttachments> framebuffer_attachments = nullptr;

VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
VkPresentInfoKHR present_info = {};
VkSubmitInfo render_submission_info = {};
std::unique_ptr<VkCommandBuffer> graphics_command_buffers = nullptr;

VkPipeline graphics_pipeline = VK_NULL_HANDLE;
VkPipelineLayout graphics_pipeline_layout = VK_NULL_HANDLE;
VkShaderModule vertex_shader_module = VK_NULL_HANDLE;
VkShaderModule fragment_shader_module = VK_NULL_HANDLE;

VkBuffer particle_vertex_buffer = VK_NULL_HANDLE;
VkDeviceMemory particle_vertex_buffer_memory = VK_NULL_HANDLE;
void* particle_vertex_mapped_buffer_memory = nullptr;

VkBuffer index_buffer = VK_NULL_HANDLE;
VkDeviceMemory index_buffer_memory = VK_NULL_HANDLE;
void* index_mapped_buffer_memory = nullptr;

VkDescriptorPool texture_descriptor_pool;
VkDescriptorSetLayout texture_descriptor_set_layout;
VkDescriptorSet texture_descriptor_set;
STexture square_texture;

bool CheckLayersSupport(const char** layers, int count)
{
	// Find out how many layers are available on the system
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	// Using the count, tell the system how many layer definitions we want to read
	// These layer properties are the layers that are available on the system
	std::unique_ptr<VkLayerProperties[]> layerProperties(new VkLayerProperties[layerCount]());
	vkEnumerateInstanceLayerProperties(&layerCount, layerProperties.get());

	// Loop through for each layer we want to check
	for (int i = 0; i < count; ++i)
	{
		bool layerFound = false;
		// Loop through for each avaliable system layer and atempt to find our required layer
		for (int j = 0; j < layerCount; ++j)
		{
			// Check to see if the layer matches
			if (strcmp(layers[i], layerProperties[j].layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}
		// If we are missing the required layer, report back
		if (!layerFound)
		{
			return false;
		}
	}
	// Found all the layers
	return true;
}

bool HasRequiredExtensions(const VkPhysicalDevice& physical_device, const char** required_extensions, uint32_t extension_count)
{
	uint32_t device_extension_count = 0;
	vkEnumerateDeviceExtensionProperties(
		physical_device,
		nullptr,
		&device_extension_count,
		nullptr
	);

	VkExtensionProperties* extensions = new VkExtensionProperties[device_extension_count];
	vkEnumerateDeviceExtensionProperties(
		physical_device,
		nullptr,
		&device_extension_count,
		extensions
	);

	for (uint32_t i = 0; i < extension_count; ++i)
	{
		bool extension_found = false;
		for (uint32_t j = 0; j < device_extension_count; ++j)
		{
			if (strcmp(required_extensions[i], extensions[j].extensionName) == 0)
			{
				extension_found = true;
				break;
			}
		}

		if (!extension_found)
		{
			delete[] extensions;
			return false;
		}
	}

	delete[] extensions;
	return true;
}

bool GetQueueFamily(const VkPhysicalDevice& physical_device, VkQueueFlags flags, uint32_t& queue_family_index, VkSurfaceKHR surface)
{
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(
		physical_device,
		&queue_family_count,
		nullptr
	);

	VkQueueFamilyProperties* queue_families = new VkQueueFamilyProperties[queue_family_count];
	vkGetPhysicalDeviceQueueFamilyProperties(
		physical_device,
		&queue_family_count,
		queue_families
	);

	for (uint32_t i = 0; i < queue_family_count; ++i)
	{
		if (queue_families[i].queueCount > 0)
		{
			if ((queue_families[i].queueFlags & flags) == flags)
			{
				if (surface == VK_NULL_HANDLE)
				{
					queue_family_index = i;
					return true;
				}

				VkBool32 present_support = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(
					physical_device,
					i,
					surface,
					&present_support
				);

				if (present_support)
				{
					queue_family_index = i;
					return true;
				}
			}
		}
	}

	return false;
}

bool CheckSwapchainSupport(VkSurfaceCapabilitiesKHR& capabilities, VkSurfaceFormatKHR*& surface_formats, uint32_t& surface_format_count,
	VkPresentModeKHR*& present_modes, uint32_t& present_mode_count)
{
	VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physical_device,
		surface,
		&capabilities
	);

	assert(result == VK_SUCCESS);

	result = vkGetPhysicalDeviceSurfaceFormatsKHR(
		physical_device,
		surface,
		&surface_format_count,
		nullptr
	);

	assert(result == VK_SUCCESS);

	surface_formats = new VkSurfaceFormatKHR[surface_format_count];

	result = vkGetPhysicalDeviceSurfaceFormatsKHR(
		physical_device,
		surface,
		&surface_format_count,
		surface_formats
	);

	assert(result == VK_SUCCESS);

	// Same but for present mode
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(
		physical_device,
		surface,
		&present_mode_count,
		nullptr
	);

	assert(result == VK_SUCCESS);

	present_modes = new VkPresentModeKHR[present_mode_count];

	result = vkGetPhysicalDeviceSurfacePresentModesKHR(
		physical_device,
		surface,
		&present_mode_count,
		present_modes
	);

	assert(result == VK_SUCCESS);

	return true;
}

VkSurfaceFormatKHR ChooseSwapchainSurfaceFormat(const VkSurfaceFormatKHR* formats, const uint32_t& format_count)
{
	if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	}

	for (int i = 0; i < format_count; i++)
	{
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			return formats[i];
		}
	}

	if (format_count > 0)
	{
		return formats[0];
	}

	return { VK_FORMAT_UNDEFINED };
}

VkPresentModeKHR ChooseSwapchainPresentMode(const VkPresentModeKHR* modes, const uint32_t& mode_count)
{
	for (int i = 0; i < mode_count; i++)
	{
		if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			return modes[i];
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseSwapchainExtent(VkSurfaceCapabilitiesKHR& capabilities)
{
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}
	else
	{
		VkExtent2D extent = { screen_width, screen_height };
		if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
		if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;

		if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
		if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;

		return extent;
	}
}

uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& physical_device_mem_properties, uint32_t type_filter, VkMemoryPropertyFlags properties)
{
	for (uint32_t i = 0; i < physical_device_mem_properties.memoryTypeCount; i++)
	{
		if (type_filter & (1 << i) && (physical_device_mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	return -1;
}

VkFormat FindSupportedFormat(VkPhysicalDevice physical_device, const VkFormat* candidate_formats,
							 const uint32_t candidate_format_count, VkImageTiling tiling, VkFormatFeatureFlags features)
{
	for (int i = 0; i < candidate_format_count; i++)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physical_device, candidate_formats[i], &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
		{
			return candidate_formats[i];
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
		{
			return candidate_formats[i];
		}

		assert(0 && "All formats are not supported");
		return VK_FORMAT_UNDEFINED;
	}
}

VkMemoryAllocateInfo MemoryAllocateInfo(VkDeviceSize size, uint32_t memory_type)
{
	VkMemoryAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.allocationSize = size;
	info.memoryTypeIndex = memory_type;
	return info;
}

void CreateImage(const VkDevice& device, const VkPhysicalDeviceMemoryProperties& physical_device_mem_properties, uint32_t width, uint32_t height, VkFormat format,
				 VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& image_memory, VkImageLayout initialLayout)
{
	VkImageCreateInfo image_create_info = {};
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.extent.width = width;
	image_create_info.extent.height = height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.format = format;
	image_create_info.tiling = tiling;
	image_create_info.initialLayout = initialLayout;
	image_create_info.usage = usage;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult create_image_info_result = vkCreateImage(
		device,
		&image_create_info,
		nullptr,
		&image
	);

	assert(create_image_info_result == VK_SUCCESS);

	VkMemoryRequirements mem_requirements;
	vkGetImageMemoryRequirements(
		device,
		image,
		&mem_requirements
	);

	uint32_t memory_type = FindMemoryType(
		physical_device_mem_properties,
		mem_requirements.memoryTypeBits,
		properties
	);

	VkMemoryAllocateInfo memory_allocate_info = MemoryAllocateInfo(
		mem_requirements.size,
		memory_type
	);

	VkResult allocate_result = vkAllocateMemory(
		device,
		&memory_allocate_info,
		nullptr,
		&image_memory
	);

	assert(allocate_result == VK_SUCCESS);

	VkResult bind_result = vkBindImageMemory(
		device,
		image,
		image_memory,
		0
	);

	assert(bind_result == VK_SUCCESS);
}

void SetImageLayout(VkCommandBuffer cmdbuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange)
{
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

	VkImageMemoryBarrier imageMemoryBarrier{};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	imageMemoryBarrier.oldLayout = oldImageLayout;
	imageMemoryBarrier.newLayout = newImageLayout;
	imageMemoryBarrier.image = image;
	imageMemoryBarrier.subresourceRange = subresourceRange;

	if (oldImageLayout == VK_IMAGE_LAYOUT_UNDEFINED && newImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
}

VkCommandBuffer BeginSingleTimeCommands(const VkCommandPool& command_pool)
{
	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer;
	VkResult allocate_command_buffer_result = vkAllocateCommandBuffers(
		device,
		&alloc_info,
		&command_buffer
	);
	assert(allocate_command_buffer_result == VK_SUCCESS);

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = nullptr;
	VkResult begin_command_buffer = vkBeginCommandBuffer(
		command_buffer,
		&begin_info
	);
	assert(begin_command_buffer == VK_SUCCESS);

	return command_buffer;
}

void EndSingleTimeCommands(VkCommandBuffer command_buffer, VkCommandPool command_pool)
{
	vkEndCommandBuffer(command_buffer);
	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	VkResult queue_submit_result = vkQueueSubmit(
		graphics_queue,
		1,
		&submit_info,
		VK_NULL_HANDLE
	);
	assert(queue_submit_result == VK_SUCCESS);

	vkQueueWaitIdle(
		graphics_queue
	);

	//vkFreeCommandBuffers(
	//	device,
	//	command_pool,
	//	1,
	//	&command_buffer
	//);
}

VkDescriptorPool CreateDescriptorPool(const VkDevice &device, VkDescriptorPoolSize* pool_sizes, uint32_t pool_sizes_count, uint32_t max_sets)
{
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	VkDescriptorPoolCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	create_info.pPoolSizes = pool_sizes;
	create_info.maxSets = max_sets;

	VkResult create_descriptor_pool = vkCreateDescriptorPool(
		device,
		&create_info,
		nullptr,
		&descriptor_pool
	);
	assert(create_descriptor_pool == VK_SUCCESS);

	return descriptor_pool;
}

VkDescriptorSetLayout CreateDescriptorSetLayout(const VkDevice &device, VkDescriptorSetLayoutBinding* layout_bindings, uint32_t layout_binding_count)
{
	VkDescriptorSetLayoutCreateInfo layout_info = {};
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.pBindings = layout_bindings;
	layout_info.bindingCount = layout_binding_count;

	VkResult create_descriptor_set_layout = vkCreateDescriptorSetLayout(
		device,
		&layout_info,
		nullptr,
		&descriptor_set_layout
	);
	assert(create_descriptor_set_layout == VK_SUCCESS);

	return descriptor_set_layout;
}

VkDescriptorSet AllocateDescriptorSet(const VkDevice &device, const VkDescriptorPool &descriptor_pool, VkDescriptorSet &descriptor_set, const VkDescriptorSetLayout &descriptor_set_layout, uint32_t count)
{
	VkDescriptorSetAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = descriptor_pool;
	alloc_info.descriptorSetCount = count;
	alloc_info.pSetLayouts = &descriptor_set_layout;

	VkResult create_descriptor_set = vkAllocateDescriptorSets(
		device,
		&alloc_info,
		&descriptor_set
	);
	assert(create_descriptor_set == VK_SUCCESS);

	return descriptor_set;
}

bool HasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkImageMemoryBarrier ImageMemoryBarrier(VkImage& image, VkFormat& format, VkImageLayout& old_layout, VkImageLayout& new_layout)
{
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.image = image;
	if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	{
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (format != VK_FORMAT_UNDEFINED && HasStencilComponent(format))
		{
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else
	{
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	if (old_layout == VK_IMAGE_LAYOUT_PREINITIALIZED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_GENERAL)
	{
		barrier.srcAccessMask = 0;
	}

	return barrier;
}

void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, VkImageSubresourceRange subresourceRange)
{
	// Create a new single time command
	VkCommandBuffer command_buffer = BeginSingleTimeCommands(command_pool);

	// Define how we will convert between the image layouts
	VkImageMemoryBarrier barrier = ImageMemoryBarrier(image, format, old_layout, new_layout);

	barrier.subresourceRange = subresourceRange;
	// Submit the barrier update
	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&barrier
	);
	// Submit the command to the GPU
	EndSingleTimeCommands(command_buffer, command_pool);
}

void CreateAttachmentImages(VkFormat format, VkImageUsageFlags usage, FrameBufferAttachment & attachment)
{
	attachment.format = format;

	VkImageAspectFlags aspectMask = 0;
	VkImageLayout imageLayout;

	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
	{
		aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
	{
		aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	CreateImage(
		device,
		chosen_physical_device_mem_properties,
		screen_width,
		screen_height,
		format,
		VK_IMAGE_TILING_OPTIMAL,
		usage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		attachment.image,
		attachment.memory,
		VK_IMAGE_LAYOUT_UNDEFINED
	);

	{
		VkImageViewCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		create_info.image = attachment.image;
		create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		create_info.format = format;
		create_info.subresourceRange = {};
		create_info.subresourceRange.aspectMask = aspectMask;
		create_info.subresourceRange.baseMipLevel = 0;
		create_info.subresourceRange.levelCount = 1;
		create_info.subresourceRange.baseArrayLayer = 0;
		create_info.subresourceRange.layerCount = 1;

		VkResult create_image_view_result = vkCreateImageView(
			device,
			&create_info,
			nullptr,
			&attachment.view
		);

		assert(create_image_view_result == VK_SUCCESS);
	}

	{
		VkSamplerCreateInfo sampler_info{};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.maxAnisotropy = 1.0f;
		sampler_info.magFilter = VK_FILTER_LINEAR;
		sampler_info.minFilter = VK_FILTER_LINEAR;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.compareOp = VK_COMPARE_OP_NEVER;
		sampler_info.minLod = 0.0f;
		sampler_info.maxLod = (float)1;

		if (chosen_physical_device_features.samplerAnisotropy)
		{
			sampler_info.maxAnisotropy = chosen_physical_device_properties.limits.maxSamplerAnisotropy;
			sampler_info.anisotropyEnable = VK_TRUE;
		}
		else
		{
			sampler_info.maxAnisotropy = 1.0f;
			sampler_info.anisotropyEnable = VK_FALSE;
		}

		sampler_info.borderColor - VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VkResult create_sampler_result = vkCreateSampler(
			device,
			&sampler_info,
			nullptr,
			&attachment.sampler
		);

		assert(create_sampler_result == VK_SUCCESS);

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			TransitionImageLayout(attachment.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		}
	}
}

void CreateImageSampler(const VkImage &image, VkFormat format, VkImageView &imageView, VkSampler &sampler)
{
	VkSamplerCreateInfo sampler_info = VkSamplerCreateInfo();
	vkCreateSampler(
		device,
		&sampler_info,
		nullptr,
		&sampler
	);

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.components.r = VK_COMPONENT_SWIZZLE_R;
	view_info.components.g = VK_COMPONENT_SWIZZLE_G;
	view_info.components.b = VK_COMPONENT_SWIZZLE_B;
	view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	view_info.subresourceRange.levelCount = 1;

	vkCreateImageView(
		device,
		&view_info,
		nullptr,
		&imageView
	);
}

void BuildCommandBuffers(std::unique_ptr<VkCommandBuffer> &command_buffers, const uint32_t buffer_count)
{
	VkCommandBufferBeginInfo command_buffer_begin_info = {};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	command_buffer_begin_info.pInheritanceInfo = nullptr;

	float clear_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	VkClearValue clear_values[3]{};

	std::copy(std::begin(clear_color), std::end(clear_color), std::begin(clear_values[0].color.float32));
	std::copy(std::begin(clear_color), std::end(clear_color), std::begin(clear_values[1].color.float32));
	clear_values[2].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_info.renderPass = renderpass;
	render_pass_info.renderArea.offset = { 0, 0 };
	render_pass_info.renderArea.extent = { screen_width, screen_height };
	render_pass_info.clearValueCount = 3;
	render_pass_info.pClearValues = clear_values;

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = screen_width;
	viewport.height = screen_height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.extent.width = screen_width;
	scissor.extent.height = screen_height;
	scissor.offset.x = 0;
	scissor.offset.y = 0;

	for (unsigned int i = 0; i < buffer_count; i++)
	{
		VkResult reset_command_buffer_result = vkResetCommandBuffer(
			command_buffers.get()[i],
			0
		);
		assert(reset_command_buffer_result == VK_SUCCESS);

		render_pass_info.framebuffer = framebuffers.get()[i];
		VkResult begin_command_buffer_result = vkBeginCommandBuffer(
			command_buffers.get()[i],
			&command_buffer_begin_info
		);
		assert(begin_command_buffer_result == VK_SUCCESS);

		vkCmdBeginRenderPass(
			command_buffers.get()[i],
			&render_pass_info,
			VK_SUBPASS_CONTENTS_INLINE
		);
		vkCmdSetViewport(
			command_buffers.get()[i],
			0,
			1,
			&viewport
		);
		vkCmdSetScissor(
			command_buffers.get()[i],
			0,
			1,
			&scissor
		);

		vkCmdEndRenderPass(
			command_buffers.get()[i]
		);

		VkResult end_command_buffer_result = vkEndCommandBuffer(
			command_buffers.get()[i]
		);
		assert(end_command_buffer_result == VK_SUCCESS);
	}
}

void ReadShaderFile(const char* filename, char* &data, unsigned int &size)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		throw std::runtime_error("failed to open file!");
	}
	size = file.tellg();
	data = new char[size];
	file.seekg(0);
	file.read(data, size);
	file.close();
}

void WindowSetup(const char* title, int width, int height)
{
	window = SDL_CreateWindow(
		title,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width, height,
		SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
	);

	SDL_ShowWindow(window);

	screen_width = width;
	screen_height = height;

	SDL_VERSION(&window_info.version);

	bool created = SDL_GetWindowWMInfo(window, &window_info);
	HWND hwnd = window_info.info.win.window; // Continue here, you can't find window info for some reason
	assert(created && "Error, unable to get window info");
}

void PollWindow()
{
	SDL_Event event;
	bool rebuild = false;
	while (SDL_PollEvent(&event) > 0)
	{
		switch (event.type)
		{
		case SDL_QUIT:
			window_open = false;
			break;
		}
	}
}

void DestroyWindow()
{
	SDL_DestroyWindow(window);
}

void CreateSurface()
{
	auto CreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");

	VkWin32SurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd = window_info.info.win.window;
	createInfo.hinstance = GetModuleHandle(nullptr);

	if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
	{
		throw std::runtime_error("Failed to create VK surface");
	}
}

void Setup()
{
	const uint32_t extension_count = 3;
	const uint32_t layer_count = 1;
	const char* instance_layers[layer_count] = { "VK_LAYER_KHRONOS_validation" };
	const char* instance_extensions[extension_count] = { "VK_EXT_debug_report", VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

	assert(CheckLayersSupport(instance_layers, 1) && "Unsupported Layers Found");

	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Instance";
	app_info.pEngineName = "My engine";
	app_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	app_info.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 204);

	VkInstanceCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.enabledExtensionCount = extension_count;
	create_info.ppEnabledExtensionNames = instance_extensions;
	create_info.enabledLayerCount = 1;
	create_info.ppEnabledLayerNames = instance_layers;

	VkResult result = vkCreateInstance(
		&create_info,
		NULL,
		&instance
	);

	assert(result == VK_SUCCESS);

	CreateSurface();
}

void GetPhysicalDevice()
{
	uint32_t device_count = 0;                                      // Display device count
	vkEnumeratePhysicalDevices(                                     // This will count the physical devices available
		instance,
		&device_count,
		nullptr
	);

	VkPhysicalDevice* devices = new VkPhysicalDevice[device_count]; // This array holds device info
	vkEnumeratePhysicalDevices(                                     // This will populate said array
		instance,
		&device_count,
		devices                                                     // This time the field is populated
	);

	VkPhysicalDevice chosen_device = VK_NULL_HANDLE;
	uint32_t chosen_queue_family_index = 0;

	for (uint32_t i = 0; i < device_count; ++i)
	{
		if (HasRequiredExtensions(devices[i], physical_device_extensions, physical_device_extension_count))
		{
			if (GetQueueFamily(devices[i], VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT, chosen_queue_family_index, surface))
			{
				VkPhysicalDeviceProperties physical_device_properties;
				vkGetPhysicalDeviceProperties(
					devices[i],
					&physical_device_properties
				);

				VkPhysicalDeviceFeatures physical_device_features;
				vkGetPhysicalDeviceFeatures(
					devices[i],
					&physical_device_features
				);

				VkPhysicalDeviceMemoryProperties physical_device_mem_properties;
				vkGetPhysicalDeviceMemoryProperties(
					devices[i],
					&physical_device_mem_properties
				);

				if (chosen_device == VK_NULL_HANDLE || physical_device_properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{ // Not our favourite choice, maybe it found an iGPU first time round
					chosen_device = devices[i];
					chosen_physical_device_properties = physical_device_properties;
					chosen_physical_device_features = physical_device_features;
					chosen_physical_device_mem_properties = physical_device_mem_properties;
					queue_family_index = chosen_queue_family_index;
				}
			}
		}
	}

	assert(chosen_device != VK_NULL_HANDLE);
	physical_device = chosen_device;
}

void CreateDevice()
{
	static const float queue_priority = 1.0f;
	VkDeviceQueueCreateInfo queue_create_info = {};
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = queue_family_index;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = &queue_priority;

	VkDeviceCreateInfo create_info = { };
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create_info.pQueueCreateInfos = &queue_create_info;
	create_info.queueCreateInfoCount = 1;
	create_info.pEnabledFeatures = &chosen_physical_device_features;
	create_info.enabledExtensionCount = physical_device_extension_count;
	create_info.ppEnabledExtensionNames = physical_device_extensions;

	VkResult device_create = vkCreateDevice(
		physical_device,
		&create_info,
		nullptr,
		&device
	);

	assert(device_create == VK_SUCCESS);

	vkGetDeviceQueue(
		device,
		queue_family_index,
		0,
		&graphics_queue
	);

	vkGetDeviceQueue(
		device,
		queue_family_index,
		0,
		&present_queue
	);
}

void CreateCommandPool()
{
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = queue_family_index;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult result = vkCreateCommandPool(
		device,
		&pool_info,
		nullptr,
		&command_pool
	);

	assert(result == VK_SUCCESS);
}

void CreateBuffer()
{
	unsigned int example_input_data[64];
	for (int i = 0; i < 64; i++)
	{
		example_input_data[i] = i;
	}

	VkMemoryPropertyFlags buffer_memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

	VkBufferCreateInfo buffer_info = {};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = index_buffer_size;
	buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult buffer_create = vkCreateBuffer(
		device,
		&buffer_info,
		nullptr,
		&buffer
	);

	assert(buffer_create == VK_SUCCESS);

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(
		device,
		buffer,
		&memory_requirements
	);

	uint32_t memory_type = FindMemoryType(
		chosen_physical_device_mem_properties,
		memory_requirements.memoryTypeBits,
		buffer_memory_properties
	);

	VkMemoryAllocateInfo memory_allocate_info = {};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = index_buffer_size;
	memory_allocate_info.memoryTypeIndex = memory_type;

	VkResult memory_allocation_result = vkAllocateMemory(
		device,
		&memory_allocate_info,
		nullptr,
		&buffer_memory
	);

	assert(memory_allocation_result == VK_SUCCESS);

	VkResult buffer_bind_result = vkBindBufferMemory(
		device,
		buffer,
		buffer_memory,
		0
	);

	assert(buffer_bind_result == VK_SUCCESS);

	void* mapped_memory = nullptr;

	VkResult map_memory_result = vkMapMemory(
		device,
		buffer_memory,
		0,
		index_buffer_size,
		0,
		&mapped_memory
	);

	assert(map_memory_result == VK_SUCCESS);

	memcpy(
		mapped_memory,
		example_input_data,
		index_buffer_size
	);

	vkUnmapMemory(
		device,
		buffer_memory
	);

	map_memory_result = vkMapMemory(
		device,
		buffer_memory,
		0,
		index_buffer_size,
		0,
		&mapped_memory
	);

	assert(map_memory_result == VK_SUCCESS);

	unsigned int example_output_data[64];

	memcpy(
		example_output_data,
		mapped_memory,
		index_buffer_size
	);
}

bool CreateBuffer(VkBuffer &buffer, VkDeviceMemory &buffer_memory,
				  VkDeviceSize size, VkBufferUsageFlags usage, VkSharingMode sharing_mode, VkMemoryPropertyFlags buffer_memory_properties)
{
	VkBufferCreateInfo buffer_info = {};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = sharing_mode;

	VkResult buffer_result = vkCreateBuffer(
		device,
		&buffer_info,
		nullptr,
		&buffer
	);
	assert(buffer_result == VK_SUCCESS);

	VkMemoryRequirements buffer_memory_requirements;
	vkGetBufferMemoryRequirements(
		device,
		buffer,
		&buffer_memory_requirements
	);

	uint32_t memory_type = FindMemoryType(
		chosen_physical_device_mem_properties,
		buffer_memory_requirements.memoryTypeBits,
		buffer_memory_properties
	);

	VkMemoryAllocateInfo memory_allocate_info = MemoryAllocateInfo(
		buffer_memory_requirements.size,
		memory_type
	);

	VkResult memory_allocation_result = vkAllocateMemory(
		device,
		&memory_allocate_info,
		nullptr,
		&buffer_memory
	);
	assert(memory_allocation_result == VK_SUCCESS);

	VkResult bind_buffer_memory = vkBindBufferMemory(
		device,
		buffer,
		buffer_memory,
		0
	);
	assert(bind_buffer_memory == VK_SUCCESS);

	return true;
}

void CreateDescriptor()
{
	VkWriteDescriptorSet descriptor_write_set;

	{
		VkDescriptorPoolSize pool_size = {};
		pool_size.type = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		pool_size.descriptorCount = 1;

		VkDescriptorPoolCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		create_info.poolSizeCount = 1;
		create_info.pPoolSizes = &pool_size;
		create_info.maxSets = 1;

		VkResult create_descriptor_pool = vkCreateDescriptorPool(
			device,
			&create_info,
			nullptr,
			&descriptor_pool
		);

		assert(create_descriptor_pool == VK_SUCCESS);
	}

	{
		VkDescriptorSetLayoutBinding layout_binding = {};
		layout_binding.binding = 0;
		layout_binding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layout_binding.descriptorCount = 1;
		layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Which shader the variable will be visible for

		descriptor_set_layout = CreateDescriptorSetLayout(
			device,
			&layout_binding,
			1
		);
	}

	descriptor_set = AllocateDescriptorSet(
		device,
		descriptor_pool,
		descriptor_set,
		descriptor_set_layout,
		1
	);

	{
		VkDescriptorBufferInfo buffer_info = {};
		buffer_info.buffer = buffer;
		buffer_info.offset = 0;
		buffer_info.range = index_buffer_size; // How much memory the buffer takes up

		descriptor_write_set = {};
		descriptor_write_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_write_set.dstSet = descriptor_set;
		descriptor_write_set.dstBinding = 0;
		descriptor_write_set.dstArrayElement = 0;
		descriptor_write_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptor_write_set.descriptorCount = 1;
		descriptor_write_set.pBufferInfo = &buffer_info;

		vkUpdateDescriptorSets(
			device,
			1,
			&descriptor_write_set,
			0,
			NULL
		);
	}
}

void CreateSwapchain()
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR* surface_formats = nullptr;
	uint32_t surface_format_count = 0;
	VkPresentModeKHR* present_modes = nullptr;
	uint32_t present_mode_count = 0;

	bool hasSupport = CheckSwapchainSupport(capabilities, surface_formats, surface_format_count, present_modes, present_mode_count);

	assert(hasSupport);

	surface_format = ChooseSwapchainSurfaceFormat(surface_formats, surface_format_count);
	present_mode = ChooseSwapchainPresentMode(present_modes, present_mode_count);

	VkExtent2D extent = ChooseSwapchainExtent(capabilities);

	uint32_t image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
	{
		image_count = capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info.surface = surface;
	create_info.minImageCount = image_count;
	create_info.imageFormat = surface_format.format;
	create_info.imageColorSpace = surface_format.colorSpace;
	create_info.imageExtent = extent;
	create_info.imageArrayLayers = 1;
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	create_info.presentMode = present_mode;
	create_info.clipped = VK_TRUE;
	create_info.oldSwapchain = VK_NULL_HANDLE;
	create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info.queueFamilyIndexCount = 0;
	create_info.pQueueFamilyIndices = nullptr;
	create_info.preTransform = capabilities.currentTransform;
	create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	{
		create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	{
		create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	VkResult result = vkCreateSwapchainKHR(
		device,
		&create_info,
		nullptr,
		&swapchain
	);

	assert(result == VK_SUCCESS);

	result = vkGetSwapchainImagesKHR(
		device,
		swapchain,
		&swapchain_image_count,
		nullptr
	);

	assert(result == VK_SUCCESS);

	swapchain_images = std::unique_ptr<VkImage>(new VkImage[swapchain_image_count]);

	result = vkGetSwapchainImagesKHR(
		device,
		swapchain,
		&swapchain_image_count,
		swapchain_images.get()
	);

	assert(result == VK_SUCCESS);

	swapchain_image_views = std::unique_ptr<VkImageView>(new VkImageView[swapchain_image_count]);

	for (int i = 0; i < swapchain_image_count; i++)
	{
		VkImageViewCreateInfo image_view_create_info = {};
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.image = swapchain_images.get()[i];
		image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.format = surface_format.format;
		image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
		image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
		image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
		image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = 1;

		result = vkCreateImageView(
			device,
			&image_view_create_info,
			nullptr,
			&swapchain_image_views.get()[i]
		);

		assert(result == VK_SUCCESS);
	}
}

void CreateRenderPass()
{
	const uint32_t candidate_format_count = 3;

	const VkFormat candidate_formats[candidate_format_count] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT
	};

	VkFormat depth_image_format = FindSupportedFormat(
		physical_device,
		candidate_formats,
		candidate_format_count,
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	VkAttachmentDescription present_attachment = {};
	VkAttachmentDescription color_attachment = {};
	VkAttachmentDescription depth_attachment = {};

	{
		present_attachment.format = surface_format.format;
		present_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		present_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		present_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		present_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		present_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		present_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		present_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	{
		color_attachment.format = surface_format.format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	{
		depth_attachment.format = surface_format.format;
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	const uint32_t attachment_description_count = 3;
	VkAttachmentDescription attachment_descriptions[attachment_description_count] = {
		present_attachment,
		color_attachment,
		depth_attachment
	};

	VkAttachmentReference present_attachment_reference = {};
	present_attachment_reference.attachment = 0;
	present_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_attachment_reference = {};
	color_attachment_reference.attachment = 1;
	color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_reference = {};
	depth_attachment_reference.attachment = 2;
	depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	const uint32_t color_attachment_reference_count = 2;
	VkAttachmentReference color_attachment_references[color_attachment_reference_count] = {
		present_attachment_reference,
		color_attachment_reference
	};

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = color_attachment_reference_count;
	subpass.pColorAttachments = color_attachment_references;
	subpass.pDepthStencilAttachment = &depth_attachment_reference;

	VkSubpassDependency subpass_dependency = {};
	subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpass_dependency.dstSubpass = 0;
	subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependency.srcAccessMask = 0;
	subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpass_dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo render_pass_create_info = {};
	render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.attachmentCount = attachment_description_count;
	render_pass_create_info.pAttachments = attachment_descriptions;
	render_pass_create_info.subpassCount = 1;
	render_pass_create_info.pSubpasses = &subpass;
	render_pass_create_info.dependencyCount = 1;
	render_pass_create_info.pDependencies = &subpass_dependency;

	VkResult create_render_pass_result = vkCreateRenderPass(
		device,
		&render_pass_create_info,
		nullptr,
		&renderpass
	);

	assert(create_render_pass_result == VK_SUCCESS);

	framebuffers = std::unique_ptr<VkFramebuffer>(new VkFramebuffer[swapchain_image_count]);
	framebuffer_attachments = std::unique_ptr<VulkanAttachments>(new VulkanAttachments[swapchain_image_count]);

	for (uint32_t i = 0; i < swapchain_image_count; i++)
	{
		CreateAttachmentImages(colorFormat,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, framebuffer_attachments.get()[i].color);

		CreateAttachmentImages(depth_image_format,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, framebuffer_attachments.get()[i].depth);

		const uint32_t attachment_count = 3;
		VkImageView attachments[attachment_count] = {
			swapchain_image_views.get()[i],
			framebuffer_attachments.get()[i].color.view,
			framebuffer_attachments.get()[i].depth.view,
		};

		VkFramebufferCreateInfo framebuffer_info = {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = renderpass;
		framebuffer_info.attachmentCount = attachment_count;
		framebuffer_info.pAttachments = attachments;
		framebuffer_info.width = screen_width;
		framebuffer_info.height = screen_height;
		framebuffer_info.layers = 1;

		VkResult create_frame_buffer_result = vkCreateFramebuffer(
			device,
			&framebuffer_info,
			nullptr,
			&framebuffers.get()[i]
		);
		assert(create_render_pass_result == VK_SUCCESS);
	}
}

void DestroyRenderResources()
{
	vkDestroyRenderPass(
		device,
		renderpass,
		nullptr
	);

	vkDestroySwapchainKHR(
		device,
		swapchain,
		nullptr
	);

	for (int i = 0; i < swapchain_image_count; i++)
	{
		vkDestroyImageView(
			device,
			swapchain_image_views.get()[i],
			nullptr
		);

		vkDestroyFramebuffer(
			device,
			framebuffers.get()[i],
			nullptr
		);
		vkDestroyImageView(
			device,
			framebuffer_attachments.get()[i].color.view,
			nullptr
		);
		vkDestroyImage(
			device,
			framebuffer_attachments.get()[i].color.image,
			nullptr
		);
		vkFreeMemory(
			device,
			framebuffer_attachments.get()[i].color.memory,
			nullptr
		);
		vkDestroySampler(
			device,
			framebuffer_attachments.get()[i].color.sampler,
			nullptr
		);
		vkDestroyImageView(
			device,
			framebuffer_attachments.get()[i].depth.view,
			nullptr
		);
		vkDestroyImage(
			device,
			framebuffer_attachments.get()[i].depth.image,
			nullptr
		);
		vkFreeMemory(
			device,
			framebuffer_attachments.get()[i].depth.memory,
			nullptr
		);
		vkDestroySampler(
			device,
			framebuffer_attachments.get()[i].depth.sampler,
			nullptr
		);
	}
}

void CreateRenderResources()
{
	CreateSwapchain();
	CreateRenderPass();

	render_submission_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	render_submission_info.waitSemaphoreCount = 1;
	render_submission_info.pWaitDstStageMask = &wait_stages;
	render_submission_info.commandBufferCount = 1;
	render_submission_info.signalSemaphoreCount = 1;

	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &swapchain;
	present_info.pResults = nullptr;
}

void RebuildRenderResources()
{
	VkResult device_idle_result = vkDeviceWaitIdle(device);
	assert(device_idle_result == VK_SUCCESS);
	DestroyRenderResources();
	CreateRenderResources();
	BuildCommandBuffers(graphics_command_buffers, swapchain_image_count);
}

void Present()
{
	current_frame_index = 0;
	fences = std::unique_ptr<VkFence>(new VkFence[swapchain_image_count]);
	for (unsigned int i = 0; i < swapchain_image_count; i++)
	{
		VkFenceCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		VkResult create_fence_result = vkCreateFence(
			device,
			&info,
			nullptr,
			&fences.get()[i]
		);
		assert(create_fence_result == VK_SUCCESS);
	}

	VkSemaphoreCreateInfo semaphore_create_info = {};
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkResult create_semaphore_result = vkCreateSemaphore(
		device,
		&semaphore_create_info,
		nullptr,
		&image_available_semaphore
	);
	assert(create_semaphore_result == VK_SUCCESS);

	create_semaphore_result = vkCreateSemaphore(
		device,
		&semaphore_create_info,
		nullptr,
		&render_finished_semaphore
	);
	assert(create_semaphore_result == VK_SUCCESS);

	graphics_command_buffers = std::unique_ptr<VkCommandBuffer>(new VkCommandBuffer[swapchain_image_count]);

	VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_allocate_info.commandBufferCount = swapchain_image_count;

	VkResult allocate_command_buffer_result = vkAllocateCommandBuffers(
		device,
		&command_buffer_allocate_info,
		graphics_command_buffers.get()
	);
	assert(allocate_command_buffer_result == VK_SUCCESS);

	BuildCommandBuffers(graphics_command_buffers, swapchain_image_count);

	render_submission_info.pSignalSemaphores = &render_finished_semaphore;
	render_submission_info.pWaitSemaphores = &image_available_semaphore;
	present_info.pWaitSemaphores = &render_finished_semaphore;
}

void SetupGraphicsPipeline()
{
	VkPipelineLayoutCreateInfo pipeline_layout_info = {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 0;
	pipeline_layout_info.pSetLayouts = nullptr;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = 0;

	VkResult create_pipeline_layout_result = vkCreatePipelineLayout(
		device,
		&pipeline_layout_info,
		nullptr,
		&graphics_pipeline_layout
	);
	assert(create_pipeline_layout_result == VK_SUCCESS);

	unsigned int shader_stage_count = 2;

	std::unique_ptr<VkPipelineShaderStageCreateInfo> shader_stages =
		std::unique_ptr<VkPipelineShaderStageCreateInfo>(new VkPipelineShaderStageCreateInfo[shader_stage_count]);

	char* vertex_shader_data = nullptr;
	unsigned int vertex_shader_size = 0;
	ReadShaderFile(
		"G:/Backup/Coding progress/Personal/Vulkan Practice/build/Data/Shaders/9-GraphicsPipeline/vert.spv",
		vertex_shader_data,
		vertex_shader_size
	);

	char* fragment_shader_data = nullptr;
	unsigned int fragment_shader_size = 0;
	ReadShaderFile(
		"G:/Backup/Coding progress/Personal/Vulkan Practice/build/Data/Shaders/9-GraphicsPipeline/frag.spv",
		fragment_shader_data,
		fragment_shader_size
	);

	VkShaderModuleCreateInfo vertex_shader_module_create_info = {};
	vertex_shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vertex_shader_module_create_info.codeSize = vertex_shader_size;
	vertex_shader_module_create_info.pCode = reinterpret_cast<const uint32_t*>(vertex_shader_data);

	VkShaderModuleCreateInfo fragment_shader_module_create_info = {};
	fragment_shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	fragment_shader_module_create_info.codeSize = fragment_shader_size;
	fragment_shader_module_create_info.pCode = reinterpret_cast<const uint32_t*>(fragment_shader_data);

	VkResult create_shader_module = vkCreateShaderModule(
		device,
		&vertex_shader_module_create_info,
		nullptr,
		&vertex_shader_module
	);
	assert(create_shader_module == VK_SUCCESS);

	create_shader_module = vkCreateShaderModule(
		device,
		&fragment_shader_module_create_info,
		nullptr,
		&fragment_shader_module
	);
	assert(create_shader_module == VK_SUCCESS);

	shader_stages.get()[0] = {};
	shader_stages.get()[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages.get()[0].pName = "main";
	shader_stages.get()[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages.get()[0].module = vertex_shader_module;

	shader_stages.get()[1] = {};
	shader_stages.get()[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages.get()[1].pName = "main";
	shader_stages.get()[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages.get()[1].module = fragment_shader_module;

	const uint32_t vertex_input_binding_description_count = 1;
	std::unique_ptr<VkVertexInputBindingDescription> vertex_input_binding_descriptions =
		std::unique_ptr<VkVertexInputBindingDescription>(new VkVertexInputBindingDescription[vertex_input_binding_description_count]);

	vertex_input_binding_descriptions.get()[0].binding = 0;
	vertex_input_binding_descriptions.get()[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertex_input_binding_descriptions.get()[0].stride = sizeof(SVertexData);

	const uint32_t vertex_input_attribute_description_count = 2;
	std::unique_ptr<VkVertexInputAttributeDescription> vertex_input_attribute_descriptions =
		std::unique_ptr<VkVertexInputAttributeDescription>(new VkVertexInputAttributeDescription[vertex_input_attribute_description_count]);

	vertex_input_attribute_descriptions.get()[0].binding = 0;
	vertex_input_attribute_descriptions.get()[0].location = 0;
	vertex_input_attribute_descriptions.get()[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertex_input_attribute_descriptions.get()[0].offset = offsetof(SVertexData, position);

	vertex_input_attribute_descriptions.get()[1].binding = 0;
	vertex_input_attribute_descriptions.get()[1].location = 1;
	vertex_input_attribute_descriptions.get()[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertex_input_attribute_descriptions.get()[1].offset = offsetof(SVertexData, color);

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = vertex_input_binding_description_count;
	vertex_input_info.vertexAttributeDescriptionCount = vertex_input_attribute_description_count;
	vertex_input_info.pVertexBindingDescriptions = vertex_input_binding_descriptions.get();
	vertex_input_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions.get();

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewport_state = {};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f;
	rasterizer.depthBiasClamp = 0.0f;
	rasterizer.depthBiasSlopeFactor = 0.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
	depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil.depthTestEnable = VK_TRUE;
	depth_stencil.depthWriteEnable = VK_TRUE;
	depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depth_stencil.depthBoundsTestEnable = VK_FALSE;
	depth_stencil.stencilTestEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState color_blend_attachment = {};
	color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	color_blend_attachment.blendEnable = VK_TRUE;
	color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo color_blending = {};
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.logicOpEnable = VK_FALSE;
	color_blending.logicOp = VK_LOGIC_OP_COPY;
	color_blending.attachmentCount = 1;
	color_blending.pAttachments = &color_blend_attachment;
	color_blending.blendConstants[0] = 0.0f;
	color_blending.blendConstants[1] = 0.0f;
	color_blending.blendConstants[2] = 0.0f;
	color_blending.blendConstants[3] = 0.0f;

	const uint32_t dynamic_state_count = 3;

	VkDynamicState dynamic_states[dynamic_state_count] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_LINE_WIDTH
	};

	VkPipelineDynamicStateCreateInfo dynamic_state = {};
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.pDynamicStates = dynamic_states;
	dynamic_state.dynamicStateCount = dynamic_state_count;
	dynamic_state.flags = 0;

	VkGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = shader_stage_count;
	pipeline_info.pStages = shader_stages.get();
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pDepthStencilState = &depth_stencil;
	pipeline_info.pColorBlendState = &color_blending;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = graphics_pipeline_layout;
	pipeline_info.renderPass = renderpass;
	pipeline_info.subpass = 0;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
	pipeline_info.basePipelineIndex = -1;
	pipeline_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;

	VkResult create_graphics_pipeline_result = vkCreateGraphicsPipelines(
		device,
		VK_NULL_HANDLE,
		1,
		&pipeline_info,
		nullptr,
		&graphics_pipeline
	);
	assert(create_graphics_pipeline_result == VK_SUCCESS);

	CreateBuffer(
		particle_vertex_buffer,
		particle_vertex_buffer_memory,
		particle_vertex_buffer_size,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);

	VkResult vertex_mapped_memory_result = vkMapMemory(
		device,
		particle_vertex_buffer_memory,
		0,
		particle_vertex_buffer_size,
		0,
		&particle_vertex_mapped_buffer_memory
	);
	assert(vertex_mapped_memory_result == VK_SUCCESS);

	memcpy(
		particle_vertex_mapped_buffer_memory,
		particle_vertices,
		particle_vertex_buffer_size
	);

	CreateBuffer(
		index_buffer,
		index_buffer_memory,
		index_buffer_size,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);

	VkResult index_mapped_memory_result = vkMapMemory(
		device,
		index_buffer_memory,
		0,
		index_buffer_size,
		0,
		&index_mapped_buffer_memory
	);
	assert(index_mapped_memory_result == VK_SUCCESS);

	memcpy(
		index_mapped_buffer_memory,
		indices,
		index_buffer_size
	);

	BuildCommandBuffers(graphics_command_buffers, swapchain_image_count);
}

void Destroy()
{
	vkDestroyPipeline(
		device,
		graphics_pipeline,
		nullptr
	);

	vkDestroyPipelineLayout(
		device,
		graphics_pipeline_layout,
		nullptr
	);

	vkDestroyShaderModule(
		device,
		vertex_shader_module,
		nullptr
	);

	vkDestroyShaderModule(
		device,
		fragment_shader_module,
		nullptr
	);


	// Now we unmap the data
	vkUnmapMemory(
		device,
		particle_vertex_buffer_memory
	);

	// Clean up the buffer data
	vkDestroyBuffer(
		device,
		particle_vertex_buffer,
		nullptr
	);

	// Free the memory that was allocated for the buffer
	vkFreeMemory(
		device,
		particle_vertex_buffer_memory,
		nullptr
	);


	// Now we unmap the data
	vkUnmapMemory(
		device,
		index_buffer_memory
	);

	// Clean up the buffer data
	vkDestroyBuffer(
		device,
		index_buffer,
		nullptr
	);

	// Free the memory that was allocated for the buffer
	vkFreeMemory(
		device,
		index_buffer_memory,
		nullptr
	);

	for (int i = 0; i < swapchain_image_count; i++)
	{
		vkDestroyFramebuffer(
			device,
			framebuffers.get()[i],
			nullptr
		);
		vkDestroyImageView(
			device,
			framebuffer_attachments.get()[i].color.view,
			nullptr
		);
		vkDestroyImage(
			device,
			framebuffer_attachments.get()[i].color.image,
			nullptr
		);
		vkFreeMemory(
			device,
			framebuffer_attachments.get()[i].color.memory,
			nullptr
		);
		vkDestroySampler(
			device,
			framebuffer_attachments.get()[i].color.sampler,
			nullptr
		);
		vkDestroyImageView(
			device,
			framebuffer_attachments.get()[i].depth.view,
			nullptr
		);
		vkDestroyImage(
			device,
			framebuffer_attachments.get()[i].depth.image,
			nullptr
		);
		vkFreeMemory(
			device,
			framebuffer_attachments.get()[i].depth.memory,
			nullptr
		);
		vkDestroySampler(
			device,
			framebuffer_attachments.get()[i].depth.sampler,
			nullptr
		);
		vkDestroyImageView(
			device,
			swapchain_image_views.get()[i],
			nullptr
		);
	}

	vkDestroyRenderPass(
		device,
		renderpass,
		nullptr
	);

	vkDestroySwapchainKHR(
		device,
		swapchain,
		nullptr
	);

	DestroyWindow();

	vkDestroyDescriptorSetLayout(
		device,
		descriptor_set_layout,
		nullptr
	);

	vkDestroyDescriptorPool(
		device,
		descriptor_pool,
		nullptr
	);

	vkUnmapMemory(
		device,
		buffer_memory
	);

	vkDestroyBuffer(
		device,
		buffer,
		nullptr
	);

	vkFreeMemory(
		device,
		buffer_memory,
		nullptr
	);

	vkDestroyCommandPool(
		device,
		command_pool,
		nullptr
	);

	vkDestroyDevice(
		device,
		nullptr
	);

	vkDestroyInstance(
		instance,
		NULL
	);
}

int main(int argc, char** argv)
{
	WindowSetup("Vulkan", 1080, 720);

	Setup();
	GetPhysicalDevice();
	CreateDevice();
	CreateCommandPool();
	CreateBuffer();
	CreateDescriptor();
	CreateSwapchain();
	CreateRenderPass();
	Present();
	SetupGraphicsPipeline();

	std::vector<unsigned char> image_data;

	unsigned error = lodepng::decode(
		image_data,
		square_texture.width,
		square_texture.height,
		"G:/Backup/Coding progress/Personal/Vulkan Practice/build/Data/Images/eye.png"
	);

	if (error)
	{
		std::cout << lodepng_error_text(error) << std::endl;
		return -1;
	}

	const unsigned int texture_size = square_texture.width * square_texture.height * 4;

	square_texture.transfer_buffer.buffer_size = texture_size;
	square_texture.transfer_buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	square_texture.transfer_buffer.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
	square_texture.transfer_buffer.buffer_memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	CreateBuffer(
		square_texture.transfer_buffer.buffer,
		square_texture.transfer_buffer.buffer_memory,
		square_texture.transfer_buffer.buffer_size,
		square_texture.transfer_buffer.usage,
		square_texture.transfer_buffer.sharing_mode,
		square_texture.transfer_buffer.buffer_memory_properties
	);

	VkResult mapped_memory_result = vkMapMemory(
		device,
		square_texture.transfer_buffer.buffer_memory,
		0,
		square_texture.transfer_buffer.buffer_size,
		0,
		&square_texture.transfer_buffer.mapped_buffer_memory
	);
	assert(mapped_memory_result == VK_SUCCESS);

	memcpy(
		square_texture.transfer_buffer.mapped_buffer_memory,
		image_data.data(),
		texture_size
	);

	vkUnmapMemory(
		device,
		square_texture.transfer_buffer.buffer_memory
	);

	square_texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	square_texture.format = VK_FORMAT_R8G8B8A8_UNORM;

	CreateImage(
		device,
		chosen_physical_device_mem_properties,
		square_texture.width,
		square_texture.height,
		square_texture.format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		square_texture.image,
		square_texture.memory,
		VK_IMAGE_LAYOUT_UNDEFINED
	);

	VkCommandBuffer copy_cmd = BeginSingleTimeCommands(
		command_pool
	);

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;

	SetImageLayout(
		copy_cmd,
		square_texture.image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		subresourceRange
	);

	VkBufferImageCopy bufferCopyRegion = {};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.mipLevel = 0;
	bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(square_texture.width);
	bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(square_texture.height);
	bufferCopyRegion.imageExtent.depth = 1;
	bufferCopyRegion.bufferOffset = 0;

	vkCmdCopyBufferToImage(
		copy_cmd,
		square_texture.transfer_buffer.buffer,
		square_texture.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&bufferCopyRegion
	);

	vkDestroyBuffer(
		device,
		square_texture.transfer_buffer.buffer,
		nullptr
	);

	SetImageLayout(
		copy_cmd,
		square_texture.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		square_texture.layout,
		subresourceRange
	);

	EndSingleTimeCommands(
		copy_cmd,
		command_pool
	);

	vkFreeCommandBuffers(
		device,
		command_pool,
		1,
		&copy_cmd
	);

	CreateImageSampler(
		square_texture.image,
		square_texture.format,
		square_texture.view,
		square_texture.sampler
	);

	const uint32_t descriptor_pool_size_count = 1;

	VkDescriptorPoolSize texture_pool_size[descriptor_pool_size_count] = {};
	{
		VkDescriptorPoolSize info = {};
		info.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		info.descriptorCount = 1;
		texture_pool_size[0] = info;
	}

	texture_descriptor_pool = CreateDescriptorPool(
		device,
		texture_pool_size,
		descriptor_pool_size_count,
		1
	);

	VkDescriptorSetLayoutBinding layout_bindings[descriptor_pool_size_count] = {};
	{
		VkDescriptorSetLayoutBinding info = {};
		info.binding = 0;
		info.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		info.descriptorCount = 1;
		info.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		layout_bindings[0] = info;
	}

	texture_descriptor_set_layout = CreateDescriptorSetLayout(
		device,
		layout_bindings,
		descriptor_pool_size_count
	);

	texture_descriptor_set = AllocateDescriptorSet(
		device,
		texture_descriptor_pool,
		texture_descriptor_set,
		texture_descriptor_set_layout,
		1
	);

	while (window_open)
	{
		PollWindow();

		vkWaitForFences(
			device,
			1,
			&fences.get()[current_frame_index],
			VK_TRUE,
			UINT32_MAX
		);
		VkResult acquire_next_image_result = vkAcquireNextImageKHR(
			device,
			swapchain,
			UINT64_MAX,
			image_available_semaphore,
			VK_NULL_HANDLE,
			&current_frame_index
		);
		assert(acquire_next_image_result == VK_SUCCESS);

		VkResult queue_idle_result = vkQueueWaitIdle(
			graphics_queue
		);
		assert(queue_idle_result == VK_SUCCESS);

		VkResult reset_fences_result = vkResetFences(
			device,
			1,
			&fences.get()[current_frame_index]
		);
		assert(reset_fences_result == VK_SUCCESS);

		render_submission_info.pCommandBuffers = &graphics_command_buffers.get()[current_frame_index];
		VkResult queue_submit_result = vkQueueSubmit(
			graphics_queue,
			1,
			&render_submission_info,
			fences.get()[current_frame_index]
		);
		assert(queue_submit_result == VK_SUCCESS);

		present_info.pImageIndices = &current_frame_index;

		VkResult queue_present_result = vkQueuePresentKHR(
			present_queue,
			&present_info
		);

		if (queue_present_result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RebuildRenderResources();
		}
		assert(queue_present_result == VK_SUCCESS || queue_present_result == VK_ERROR_OUT_OF_DATE_KHR);

		vkDestroySemaphore(
			device,
			image_available_semaphore,
			nullptr
		);
		vkDestroySemaphore(
			device,
			render_finished_semaphore,
			nullptr
		);

		for (unsigned int i = 0; i < swapchain_image_count; i++)
		{
			vkDestroyFence(
				device,
				fences.get()[i],
				nullptr
			);
		}
	}

	Destroy();

	return 0;
}