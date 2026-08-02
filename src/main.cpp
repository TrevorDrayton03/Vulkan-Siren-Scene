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

int main()
{
    std::cout << "Hello World";
    return EXIT_SUCCESS;
}