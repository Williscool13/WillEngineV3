//
// Created by William on 2025-10-09.
//

#if WILL_EDITOR
#include "vk_imgui_wrapper.h"

#include <volk.h>
#include <SDL3/SDL.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_sdl3.h>

#include "VkBootstrap.h"
#include "vk_context.h"
#include "core/include/render_interface.h"


namespace Render
{
ImguiWrapper::ImguiWrapper(VulkanContext* context, SDL_Window* window, int32_t swapchainImageCount, VkFormat swapchainFormat)
    : context(context)
{
    // DearImGui implementation, basically copied directly from the Vulkan/SDl3 from DearImGui samples.
    // Descriptor Pool
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100}, //IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 0;
        for (VkDescriptorPoolSize& pool_size : pool_sizes)
            pool_info.maxSets += pool_size.descriptorCount;
        pool_info.poolSizeCount = static_cast<uint32_t>(1);
        pool_info.pPoolSizes = pool_sizes;
        vkCreateDescriptorPool(context->device, &pool_info, nullptr, &imguiPool);
    }

    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());


    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Actual Init
    //ImGui::StyleColorsDark();
    ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale; // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    // init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
    initInfo.Instance = context->instance;
    initInfo.PhysicalDevice = context->physicalDevice;
    initInfo.Device = context->device;
    initInfo.QueueFamily = context->graphicsQueueFamily;
    initInfo.Queue = context->graphicsQueue;
    // init_info.PipelineCache = g_PipelineCache;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = Core::FRAME_BUFFER_COUNT;
    initInfo.ImageCount = Core::FRAME_BUFFER_COUNT;
    initInfo.MinAllocationSize = 1024 * 1024;
    // initInfo.Allocator = g_Allocator;
    // initInfo.PipelineInfoMain.RenderPass = wd->RenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // initInfo.CheckVkResultFn = check_vk_result;


    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
    ImGui_ImplVulkan_Init(&initInfo);


    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details. If you like the default font but want it to scale better, consider using the 'ProggyVector' from the same author!
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);
}

ImguiWrapper::~ImguiWrapper()
{
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(context->device, imguiPool, nullptr);
}

void ImguiWrapper::HandleInput(const SDL_Event& e)
{
    ImGui_ImplSDL3_ProcessEvent(&e);
}
}

#endif
