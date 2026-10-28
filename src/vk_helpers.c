#include "vk_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *vk_result_name(VkResult result)
{
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "unknown VkResult";
    }
}

const char *vk_physical_device_type_name(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "unknown";
    }
}

bool vk_instance_layer_available(const char *layer_name)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceLayerProperties(&count, NULL);
    if (result != VK_SUCCESS || count == 0) {
        return false;
    }

    VkLayerProperties *layers =
        (VkLayerProperties *)calloc((size_t)count, sizeof(*layers));
    if (layers == NULL) {
        return false;
    }

    result = vkEnumerateInstanceLayerProperties(&count, layers);
    if (result != VK_SUCCESS) {
        free(layers);
        return false;
    }

    bool found = false;
    for (uint32_t index = 0; index < count; ++index) {
        if (strcmp(layers[index].layerName, layer_name) == 0) {
            found = true;
            break;
        }
    }

    free(layers);
    return found;
}

bool vk_instance_extension_available(const char *extension_name)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (result != VK_SUCCESS || count == 0) {
        return false;
    }

    VkExtensionProperties *extensions =
        (VkExtensionProperties *)calloc((size_t)count, sizeof(*extensions));
    if (extensions == NULL) {
        return false;
    }

    result = vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);
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

bool vk_require(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) {
        return true;
    }

    fprintf(stderr,
            "%s failed with %s (%d)\n",
            operation,
            vk_result_name(result),
            (int)result);
    return false;
}
