# Vulkan with C: suggested progression

Vulkan is a C API. The lack of classes is an advantage while learning: every
object is represented by an opaque handle, every operation is an explicit
function call, and every object has an explicit creation and destruction point.

## 1. Loader and instance

Start with `examples/00_enumerate`. Learn these ideas before drawing anything:

- `VkResult` and error handling;
- `VkInstance` versus `VkDevice`;
- layers during development versus extensions for features;
- `VkApplicationInfo` and `VkInstanceCreateInfo`;
- the lifetime of an instance.

Useful checks:

```powershell
vulkaninfo --summary
.\build.ps1 check
```

## 2. Physical and logical devices

The next step is to choose a `VkPhysicalDevice` and create a `VkDevice`.

Learn:

- queue families and why hardware has several queues;
- how to request queues at device creation;
- `VkPhysicalDeviceFeatures` and feature structures;
- device extensions such as `VK_KHR_swapchain`;
- how the selected device relates to the instance, but is a separate object.

## 3. Window and surface

`examples/01_window` creates the window with GLFW. Learn how
`glfwGetRequiredInstanceExtensions` selects the surface extensions for the
current operating system, how `glfwCreateWindowSurface` produces a
`VkSurfaceKHR`, and then follow the queue-family queries that check whether a
graphics queue can also present to that surface.

The example keeps platform setup and Vulkan setup in plain functions in one C
file. As the program grows, GLFW event handling can move into a separate
platform `.c` file while the Vulkan code remains behind a plain data structure.

## 4. Swapchain

`examples/01_window` already owns a swapchain that clears the window. Study:

- surface capabilities and formats;
- present modes;
- image views and framebuffers;
- acquire, submit, and present synchronization.

Do not cache old assumptions: swapchain images can change after resize. The
example waits for the device to become idle, destroys the old swapchain
resources, and creates replacements at the new client size.

## 5. Pipeline and an indexed model

The included shaders are already compiled by the build. `02_triangle` extends
the window renderer with:

- shader modules;
- pipeline layout;
- render pass and depth attachment;
- graphics pipeline using those compiled shader modules;
- vertex and index buffers;
- command buffers;
- semaphores, fences, and command buffer recording.

The example loads and shades an OBJ model. Keep each Vulkan object's purpose and
lifetime readable in plain functions and structs.

## 6. Memory and synchronization

Once a triangle is visible, study:

- memory types and heaps;
- allocation and binding;
- staging buffers;
- host/device visibility;
- image barriers and layout transitions;
- frames in flight and GPU/CPU synchronization.

Validation messages are part of the learning process. Read the first error,
find the invalid object or state transition, and fix that before continuing.

## Rules that prevent most beginner bugs

- Initialize Vulkan create-info structs to zero, then set `sType`.
- Check every non-ignorable `VkResult`.
- Destroy objects in reverse creation order.
- Keep one function responsible for creating a resource and one for destroying
  it.
- Do not access swapchain images outside their acquired frame.
- Never assume the queue family index is `0`.
- Enable validation while developing.
