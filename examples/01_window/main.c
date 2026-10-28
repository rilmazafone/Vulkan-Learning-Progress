#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vk_helpers.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_TITLE "Vulkan C Lab - 01 Window (Escape to close)"
#define VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"
#define INITIAL_CLIENT_WIDTH 960
#define INITIAL_CLIENT_HEIGHT 540
#define SMOKE_TEST_FRAME_COUNT 3U

typedef struct App {
    GLFWwindow *window;
    bool glfw_initialized;
    bool framebuffer_resized;
    bool smoke_test;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
    VkQueue graphics_queue;
    VkQueue present_queue;

    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *image_views;
    VkRenderPass render_pass;
    VkFramebuffer *framebuffers;

    VkCommandPool command_pool;
    VkCommandBuffer *command_buffers;
    bool command_buffers_allocated;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence frame_in_flight;

    uint32_t frames_rendered;
} App;

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
               VkDebugUtilsMessageTypeFlagsEXT types,
               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
               void *user_data)
{
    (void)types;
    (void)user_data;

    const char *severity_name = "message";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U) {
        severity_name = "error";
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0U) {
        severity_name = "warning";
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0U) {
        severity_name = "info";
    }

    if (callback_data->pMessage != NULL) {
        fprintf(stderr,
                "[Vulkan validation %s] %s\n",
                severity_name,
                callback_data->pMessage);
    }
    return VK_FALSE;
}

static void glfw_error_callback(int error_code, const char *description)
{
    fprintf(stderr,
            "GLFW error 0x%08X: %s\n",
            (unsigned int)error_code,
            description != NULL ? description : "unknown error");
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void)width;
    (void)height;

    App *app = (App *)glfwGetWindowUserPointer(window);
    if (app != NULL) {
        app->framebuffer_resized = true;
    }
}

static void key_callback(GLFWwindow *window,
                         int key,
                         int scancode,
                         int action,
                         int mods)
{
    (void)scancode;
    (void)mods;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

static bool create_window(App *app)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        fprintf(stderr, "GLFW initialization failed.\n");
        return false;
    }
    app->glfw_initialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    app->window = glfwCreateWindow(INITIAL_CLIENT_WIDTH,
                                   INITIAL_CLIENT_HEIGHT,
                                   WINDOW_TITLE,
                                   NULL,
                                   NULL);
    if (app->window == NULL) {
        fprintf(stderr, "GLFW could not create a window.\n");
        return false;
    }

    glfwSetWindowUserPointer(app->window, app);
    glfwSetFramebufferSizeCallback(app->window, framebuffer_size_callback);
    glfwSetKeyCallback(app->window, key_callback);
    return true;
}

static VkDebugUtilsMessengerEXT
create_debug_messenger(VkInstance instance)
{
    PFN_vkCreateDebugUtilsMessengerEXT create_messenger =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
    if (create_messenger == NULL) {
        return VK_NULL_HANDLE;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info = {0};
    create_info.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = debug_callback;

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkResult result =
        create_messenger(instance, &create_info, NULL, &messenger);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "Could not create the debug messenger: %s\n",
                vk_result_name(result));
        return VK_NULL_HANDLE;
    }
    return messenger;
}

