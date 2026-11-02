#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

#include "accl.h"
#include "vulkan_idct.h"
#include "vk_idct.h"


#define VK_FUNCTION_LIST \
    PFN(vkEnumerateInstanceVersion) \
    PFN(vkEnumerateInstanceLayerProperties) \
    PFN(vkCreateInstance) \
    PFN(vkEnumerateInstanceExtensionProperties) \
    PFN(vkGetInstanceProcAddr) \
    PFN(vkMapMemory) \
    PFN(vkUnmapMemory) \
    PFN(vkGetBufferMemoryRequirements) \
    PFN(vkGetPhysicalDeviceMemoryProperties) \
    PFN(vkAllocateMemory) \
    PFN(vkAllocateCommandBuffers) \
    PFN(vkBindBufferMemory) \
    PFN(vkCmdBindPipeline) \
    PFN(vkCmdDispatch) \
    PFN(vkCmdWriteTimestamp) \
    PFN(vkCmdBindDescriptorSets) \
    PFN(vkCmdResetQueryPool) \
    PFN(vkBeginCommandBuffer) \
    PFN(vkEndCommandBuffer) \
    PFN(vkQueueSubmit) \
    PFN(vkQueueWaitIdle) \
    PFN(vkCreateBuffer) \
    PFN(vkCreateQueryPool) \
    PFN(vkCreateDescriptorPool) \
    PFN(vkAllocateDescriptorSets) \
    PFN(vkUpdateDescriptorSets) \
    PFN(vkCreateCommandPool) \
    PFN(vkResetCommandPool) \
    PFN(vkCreateComputePipelines) \
    PFN(vkCreateDevice) \
    PFN(vkGetDeviceQueue) \
    PFN(vkCreateDescriptorSetLayout) \
    PFN(vkCreatePipelineLayout) \
    PFN(vkDestroyBuffer) \
    PFN(vkDestroyQueryPool) \
    PFN(vkDestroyDescriptorPool) \
    PFN(vkDestroyPipeline) \
    PFN(vkDestroyPipelineLayout) \
    PFN(vkDestroyDescriptorSetLayout) \
    PFN(vkDestroyDevice) \
    PFN(vkDestroyInstance) \
    PFN(vkGetQueryPoolResults) \
    PFN(vkCreateShaderModule) \
    PFN(vkDestroyShaderModule) \
    PFN(vkDestroyCommandPool) \
    PFN(vkFreeMemory) \
    PFN(vkGetPhysicalDeviceQueueFamilyProperties) \
    PFN(vkGetPhysicalDeviceProperties) \
    PFN(vkGetPhysicalDeviceProperties2) \
    PFN(vkEnumeratePhysicalDevices) \
    PFN(vkEnumerateDeviceExtensionProperties) \
    PFN(vkResetCommandBuffer) \
    PFN(vkFreeCommandBuffers) \
    PFN(vkGetPhysicalDeviceFeatures) \
    PFN(vkGetPhysicalDeviceFeatures2) \
    PFN(vkBindBufferMemory2) \
    PFN(vkCreateImage) \
    PFN(vkGetImageMemoryRequirements) \
    PFN(vkDestroyImage) \
    PFN(vkBindImageMemory) \
    PFN(vkCreateImageView) \
    PFN(vkDestroyImageView) \
    PFN(vkCreateSampler) \
    PFN(vkDestroySampler) \
    PFN(vkCmdPipelineBarrier) \
    PFN(vkGetImageSubresourceLayout) \
    PFN(vkCmdCopyBufferToImage) \
    PFN(vkCmdCopyImageToBuffer) \
    PFN(vkCmdCopyBuffer) \
    PFN(vkCmdCopyImage) \
    PFN(vkFreeDescriptorSets) \
    PFN(vkCreateDescriptorUpdateTemplate) \
    PFN(vkResetQueryPool) \
    PFN(vkGetImageMemoryRequirements2) \
    PFN(vkGetBufferMemoryRequirements2) \
    PFN(vkGetImageSparseMemoryRequirements2) \
    PFN(vkCmdPushConstants)

    
struct vk_buff {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct vk_priv {
    void *lib;
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkDevice device;
    VkQueue computeQueue;
    VkCommandPool pool;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkDescriptorSetLayout descriptorSetLayout;
    VkCommandBuffer cmdbuffer;

