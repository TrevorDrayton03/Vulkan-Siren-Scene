#define GLFW_INCLUDE_VULKAN
#define STB_IMAGE_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS 1
#define VMA_IMPLEMENTATION
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
#include "tiny_gltf_v3.h"
#include <filesystem>
#include <cstring>
#include <cstddef>

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

uint32_t WIDTH = 1200;
uint32_t HEIGHT = 800;

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

class SirenScene
{
public:
    void run()
    {
        initWindow();
        initVulkan();
        initSiren();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow *window;
    vk::raii::Context context{};
    vk::raii::Instance instance = VK_NULL_HANDLE;
    vk::raii::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
    vk::raii::Device device = VK_NULL_HANDLE;
    vk::raii::Queue queue = VK_NULL_HANDLE;
    vk::raii::SurfaceKHR surface = VK_NULL_HANDLE;
    vk::raii::SwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<vk::Image> swapchainImages{};
    std::vector<vk::raii::ImageView> swapchainImageViews{};
    vk::SurfaceFormatKHR swapchainSurfaceFormat{};
    vk::Extent2D swapchainExtent{};
    vk::Format swapchainFormat = vk::Format::eB8G8R8A8Srgb;
    vk::raii::CommandPool commandPool = VK_NULL_HANDLE;
    vk::raii::CommandBuffers commandBuffers = VK_NULL_HANDLE;
    std::vector<vk::raii::Fence> inFlightFences{};
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores{};
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores{};
    vk::raii::PipelineLayout pipelineLayout = VK_NULL_HANDLE;
    vk::raii::Pipeline pipeline = VK_NULL_HANDLE;

    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer vertexStagingBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexStagingAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
    VkBuffer indexStagingBuffer = VK_NULL_HANDLE;
    VmaAllocation indexStagingAllocation = VK_NULL_HANDLE;    

    uint32_t queueIndex = ~0;
    uint32_t swapchainImageCount = 3;
    uint32_t framesInFlight = 2;
    uint32_t frameIndex = 0;

    tg3_model sirenModel;
    std::vector<Vertex> sirenVertices;
    std::vector<uint32_t> sirenIndices;

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
        window = glfwCreateWindow(WIDTH, HEIGHT, "Siren Scene", NULL, NULL);
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
                                        features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
                                        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        // Return true if the physicalDevice meets all the criteria
        return supportsVulkan1_4 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }

    void createLogicalDevice()
    {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports both graphics and present
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
                {},                                                   // vk::PhysicalDeviceFeatures2
                {.shaderDrawParameters = true},                       // vk::PhysicalDeviceVulkan11Features
                {.synchronization2 = true, .dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
                {.extendedDynamicState = true},                       // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
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
            .surface = *surface,
            .minImageCount = swapchainImageCount,
            .imageFormat = swapchainFormat,                                                                          // hard coded for windows (instead of querying the surface)
            .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,                                                    // hard coded for Windows (instead of querying the surface)
            .imageExtent = physicalDevice.getSurfaceCapabilities2KHR(surfaceInfo).surfaceCapabilities.currentExtent, // just getting the fixed extent (for Windows)
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = VK_NULL_HANDLE,
            .preTransform = physicalDevice.getSurfaceCapabilities2KHR(surfaceInfo).surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = vk::PresentModeKHR::eFifo, // hard coded for windows (instead of querying the surface and the physical device)
            .clipped = VK_FALSE,
            .oldSwapchain = VK_NULL_HANDLE,
        };
        swapchain = vk::raii::SwapchainKHR(device, createInfo);
        swapchainExtent = createInfo.imageExtent;
        swapchainImages = swapchain.getImages();
    };

    void createImageViews()
    {
        for (int i = 0; i < swapchainImageCount; i++)
        {
            vk::ImageViewCreateInfo createInfo = {
                .image = swapchainImages[i],
                .viewType = vk::ImageViewType::e2D,
                .format = swapchainFormat,
                .components = {.r = vk::ComponentSwizzle::eIdentity, .g = vk::ComponentSwizzle::eIdentity, .b = vk::ComponentSwizzle::eIdentity, .a = vk::ComponentSwizzle::eIdentity},
                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
            };
            swapchainImageViews.emplace_back(vk::raii::ImageView(device, createInfo));
        };
    };

