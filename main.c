#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

constexpr uint16_t MAX_TMP_BUFFER = 256;

/// @param[in] filename
/// @param[out] content
/// @param[out] content_size
/// @return `true` on success and `false` otherwise
/// @note Caller is responsible for freeing `content` after it is no longer needed
static bool file_read(const char *filename, uint8_t **content, size_t *content_size) {
    bool success = false;

    FILE *file = nullptr;
    uint8_t *read_buffer = nullptr;

    file = fopen(filename, "rb");
    if (file == nullptr) {
        fprintf(stderr, "file_read: fopen(\"%s\", \"rb\") failed\n", filename);
        goto cleanup;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "file_read: fseek(fd, 0, SEEK_END) failed\n");
        goto cleanup;
    }

    size_t file_size = ftell(file);
    if (file_size == -1) {
        fprintf(stderr, "file_read: ftell(file) failed\n");
        goto cleanup;
    }

    rewind(file);

    read_buffer = malloc(sizeof(uint8_t) * file_size);
    if (read_buffer == nullptr) {
        fprintf(stderr, "file_read: malloc failed\n");
        goto cleanup;
    }

    if (fread(read_buffer, file_size, 1, file) != 1) {
        fprintf(stderr, "file_read: fread failed\n");
        goto cleanup;
    }

    *content = read_buffer;
    *content_size = file_size;

    success = true;

cleanup:
    if (!success) {
        free(read_buffer);
    }

    fclose(file);

    return success;
}

constexpr uint8_t MAX_SWAPCHAIN_IMAGES = 10;

struct vulkan {
    const char *application_name;
    bool enable_validation_layers;

    GLFWwindow *window;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicaldevice;
    VkDevice device;
    VkSwapchainKHR swapchain;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;

    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;

    VkImage swapchain_images[MAX_SWAPCHAIN_IMAGES];
    size_t swapchain_images_count;

    VkImageView swapchain_imageviews[MAX_SWAPCHAIN_IMAGES];
    size_t swapchain_imageviews_count;

    VkFramebuffer swapchain_framebuffers[MAX_SWAPCHAIN_IMAGES];
    size_t swapchain_framebuffers_count;

    uint32_t graphics_queuefamily_index;
    VkQueue graphics_queue;

    uint32_t present_queuefamily_index;
    VkQueue present_queue;

    VkSemaphore swapchain_image_available;
    VkSemaphore render_finished;
    VkFence frame_in_flight;
};