    struct vk_buff b[3];
#define PFN(name) PFN_##name name;
    VK_FUNCTION_LIST
#undef PFN
};

static struct vk_priv priv;

/* Validation and performance messages only exist when the debug utils
 * extension is enabled; the callback forwards them to stderr so a broken
 * pipeline shows up in the same place as the loader's own output. */
static VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                      VkDebugUtilsMessageTypeFlagsEXT types,
                      const VkDebugUtilsMessengerCallbackDataEXT *data,
                      void *user_data)
{
    (void)types;
    (void)user_data;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage) {
        fprintf(stderr, "vulkan: %s\n", data->pMessage);
    }
    return VK_FALSE;
}


static struct vk_buff createbuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkPhysicalDevice phydev, uint32_t qid, VkMemoryPropertyFlags requireProperties) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &qid,
    };
    struct vk_buff b = {NULL, NULL};
    if (priv.vkCreateBuffer(priv.device, &bufferInfo, NULL, &b.buffer) != VK_SUCCESS) {
        printf("fail to create vk buffer\n");
        return b;
    }

    VkMemoryRequirements memRequirements;
    priv.vkGetBufferMemoryRequirements(priv.device, b.buffer, &memRequirements);

    VkPhysicalDeviceMemoryProperties memoryProperties;
    priv.vkGetPhysicalDeviceMemoryProperties(phydev, &memoryProperties);
    
    int32_t memoryTypeIndex = -1;
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if (((memRequirements.memoryTypeBits & (1 << index))) &&
            ((memoryProperties.memoryTypes[index].propertyFlags & requireProperties) == requireProperties)) {
            memoryTypeIndex = index;
            break;
        }
    }

    VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    priv.vkAllocateMemory(priv.device, &allocateInfo, NULL, &b.memory);

    priv.vkBindBufferMemory(priv.device, b.buffer, b.memory, 0);

    return b;
}

void vulkan_idct_4x4(int16_t *in, int bitdepth)
{
    const int16_t transMatrix[16] = {
        29, 55, 74, 84,
        74, 74, 0, -74,
        84, -29, -74, 55,
        55, -84, 74, -29
    };
    void *data;
    priv.vkMapMemory(priv.device, priv.b[0].memory, 0, VK_WHOLE_SIZE, 0, &data);
    memcpy(data, in, 16 * sizeof(int16_t));
    priv.vkUnmapMemory(priv.device, priv.b[0].memory);

    priv.vkMapMemory(priv.device, priv.b[1].memory, 0, VK_WHOLE_SIZE, 0, &data);
    memcpy(data, transMatrix, 16 * sizeof(int16_t));
    priv.vkUnmapMemory(priv.device, priv.b[1].memory);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    priv.vkBeginCommandBuffer(priv.cmdbuffer, &beginInfo);
    priv.vkCmdBindPipeline(priv.cmdbuffer, VK_PIPELINE_BIND_POINT_COMPUTE, priv.pipeline);
    priv.vkCmdBindDescriptorSets(priv.cmdbuffer, VK_PIPELINE_BIND_POINT_COMPUTE, priv.pipelineLayout, 0, 1, &priv.descriptorSet, 0, NULL);
    priv.vkCmdPushConstants(priv.cmdbuffer, priv.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &bitdepth);
    priv.vkCmdDispatch(priv.cmdbuffer, 1, 1, 1);

    priv.vkEndCommandBuffer(priv.cmdbuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &priv.cmdbuffer,
    };
    priv.vkQueueSubmit(priv.computeQueue, 1, &submitInfo, NULL);
    priv.vkQueueWaitIdle(priv.computeQueue);

    priv.vkMapMemory(priv.device, priv.b[2].memory, 0, VK_WHOLE_SIZE, 0, &data);
    memcpy(in, data, 16 * sizeof(int16_t));
    priv.vkUnmapMemory(priv.device, priv.b[2].memory);

}

struct accl_ops vulkan_accl = {
    .idct_4x4 = vulkan_idct_4x4,
    .type = GPU_TYPE_VULKAN,
};

