#include "vk_context.h"
#include <VkBootstrap.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace spectre {

bool VkContext::initialize(SDL_Window* window) {
    window_ = window;

    // Instance
    vkb::InstanceBuilder instance_builder;
    auto inst_ret = instance_builder
        .set_app_name("spectre")
        .set_engine_name("spectre")
        .require_api_version(1, 2, 0)
#ifndef NDEBUG
        .request_validation_layers()
        .use_default_debug_messenger()
#endif
        .build();

    if (!inst_ret) {
        fprintf(stderr, "Failed to create Vulkan instance: %s\n", inst_ret.error().message().c_str());
        return false;
    }

    auto vkb_inst = inst_ret.value();
    instance_ = vkb_inst.instance;
    debug_messenger_ = vkb_inst.debug_messenger;

    // Surface
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        fprintf(stderr, "Failed to create Vulkan surface: %s\n", SDL_GetError());
        return false;
    }

    // Physical device
    vkb::PhysicalDeviceSelector selector(vkb_inst);
    auto phys_ret = selector
        .set_surface(surface_)
        .set_minimum_version(1, 2)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .select();

    if (!phys_ret) {
        fprintf(stderr, "Failed to select physical device: %s\n", phys_ret.error().message().c_str());
        return false;
    }

    auto vkb_phys = phys_ret.value();
    physical_device_ = vkb_phys.physical_device;

    // Logical device
    vkb::DeviceBuilder device_builder(vkb_phys);
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        fprintf(stderr, "Failed to create logical device: %s\n", dev_ret.error().message().c_str());
        return false;
    }

    auto vkb_dev = dev_ret.value();
    device_ = vkb_dev.device;

    auto queue_ret = vkb_dev.get_queue(vkb::QueueType::graphics);
    if (!queue_ret) {
        fprintf(stderr, "Failed to get graphics queue\n");
        return false;
    }
    graphics_queue_ = queue_ret.value();
    graphics_queue_family_ = vkb_dev.get_queue_index(vkb::QueueType::graphics).value();

    // VMA allocator
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.physicalDevice = physical_device_;
    alloc_info.device = device_;
    alloc_info.instance = instance_;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&alloc_info, &allocator_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create VMA allocator\n");
        return false;
    }

    // Render pass
    if (!create_render_pass()) return false;

    // Swapchain
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (!recreate_swapchain(w, h)) return false;

    return true;
}

bool VkContext::create_render_pass() {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &color_attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create render pass\n");
        return false;
    }

    return true;
}

bool VkContext::recreate_swapchain(int width, int height) {
    vkDeviceWaitIdle(device_);

    // Destroy old framebuffers and image views
    for (auto fb : swapchain_.framebuffers)
        vkDestroyFramebuffer(device_, fb, nullptr);
    swapchain_.framebuffers.clear();
    for (auto iv : swapchain_.image_views)
        vkDestroyImageView(device_, iv, nullptr);
    swapchain_.image_views.clear();

    if (render_pass_ != VK_NULL_HANDLE && swapchain_.swapchain != VK_NULL_HANDLE) {
    }

    vkb::SwapchainBuilder sc_builder(physical_device_, device_, surface_);
    auto sc_ret = sc_builder
        .set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .set_old_swapchain(swapchain_.swapchain)
        .build();

    if (!sc_ret) {
        fprintf(stderr, "Failed to create swapchain: %s\n", sc_ret.error().message().c_str());
        return false;
    }

    // Destroy old swapchain after building new one
    if (swapchain_.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_.swapchain, nullptr);
    }

    auto vkb_sc = sc_ret.value();
    swapchain_.swapchain = vkb_sc.swapchain;
    swapchain_.format = vkb_sc.image_format;
    swapchain_.extent = vkb_sc.extent;
    swapchain_.images = vkb_sc.get_images().value();
    swapchain_.image_views = vkb_sc.get_image_views().value();

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    VkAttachmentDescription color_attachment = {};
    color_attachment.format = swapchain_.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &color_attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to recreate render pass\n");
        return false;
    }

    create_framebuffers();
    return true;
}

void VkContext::create_framebuffers() {
    swapchain_.framebuffers.resize(swapchain_.image_views.size());
    for (size_t i = 0; i < swapchain_.image_views.size(); i++) {
        VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &swapchain_.image_views[i];
        fb_info.width = swapchain_.extent.width;
        fb_info.height = swapchain_.extent.height;
        fb_info.layers = 1;

        vkCreateFramebuffer(device_, &fb_info, nullptr, &swapchain_.framebuffers[i]);
    }
}

void VkContext::destroy_swapchain() {
    for (auto fb : swapchain_.framebuffers)
        vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchain_.image_views)
        vkDestroyImageView(device_, iv, nullptr);
    if (swapchain_.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device_, swapchain_.swapchain, nullptr);
    swapchain_ = {};
}

void VkContext::shutdown() {
    if (device_) vkDeviceWaitIdle(device_);

    destroy_swapchain();
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);

    vkb::destroy_debug_utils_messenger(instance_, debug_messenger_);
    vkDestroyInstance(instance_, nullptr);
}

} // namespace spectre
