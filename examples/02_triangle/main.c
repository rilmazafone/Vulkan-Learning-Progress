#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "obj_model.h"
#include "vk_helpers.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_TITLE "Vulkan C Lab - Arrows: orbit, Wheel: zoom, F: wireframe, R: reset"
#define VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"
#define DEFAULT_MODEL_PATH "assets/models/african_head.obj"
#define MODEL_VERTEX_SHADER_PATH "build/shaders/model.vert.spv"
#define MODEL_FRAGMENT_SHADER_PATH "build/shaders/model.frag.spv"
#define INITIAL_CLIENT_WIDTH 960
#define INITIAL_CLIENT_HEIGHT 540
#define SMOKE_TEST_FRAME_COUNT 3U
#define CAMERA_ORBIT_SPEED 1.4F
#define CAMERA_MIN_PITCH (-1.2F)
#define CAMERA_MAX_PITCH 1.2F
#define CAMERA_DEFAULT_DISTANCE 3.0F
#define CAMERA_MIN_DISTANCE 1.5F
#define CAMERA_MAX_DISTANCE 8.0F

typedef struct App {
    GLFWwindow *window;
    bool glfw_initialized;
    bool framebuffer_resized;
    bool smoke_test;
    bool camera_left;
    bool camera_right;
    bool camera_up;
    bool camera_down;
    bool wireframe;
    float camera_yaw;
    float camera_pitch;
    float camera_distance;
    double camera_last_update_time;

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
    VkFormat depth_format;
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    VkRenderPass render_pass;
    VkFramebuffer *framebuffers;

    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
    VkPipeline graphics_pipeline_wireframe;

    ObjModel model;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_buffer_memory;

    VkCommandPool command_pool;
    VkCommandBuffer *command_buffers;
    bool command_buffers_allocated;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence frame_in_flight;

    uint32_t frames_rendered;
} App;

typedef struct ModelPushConstants {
    float model_view_projection[16];
    float model[16];
} ModelPushConstants;

_Static_assert(sizeof(ObjVertex) == 24U,
               "ObjVertex must match the shader vertex layout");
_Static_assert(sizeof(ModelPushConstants) == 128U,
               "Model transforms must fit the 128-byte push constant minimum");

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
        return;
    }

    App *app = (App *)glfwGetWindowUserPointer(window);
    if (app == NULL) {
        return;
    }

    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        app->wireframe = !app->wireframe;
    } else if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        app->camera_yaw = 0.0F;
        app->camera_pitch = 0.0F;
        app->camera_distance = CAMERA_DEFAULT_DISTANCE;
    }

    const bool pressed = action != GLFW_RELEASE;
    if (key == GLFW_KEY_LEFT) {
        app->camera_left = pressed;
    } else if (key == GLFW_KEY_RIGHT) {
        app->camera_right = pressed;
    } else if (key == GLFW_KEY_UP) {
        app->camera_up = pressed;
    } else if (key == GLFW_KEY_DOWN) {
        app->camera_down = pressed;
    }
}