void vulkan_uninit(void) {
    /* the device has to go before the instance it was created from, and the
     * messenger entry point is only present together with the extension */
    if (priv.device != VK_NULL_HANDLE) {
        priv.vkFreeCommandBuffers(priv.device, priv.pool, 1, &priv.cmdbuffer);
        priv.vkDestroyBuffer(priv.device, priv.b[0].buffer, NULL);
        priv.vkDestroyBuffer(priv.device, priv.b[1].buffer, NULL);
        priv.vkDestroyBuffer(priv.device, priv.b[2].buffer, NULL);
        priv.vkFreeMemory(priv.device, priv.b[0].memory, NULL);
        priv.vkFreeMemory(priv.device, priv.b[1].memory, NULL);
        priv.vkFreeMemory(priv.device, priv.b[2].memory, NULL);
        priv.vkFreeDescriptorSets(priv.device, priv.descriptorPool, 1, &priv.descriptorSet);
        priv.vkDestroyDescriptorPool(priv.device, priv.descriptorPool, NULL);
        priv.vkDestroyPipeline(priv.device, priv.pipeline, NULL);
        priv.vkDestroyPipelineLayout(priv.device, priv.pipelineLayout, NULL);
        priv.vkDestroyDescriptorSetLayout(priv.device, priv.descriptorSetLayout, NULL);
        priv.vkDestroyCommandPool(priv.device, priv.pool, NULL);
        priv.vkDestroyDevice(priv.device, NULL);
        priv.device = VK_NULL_HANDLE;
    }
    if (priv.instance != VK_NULL_HANDLE) {
        if (priv.messenger != VK_NULL_HANDLE) {
            PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger =
                (PFN_vkDestroyDebugUtilsMessengerEXT)priv.vkGetInstanceProcAddr(
                    priv.instance, "vkDestroyDebugUtilsMessengerEXT");
            if (destroy_messenger != NULL) {
                destroy_messenger(priv.instance, priv.messenger, NULL);
            }
            priv.messenger = VK_NULL_HANDLE;
        }
        priv.vkDestroyInstance(priv.instance, NULL);
        priv.instance = VK_NULL_HANDLE;
    }
    if (priv.lib) {
        dlclose(priv.lib);
        priv.lib = NULL;
    }
}

