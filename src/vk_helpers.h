#ifndef VK_HELPERS_H
#define VK_HELPERS_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

#define VK_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

const char *vk_result_name(VkResult result);
const char *vk_physical_device_type_name(VkPhysicalDeviceType type);

bool vk_instance_layer_available(const char *layer_name);
bool vk_instance_extension_available(const char *extension_name);

bool vk_require(VkResult result, const char *operation);

#endif
