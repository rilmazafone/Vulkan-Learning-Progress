#include "vk_helpers.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0U) {
        severity_name = "verbose";
    }

    if (callback_data->pMessage != NULL) {
        fprintf(stderr, "[Vulkan validation %s] %s\n", severity_name, callback_data->pMessage);
    }
    return VK_FALSE;
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
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
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

static void destroy_debug_messenger(VkInstance instance,
                                    VkDebugUtilsMessengerEXT messenger)
{
    if (messenger == VK_NULL_HANDLE) {
        return;
    }

    PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_messenger != NULL) {
        destroy_messenger(instance, messenger, NULL);
    }
}

int main(void)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    int exit_code = EXIT_FAILURE;

    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    const bool validation_available =
        vk_instance_layer_available(validation_layer);
    const bool debug_utils_available =
        vk_instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const char *enabled_layers[1] = {0};
    const char *enabled_extensions[1] = {0};
    uint32_t enabled_layer_count = 0;
    uint32_t enabled_extension_count = 0;

    if (validation_available) {
        enabled_layers[enabled_layer_count] = validation_layer;
        ++enabled_layer_count;
    } else {
        fprintf(stderr,
                "Warning: %s is unavailable. Install the Vulkan SDK "
                "validation layer for better diagnostics.\n",
                validation_layer);
    }

    if (validation_available && debug_utils_available) {
        enabled_extensions[enabled_extension_count] =
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        ++enabled_extension_count;
    }

    VkApplicationInfo application_info = {0};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "Vulkan C Lab";
    application_info.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application_info.pEngineName = "No engine";
    application_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledLayerCount = enabled_layer_count;
    instance_info.ppEnabledLayerNames = enabled_layers;
    instance_info.enabledExtensionCount = enabled_extension_count;
    instance_info.ppEnabledExtensionNames = enabled_extensions;

    if (!vk_require(vkCreateInstance(&instance_info, NULL, &instance),
                    "vkCreateInstance")) {
        goto cleanup;
    }

    printf("Vulkan instance created.\n");
    printf("Validation: %s\n", validation_available ? "enabled" : "unavailable");

    if (enabled_extension_count > 0) {
        debug_messenger = create_debug_messenger(instance);
    }

    uint32_t device_count = 0;
    if (!vk_require(vkEnumeratePhysicalDevices(instance, &device_count, NULL),
                    "vkEnumeratePhysicalDevices (count)")) {
        goto cleanup;
    }

    if (device_count == 0) {
        printf("No Vulkan physical devices were found.\n");
        printf("The loader is working, but no graphics driver exposed a device.\n");
        exit_code = EXIT_SUCCESS;
        goto cleanup;
    }

    VkPhysicalDevice *devices =
        (VkPhysicalDevice *)calloc((size_t)device_count, sizeof(*devices));
    if (devices == NULL) {
        fprintf(stderr, "Could not allocate the physical device list.\n");
        goto cleanup;
    }

    if (!vk_require(vkEnumeratePhysicalDevices(instance, &device_count, devices),
                    "vkEnumeratePhysicalDevices")) {
        free(devices);
        goto cleanup;
    }

    printf("Found %" PRIu32 " physical device(s):\n", device_count);
    for (uint32_t device_index = 0; device_index < device_count; ++device_index) {
        VkPhysicalDeviceProperties properties = {0};
        VkPhysicalDeviceMemoryProperties memory = {0};
        vkGetPhysicalDeviceProperties(devices[device_index], &properties);
        vkGetPhysicalDeviceMemoryProperties(devices[device_index], &memory);

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
            devices[device_index], &queue_family_count, NULL);

        uint32_t graphics_queue_families = 0;
        uint32_t compute_queue_families = 0;
        if (queue_family_count > 0) {
            VkQueueFamilyProperties *queue_families =
                (VkQueueFamilyProperties *)calloc(
                    (size_t)queue_family_count, sizeof(*queue_families));
            if (queue_families == NULL) {
                free(devices);
                fprintf(stderr, "Could not allocate queue family storage.\n");
                goto cleanup;
            }

            vkGetPhysicalDeviceQueueFamilyProperties(
                devices[device_index], &queue_family_count, queue_families);
            for (uint32_t family_index = 0;
                 family_index < queue_family_count;
                 ++family_index) {
                if ((queue_families[family_index].queueFlags &
                     VK_QUEUE_GRAPHICS_BIT) != 0U) {
                    ++graphics_queue_families;
                }
                if ((queue_families[family_index].queueFlags &
                     VK_QUEUE_COMPUTE_BIT) != 0U) {
                    ++compute_queue_families;
                }
            }
            free(queue_families);
        }

        printf("\nDevice %" PRIu32 ":\n", device_index);
        printf("  Name:              %s\n", properties.deviceName);
        printf("  Type:              %s\n",
               vk_physical_device_type_name(properties.deviceType));
        printf("  Vulkan API:        %u.%u.%u\n",
               VK_VERSION_MAJOR(properties.apiVersion),
               VK_VERSION_MINOR(properties.apiVersion),
               VK_VERSION_PATCH(properties.apiVersion));
        printf("  Vendor/device IDs: 0x%04" PRIx32 " / 0x%04" PRIx32 "\n",
               properties.vendorID,
               properties.deviceID);
        printf("  Driver version:    0x%08" PRIx32 "\n",
               properties.driverVersion);

        uint64_t local_memory = 0;
        for (uint32_t heap_index = 0;
             heap_index < memory.memoryHeapCount;
             ++heap_index) {
            if ((memory.memoryHeaps[heap_index].flags &
                 VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0U) {
                local_memory +=
                    (uint64_t)memory.memoryHeaps[heap_index].size;
            }
        }
        printf("  Device-local RAM:  %.1f MiB\n",
               (double)local_memory / (1024.0 * 1024.0));
        printf("  Queue families:    %" PRIu32
               " (graphics-capable: %" PRIu32
               ", compute-capable: %" PRIu32 ")\n",
               queue_family_count,
               graphics_queue_families,
               compute_queue_families);
    }

    free(devices);
    exit_code = EXIT_SUCCESS;

cleanup:
    destroy_debug_messenger(instance, debug_messenger);
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, NULL);
    }
    return exit_code;
}