/// @param[in] validation_layers
/// @param[in] validation_layers_check
/// @return `true` on success and `false` otherwise
static bool vulkan_validationlayers_check(
    const char *validation_layers[], size_t validation_layers_count
) {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    if (layer_count > MAX_TMP_BUFFER) {
        fprintf(
            stderr,
            "vulkan_validationlayers_check: layer_count (%u) is out of bounds\n",
            layer_count
        );
        return false;
    }
    VkLayerProperties available_layers[MAX_TMP_BUFFER];
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

    for (size_t i = 0; i < validation_layers_count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < layer_count; j++) {
            if (strcmp(validation_layers[i], available_layers[j].layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_instance_create(struct vulkan *vulkan) {
    const char *validation_layers[] = {
        "VK_LAYER_KHRONOS_validation",
    };
    size_t validation_layers_count = (
        sizeof(validation_layers) / sizeof(validation_layers[0])
    );

    if (
        vulkan->enable_validation_layers &&
        !vulkan_validationlayers_check(validation_layers, validation_layers_count)
    ) {
        fprintf(stderr, "vulkan_instance_create: validation layers not found\n");
        return false;
    }

    uint32_t glfw_extension_count;
    const char **glfw_extensions = glfwGetRequiredInstanceExtensions(
        &glfw_extension_count
    );

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = vulkan->application_name,
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "No engine",
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .ppEnabledExtensionNames = glfw_extensions,
        .enabledExtensionCount = glfw_extension_count,
        .enabledLayerCount = 0,
    };
    if (vulkan->enable_validation_layers) {
        create_info.ppEnabledLayerNames = validation_layers;
        create_info.enabledLayerCount = validation_layers_count;
    }

    if (vkCreateInstance(
        &create_info, nullptr, &vulkan->instance
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_instance_create: failed to created instance\n");
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_surface_create(struct vulkan *vulkan) {
    if (glfwCreateWindowSurface(
        vulkan->instance, vulkan->window, nullptr, &vulkan->surface
    ) != VK_SUCCESS) {
        fprintf(
            stderr, "vulkan_surface_create: glfwCreateWindowSurface failed\n"
        );
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_physicaldevice_find(struct vulkan *vulkan) {
    uint32_t device_count;
    vkEnumeratePhysicalDevices(vulkan->instance, &device_count, nullptr);
    if (device_count == 0) {
        fprintf(stderr, "vulkan_physicaldevice_find: no devices found\n");
        return false;
    }
    if (device_count > MAX_TMP_BUFFER) {
        fprintf(
            stderr,
            "vulkan_physicaldevice_find: device_count (%u) is out of bounds\n",
            device_count
        );
        return false;
    }
    VkPhysicalDevice devices[MAX_TMP_BUFFER];
    vkEnumeratePhysicalDevices(vulkan->instance, &device_count, devices);

    vulkan->physicaldevice = devices[0];

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_queuefamily_find(struct vulkan *vulkan) {
    uint32_t queuefamily_count;
    vkGetPhysicalDeviceQueueFamilyProperties(
        vulkan->physicaldevice, &queuefamily_count, nullptr
    );
    if (queuefamily_count > MAX_TMP_BUFFER) {
        fprintf(
            stderr,
            "vulkan_queuefamily_find: queuefamily_count (%u) is out of bounds\n",
            queuefamily_count
        );
        return false;
    }
    VkQueueFamilyProperties queuefamilies[MAX_TMP_BUFFER];
    vkGetPhysicalDeviceQueueFamilyProperties(
        vulkan->physicaldevice, &queuefamily_count, queuefamilies
    );

    bool found_graphics_queuefamily = false;
    for (uint32_t i = 0; i < queuefamily_count; i++) {
        if (queuefamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vulkan->graphics_queuefamily_index = i;
            found_graphics_queuefamily = true;
            break;
        }
    }

    bool found_present_queuefamily = false;
    for (uint32_t i = 0; i < queuefamily_count; i++) {
        VkBool32 present_support;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            vulkan->physicaldevice, i, vulkan->surface, &present_support
        );

        if (present_support == VK_TRUE) {
            vulkan->present_queuefamily_index = i;
            found_present_queuefamily = true;
            break;
        }
    }

    return found_graphics_queuefamily && found_present_queuefamily;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_device_create(struct vulkan *vulkan) {
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queue_create_infos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vulkan->graphics_queuefamily_index,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        },
        {

            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vulkan->present_queuefamily_index,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        },
    };
    size_t queue_create_infos_count = (
        sizeof(queue_create_infos) / sizeof(*queue_create_infos)
    );
    // TODO: If the size of this array ever becomes bigger than 2 elements, this
    // should be refactored into a more suitable data structure.
    if (
        queue_create_infos[0].queueFamilyIndex == queue_create_infos[1].queueFamilyIndex
    ) {
        queue_create_infos_count = 1;
    }

    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(
        vulkan->physicaldevice, nullptr, &extension_count, nullptr
    );
    if (extension_count > MAX_TMP_BUFFER) {
        fprintf(
            stderr,
            "vulkan_device_create: extension_count (%u) is out of bounds\n",
            extension_count
        );
        return false;
    }
    VkExtensionProperties available_extensions[MAX_TMP_BUFFER];
    vkEnumerateDeviceExtensionProperties(
        vulkan->physicaldevice, nullptr, &extension_count, available_extensions
    );

    bool found_swapchain_extension = false;
    for (uint32_t i = 0; i < extension_count; i++) {
        if (strcmp(
            available_extensions[i].extensionName,
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        ) == 0) {
            found_swapchain_extension = true;
        }
    }
    if (!found_swapchain_extension) {
        fprintf(
            stderr, "vulkan_device_create: swapchain extension not found\n"
        );
        return false;
    }

    const char *device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    size_t device_extensions_count = (
        sizeof(device_extensions) / sizeof(device_extensions[0])
    );

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queue_create_infos,
        .queueCreateInfoCount = queue_create_infos_count,
        .pEnabledFeatures = &(VkPhysicalDeviceFeatures){},
        .ppEnabledExtensionNames = device_extensions,
        .enabledExtensionCount = device_extensions_count,
    };

    if (vkCreateDevice(
        vulkan->physicaldevice, &create_info, nullptr, &vulkan->device
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_device_create: vkCreateDevice failed\n");
        return false;
    }

    vkGetDeviceQueue(
        vulkan->device, vulkan->graphics_queuefamily_index, 0, &vulkan->graphics_queue
    );

    vkGetDeviceQueue(
        vulkan->device, vulkan->present_queuefamily_index, 0, &vulkan->present_queue
    );

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_swapchain_create(struct vulkan *vulkan) {
    uint32_t surface_format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        vulkan->physicaldevice, vulkan->surface, &surface_format_count, nullptr
    );
    if (surface_format_count == 0) {
        fprintf(stderr, "vulkan_swapchain_create: no surface formats found\n");
        return false;
    }
    if (surface_format_count > MAX_TMP_BUFFER) {
        fprintf(
            stderr,
            "vulkan_swapchain_create: surface_format_count (%u) is out of bounds\n",
            surface_format_count
        );
        return false;
    }
    VkSurfaceFormatKHR surface_formats[MAX_TMP_BUFFER];
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        vulkan->physicaldevice, vulkan->surface, &surface_format_count, surface_formats
    );

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        vulkan->physicaldevice, vulkan->surface, &present_mode_count, nullptr
    );
    if (present_mode_count == 0) {
        fprintf(stderr, "vulkan_swapchain_create: no present modes found\n");
        return false;
    }
    if (present_mode_count > MAX_TMP_BUFFER) {
        fprintf(
            stderr,
            "vulkan_swapchain_create: present_mode_count (%u) is out of bounds\n",
            present_mode_count
        );
        return false;
    }
    VkPresentModeKHR present_modes[MAX_TMP_BUFFER];
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        vulkan->physicaldevice,
        vulkan->surface,
        &present_mode_count,
        present_modes
    );

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        vulkan->physicaldevice, vulkan->surface, &surface_capabilities
    );

    VkSurfaceFormatKHR surface_format;
    bool found_surface_format = false;
    for (uint32_t i = 0; i < surface_format_count; i++) {
        if (
            surface_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        ) {
            surface_format = surface_formats[i];
            found_surface_format = true;
            break;
        }
    }
    if (!found_surface_format) {
        fprintf(stderr, "vulkan_swapchain_create: no surface format found\n");
        return false;
    }

    VkExtent2D image_extent;
    if (surface_capabilities.currentExtent.width != UINT32_MAX) {
        image_extent = surface_capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(vulkan->window, &width, &height);

        uint32_t min, max;

        min = surface_capabilities.minImageExtent.width;
        max = surface_capabilities.maxImageExtent.width;
        image_extent.width = width;
        if (image_extent.width < min) {
            image_extent.width = min;
        } else if (image_extent.width > max) {
            image_extent.width = max;
        }

        min = surface_capabilities.minImageExtent.height;
        max = surface_capabilities.maxImageExtent.height;
        image_extent.height = height;
        if (image_extent.height < min) {
            image_extent.height = min;
        } else if (image_extent.height > min) {
            image_extent.height = max;
        }
    }

    if (surface_capabilities.minImageCount > MAX_SWAPCHAIN_IMAGES) {
        fprintf(
            stderr, "vulkan_swapchain_create: Minimum surface image count is too high\n"
        );
        return false;
    }
    uint32_t image_count = surface_capabilities.minImageCount + 1;
    if (
        surface_capabilities.maxImageCount > 0 &&
        image_count > surface_capabilities.maxImageCount
    ) {
        image_count = surface_capabilities.maxImageCount;
    }
    if (image_count > MAX_SWAPCHAIN_IMAGES) {
        image_count = MAX_SWAPCHAIN_IMAGES;
    }

    uint32_t queue_family_indices[2] = {
        vulkan->graphics_queuefamily_index,
        vulkan->present_queuefamily_index
    };
    size_t queue_family_indices_count = (
        sizeof(queue_family_indices) / sizeof(queue_family_indices[0])
    );
    // TODO: If the size of this array ever becomes bigger than 2 elements, this
    // should be refactored into a more suitable data structure.
    if (queue_family_indices[0] == queue_family_indices[1]) {
        queue_family_indices_count = 1;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vulkan->surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = image_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = vulkan->swapchain,
    };
    if (queue_family_indices_count > 1) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.pQueueFamilyIndices = queue_family_indices;
        create_info.queueFamilyIndexCount = queue_family_indices_count;
    }

    if (vkCreateSwapchainKHR(
        vulkan->device, &create_info, nullptr, &vulkan->swapchain
    ) != VK_SUCCESS) {
        fprintf(
            stderr, "vulkan_swapchain_create: vkCreateSwapchainKHR failed\n"
        );
        return false;
    }

    vkGetSwapchainImagesKHR(
        vulkan->device,
        vulkan->swapchain,
        &image_count,
        vulkan->swapchain_images
    );
    vulkan->swapchain_images_count = image_count;

    vulkan->swapchain_image_format = surface_format.format;
    vulkan->swapchain_extent = image_extent;

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_imageviews_create(struct vulkan *vulkan) {
    for (size_t i = 0; i < vulkan->swapchain_images_count; i++) {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vulkan->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vulkan->swapchain_image_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(
            vulkan->device,
            &create_info,
            nullptr,
            &vulkan->swapchain_imageviews[i]
        ) != VK_SUCCESS) {
            fprintf(
                stderr,
                "vulkan_imageviews_create: vkCreateImageView(%zu) failed\n",
                i
            );
            return false;
        }
    }
    vulkan->swapchain_imageviews_count = vulkan->swapchain_images_count;

    return true;
}

/// @param[in] vulkan
/// @param[in] code
/// @param[in] code_size
/// @param[out] shader_module
/// @return `true` on success and `false` otherwise
/// @note Caller is responsible for freeing `shader_module` after successful return
static bool vulkan_shadermodule_create(
    const struct vulkan *vulkan,
    const uint8_t *code,
    size_t code_size,
    VkShaderModule *shader_module
) {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pCode = (uint32_t *) code,
        .codeSize = code_size,
    };

    if (vkCreateShaderModule(
        vulkan->device, &create_info, nullptr, shader_module
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_shadermodule_create: vkCreateShaderModule failed\n");
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_renderpass_create(struct vulkan *vulkan) {
    VkAttachmentDescription color_attachment = {
        .format = vulkan->swapchain_image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_attachment_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pColorAttachments = &color_attachment_reference,
        .colorAttachmentCount = 1,
    };

    VkSubpassDependency subpass_dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo render_pass_create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pAttachments = &color_attachment,
        .attachmentCount = 1,
        .pSubpasses = &subpass,
        .subpassCount = 1,
        .pDependencies = &subpass_dependency,
        .dependencyCount = 1,
    };

    if (vkCreateRenderPass(
        vulkan->device, &render_pass_create_info, nullptr, &vulkan->render_pass
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_renderpass_create: vkCreateRenderPass failed\n");
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_graphicspipeline_create(struct vulkan *vulkan) {
    bool success = false;

    uint8_t *vertex_shader_code = nullptr;
    uint8_t *fragment_shader_code = nullptr;
    VkShaderModule vertex_shadermodule = VK_NULL_HANDLE;
    VkShaderModule fragment_shadermodule = VK_NULL_HANDLE;

    size_t vertex_shader_code_size;
    if (!file_read(
        "./shaders/vertex.spv", &vertex_shader_code, &vertex_shader_code_size
    )) {
        fprintf(
            stderr,
            "vulkan_graphicspipeline_create: file_read(\"vertex.spv\") failed\n"
        );
        goto cleanup;
    }

    size_t fragment_shader_code_size;
    if (!file_read(
        "./shaders/fragment.spv", &fragment_shader_code, &fragment_shader_code_size
    )) {
        fprintf(
            stderr,
            "vulkan_graphicspipeline_create: "
            "file_read(\"fragment.spv\") failed\n"
        );
        goto cleanup;
    }

    if (!vulkan_shadermodule_create(
        vulkan, vertex_shader_code, vertex_shader_code_size, &vertex_shadermodule
    )) {
        fprintf(
            stderr,
            "vulkan_graphicspipeline_create: "
            "vulkan_shader_module_create(vertex_shader_code) failed\n"
        );
        goto cleanup;
    }

    if (!vulkan_shadermodule_create(
        vulkan, fragment_shader_code, fragment_shader_code_size, &fragment_shadermodule
    )) {
        fprintf(
            stderr,
            "vulkan_graphicspipeline_create: "
            "vulkan_shader_module_create(fragment_shader_code) failed\n"
        );
        goto cleanup;
    }

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shadermodule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shadermodule,
            .pName = "main",
        },
    };
    size_t shader_stages_count = sizeof(shader_stages) / sizeof(shader_stages[0]);

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    size_t dynamic_states_count = sizeof(dynamic_states) / sizeof(dynamic_states[0]);

    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pDynamicStates = dynamic_states,
        .dynamicStateCount = dynamic_states_count,
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pVertexBindingDescriptions = nullptr,
        .vertexBindingDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = (
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT
        ),
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .pAttachments = &color_blend_attachment,
        .attachmentCount = 1,
        .blendConstants = {
            0.0f,
            0.0f,
            0.0f,
            0.0f,
        },
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pSetLayouts = nullptr,
        .setLayoutCount = 0,
        .pPushConstantRanges = nullptr,
        .pushConstantRangeCount = 0,
    };

    if (vkCreatePipelineLayout(
        vulkan->device, &pipeline_layout_create_info, nullptr, &vulkan->pipeline_layout
    ) != VK_SUCCESS) {
        fprintf(
            stderr, "vulkan_graphicspipeline_create: vkCreatePipelineLayout failed\n"
        );
        goto cleanup;
    }

    VkGraphicsPipelineCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pStages = shader_stages,
        .stageCount = shader_stages_count,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = vulkan->pipeline_layout,
        .renderPass = vulkan->render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    if (vkCreateGraphicsPipelines(
        vulkan->device,
        VK_NULL_HANDLE,
        1,
        &pipeline_create_info,
        nullptr,
        &vulkan->graphics_pipeline
    ) != VK_SUCCESS) {
        fprintf(
            stderr, "vulkan_graphicspipeline_create: vkCreateGraphicsPipelines failed\n"
        );
        return false;
    }

    success = true;

cleanup:
    vkDestroyShaderModule(vulkan->device, fragment_shadermodule, nullptr);
    vkDestroyShaderModule(vulkan->device, vertex_shadermodule, nullptr);
    free(fragment_shader_code);
    free(vertex_shader_code);

    return success;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_framebuffers_create(struct vulkan *vulkan) {
    for (size_t i = 0; i < vulkan->swapchain_imageviews_count; i++) {
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vulkan->render_pass,
            .pAttachments = &vulkan->swapchain_imageviews[i],
            .attachmentCount = 1,
            .width = vulkan->swapchain_extent.width,
            .height = vulkan->swapchain_extent.height,
            .layers = 1,
        };

        if (vkCreateFramebuffer(
            vulkan->device, &create_info, nullptr, &vulkan->swapchain_framebuffers[i]
        ) != VK_SUCCESS) {
            fprintf(
                stderr,
                "vulkan_framebuffers_create: vkCreateFramebuffer(%zu) failed\n",
                i
            );
            return false;
        }
    }
    vulkan->swapchain_framebuffers_count = vulkan->swapchain_imageviews_count;

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_commandpool_create(struct vulkan *vulkan) {
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vulkan->graphics_queuefamily_index,
    };

    if (vkCreateCommandPool(
        vulkan->device, &create_info, nullptr, &vulkan->command_pool
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_commanpool_create: vkCreateCommandPool failed\n");
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_commandbuffer_allocate(struct vulkan *vulkan) {
    VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vulkan->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(
        vulkan->device, &allocate_info, &vulkan->command_buffer
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_commanbuffer_create: vkAllocateCommandBuffers failed\n");
        return false;
    }

    return true;
}

/// @param[in] vulkan
/// @param[in] command_buffer
/// @param[in] framebuffer_index
/// @return `true` on success and `false` otherwise
static bool vulkan_commandbuffer_record(
    const struct vulkan *vulkan,
    VkCommandBuffer command_buffer,
    uint32_t framebuffer_index
) {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_commandbuffer_record: vkBeginCommandBuffer failed\n");
        return false;
    }

    VkClearValue clear_color = {
        .color = {
            {0.0f, 0.0f, 0.0f, 1.0f},
        },
    };

    VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vulkan->render_pass,
        .framebuffer = vulkan->swapchain_framebuffers[framebuffer_index],
        .renderArea = {
            .offset = {0, 0},
            .extent = vulkan->swapchain_extent,
        },
        .pClearValues = &clear_color,
        .clearValueCount = 1,
    };

    vkCmdBeginRenderPass(
        command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan->graphics_pipeline
    );

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float) vulkan->swapchain_extent.width,
        .height = (float) vulkan->swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = vulkan->swapchain_extent,
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    vkCmdDraw(command_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_commandbuffer_record: vkEndCommandBuffer failed\n");
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_synchronizationobjects_create(struct vulkan *vulkan) {
    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    if (vkCreateSemaphore(
        vulkan->device,
        &semaphore_create_info,
        nullptr,
        &vulkan->swapchain_image_available
    ) != VK_SUCCESS) {
        fprintf(
            stderr,
            "vulkan_scynhronizationobjects_create: "
            "vkCreateSempaphore(image_available) failed"
        );
        return false;
    }

    if (vkCreateSemaphore(
        vulkan->device, &semaphore_create_info, nullptr, &vulkan->render_finished
    ) != VK_SUCCESS) {
        fprintf(
            stderr,
            "vulkan_scynhronizationobjects_create: "
            "vkCreateSempaphore(render_finished) failed"
        );
        return false;
    }

    VkFenceCreateInfo frames_in_flight_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    if (vkCreateFence(
        vulkan->device, &frames_in_flight_create_info, nullptr, &vulkan->frame_in_flight
    ) != VK_SUCCESS) {
        fprintf(
            stderr,
            "vulkan_scynhronizationobjects_create: "
            "vkCreateFence(frames_in_flight) failed"
        );
        return false;
    }

    return true;
}

/// @param[in] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_frame_draw(const struct vulkan *vulkan) {
    vkWaitForFences(vulkan->device, 1, &vulkan->frame_in_flight, VK_TRUE, UINT32_MAX);
    vkResetFences(vulkan->device, 1, &vulkan->frame_in_flight);

    uint32_t swapchain_image_index;
    if (vkAcquireNextImageKHR(
        vulkan->device,
        vulkan->swapchain,
        UINT32_MAX,
        vulkan->swapchain_image_available,
        VK_NULL_HANDLE,
        &swapchain_image_index
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_frame_draw: vkAcquireNextImageKHR failed\n");
        return false;
    }

    if (vkResetCommandBuffer(vulkan->command_buffer, 0) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_frame_draw: vkResetCommandBuffer failed\n");
        return false;
    }

    if (!vulkan_commandbuffer_record(
        vulkan, vulkan->command_buffer, swapchain_image_index
    )) {
        fprintf(stderr, "vulkan_frame_draw: vulkan_commandbuffer failed\n");
        return false;
    }

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pWaitSemaphores = &vulkan->swapchain_image_available,
        .waitSemaphoreCount = 1,
        .pWaitDstStageMask = &(VkPipelineStageFlags){
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        },
        .pCommandBuffers = &vulkan->command_buffer,
        .commandBufferCount = 1,
        .pSignalSemaphores = &vulkan->render_finished,
        .signalSemaphoreCount = 1,
    };

    if (vkQueueSubmit(
        vulkan->graphics_queue, 1, &submit_info, vulkan->frame_in_flight
    ) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_frame_draw: vkQueueSubmit failed\n");
        return false;
    }

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pWaitSemaphores = &vulkan->render_finished,
        .waitSemaphoreCount = 1,
        .pSwapchains = &vulkan->swapchain,
        .swapchainCount = 1,
        .pImageIndices = &swapchain_image_index,
        .pResults = nullptr,
    };

    if (vkQueuePresentKHR(vulkan->present_queue, &present_info) != VK_SUCCESS) {
        fprintf(stderr, "vulkan_frame_draw: vkQueuePresentKHR failed\n");
        return false;
    }

    return true;
}

/// @param[in,out] vulkan
/// @return `true` on success and `false` otherwise
static bool vulkan_init(struct vulkan *vulkan) {
    if (!vulkan_instance_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_instance_create failed\n");
        return false;
    }

    if (!vulkan_surface_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_surface_create failed\n");
        return false;
    }

    if (!vulkan_physicaldevice_find(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_physicaldevice_find failed\n");
        return false;
    }

    if (!vulkan_queuefamily_find(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_queuefamily_find failed\n");
        return false;
    }

    if (!vulkan_device_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_device_create failed\n");
        return false;
    }

    if (!vulkan_swapchain_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_swapchain_create failed\n");
        return false;
    }

    if (!vulkan_imageviews_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_imageviews_create failed\n");
        return false;
    }

    if (!vulkan_renderpass_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_renderpass_create failed\n");
        return false;
    }

    if (!vulkan_graphicspipeline_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_graphicspipeline_create failed\n");
        return false;
    }

    if (!vulkan_framebuffers_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_framebuffers_create failed\n");
        return false;
    }

    if (!vulkan_commandpool_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_commandpool_create failed\n");
        return false;
    }

    if (!vulkan_commandbuffer_allocate(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_commandbuffer_allocate failed\n");
        return false;
    }

    if (!vulkan_synchronizationobjects_create(vulkan)) {
        fprintf(stderr, "vulkan_init: vulkan_synchronizationobjects_create failed\n");
        return false;
    }

    return true;
}

struct application_config {
    const char *title;
    int width;
    int height;

    bool debug;
};

struct application {
    GLFWwindow *window;

    struct vulkan vulkan;
};

/// @param[in] config
/// @param[out] application
/// @return `true` on success and `false` otherwise
/// @note Caller is responsible to call `application_destroy` after
/// `application` is no longer needed
static bool application_create(
    const struct application_config *config,
    struct application *application
) {
    if (glfwInit() != GLFW_TRUE) {
        fprintf(stderr, "application_create: glfwInit failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(
        config->width,
        config->height,
        config->title,
        nullptr,
        nullptr
    );
    if (window == nullptr) {
        fprintf(stderr, "application_create: glfwCreateWindow failed\n");
        return false;
    }

    application->window = window;
    application->vulkan.window = window;
    application->vulkan.application_name = config->title;
    application->vulkan.enable_validation_layers = config->debug;

    if (!vulkan_init(&application->vulkan)) {
        fprintf(stderr, "application_create: vulkan_init failed\n");
        return false;
    }

    return true;
}

/// @param[in] application
/// @note `application` will be invalid after this function has been called
static void application_destroy(const struct application *application) {
    const struct vulkan *vulkan = &application->vulkan;

    vkDestroySemaphore(vulkan->device, vulkan->swapchain_image_available, nullptr);
    vkDestroySemaphore(vulkan->device, vulkan->render_finished, nullptr);
    vkDestroyFence(vulkan->device, vulkan->frame_in_flight, nullptr);
    vkDestroyCommandPool(vulkan->device, vulkan->command_pool, nullptr);
    for (size_t i = 0; i < application->vulkan.swapchain_framebuffers_count; i++) {
      vkDestroyFramebuffer(vulkan->device, vulkan->swapchain_framebuffers[i], nullptr);
    }
    vkDestroyPipeline(vulkan->device, vulkan->graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(vulkan->device, vulkan->pipeline_layout, nullptr);
    vkDestroyRenderPass(vulkan->device, vulkan->render_pass, nullptr);
    for (size_t i = 0; i < vulkan->swapchain_imageviews_count; i++) {
      vkDestroyImageView(vulkan->device, vulkan->swapchain_imageviews[i], nullptr);
    }
    vkDestroySwapchainKHR(vulkan->device, vulkan->swapchain, nullptr);
    vkDestroyDevice(vulkan->device, nullptr);
    vkDestroySurfaceKHR(vulkan->instance, vulkan->surface, nullptr);
    vkDestroyInstance(vulkan->instance, nullptr);
    glfwDestroyWindow(application->window);
    glfwTerminate();
}

/// @param[in] application
/// @return `true` on success and `false` otherwise
static bool application_mainloop(const struct application *application) {
    while (!glfwWindowShouldClose(application->window)) {
        glfwPollEvents();

        if (!vulkan_frame_draw(&application->vulkan)) {
            fprintf(stderr, "application_mainloop: vulkan_drawframe failed\n");
        }
    }

    return true;
}

int main(void) {
    int success = EXIT_FAILURE;

    struct application application = {};
    struct application_config config = {
        .title = "Vulkan test",
        .width = 1280,
        .height = 720,
        .debug = true,
    };

    if (!application_create(&config, &application)) {
        fprintf(stderr, "main: application_create failed\n");
        goto cleanup;
    }

    if (!application_mainloop(&application)) {
        fprintf(stderr, "main: application_mainloop failed\n");
        goto cleanup;
    }

    success = EXIT_SUCCESS;

cleanup:
    application_destroy(&application);

    return success;
}