    void createCommandPool()
    {
        vk::CommandPoolCreateInfo createInfo = {
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = queueIndex,
        };

        commandPool = vk::raii::CommandPool(device, createInfo);
    };

    void allocateCommandBuffers()
    {
        vk::CommandBufferAllocateInfo allocateInfo = {
            .commandPool = commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = framesInFlight,
        };

        commandBuffers = vk::raii::CommandBuffers(device, allocateInfo);
    };

    void createSyncObjects()
    {
        inFlightFences.reserve(framesInFlight);
        presentCompleteSemaphores.reserve(framesInFlight);
        renderFinishedSemaphores.reserve(swapchainImageCount);

        for (uint32_t i = 0; i < swapchainImageCount; ++i)
        {
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
        }

        for (uint32_t i = 0; i < framesInFlight; ++i)
        {
            inFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}); // start signaled
            presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
        }
    };

    void createPipeline() {

    };

    void drawFrame()
    {
        auto waitResult = device.waitForFences({*inFlightFences[frameIndex]}, VK_TRUE, UINT64_MAX);
        auto [acquireResult, imageIndex] = swapchain.acquireNextImage(UINT64_MAX, presentCompleteSemaphores[frameIndex], VK_NULL_HANDLE);

        vk::raii::CommandBuffer &commandBuffer = commandBuffers[frameIndex];
        vk::Semaphore renderFinished = *renderFinishedSemaphores[imageIndex];

        commandBuffer.reset();

        vk::CommandBufferBeginInfo beginInfo = {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        commandBuffer.begin(beginInfo);

        // image memory barrier that includes an execution dependency, a memory dependency, and an image layout transition
        vk::ImageMemoryBarrier2 toColorAttachment{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput, // first sync scope: COLOR_ATTACHMENT_OUTPUT ops from commands earlier in submission order must finish completing
            .srcAccessMask = {},                                                // first access scope: no memory operations
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput, // second sync scope: COLOR_ATTACHMENT_OUTPUT ops from commands later in submission order must wait for completion
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,        // COLOR_ATTACHMENT_WRITE accesses to the specified image subresources
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainImages[imageIndex],                                                                                                          // image whose subresources are affected by the image memory barrier
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}, // subresource range affected by the image memory barrier
        };

        vk::DependencyInfo toColorDependency = {
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toColorAttachment,
        };

        commandBuffer.pipelineBarrier2(toColorDependency);

        bool showRed = (static_cast<int>(glfwGetTime()) % 2) == 0;

        vk::RenderingAttachmentInfo colorAttachment = {
            .imageView = swapchainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .resolveMode = vk::ResolveModeFlagBits::eNone,
            .resolveImageView = {}, // only required when the main color attachment is multisampled
            .resolveImageLayout = {},
            .loadOp = vk::AttachmentLoadOp::eClear,   // determines what happens to the swapchain image each frame -> throw away the image for my clear color
            .storeOp = vk::AttachmentStoreOp::eStore, // determines what happens to the swapchain image each frame -> keep the data in the image attachment for the presentation engine
            .clearValue = vk::ClearValue(showRed ? vk::ClearColorValue{1.0f, 0.0f, 0.0f, 1.0f} : vk::ClearColorValue{1.0f, 1.0f, 0.0f, 1.0f}),
        };

        vk::RenderingInfo renderingInfo = {
            .flags = {},
            .renderArea = {.offset = {0, 0}, .extent = swapchainExtent},
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = nullptr,
            .pStencilAttachment = nullptr,
        };

        commandBuffer.beginRendering(renderingInfo); // dynamic render pass // groups draw commands using a particular set of image attachments

        // bind === tell GPU to use this resource
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(swapchainExtent.width),
            .height = static_cast<float>(swapchainExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        vk::Rect2D scissor = {
            .offset = {0, 0},
            .extent = {swapchainExtent.width, swapchainExtent.height},
        };

        commandBuffer.setScissor(0, scissor);
        commandBuffer.setViewport(0, viewport);

        commandBuffer.bindVertexBuffers(0, {vertexBuffer}, static_cast<uint32_t>(0));
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32); // sirenIndices convers to a uint32_t type

        commandBuffer.drawIndexed(sirenIndices.size(), 1, 0, 0, 0);

        commandBuffer.endRendering(); // image stored (eStore) // finished using these attachments

        vk::ImageMemoryBarrier2 presentBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eNone,
            .dstAccessMask = {},
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainImages[imageIndex],
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
        };

        vk::DependencyInfo presentDependencyInfo = {
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &presentBarrier,
        };

        commandBuffer.pipelineBarrier2(presentDependencyInfo);

        commandBuffer.end();

        vk::CommandBufferSubmitInfo commandInfo = {
            .commandBuffer = *commandBuffer,
        };

        // wait until the presentation engine has released the acquired swapchain image before our color-attachment operations use it
        vk::SemaphoreSubmitInfo submitSemaphoreInfo = {
            .semaphore = presentCompleteSemaphores[frameIndex],
            // .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .deviceIndex = 0,
        };

        // signal when this submission has completed for presentation to wait on it
        vk::SemaphoreSubmitInfo signalSignalInfo = {
            .semaphore = renderFinished,
            // .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .deviceIndex = 0,
        };

        vk::SubmitInfo2 submitInfo = {
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &submitSemaphoreInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalSignalInfo,
        };

        // reset only now: from this point we know that a submission will be made which will eventually signal the fence
        device.resetFences({*inFlightFences[frameIndex]});

        queue.submit2(submitInfo, inFlightFences[frameIndex]);

        vk::SwapchainKHR swapchainHandle = *swapchain;

        vk::PresentInfoKHR presentInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinished,
            .swapchainCount = 1,
            .pSwapchains = &swapchainHandle,
            .pImageIndices = &imageIndex,
        };

        auto presentResult = queue.presentKHR(presentInfo);

        frameIndex = (frameIndex + 1) % framesInFlight;
    };

    void loadModel()
    {
        tg3_parse_options options;
        tg3_error_stack errors;
        tg3_parse_options_init(&options);
        tg3_error_stack_init(&errors);

        constexpr char path[] = "assets/models/blushing_mermaid.glb";

        const tg3_error_code result = tg3_parse_file(
            &sirenModel,
            &errors,
            path,
            sizeof(path) - 1,
            &options);

        for (uint32_t i = 0; i < errors.count; ++i)
        {
            const char *message = errors.entries[i].message;
            std::cerr << (message ? message : "Unknown glTF message") << '\n';
        }

        tg3_error_stack_free(&errors);

        if (result != TG3_OK)
        {
            tg3_model_free(&sirenModel);
            throw std::runtime_error("Failed to load siren model");
        }

        std::cout << "Siren model loaded\n"
                  << "File: " << std::filesystem::absolute(path) << '\n'
                  << "Bytes: " << std::filesystem::file_size(path) << '\n\n';
    };

    void inspectGeometry(tg3_model &model)
    {
        std::cout << "\nMeshes: " << model.meshes_count
                  << "\nTextures: " << model.textures_count
                  << "\nMaterials: " << model.materials_count
                  << "\nNodes: " << model.nodes_count
                  << "\nAccessors: " << model.accessors_count << '\n';
        std::cout << '\n';

        for (uint32_t i = 0; i < model.accessors_count; ++i)
        {
            const auto &a = model.accessors[i];

            std::cout << "Accessor " << i
                      << ": count=" << a.count
                      << ", type=" << a.type
                      << ", component=" << a.component_type
                      << ", offset=" << a.byte_offset
                      << ", normalized=" << a.normalized
                      << ", sparse=" << a.sparse.is_sparse;

            if (a.buffer_view >= 0)
            {
                const auto &view = model.buffer_views[a.buffer_view];
                std::cout << ", stride=" << view.byte_stride;
            }
            std::cout << '\n';
        }

        for (uint32_t m = 0; m < model.meshes_count; ++m)
        {
            const auto &mesh = model.meshes[m];

            for (uint32_t p = 0; p < mesh.primitives_count; ++p)
            {
                const auto &primitive = mesh.primitives[p];

                std::cout << "\nMesh " << m << ", primitive " << p << '\n';

                for (uint32_t a = 0; a < primitive.attributes_count; ++a)
                {
                    const auto &[name, accessorIndex] = primitive.attributes[a];
                    const auto &[data, length] = name;

                    std::cout.write(data, static_cast<std::streamsize>(length));
                    std::cout << " -> accessor " << accessorIndex << '\n';
                }

                std::cout << "Indices -> accessor " << primitive.indices << '\n';
            }
        }
    }

    void extractGeometry()
    {
        auto packedData = [&](uint32_t accessorIndex) -> const uint8_t *
        {
            const tg3_accessor &accessor = sirenModel.accessors[accessorIndex];
            const tg3_buffer_view &view = sirenModel.buffer_views[accessor.buffer_view];
            const tg3_span_u8 &bytes = sirenModel.buffers[view.buffer].data;
            return bytes.data + view.byte_offset + accessor.byte_offset;
        };

        const uint8_t *positions = packedData(0);
        const uint8_t *normals = packedData(1);
        const uint8_t *uvs = packedData(2);
        const uint8_t *sourceIndices = packedData(6);

        const uint64_t vertexCount = sirenModel.accessors[0].count;
        const uint64_t indexCount = sirenModel.accessors[6].count;

        sirenVertices.resize(static_cast<size_t>(vertexCount));
        sirenIndices.resize(static_cast<size_t>(indexCount));

        for (int i = 0; i < sirenVertices.size(); ++i)
        {
            float position[3];
            float normal[3];
            float uv[2];

            std::memcpy(position, positions + i * 12, sizeof(position));
            std::memcpy(normal, normals + i * 12, sizeof(normal));
            std::memcpy(uv, uvs + i * 8, sizeof(uv));

            sirenVertices[i] = Vertex{
                glm::vec3(position[0], position[1], position[2]),
                glm::vec3(normal[0], normal[1], normal[2]),
                glm::vec2(uv[0], uv[1])};
        };

        for (size_t i = 0; i < sirenIndices.size(); ++i)
        {
            uint16_t index;
            std::memcpy(&index, sourceIndices + i * 2, sizeof(index));
            sirenIndices[i] = index;
        };

        std::cout << "Extracted " << sirenVertices.size() << " vertices and "
                  << sirenIndices.size() << " indices\n";
    }

    void createAllocator()
    {
        VmaAllocatorCreateInfo info{};
        info.instance = static_cast<VkInstance>(*instance);
        info.physicalDevice = static_cast<VkPhysicalDevice>(*physicalDevice);
        info.device = static_cast<VkDevice>(*device);
        info.vulkanApiVersion = VK_API_VERSION_1_4;

        vmaCreateAllocator(&info, &allocator);
    }

    void createVertexBuffer()
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(Vertex) * sirenVertices.size();
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &vertexBuffer, &vertexAllocation, nullptr);
    }

    void createVertexStagingBuffer()
    {

        const vk::DeviceSize size = sizeof(Vertex) * sirenVertices.size();
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size; // 463104
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | // tells vma: cpu needs to write memory sequentially
            VMA_ALLOCATION_CREATE_MAPPED_BIT;                        // tells vma: map the allocation immediately and expose its CPU pointer through allocationDetails.pMappedData, avoiding a separate vmaMapMemory() call.

        VmaAllocationInfo allocationDetails{};

        vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &vertexStagingBuffer, &vertexStagingAllocation, &allocationDetails);

        std::memcpy(allocationDetails.pMappedData, sirenVertices.data(), static_cast<size_t>(size));
        vmaFlushAllocation(allocator, vertexStagingAllocation, 0, size); // makes CPU writes from memcpy visible to the GPU
    }

    void uploadVertices()
    {
        const vk::DeviceSize size = sizeof(Vertex) * sirenVertices.size(); // 463104

        vk::CommandBufferAllocateInfo allocateInfo{};
        allocateInfo.commandPool = *commandPool;
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = 1;

        vk::raii::CommandBuffers uploadCommands(device, allocateInfo);
        auto &cmd = uploadCommands.front();

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::BufferCopy region{};
        region.size = size;

        cmd.copyBuffer(vk::Buffer(vertexStagingBuffer), vk::Buffer(vertexBuffer), region);

        vk::BufferMemoryBarrier2 barrier{};                       // records a dependency from the copy's transfer write to later vertex-attribute reads; the reads must wait and see the copied data
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy; // 4294967296Ui64 // Visual Studio's hover tooltip is displaying the vlaue using Microsoft's Ui64r notation
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput;
        barrier.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = vk::Buffer(vertexBuffer);
        barrier.offset = 0;
        barrier.size = size;

        vk::DependencyInfo dependency{};
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &barrier;

        cmd.pipelineBarrier2(dependency);
        cmd.end();

        vk::CommandBufferSubmitInfo commandInfo{};
        commandInfo.commandBuffer = *cmd;

        vk::SubmitInfo2 submitInfo{};
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;

        queue.submit2(submitInfo, nullptr);

        queue.waitIdle();
    }
    
        void createIndexBuffer()
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * sirenIndices.size();
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &indexBuffer, &indexAllocation, nullptr);
    }

    void createIndexStagingBuffer()
    {
        const vk::DeviceSize size = sizeof(uint32_t) * sirenIndices.size();
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size; 
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | // tells vma: cpu needs to write memory sequentially
            VMA_ALLOCATION_CREATE_MAPPED_BIT;                        // tells vma: map the allocation immediately and expose its CPU pointer through allocationDetails.pMappedData, avoiding a separate vmaMapMemory() call.

        VmaAllocationInfo allocationDetails{};

        vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &indexStagingBuffer, &indexStagingAllocation, &allocationDetails);

        std::memcpy(allocationDetails.pMappedData, sirenIndices.data(), static_cast<size_t>(size));
        vmaFlushAllocation(allocator, indexStagingAllocation, 0, size); // makes CPU writes from memcpy visible to the GPU
    }

    void uploadIndices()
    {
        const vk::DeviceSize size = sizeof(uint32_t) * sirenIndices.size();

        vk::CommandBufferAllocateInfo allocateInfo{};
        allocateInfo.commandPool = *commandPool;
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = 1;

        vk::raii::CommandBuffers uploadCommands(device, allocateInfo);
        auto &cmd = uploadCommands.front();

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::BufferCopy region{};
        region.size = size;

        cmd.copyBuffer(vk::Buffer(indexStagingBuffer), vk::Buffer(indexBuffer), region);

        vk::BufferMemoryBarrier2 barrier{};                       // records a dependency from the copy's transfer write to later index reads; the reads must wait and see the copied data
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy; // 4294967296Ui64 // Visual Studio's hover tooltip is displaying the vlaue using Microsoft's Ui64r notation
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eIndexInput;
        barrier.dstAccessMask = vk::AccessFlagBits2::eIndexRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = vk::Buffer(indexBuffer);
        barrier.offset = 0;
        barrier.size = size;

        vk::DependencyInfo dependency{};
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &barrier;

        cmd.pipelineBarrier2(dependency);
        cmd.end();

        vk::CommandBufferSubmitInfo commandInfo{};
        commandInfo.commandBuffer = *cmd;

        vk::SubmitInfo2 submitInfo{};
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;

        queue.submit2(submitInfo, nullptr);

        queue.waitIdle();
    }

    void initVulkan()
    {
        createInstance();
        createSurface();
        choosePhysicalDevice();
        createLogicalDevice();
        createAllocator();
        createSwapchain();
        createImageViews();
        createCommandPool();
        allocateCommandBuffers();
        createSyncObjects();
        createPipeline();
    }

    void initSiren()
    {
        loadModel();
        inspectGeometry(sirenModel);
        extractGeometry();
        createVertexBuffer();
        createVertexStagingBuffer();
        uploadVertices();
        createIndexBuffer();
        createIndexStagingBuffer();
        uploadIndices();
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            drawFrame();
        }
    }

    void cleanup()
    {
        device.waitIdle(); // let the GPU finish running before cleaning up

        vmaDestroyBuffer(allocator, vertexStagingBuffer, vertexStagingAllocation);
        vertexStagingBuffer = VK_NULL_HANDLE;
        vertexStagingAllocation = VK_NULL_HANDLE;

        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAllocation = VK_NULL_HANDLE;

        vmaDestroyBuffer(allocator, indexStagingBuffer, indexStagingAllocation);
        indexStagingBuffer = VK_NULL_HANDLE;
        indexStagingAllocation = VK_NULL_HANDLE;

        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
        indexBuffer = VK_NULL_HANDLE;
        indexAllocation = VK_NULL_HANDLE;

        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;

        tg3_model_free(&sirenModel);

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
