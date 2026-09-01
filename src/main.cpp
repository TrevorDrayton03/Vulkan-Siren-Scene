#define GLFW_INCLUDE_VULKAN
#define STB_IMAGE_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS 1
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <tiny_obj_loader.h>
#include <stb_image.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <vk_mem_alloc.h>

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

class SirenScene
{
public:
    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow *window;
    vk::raii::Context context;
    vk::raii::Instance instance = VK_NULL_HANDLE;
    vk::raii::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
    vk::raii::Device device = VK_NULL_HANDLE;
    vk::raii::Queue queue = VK_NULL_HANDLE;
    vk::raii::SurfaceKHR surface = VK_NULL_HANDLE;
    vk::raii::SwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<vk::Image> swapchainImages;
    std::vector<vk::ImageView> swapchainImageViews;
    vk::SurfaceFormatKHR swapchainSurfaceFormat;
    vk::Extent2D swapchainExtent;

    const std::vector<char const *> layers =
        {
            "VK_LAYER_KHRONOS_validation",
    };

    const std::vector<char const *> deviceExtensions =
        {
            vk::KHRSwapchainExtensionName,
    };

    void initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(1200, 800, "Siren Scene", NULL, NULL);
    }

    void createInstance()
    {
        // Get the required layers
        std::vector<char const *> requiredLayers;
        if (enableValidationLayers)
        {
            requiredLayers.assign(layers.begin(), layers.end());
        }

        // Check if the required layers are supported by the Vulkan implementation.
        auto layerProperties = context.enumerateInstanceLayerProperties();
        auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
                                                       [&layerProperties](auto const &requiredLayer)
                                                       {
                                                           return std::ranges::none_of(layerProperties,
                                                                                       [requiredLayer](auto const &layerProperty)
                                                                                       { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
                                                       });
        if (unsupportedLayerIt != requiredLayers.end())
        {
            throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
        }

        // Get the required instance extensions from GLFW.
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        // Check if the required GLFW extensions are supported by the Vulkan implementation.
        auto extensionProperties = context.enumerateInstanceExtensionProperties();
        for (uint32_t i = 0; i < glfwExtensionCount; ++i)
        {
            if (std::ranges::none_of(extensionProperties,
                                     [glfwExtension = glfwExtensions[i]](auto const &extensionProperty)
                                     { return strcmp(extensionProperty.extensionName, glfwExtension) == 0; }))
            {
                throw std::runtime_error("Required GLFW extension not supported: " + std::string(glfwExtensions[i]));
            }
        }

        // raw pointers are iterators in c++
        // this is a vector range-based constructor expecting iterators as arguments (first, last)
        // glfwExtension + glfwExtensionCount is pointer arithmetic
        std::vector<const char *> instanceExtensions{glfwExtensions, glfwExtensions + glfwExtensionCount};
        instanceExtensions.push_back(vk::KHRGetSurfaceCapabilities2ExtensionName);

        vk::ApplicationInfo appInfo{
            .pNext = VK_NULL_HANDLE,
            .pApplicationName = "Siren Scene",
            .applicationVersion = 1,
            .pEngineName = "Siren Engine",
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_4,
        };

        vk::InstanceCreateInfo instInfo{
            .pNext = VK_NULL_HANDLE,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
            .ppEnabledExtensionNames = instanceExtensions.data(),
        };

        instance = vk::raii::Instance(context, instInfo);
    }

    void choosePhysicalDevice()
    {
        std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
        auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const &physicalDevice)
                                                  { return isDeviceSuitable(physicalDevice); });
        if (devIter == physicalDevices.end())
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
        physicalDevice = *devIter;
    }

    bool isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice)
    {
        // Check if the physicalDevice supports the Vulkan 1.4 API version
        bool supportsVulkan1_4 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion14;

        // Check if any of the queue families support graphics operations
        auto queueFamilies = physicalDevice.getQueueFamilyProperties();
        bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const &qfp)
                                                    { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

        // Check if all required physicalDevice extensions are available
        auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        bool supportsAllRequiredExtensions =
            std::ranges::all_of(deviceExtensions,
                                [&availableDeviceExtensions](auto const &extensions)
                                {
                                    return std::ranges::any_of(availableDeviceExtensions,
                                                               [extensions](auto const &availableDeviceExtension)
                                                               { return strcmp(availableDeviceExtension.extensionName, extensions) == 0; });
                                });

        // Check if the physicalDevice supports the required features
        auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                                             vk::PhysicalDeviceVulkan11Features,
                                                             vk::PhysicalDeviceVulkan13Features,
                                                             vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
                                        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        // Return true if the physicalDevice meets all the criteria
        return supportsVulkan1_4 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }

    void createLogicalDevice()
    {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports both graphics and present
        uint32_t queueIndex = ~0;
        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
        {
            if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
                physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
            {
                // found a queue family that supports both graphics and present
                queueIndex = qfpIndex;
                break;
            }
        }
        if (queueIndex == ~0)
        {
            throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
        }

        // query for Vulkan 1.3 features
        vk::StructureChain<vk::PhysicalDeviceFeatures2,
                           vk::PhysicalDeviceVulkan11Features,
                           vk::PhysicalDeviceVulkan13Features,
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain = {
                {},                             // vk::PhysicalDeviceFeatures2
                {.shaderDrawParameters = true}, // vk::PhysicalDeviceVulkan11Features
                {.dynamicRendering = true},     // vk::PhysicalDeviceVulkan13Features
                {.extendedDynamicState = true}  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
            };

        // create a Device
        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority};
        vk::DeviceCreateInfo deviceCreateInfo{.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
                                              .queueCreateInfoCount = 1,
                                              .pQueueCreateInfos = &deviceQueueCreateInfo,
                                              .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
                                              .ppEnabledExtensionNames = deviceExtensions.data()};

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        queue = vk::raii::Queue(device, queueIndex, 0);
    }

    void createSurface()
    {
        VkSurfaceKHR _surface;
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0)
        {
            throw std::runtime_error("failed to create surface");
        }
        surface = vk::raii::SurfaceKHR(instance, _surface);
    }

    void createSwapchain()
    {
        vk::PhysicalDeviceSurfaceInfo2KHR surfaceInfo{};
        surfaceInfo.surface = *surface;

        vk::SwapchainCreateInfoKHR createInfo = {
            .pNext = VK_NULL_HANDLE,
            .flags = {},
            .surface = *surface,
            .minImageCount = 2,
            .imageFormat = vk::Format::eB8G8R8A8Srgb,                                                                // hard coded for windows (instead of querying the surface)
            .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,                                                    // hard coded for Windows (instead of querying the surface)
            .imageExtent = physicalDevice.getSurfaceCapabilities2KHR(surfaceInfo).surfaceCapabilities.currentExtent, // just getting the fixed extent (for Windows)
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = VK_NULL_HANDLE,
            .preTransform = physicalDevice.getSurfaceCapabilities2KHR(surfaceInfo).surfaceCapabilities.currentTransform,
            .compositeAlpha = {},
            .presentMode = vk::PresentModeKHR::eFifo, // hard coded for windows (instead of querying the surface and the physical device)
            .clipped = VK_FALSE,
            .oldSwapchain = VK_NULL_HANDLE,
        };
    };

    void initVulkan()
    {
        createInstance();
        createSurface();
        choosePhysicalDevice();
        createLogicalDevice();
        createSwapchain();
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
        }
    }

    void cleanup()
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

int main()
{
    try
    {
        SirenScene scene = SirenScene();
        scene.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}