static bool create_instance(App *app)
{
    const bool validation_available =
        vk_instance_layer_available(VALIDATION_LAYER_NAME);
    const bool debug_utils_available =
        vk_instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    uint32_t glfw_extension_count = 0;
    const char **glfw_extensions =
        glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (glfw_extensions == NULL || glfw_extension_count == 0) {
        fprintf(stderr,
                "GLFW did not provide the required Vulkan instance "
                "extensions.\n");
        return false;
    }

    const bool enable_debug_messenger =
        validation_available && debug_utils_available;
    const size_t extension_capacity =
        (size_t)glfw_extension_count +
        (enable_debug_messenger ? (size_t)1 : (size_t)0);
    const char **enabled_extensions =
        (const char **)calloc(extension_capacity,
                             sizeof(*enabled_extensions));
    if (enabled_extensions == NULL) {
        fprintf(stderr,
                "Could not allocate Vulkan instance extension names.\n");
        return false;
    }

    const char *enabled_layers[1] = {0};
    uint32_t enabled_layer_count = 0;

    if (validation_available) {
        enabled_layers[enabled_layer_count] = VALIDATION_LAYER_NAME;
        ++enabled_layer_count;
    } else {
        fprintf(stderr,
                "Warning: %s is unavailable.\n",
                VALIDATION_LAYER_NAME);
    }

    for (uint32_t index = 0; index < glfw_extension_count; ++index) {
        enabled_extensions[index] = glfw_extensions[index];
    }
    uint32_t enabled_extension_count = glfw_extension_count;
    if (enable_debug_messenger) {
        enabled_extensions[enabled_extension_count] =
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        ++enabled_extension_count;
    }

    VkApplicationInfo application_info = {0};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "Vulkan C Lab";
    application_info.applicationVersion = VK_MAKE_API_VERSION(0, 0, 2, 0);
    application_info.pEngineName = "No engine";
    application_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 2, 0);
    application_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledLayerCount = enabled_layer_count;
    instance_info.ppEnabledLayerNames = enabled_layers;
    instance_info.enabledExtensionCount = enabled_extension_count;
    instance_info.ppEnabledExtensionNames = enabled_extensions;

    const VkResult instance_result =
        vkCreateInstance(&instance_info, NULL, &app->instance);
    free(enabled_extensions);
    if (!vk_require(instance_result, "vkCreateInstance")) {
        return false;
    }

    if (enable_debug_messenger) {
        app->debug_messenger = create_debug_messenger(app->instance);
    }

    const VkResult surface_result =
        glfwCreateWindowSurface(app->instance,
                                app->window,
                                NULL,
                                &app->surface);
    if (!vk_require(surface_result, "glfwCreateWindowSurface")) {
        return false;
    }

    printf("Vulkan instance and GLFW surface created.\n");
    printf("Validation: %s\n", validation_available ? "enabled" : "unavailable");
    return true;
}

static bool physical_device_extension_available(
    VkPhysicalDevice device,
    const char *extension_name)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(
            device, NULL, &count, NULL) != VK_SUCCESS ||
        count == 0) {
        return false;
    }

    VkExtensionProperties *extensions =
        (VkExtensionProperties *)calloc((size_t)count, sizeof(*extensions));
    if (extensions == NULL) {
        return false;
    }

    const VkResult result =
        vkEnumerateDeviceExtensionProperties(device,
                                             NULL,
                                             &count,
                                             extensions);
    if (result != VK_SUCCESS) {
        free(extensions);
        return false;
    }

    bool found = false;
    for (uint32_t index = 0; index < count; ++index) {
        if (strcmp(extensions[index].extensionName, extension_name) == 0) {
            found = true;
            break;
        }
    }

    free(extensions);
    return found;
}