static void scroll_callback(GLFWwindow *window,
                            double x_offset,
                            double y_offset)
{
    (void)x_offset;
    App *app = (App *)glfwGetWindowUserPointer(window);
    if (app == NULL) {
        return;
    }

    app->camera_distance -= (float)y_offset * 0.25F;
    if (app->camera_distance < CAMERA_MIN_DISTANCE) {
        app->camera_distance = CAMERA_MIN_DISTANCE;
    } else if (app->camera_distance > CAMERA_MAX_DISTANCE) {
        app->camera_distance = CAMERA_MAX_DISTANCE;
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

    app->camera_last_update_time = glfwGetTime();
    app->camera_distance = CAMERA_DEFAULT_DISTANCE;
    glfwSetWindowUserPointer(app->window, app);
    glfwSetFramebufferSizeCallback(app->window, framebuffer_size_callback);
    glfwSetKeyCallback(app->window, key_callback);
    glfwSetScrollCallback(app->window, scroll_callback);
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
    features.fillModeNonSolid = VK_TRUE;

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

static uint32_t find_memory_type(App *app,
                                 uint32_t type_filter,
                                 VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties = {0};
    vkGetPhysicalDeviceMemoryProperties(app->physical_device, &memory_properties);

    for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((type_filter & (1U << index)) != 0U &&
            (memory_properties.memoryTypes[index].propertyFlags & properties) ==
                properties) {
            return index;
        }
    }
    return UINT32_MAX;
}

static bool create_buffer(App *app,
                          VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer *buffer,
                          VkDeviceMemory *memory)
{
    *buffer = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!vk_require(vkCreateBuffer(app->device, &buffer_info, NULL, buffer),
                    "vkCreateBuffer")) {
        return false;
    }

    VkMemoryRequirements requirements = {0};
    vkGetBufferMemoryRequirements(app->device, *buffer, &requirements);
    const uint32_t memory_type =
        find_memory_type(app, requirements.memoryTypeBits, properties);
    if (memory_type == UINT32_MAX) {
        fprintf(stderr, "No suitable Vulkan memory type for a buffer.\n");
        vkDestroyBuffer(app->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocation_info = {0};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.allocationSize = requirements.size;
    allocation_info.memoryTypeIndex = memory_type;
    if (!vk_require(vkAllocateMemory(app->device,
                                     &allocation_info,
                                     NULL,
                                     memory),
                    "vkAllocateMemory (buffer)")) {
        vkDestroyBuffer(app->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    if (!vk_require(vkBindBufferMemory(app->device, *buffer, *memory, 0),
                    "vkBindBufferMemory")) {
        vkFreeMemory(app->device, *memory, NULL);
        vkDestroyBuffer(app->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool copy_buffer(App *app,
                        VkBuffer source,
                        VkBuffer destination,
                        VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocation_info = {0};
    allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation_info.commandPool = app->command_pool;
    allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (!vk_require(vkAllocateCommandBuffers(app->device,
                                             &allocation_info,
                                             &command_buffer),
                    "vkAllocateCommandBuffers (buffer copy)")) {
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool success = vk_require(vkBeginCommandBuffer(command_buffer, &begin_info),
                              "vkBeginCommandBuffer (buffer copy)");
    if (success) {
        const VkBufferCopy copy_region = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size,
        };
        vkCmdCopyBuffer(command_buffer, source, destination, 1, &copy_region);

        VkBufferMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                                VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0,
                             0,
                             NULL,
                             1,
                             &barrier,
                             0,
                             NULL);
        success = vk_require(vkEndCommandBuffer(command_buffer),
                             "vkEndCommandBuffer (buffer copy)");
    }

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (success) {
        success = vk_require(vkCreateFence(app->device,
                                           &fence_info,
                                           NULL,
                                           &fence),
                             "vkCreateFence (buffer copy)");
    }

    if (success) {
        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        success = vk_require(vkQueueSubmit(app->graphics_queue,
                                           1,
                                           &submit_info,
                                           fence),
                             "vkQueueSubmit (buffer copy)") &&
                  vk_require(vkWaitForFences(app->device,
                                             1,
                                             &fence,
                                             VK_TRUE,
                                             UINT64_MAX),
                             "vkWaitForFences (buffer copy)");
    }

    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(app->device, fence, NULL);
    }
    vkFreeCommandBuffers(app->device, app->command_pool, 1, &command_buffer);
    return success;
}

static bool upload_model_buffer(App *app,
                                const void *data,
                                VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                VkBuffer *destination,
                                VkDeviceMemory *destination_memory)
{
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    if (!create_buffer(app,
                       size,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &staging_buffer,
                       &staging_memory)) {
        return false;
    }

    void *mapped = NULL;
    bool success = vk_require(vkMapMemory(app->device,
                                          staging_memory,
                                          0,
                                          size,
                                          0,
                                          &mapped),
                              "vkMapMemory (model upload)");
    if (success) {
        memcpy(mapped, data, (size_t)size);
        vkUnmapMemory(app->device, staging_memory);
    }

    if (success) {
        success = create_buffer(app,
                                size,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                destination,
                                destination_memory);
    }
    if (success) {
        success = copy_buffer(app, staging_buffer, *destination, size);
    }

    vkDestroyBuffer(app->device, staging_buffer, NULL);
    vkFreeMemory(app->device, staging_memory, NULL);
    return success;
}

static bool create_model_buffers(App *app)
{
    const VkDeviceSize vertex_size =
        (VkDeviceSize)app->model.vertex_count * (VkDeviceSize)sizeof(ObjVertex);
    const VkDeviceSize index_size =
        (VkDeviceSize)app->model.index_count * (VkDeviceSize)sizeof(uint32_t);

    if (!upload_model_buffer(app,
                             app->model.vertices,
                             vertex_size,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             &app->vertex_buffer,
                             &app->vertex_buffer_memory)) {
        return false;
    }
    return upload_model_buffer(app,
                               app->model.indices,
                               index_size,
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               &app->index_buffer,
                               &app->index_buffer_memory);
}

static void destroy_model_buffers(App *app)
{
    if (app->device == VK_NULL_HANDLE) {
        return;
    }
    if (app->index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(app->device, app->index_buffer, NULL);
    }
    if (app->index_buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(app->device, app->index_buffer_memory, NULL);
    }
    if (app->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(app->device, app->vertex_buffer, NULL);
    }
    if (app->vertex_buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(app->device, app->vertex_buffer_memory, NULL);
    }
    app->index_buffer = VK_NULL_HANDLE;
    app->index_buffer_memory = VK_NULL_HANDLE;
    app->vertex_buffer = VK_NULL_HANDLE;
    app->vertex_buffer_memory = VK_NULL_HANDLE;
}

static VkShaderModule create_shader_module(App *app, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open shader '%s'.\n", path);
        return VK_NULL_HANDLE;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return VK_NULL_HANDLE;
    }
    const long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return VK_NULL_HANDLE;
    }

    const size_t byte_count = (size_t)file_size;
    if ((byte_count % sizeof(uint32_t)) != 0U) {
        fprintf(stderr, "Shader '%s' is not valid SPIR-V.\n", path);
        fclose(file);
        return VK_NULL_HANDLE;
    }

    uint32_t *code = (uint32_t *)malloc(byte_count);
    if (code == NULL) {
        fclose(file);
        return VK_NULL_HANDLE;
    }
    const size_t read_count = fread(code, 1, byte_count, file);
    fclose(file);
    if (read_count != byte_count) {
        fprintf(stderr, "Could not read shader '%s'.\n", path);
        free(code);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo module_info = {0};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = byte_count;
    module_info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (!vk_require(vkCreateShaderModule(app->device,
                                         &module_info,
                                         NULL,
                                         &module),
                    "vkCreateShaderModule")) {
        module = VK_NULL_HANDLE;
    }
    free(code);
    return module;
}

static void matrix_identity(float matrix[16])
{
    memset(matrix, 0, 16U * sizeof(*matrix));
    matrix[0] = 1.0F;
    matrix[5] = 1.0F;
    matrix[10] = 1.0F;
    matrix[15] = 1.0F;
}

static void matrix_multiply(float result[16],
                            const float left[16],
                            const float right[16])
{
    float output[16] = {0};
    for (uint32_t column = 0; column < 4U; ++column) {
        for (uint32_t row = 0; row < 4U; ++row) {
            float value = 0.0F;
            for (uint32_t inner = 0; inner < 4U; ++inner) {
                value += left[(inner * 4U) + row] *
                         right[(column * 4U) + inner];
            }
            output[(column * 4U) + row] = value;
        }
    }
    memcpy(result, output, sizeof(output));
}

static void matrix_translation(float matrix[16],
                               float x,
                               float y,
                               float z)
{
    matrix_identity(matrix);
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
}

static void matrix_scale(float matrix[16], float scale)
{
    matrix_identity(matrix);
    matrix[0] = scale;
    matrix[5] = scale;
    matrix[10] = scale;
}

static void matrix_rotation_x(float matrix[16], float radians)
{
    matrix_identity(matrix);
    const float cosine = cosf(radians);
    const float sine = sinf(radians);
    matrix[5] = cosine;
    matrix[6] = sine;
    matrix[9] = -sine;
    matrix[10] = cosine;
}

static void matrix_rotation_y(float matrix[16], float radians)
{
    matrix_identity(matrix);
    const float cosine = cosf(radians);
    const float sine = sinf(radians);
    matrix[0] = cosine;
    matrix[2] = -sine;
    matrix[8] = sine;
    matrix[10] = cosine;
}

static void matrix_perspective(float matrix[16],
                               float field_of_view_radians,
                               float aspect_ratio,
                               float near_plane,
                               float far_plane)
{
    memset(matrix, 0, 16U * sizeof(*matrix));
    const float focal_length =
        1.0F / tanf(0.5F * field_of_view_radians);
    matrix[0] = focal_length / aspect_ratio;
    matrix[5] = -focal_length;
    matrix[10] = far_plane / (near_plane - far_plane);
    matrix[11] = -1.0F;
    matrix[14] =
        (far_plane * near_plane) / (near_plane - far_plane);
}

static void make_model_push_constants(const App *app,
                                      ModelPushConstants *constants)
{
    const float model_scale = 1.0F / app->model.radius;
    float translation[16];
    float scale[16];
    float view_pitch[16];
    float view_yaw[16];
    float view_rotation[16];
    float view_translation[16];
    float view[16];
    float projection[16];
    float model[16];
    float view_model[16];

    /* Model space -> a centered model roughly one unit in radius. */
    matrix_translation(translation,
                       -app->model.center[0],
                       -app->model.center[1],
                       -app->model.center[2]);
    matrix_scale(scale, model_scale);
    matrix_multiply(model, scale, translation);

    /*
     * The view matrix is the inverse camera transform. Orbit rotations happen
     * around the centered model, then the camera is moved three units back.
     */
    matrix_rotation_x(view_pitch, -app->camera_pitch);
    matrix_rotation_y(view_yaw, -app->camera_yaw);
    matrix_multiply(view_rotation, view_pitch, view_yaw);
    matrix_translation(view_translation,
                       0.0F,
                       0.0F,
                       -app->camera_distance);
    matrix_multiply(view, view_translation, view_rotation);
    matrix_perspective(projection,
                       0.78539816339F,
                       (float)app->swapchain_extent.width /
                           (float)app->swapchain_extent.height,
                       0.1F,
                       100.0F);
    matrix_multiply(view_model, view, model);
    matrix_multiply(constants->model_view_projection,
                    projection,
                    view_model);
    memcpy(constants->model, model, sizeof(constants->model));
}

static VkFormat find_depth_format(App *app)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (uint32_t index = 0; index < (uint32_t)VK_ARRAY_COUNT(candidates);
         ++index) {
        VkFormatProperties properties = {0};
        vkGetPhysicalDeviceFormatProperties(app->physical_device,
                                            candidates[index],
                                            &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
            return candidates[index];
        }
    }
    return VK_FORMAT_UNDEFINED;
}

static bool create_depth_resources(App *app)
{
    app->depth_format = find_depth_format(app);
    if (app->depth_format == VK_FORMAT_UNDEFINED) {
        fprintf(stderr, "No supported depth format was found.\n");
        return false;
    }

    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = app->swapchain_extent.width;
    image_info.extent.height = app->swapchain_extent.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = app->depth_format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!vk_require(vkCreateImage(app->device,
                                  &image_info,
                                  NULL,
                                  &app->depth_image),
                    "vkCreateImage (depth)")) {
        return false;
    }

    VkMemoryRequirements requirements = {0};
    vkGetImageMemoryRequirements(app->device,
                                 app->depth_image,
                                 &requirements);
    const uint32_t memory_type =
        find_memory_type(app,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        fprintf(stderr, "No suitable memory type for the depth image.\n");
        return false;
    }

    VkMemoryAllocateInfo allocation_info = {0};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.allocationSize = requirements.size;
    allocation_info.memoryTypeIndex = memory_type;
    if (!vk_require(vkAllocateMemory(app->device,
                                     &allocation_info,
                                     NULL,
                                     &app->depth_image_memory),
                    "vkAllocateMemory (depth)") ||
        !vk_require(vkBindImageMemory(app->device,
                                      app->depth_image,
                                      app->depth_image_memory,
                                      0),
                    "vkBindImageMemory (depth)")) {
        return false;
    }

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = app->depth_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = app->depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    return vk_require(vkCreateImageView(app->device,
                                        &view_info,
                                        NULL,
                                        &app->depth_image_view),
                      "vkCreateImageView (depth)");
}

static void destroy_depth_resources(App *app)
{
    if (app->device == VK_NULL_HANDLE) {
        return;
    }
    if (app->depth_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(app->device, app->depth_image_view, NULL);
    }
    if (app->depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(app->device, app->depth_image, NULL);
    }
    if (app->depth_image_memory != VK_NULL_HANDLE) {
        vkFreeMemory(app->device, app->depth_image_memory, NULL);
    }
    app->depth_format = VK_FORMAT_UNDEFINED;
    app->depth_image_view = VK_NULL_HANDLE;
    app->depth_image = VK_NULL_HANDLE;
    app->depth_image_memory = VK_NULL_HANDLE;
}

static bool create_graphics_pipeline(App *app,
                                     VkPolygonMode polygon_mode,
                                     VkPipeline *pipeline)
{
    VkShaderModule vertex_shader =
        create_shader_module(app, MODEL_VERTEX_SHADER_PATH);
    VkShaderModule fragment_shader =
        create_shader_module(app, MODEL_FRAGMENT_SHADER_PATH);
    if (vertex_shader == VK_NULL_HANDLE ||
        fragment_shader == VK_NULL_HANDLE) {
        if (vertex_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(app->device, vertex_shader, NULL);
        }
        if (fragment_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(app->device, fragment_shader, NULL);
        }
        return false;
    }

    bool success = true;
    if (app->pipeline_layout == VK_NULL_HANDLE) {
        VkPushConstantRange push_constant = {0};
        push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant.offset = 0;
        push_constant.size = (uint32_t)sizeof(ModelPushConstants);

        VkPipelineLayoutCreateInfo layout_info = {0};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant;
        success = vk_require(vkCreatePipelineLayout(app->device,
                                                    &layout_info,
                                                    NULL,
                                                    &app->pipeline_layout),
                             "vkCreatePipelineLayout");
    }

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = (uint32_t)sizeof(ObjVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2] = {0};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(ObjVertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(ObjVertex, normal);

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygon_mode;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {0};
    depth_stencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    blend_attachment.blendEnable = VK_FALSE;
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend = {0};
    color_blend.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        (uint32_t)VK_ARRAY_COUNT(dynamic_states);
    dynamic_state.pDynamicStates = dynamic_states;

    if (success) {
        VkGraphicsPipelineCreateInfo pipeline_info = {0};
        pipeline_info.sType =
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = (uint32_t)VK_ARRAY_COUNT(stages);
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = app->pipeline_layout;
        pipeline_info.renderPass = app->render_pass;
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_info.basePipelineIndex = -1;

        success = vk_require(vkCreateGraphicsPipelines(app->device,
                                                       VK_NULL_HANDLE,
                                                       1,
                                                       &pipeline_info,
                                                       NULL,
                                                       pipeline),
                             "vkCreateGraphicsPipelines");
    }

    vkDestroyShaderModule(app->device, fragment_shader, NULL);
    vkDestroyShaderModule(app->device, vertex_shader, NULL);
    return success;
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
    VkAttachmentDescription attachments[2] = {0};
    attachments[0].format = app->swapchain_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = app->depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_reference = {0};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_reference = {0};
    depth_reference.attachment = 1;
    depth_reference.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;
    subpass.pDepthStencilAttachment = &depth_reference;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount =
        (uint32_t)VK_ARRAY_COUNT(attachments);
    render_pass_info.pAttachments = attachments;
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

    if (!create_depth_resources(app) ||
        !create_render_pass(app) ||
        !create_graphics_pipeline(app,
                                  VK_POLYGON_MODE_FILL,
                                  &app->graphics_pipeline) ||
        !create_graphics_pipeline(app,
                                  VK_POLYGON_MODE_LINE,
                                  &app->graphics_pipeline_wireframe)) {
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

        const VkImageView framebuffer_attachments[] = {
            app->image_views[image_index],
            app->depth_image_view,
        };
        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType =
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = app->render_pass;
        framebuffer_info.attachmentCount =
            (uint32_t)VK_ARRAY_COUNT(framebuffer_attachments);
        framebuffer_info.pAttachments = framebuffer_attachments;
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
    if (app->device != VK_NULL_HANDLE &&
        app->graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->graphics_pipeline, NULL);
    }
    if (app->device != VK_NULL_HANDLE &&
        app->graphics_pipeline_wireframe != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->graphics_pipeline_wireframe, NULL);
    }
    if (app->device != VK_NULL_HANDLE &&
        app->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->pipeline_layout, NULL);
    }

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

    destroy_depth_resources(app);

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
    app->graphics_pipeline = VK_NULL_HANDLE;
    app->graphics_pipeline_wireframe = VK_NULL_HANDLE;
    app->pipeline_layout = VK_NULL_HANDLE;
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

static bool record_model_command(App *app, uint32_t image_index)
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

    VkClearValue clear_values[2] = {0};
    clear_values[0].color.float32[0] = 0.025F;
    clear_values[0].color.float32[1] = 0.055F;
    clear_values[0].color.float32[2] = 0.11F;
    clear_values[0].color.float32[3] = 1.0F;
    clear_values[1].depthStencil.depth = 1.0F;
    clear_values[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo render_pass_info = {0};
    render_pass_info.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = app->render_pass;
    render_pass_info.framebuffer = app->framebuffers[image_index];
    render_pass_info.renderArea.offset.x = 0;
    render_pass_info.renderArea.offset.y = 0;
    render_pass_info.renderArea.extent = app->swapchain_extent;
    render_pass_info.clearValueCount =
        (uint32_t)VK_ARRAY_COUNT(clear_values);
    render_pass_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer,
                         &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport = {
        .x = 0.0F,
        .y = 0.0F,
        .width = (float)app->swapchain_extent.width,
        .height = (float)app->swapchain_extent.height,
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkRect2D scissor = {
        .offset = {0, 0},
        .extent = app->swapchain_extent,
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    ModelPushConstants transform = {0};
    make_model_push_constants(app, &transform);
    vkCmdPushConstants(command_buffer,
                       app->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       (uint32_t)sizeof(transform),
                       &transform);

    vkCmdBindPipeline(command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      app->wireframe
                          ? app->graphics_pipeline_wireframe
                          : app->graphics_pipeline);
    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command_buffer,
                           0,
                           1,
                           &app->vertex_buffer,
                           &vertex_offset);
    vkCmdBindIndexBuffer(command_buffer,
                         app->index_buffer,
                         0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer,
                     app->model.index_count,
                     1,
                     0,
                     0,
                     0);

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

    if (!record_model_command(app, image_index)) {
        return false;
    }

    const VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
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

static void update_camera(App *app)
{
    const double now = glfwGetTime();
    float delta_time = (float)(now - app->camera_last_update_time);
    app->camera_last_update_time = now;

    /* Prevent a paused/debugged process from causing a camera jump or some shit, idk . */
    if (delta_time <= 0.0F) {
        return;
    }
    if (delta_time > 0.05F) {
        delta_time = 0.05F;
    }

    const int horizontal =
        (app->camera_right ? 1 : 0) - (app->camera_left ? 1 : 0);
    const int vertical =
        (app->camera_up ? 1 : 0) - (app->camera_down ? 1 : 0);
    app->camera_yaw +=
        (float)horizontal * CAMERA_ORBIT_SPEED * delta_time;
    app->camera_pitch +=
        (float)vertical * CAMERA_ORBIT_SPEED * delta_time;

    if (app->camera_pitch < CAMERA_MIN_PITCH) {
        app->camera_pitch = CAMERA_MIN_PITCH;
    } else if (app->camera_pitch > CAMERA_MAX_PITCH) {
        app->camera_pitch = CAMERA_MAX_PITCH;
    }
}

static bool run_window_loop(App *app)
{
    while (glfwWindowShouldClose(app->window) == GLFW_FALSE) {
        glfwPollEvents();

        if (glfwGetWindowAttrib(app->window, GLFW_ICONIFIED) == GLFW_TRUE) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        update_camera(app);
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
        destroy_model_buffers(app);

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
    const char *model_path = DEFAULT_MODEL_PATH;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--smoke-test") == 0) {
            app.smoke_test = true;
        } else if (strcmp(argv[index], "--model") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "--model requires an OBJ file path.\n");
                goto cleanup;
            }
            ++index;
            model_path = argv[index];
        }
    }

    char model_error[256] = {0};
    if (!obj_model_load(model_path,
                        &app.model,
                        model_error,
                        sizeof(model_error))) {
        fprintf(stderr,
                "Could not load model '%s': %s\n",
                model_path,
                model_error);
        goto cleanup;
    }
    printf("Model: %s (%" PRIu32 " vertices, %" PRIu32 " triangles)\n",
           model_path,
           app.model.vertex_count,
           app.model.triangle_count);

    if (!create_window(&app)) {
        goto cleanup;
    }

    if (!create_instance(&app) ||
        !choose_physical_device(&app) ||
        !create_logical_device(&app) ||
        !create_command_pool(&app) ||
        !create_sync_objects(&app) ||
        !create_model_buffers(&app) ||
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
    obj_model_destroy(&app.model);

    if (success) {
        printf("Vulkan window shut down cleanly.\n");
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