void vulkan_init(void)
{
    void *lib;
#ifdef __APPLE__
    lib = dlopen("libvulkan.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (!lib)
        lib = dlopen("libvulkan.1.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (!lib)
        lib = dlopen("libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!lib && getenv("DYLD_FALLBACK_LIBRARY_PATH") == NULL)
        lib = dlopen("/usr/local/lib/libvulkan.dylib", RTLD_NOW | RTLD_LOCAL);
#elif defined __linux__
    lib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!lib)
        lib = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
#endif
    priv.lib = lib;

#define PFN(name) priv.name = (PFN_##name)(uintptr_t)(dlsym(lib, #name));
    VK_FUNCTION_LIST
#undef PFN

    /* Debug utils are part of the loader, not of the API version, so the
     * entry points are fetched explicitly and only when the extension is
     * really present.  VK_EXT_debug_utils is a preprocessor macro in the
     * headers and cannot be switched on from a runtime check. */
    PFN_vkCreateDebugUtilsMessengerEXT create_messenger = NULL;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = NULL;
    int have_debug_utils = 0;
    uint32_t ext_count = 0;
    if (priv.vkEnumerateInstanceExtensionProperties != NULL &&
        priv.vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL) == VK_SUCCESS &&
        ext_count > 0) {
        VkExtensionProperties *exts = calloc(ext_count, sizeof(VkExtensionProperties));
        if (exts != NULL) {
            priv.vkEnumerateInstanceExtensionProperties(NULL, &ext_count, exts);
            for (uint32_t i = 0; i < ext_count; i++) {
                if (strcmp(exts[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                    have_debug_utils = 1;
                    break;
                }
            }
            free(exts);
        }
    }

    uint32_t version = VK_API_VERSION_1_0;
    priv.vkEnumerateInstanceVersion(&version);

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName = "ffpic",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = version,
        .pApplicationName = "ffpic",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    };

    // Instance creation info
    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    };

    const char *extensions[2];
    uint32_t next_ext = 0;
    if (have_debug_utils) {
        extensions[next_ext++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#ifdef VK_KHR_portability_enumeration
    extensions[next_ext++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
#if VK_KHR_portability_enumeration
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    createInfo.enabledExtensionCount = next_ext;
    createInfo.ppEnabledExtensionNames = extensions;

    uint32_t layerCount;
    priv.vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    const char *valid[] = {
        "VK_LAYER_KHRONOS_validation",
        "VK_LAYER_LUNARG_standard_validation"
    };
    VkLayerProperties* props = malloc(sizeof(VkLayerProperties)* layerCount);
    priv.vkEnumerateInstanceLayerProperties(&layerCount, props);

    for (uint32_t i = 0; i < layerCount; i++) {
        for (uint32_t j = 0; j < 2; j++) {
            if (strcmp(props[i].layerName, valid[j]) == 0) {
                createInfo.enabledLayerCount = 1;
                createInfo.ppEnabledLayerNames = &valid[j];
                break;
            }
        }
    }
    free(props);

    if (priv.vkCreateInstance(&createInfo, NULL, &priv.instance) != VK_SUCCESS) {
        printf("fail to create vulkan instance\n");
        return;
    }

    if (have_debug_utils) {
        /* the entry points come from the instance, so they are fetched only
         * after vkCreateInstance */
        create_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)priv.vkGetInstanceProcAddr(priv.instance, "vkCreateDebugUtilsMessengerEXT");
        destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)priv.vkGetInstanceProcAddr(priv.instance, "vkDestroyDebugUtilsMessengerEXT");
    }
    if (create_messenger != NULL && destroy_messenger != NULL) {
        VkDebugUtilsMessengerCreateInfoEXT msgr = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vulkan_debug_callback,
        };
        if (create_messenger(priv.instance, &msgr, NULL, &priv.messenger) != VK_SUCCESS) {
            priv.messenger = VK_NULL_HANDLE;
        }
    }

    uint32_t count;
    if (priv.vkEnumeratePhysicalDevices(priv.instance, &count, NULL) != VK_SUCCESS) {
        printf("fail to enumrate physocal devices\n");
        return;
    }

    VkPhysicalDevice *physicalDevices = malloc(sizeof(VkPhysicalDevice)* count);
    priv.vkEnumeratePhysicalDevices(priv.instance, &count, physicalDevices);

    VkPhysicalDeviceProperties2 properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    // use the first gpu
    priv.vkGetPhysicalDeviceProperties2(physicalDevices[0], &properties2);

    VkPhysicalDevice phydev = physicalDevices[0];
    free(physicalDevices);

    uint32_t queueFamilyCount = 0;
    priv.vkGetPhysicalDeviceQueueFamilyProperties(phydev, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    priv.vkGetPhysicalDeviceQueueFamilyProperties(phydev, &queueFamilyCount, queueFamilies);

    uint32_t queueidx = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) {
            queueidx = i;
            break;
        }
    }

    VkPhysicalDeviceFeatures deviceFeatures;
    priv.vkGetPhysicalDeviceFeatures(phydev, &deviceFeatures);

    /* The IDCT stage declares the 16 bit storage capabilities, so the device
     * has to enable the matching 16 bit storage features.  Without them the
     * pipeline the stage builds is invalid and the validation layers report
     * VUID-VkShaderModuleCreateInfo-pCode-08740 and
     * VUID-RuntimeSpirv-uniformAndStorageBuffer16BitAccess-06332. */
    VkPhysicalDevice16BitStorageFeatures device16BitStorage = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
    };
    VkPhysicalDeviceShaderFloat16Int8Features devicefloat16Int8Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,
        .pNext = &device16BitStorage,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &devicefloat16Int8Features,
    };
    priv.vkGetPhysicalDeviceFeatures2(phydev, &features2);

    if (!deviceFeatures.shaderInt16 ||
        !device16BitStorage.storageBuffer16BitAccess ||
        !device16BitStorage.uniformAndStorageBuffer16BitAccess) {
        /* Register no backend, so the callers fall back to the software IDCT
         * instead of building a pipeline the device cannot run. */
        fprintf(stderr, "the device has no 16 bit storage, no vulkan backend\n");
        return;
    }

    VkPhysicalDeviceFeatures features = {
        .shaderInt16 = deviceFeatures.shaderInt16,
    };

    /* the pNext chain of the features handed to vkCreateDevice must list the
     * 16 bit storage features before the float16 ones that point at them */
    VkPhysicalDevice16BitStorageFeatures storage16BitFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
        .storageBuffer16BitAccess = device16BitStorage.storageBuffer16BitAccess,
        .uniformAndStorageBuffer16BitAccess =
            device16BitStorage.uniformAndStorageBuffer16BitAccess,
    };
    VkPhysicalDeviceFloat16Int8FeaturesKHR float16Int8Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,
        .pNext = &storage16BitFeatures,
        .shaderFloat16 = devicefloat16Int8Features.shaderFloat16,
    };

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueidx,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkDeviceCreateInfo devcreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .pEnabledFeatures = &features,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL,
        .pNext = &float16Int8Features,
    };

    if (priv.vkCreateDevice(phydev, &devcreateInfo, NULL, &priv.device) != VK_SUCCESS) {
        printf("Failed to create device\n");
        return ;
    }
    priv.vkGetDeviceQueue(priv.device, queueidx, 0, &priv.computeQueue);

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queueidx,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    if (priv.vkCreateCommandPool(priv.device, &poolInfo, NULL, &priv.pool) != VK_SUCCESS) {
        printf("fail to create command pool\n");
        return;
    }

    VkShaderModuleCreateInfo shadercreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (uint32_t)vulkan_idct_spv_len,
        .pCode = (uint32_t *)vulkan_idct_spv,
    };

    VkShaderModule shaderModule;
    VkResult result = priv.vkCreateShaderModule(priv.device, &shadercreateInfo, NULL, &shaderModule);
    if (result != VK_SUCCESS) {
        printf("fail to create shader module\n");
        return;
    }

    VkDescriptorSetLayoutBinding bindings[3] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = bindings,
    };

    priv.vkCreateDescriptorSetLayout(priv.device, &layoutInfo, NULL, &priv.descriptorSetLayout);

    VkPushConstantRange pushrange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(int),
    };

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &priv.descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushrange,
    };

    if(priv.vkCreatePipelineLayout(priv.device, &pipelineLayoutCreateInfo, NULL, &priv.pipelineLayout) != VK_SUCCESS) {
        printf("Failed to create pipeline layout\n");
        return;
    }
    
    VkPipelineShaderStageCreateInfo shader_stage_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main",
        .pSpecializationInfo = NULL,
    };

    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shader_stage_create_info,
        .layout = priv.pipelineLayout,
    };

    if (priv.vkCreateComputePipelines(priv.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &priv.pipeline) != VK_SUCCESS) {
        printf("Failed to create compute pipeline\n");
        return;
    }

    priv.vkDestroyShaderModule(priv.device, shaderModule, NULL);

    priv.b[0] = createbuffer(16 * sizeof(uint16_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, phydev, queueidx, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    priv.b[1] = createbuffer(16 * sizeof(uint16_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, phydev, queueidx, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    priv.b[2] = createbuffer(16 * sizeof(uint16_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, phydev, queueidx, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    VkDescriptorPoolSize poolSizes[2] = {
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1}
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes,
    };
    if (priv.vkCreateDescriptorPool(priv.device, &descriptorPoolCreateInfo, NULL, &priv.descriptorPool)!= VK_SUCCESS) {
        printf("Failed to create descriptor pool\n");
        return;
    }

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = priv.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &priv.descriptorSetLayout,
    };
    if(priv.vkAllocateDescriptorSets(priv.device, &descriptorSetAllocateInfo, &priv.descriptorSet) != VK_SUCCESS) {
        printf("Failed to allocate descriptor set!");
        return;
    }
    VkDescriptorBufferInfo buffinfo[3] = {
        {.buffer = priv.b[0].buffer, .offset = 0, .range = 16 * sizeof(uint16_t)},
        {.buffer = priv.b[1].buffer, .offset = 0, .range = 16 * sizeof(uint16_t)},
        {.buffer = priv.b[2].buffer, .offset = 0, .range = 16 * sizeof(uint16_t)},
    };
    VkWriteDescriptorSet writeDescriptorSets[3] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet= priv.descriptorSet, .dstBinding=0, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &buffinfo[0],},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet= priv.descriptorSet, .dstBinding=1, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &buffinfo[1],},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet= priv.descriptorSet, .dstBinding=2, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffinfo[2],},
    };
    priv.vkUpdateDescriptorSets(priv.device, 3, writeDescriptorSets, 0, NULL);

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = priv.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (priv.vkAllocateCommandBuffers(priv.device, &allocInfo, &priv.cmdbuffer) != VK_SUCCESS) {
        printf("Failed to allocate command buffer\n");
    }

    accl_ops_register(&vulkan_accl);
}