static bool choose_queue_families(App *app)
{
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        app->physical_device, &family_count, NULL);
    if (family_count == 0) {
        return false;
    }

    VkQueueFamilyProperties *families =
        (VkQueueFamilyProperties *)calloc(
            (size_t)family_count, sizeof(*families));
    if (families == NULL) {
        fprintf(stderr, "Could not allocate queue family data.\n");
        return false;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(
        app->physical_device, &family_count, families);

    app->graphics_queue_family = UINT32_MAX;
    app->present_queue_family = UINT32_MAX;

    for (uint32_t family_index = 0;
         family_index < family_count;
         ++family_index) {
        VkBool32 supports_present = VK_FALSE;
        const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
            app->physical_device,
            family_index,
            app->surface,
            &supports_present);
        if (result != VK_SUCCESS) {
            continue;
        }

        const bool supports_graphics =
            (families[family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U;
        if (supports_graphics && supports_present == VK_TRUE) {
            app->graphics_queue_family = family_index;
            app->present_queue_family = family_index;
            free(families);
            return true;
        }
    }

    for (uint32_t family_index = 0;
         family_index < family_count;
         ++family_index) {
        if (app->graphics_queue_family == UINT32_MAX &&
            (families[family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            app->graphics_queue_family = family_index;
        }

        VkBool32 supports_present = VK_FALSE;
        if (app->present_queue_family == UINT32_MAX &&
            vkGetPhysicalDeviceSurfaceSupportKHR(app->physical_device,
                                                 family_index,
                                                 app->surface,
                                                 &supports_present) ==
                VK_SUCCESS &&
            supports_present == VK_TRUE) {
            app->present_queue_family = family_index;
        }
    }

    free(families);
    return app->graphics_queue_family != UINT32_MAX &&
           app->present_queue_family != UINT32_MAX;
}

static bool choose_physical_device(App *app)
{
    uint32_t device_count = 0;
    if (!vk_require(vkEnumeratePhysicalDevices(app->instance,
                                               &device_count,
                                               NULL),
                    "vkEnumeratePhysicalDevices (count)")) {
        return false;
    }
    if (device_count == 0) {
        fprintf(stderr, "No Vulkan physical devices were found.\n");
        return false;
    }

    VkPhysicalDevice *devices =
        (VkPhysicalDevice *)calloc((size_t)device_count, sizeof(*devices));
    if (devices == NULL) {
        fprintf(stderr, "Could not allocate the physical device list.\n");
        return false;
    }

    if (!vk_require(vkEnumeratePhysicalDevices(app->instance,
                                               &device_count,
                                               devices),
                    "vkEnumeratePhysicalDevices")) {
        free(devices);
        return false;
    }

    for (uint32_t device_index = 0;
         device_index < device_count;
         ++device_index) {
        if (!physical_device_extension_available(
                devices[device_index], VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        app->physical_device = devices[device_index];
        if (choose_queue_families(app)) {
            break;
        }
        app->physical_device = VK_NULL_HANDLE;
    }
    free(devices);

    if (app->physical_device == VK_NULL_HANDLE) {
        fprintf(stderr,
                "No Vulkan device supports graphics, presentation, and "
                "VK_KHR_swapchain.\n");
        return false;
    }

    VkPhysicalDeviceProperties properties = {0};
    vkGetPhysicalDeviceProperties(app->physical_device, &properties);
    printf("Using device: %s\n", properties.deviceName);
    return true;
}

static bool create_logical_device(App *app)
{
    const float queue_priority = 1.0F;
    VkDeviceQueueCreateInfo queue_infos[2] = {0};
    uint32_t queue_info_count = 0;

    queue_infos[queue_info_count].sType =
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_infos[queue_info_count].queueFamilyIndex =
        app->graphics_queue_family;
    queue_infos[queue_info_count].queueCount = 1;
    queue_infos[queue_info_count].pQueuePriorities = &queue_priority;
    ++queue_info_count;

    if (app->present_queue_family != app->graphics_queue_family) {
        queue_infos[queue_info_count].sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[queue_info_count].queueFamilyIndex =
            app->present_queue_family;
        queue_infos[queue_info_count].queueCount = 1;
        queue_infos[queue_info_count].pQueuePriorities = &queue_priority;
        ++queue_info_count;
    }

    const char *device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    VkPhysicalDeviceFeatures features = {0};

    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = queue_info_count;
    device_info.pQueueCreateInfos = queue_infos;
    device_info.enabledExtensionCount =
        (uint32_t)VK_ARRAY_COUNT(device_extensions);
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &features;

    if (!vk_require(vkCreateDevice(app->physical_device,
                                   &device_info,
                                   NULL,
                                   &app->device),
                    "vkCreateDevice")) {
        return false;
    }

    vkGetDeviceQueue(app->device,
                     app->graphics_queue_family,
                     0,
                     &app->graphics_queue);
    vkGetDeviceQueue(app->device,
                     app->present_queue_family,
                     0,
                     &app->present_queue);
    return true;
}

static bool create_command_pool(App *app)
{
    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = app->graphics_queue_family;

    return vk_require(vkCreateCommandPool(app->device,
                                          &pool_info,
                                          NULL,
                                          &app->command_pool),
                      "vkCreateCommandPool");
}

static bool create_sync_objects(App *app)
{
    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (!vk_require(vkCreateSemaphore(app->device,
                                      &semaphore_info,
                                      NULL,
                                      &app->image_available),
                    "vkCreateSemaphore (image available)")) {
        return false;
    }
    if (!vk_require(vkCreateSemaphore(app->device,
                                      &semaphore_info,
                                      NULL,
                                      &app->render_finished),
                    "vkCreateSemaphore (render finished)")) {
        return false;
    }
    return vk_require(vkCreateFence(app->device,
                                    &fence_info,
                                    NULL,
                                    &app->frame_in_flight),
                      "vkCreateFence");
}

static VkSurfaceFormatKHR
choose_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count)
{
    for (uint32_t index = 0; index < count; ++index) {
        if (formats[index].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[index];
        }
    }
    return formats[0];
}

static VkExtent2D
choose_swapchain_extent(const VkSurfaceCapabilitiesKHR *capabilities,
                        GLFWwindow *window)
{
    if (capabilities->currentExtent.width != UINT32_MAX) {
        return capabilities->currentExtent;
    }

    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window,
                           &framebuffer_width,
                           &framebuffer_height);
    VkExtent2D extent = {0, 0};
    if (framebuffer_width > 0) {
        extent.width = (uint32_t)framebuffer_width;
    }
    if (framebuffer_height > 0) {
        extent.height = (uint32_t)framebuffer_height;
    }

    if (extent.width < capabilities->minImageExtent.width) {
        extent.width = capabilities->minImageExtent.width;
    }
    if (extent.width > capabilities->maxImageExtent.width) {
        extent.width = capabilities->maxImageExtent.width;
    }
    if (extent.height < capabilities->minImageExtent.height) {
        extent.height = capabilities->minImageExtent.height;
    }
    if (extent.height > capabilities->maxImageExtent.height) {
        extent.height = capabilities->maxImageExtent.height;
    }
    return extent;
}

static VkSurfaceTransformFlagBitsKHR
choose_surface_transform(const VkSurfaceCapabilitiesKHR *capabilities)
{
    if ((capabilities->supportedTransforms &
         VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0U) {
        return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
    return capabilities->currentTransform;
}

static VkCompositeAlphaFlagBitsKHR
choose_composite_alpha(const VkSurfaceCapabilitiesKHR *capabilities)
{
    const VkCompositeAlphaFlagBitsKHR choices[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (uint32_t index = 0; index < (uint32_t)VK_ARRAY_COUNT(choices); ++index) {
        if ((capabilities->supportedCompositeAlpha & choices[index]) != 0U) {
            return choices[index];
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static bool create_render_pass(App *app)
{
    VkAttachmentDescription color_attachment = {0};
    color_attachment.format = app->swapchain_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_reference = {0};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    return vk_require(vkCreateRenderPass(app->device,
                                         &render_pass_info,
                                         NULL,
                                         &app->render_pass),
                      "vkCreateRenderPass");
}

static bool create_swapchain(App *app)
{
    VkSurfaceCapabilitiesKHR capabilities = {0};
    if (!vk_require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                        app->physical_device,
                        app->surface,
                        &capabilities),
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return false;
    }

    uint32_t format_count = 0;
    if (!vk_require(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        app->physical_device,
                        app->surface,
                        &format_count,
                        NULL),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR (count)") ||
        format_count == 0) {
        fprintf(stderr, "The surface has no supported formats.\n");
        return false;
    }

    VkSurfaceFormatKHR *formats =
        (VkSurfaceFormatKHR *)calloc(
            (size_t)format_count, sizeof(*formats));
    if (formats == NULL) {
        fprintf(stderr, "Could not allocate surface format data.\n");
        return false;
    }

    if (!vk_require(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        app->physical_device,
                        app->surface,
                        &format_count,
                        formats),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        free(formats);
        return false;
    }
    const VkSurfaceFormatKHR surface_format =
        choose_surface_format(formats, format_count);
    free(formats);

    VkExtent2D extent = choose_swapchain_extent(&capabilities, app->window);
    if (extent.width == 0 || extent.height == 0) {
        fprintf(stderr, "The window client area has zero size.\n");
        return false;
    }

    uint32_t image_count = capabilities.minImageCount + 1U;
    if (capabilities.maxImageCount > 0 &&
        image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    const uint32_t queue_family_indices[] = {
        app->graphics_queue_family,
        app->present_queue_family,
    };
    const bool separate_queues =
        app->graphics_queue_family != app->present_queue_family;

    VkSwapchainCreateInfoKHR swapchain_info = {0};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = app->surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode =
        separate_queues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.queueFamilyIndexCount =
        separate_queues ? (uint32_t)VK_ARRAY_COUNT(queue_family_indices) : 0U;
    swapchain_info.pQueueFamilyIndices =
        separate_queues ? queue_family_indices : NULL;
    swapchain_info.preTransform = choose_surface_transform(&capabilities);
    swapchain_info.compositeAlpha = choose_composite_alpha(&capabilities);
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = VK_NULL_HANDLE;

    if (!vk_require(vkCreateSwapchainKHR(app->device,
                                         &swapchain_info,
                                         NULL,
                                         &app->swapchain),
                    "vkCreateSwapchainKHR")) {
        return false;
    }

    uint32_t actual_image_count = 0;
    if (!vk_require(vkGetSwapchainImagesKHR(app->device,
                                            app->swapchain,
                                            &actual_image_count,
                                            NULL),
                    "vkGetSwapchainImagesKHR (count)") ||
        actual_image_count == 0) {
        return false;
    }

    app->images =
        (VkImage *)calloc((size_t)actual_image_count, sizeof(*app->images));
    if (app->images == NULL) {
        fprintf(stderr, "Could not allocate swapchain image data.\n");
        return false;
    }

    if (!vk_require(vkGetSwapchainImagesKHR(app->device,
                                            app->swapchain,
                                            &actual_image_count,
                                            app->images),
                    "vkGetSwapchainImagesKHR")) {
        return false;
    }

    app->image_count = actual_image_count;
    app->swapchain_format = surface_format.format;
    app->swapchain_extent = extent;

    if (!create_render_pass(app)) {
        return false;
    }

    app->image_views =
        (VkImageView *)calloc((size_t)app->image_count,
                              sizeof(*app->image_views));
    app->framebuffers =
        (VkFramebuffer *)calloc((size_t)app->image_count,
                                sizeof(*app->framebuffers));
    app->command_buffers =
        (VkCommandBuffer *)calloc((size_t)app->image_count,
                                  sizeof(*app->command_buffers));
    if (app->image_views == NULL || app->framebuffers == NULL ||
        app->command_buffers == NULL) {
        fprintf(stderr, "Could not allocate swapchain resource arrays.\n");
        return false;
    }

    for (uint32_t image_index = 0;
         image_index < app->image_count;
         ++image_index) {
        VkImageViewCreateInfo view_info = {0};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = app->images[image_index];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = app->swapchain_format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (!vk_require(vkCreateImageView(app->device,
                                          &view_info,
                                          NULL,
                                          &app->image_views[image_index]),
                        "vkCreateImageView")) {
            return false;
        }

        const VkImageView attachments[] = {
            app->image_views[image_index],
        };
        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType =
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = app->render_pass;
        framebuffer_info.attachmentCount =
            (uint32_t)VK_ARRAY_COUNT(attachments);
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = app->swapchain_extent.width;
        framebuffer_info.height = app->swapchain_extent.height;
        framebuffer_info.layers = 1;

        if (!vk_require(vkCreateFramebuffer(app->device,
                                            &framebuffer_info,
                                            NULL,
                                            &app->framebuffers[image_index]),
                        "vkCreateFramebuffer")) {
            return false;
        }
    }

    VkCommandBufferAllocateInfo command_info = {0};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandPool = app->command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = app->image_count;

    if (!vk_require(vkAllocateCommandBuffers(app->device,
                                             &command_info,
                                             app->command_buffers),
                    "vkAllocateCommandBuffers")) {
        return false;
    }
    app->command_buffers_allocated = true;

    printf("Swapchain: %" PRIu32 "x%" PRIu32
           ", %" PRIu32 " images, format %d\n",
           app->swapchain_extent.width,
           app->swapchain_extent.height,
           app->image_count,
           (int)app->swapchain_format);
    app->framebuffer_resized = false;
    return true;
}

static void destroy_swapchain(App *app)
{
    if (app->device != VK_NULL_HANDLE && app->command_pool != VK_NULL_HANDLE &&
        app->command_buffers_allocated) {
        vkFreeCommandBuffers(app->device,
                             app->command_pool,
                             app->image_count,
                             app->command_buffers);
    }

    if (app->device != VK_NULL_HANDLE && app->framebuffers != NULL) {
        for (uint32_t index = 0; index < app->image_count; ++index) {
            if (app->framebuffers[index] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(app->device,
                                     app->framebuffers[index],
                                     NULL);
            }
        }
    }

    if (app->device != VK_NULL_HANDLE && app->image_views != NULL) {
        for (uint32_t index = 0; index < app->image_count; ++index) {
            if (app->image_views[index] != VK_NULL_HANDLE) {
                vkDestroyImageView(app->device,
                                   app->image_views[index],
                                   NULL);
            }
        }
    }

    if (app->device != VK_NULL_HANDLE &&
        app->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(app->device, app->render_pass, NULL);
    }

    if (app->device != VK_NULL_HANDLE &&
        app->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
    }

    free(app->command_buffers);
    free(app->framebuffers);
    free(app->image_views);
    free(app->images);
    app->command_buffers = NULL;
    app->framebuffers = NULL;
    app->image_views = NULL;
    app->images = NULL;
    app->image_count = 0;
    app->command_buffers_allocated = false;
    app->render_pass = VK_NULL_HANDLE;
    app->swapchain = VK_NULL_HANDLE;
}

static bool recreate_swapchain(App *app)
{
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(app->window,
                           &framebuffer_width,
                           &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) {
        return true;
    }

    if (!vk_require(vkDeviceWaitIdle(app->device), "vkDeviceWaitIdle")) {
        return false;
    }

    destroy_swapchain(app);
    return create_swapchain(app);
}

static bool record_clear_command(App *app,
                                 uint32_t image_index,
                                 float phase)
{
    VkCommandBuffer command_buffer = app->command_buffers[image_index];
    if (!vk_require(vkResetCommandBuffer(command_buffer, 0),
                    "vkResetCommandBuffer")) {
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!vk_require(vkBeginCommandBuffer(command_buffer, &begin_info),
                    "vkBeginCommandBuffer")) {
        return false;
    }

    VkClearValue clear_value = {0};
    clear_value.color.float32[0] = 0.025F + (0.06F * phase);
    clear_value.color.float32[1] = 0.065F + (0.12F * phase);
    clear_value.color.float32[2] = 0.14F + (0.22F * phase);
    clear_value.color.float32[3] = 1.0F;

    VkRenderPassBeginInfo render_pass_info = {0};
    render_pass_info.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = app->render_pass;
    render_pass_info.framebuffer = app->framebuffers[image_index];
    render_pass_info.renderArea.offset.x = 0;
    render_pass_info.renderArea.offset.y = 0;
    render_pass_info.renderArea.extent = app->swapchain_extent;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command_buffer,
                         &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(command_buffer);

    return vk_require(vkEndCommandBuffer(command_buffer),
                      "vkEndCommandBuffer");
}

static bool render_frame(App *app)
{
    if (app->framebuffer_resized) {
        if (!recreate_swapchain(app)) {
            return false;
        }
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(app->window,
                               &framebuffer_width,
                               &framebuffer_height);
        if (framebuffer_width <= 0 || framebuffer_height <= 0) {
            return true;
        }
    }

    if (!vk_require(vkWaitForFences(app->device,
                                    1,
                                    &app->frame_in_flight,
                                    VK_TRUE,
                                    UINT64_MAX),
                    "vkWaitForFences")) {
        return false;
    }

    uint32_t image_index = 0;
    const VkResult acquire_result =
        vkAcquireNextImageKHR(app->device,
                              app->swapchain,
                              UINT64_MAX,
                              app->image_available,
                              VK_NULL_HANDLE,
                              &image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        app->framebuffer_resized = true;
        return true;
    }
    if (acquire_result == VK_SUBOPTIMAL_KHR) {
        app->framebuffer_resized = true;
    } else if (acquire_result != VK_SUCCESS) {
        fprintf(stderr,
                "vkAcquireNextImageKHR failed with %s (%d)\n",
                vk_result_name(acquire_result),
                (int)acquire_result);
        return false;
    }

    if (!vk_require(vkResetFences(app->device,
                                  1,
                                  &app->frame_in_flight),
                    "vkResetFences")) {
        return false;
    }

    const uint32_t phase_index = app->frames_rendered % 240U;
    const uint32_t triangle_phase =
        phase_index < 120U ? phase_index : 240U - phase_index;
    const float phase = (float)triangle_phase / 120.0F;
    if (!record_clear_command(app, image_index, phase)) {
        return false;
    }

    const VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSemaphore wait_semaphores[] = {app->image_available};
    const VkSemaphore signal_semaphores[] = {app->render_finished};

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount =
        (uint32_t)VK_ARRAY_COUNT(wait_semaphores);
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &app->command_buffers[image_index];
    submit_info.signalSemaphoreCount =
        (uint32_t)VK_ARRAY_COUNT(signal_semaphores);
    submit_info.pSignalSemaphores = signal_semaphores;

    if (!vk_require(vkQueueSubmit(app->graphics_queue,
                                  1,
                                  &submit_info,
                                  app->frame_in_flight),
                    "vkQueueSubmit")) {
        return false;
    }

    const VkSwapchainKHR swapchains[] = {app->swapchain};
    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount =
        (uint32_t)VK_ARRAY_COUNT(signal_semaphores);
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount =
        (uint32_t)VK_ARRAY_COUNT(swapchains);
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;
    present_info.pResults = NULL;

    const VkResult present_result =
        vkQueuePresentKHR(app->present_queue, &present_info);

    /*
     * One frame is in flight at a time. Waiting here makes it safe to reuse the
     * same two semaphores and keeps this first window example easy to follow.
     */
    if (!vk_require(vkQueueWaitIdle(app->present_queue),
                    "vkQueueWaitIdle (present)")) {
        return false;
    }

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_SUBOPTIMAL_KHR) {
        app->framebuffer_resized = true;
    } else if (present_result != VK_SUCCESS) {
        fprintf(stderr,
                "vkQueuePresentKHR failed with %s (%d)\n",
                vk_result_name(present_result),
                (int)present_result);
        return false;
    }

    ++app->frames_rendered;
    return true;
}

static bool run_window_loop(App *app)
{
    while (glfwWindowShouldClose(app->window) == GLFW_FALSE) {
        glfwPollEvents();

        if (glfwGetWindowAttrib(app->window, GLFW_ICONIFIED) == GLFW_TRUE) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        if (!render_frame(app)) {
            return false;
        }

        if (app->smoke_test &&
            app->frames_rendered >= SMOKE_TEST_FRAME_COUNT) {
            printf("Smoke test rendered %" PRIu32 " frames.\n",
                   app->frames_rendered);
            break;
        }
    }
    return true;
}

static void shutdown_vulkan(App *app)
{
    if (app->device != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(app->device);
        destroy_swapchain(app);

        if (app->image_available != VK_NULL_HANDLE) {
            vkDestroySemaphore(app->device, app->image_available, NULL);
        }
        if (app->render_finished != VK_NULL_HANDLE) {
            vkDestroySemaphore(app->device, app->render_finished, NULL);
        }
        if (app->frame_in_flight != VK_NULL_HANDLE) {
            vkDestroyFence(app->device, app->frame_in_flight, NULL);
        }
        if (app->command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(app->device, app->command_pool, NULL);
        }
        vkDestroyDevice(app->device, NULL);
    }

    if (app->instance != VK_NULL_HANDLE &&
        app->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(app->instance, app->surface, NULL);
    }

    if (app->instance != VK_NULL_HANDLE &&
        app->debug_messenger != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                app->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger != NULL) {
            destroy_messenger(app->instance, app->debug_messenger, NULL);
        }
    }

    if (app->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(app->instance, NULL);
    }
}

int main(int argc, char **argv)
{
    App app = {0};
    bool success = false;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--smoke-test") == 0) {
            app.smoke_test = true;
        }
    }

    if (!create_window(&app)) {
        goto cleanup;
    }

    if (!create_instance(&app) ||
        !choose_physical_device(&app) ||
        !create_logical_device(&app) ||
        !create_command_pool(&app) ||
        !create_sync_objects(&app) ||
        !create_swapchain(&app)) {
        goto cleanup;
    }

    if (!app.smoke_test) {
        printf("Window is running. Close it or press Escape to exit.\n");
        fflush(stdout);
    }

    success = run_window_loop(&app);

cleanup:
    shutdown_vulkan(&app);
    if (app.window != NULL) {
        glfwDestroyWindow(app.window);
    }
    if (app.glfw_initialized) {
        glfwTerminate();
    }

    if (success) {
        printf("Vulkan window shut down cleanly.\n");
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
