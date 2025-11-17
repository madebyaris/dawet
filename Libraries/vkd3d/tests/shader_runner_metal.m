/*
 * Copyright 2024 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#import <Metal/Metal.h>
#define COBJMACROS
#define VKD3D_TEST_NO_DEFS
/* Avoid conflicts with the Objective C BOOL definition. */
#define BOOL VKD3D_BOOLEAN
#include "shader_runner.h"
#include "vkd3d_d3dcommon.h"
#undef BOOL

static const MTLResourceOptions DEFAULT_BUFFER_RESOURCE_OPTIONS = MTLResourceCPUCacheModeDefaultCache
        | MTLResourceStorageModeShared
        | MTLResourceHazardTrackingModeDefault;

struct metal_resource
{
    struct resource r;

    id<MTLBuffer> buffer;
    id<MTLTexture> texture;
};

struct metal_resource_readback
{
    struct resource_readback rb;
    id<MTLBuffer> buffer;
};

struct metal_runner
{
    struct shader_runner r;
    struct shader_runner_caps caps;

    id<MTLDevice> device;
    id<MTLCommandQueue> queue;

    ID3D10Blob *d3d_blobs[SHADER_TYPE_COUNT];
    struct vkd3d_shader_scan_signature_info signatures[SHADER_TYPE_COUNT];
};

static MTLPixelFormat get_metal_pixel_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return MTLPixelFormatRGBA32Float;
        case DXGI_FORMAT_R32G32B32A32_UINT:
            return MTLPixelFormatRGBA32Uint;
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return MTLPixelFormatRGBA32Sint;
        case DXGI_FORMAT_R32_FLOAT:
            return MTLPixelFormatR32Float;
        case DXGI_FORMAT_R32_UINT:
            return MTLPixelFormatR32Uint;
        case DXGI_FORMAT_D32_FLOAT:
            return MTLPixelFormatDepth32Float;
        default:
            return MTLPixelFormatInvalid;
    }
}

static MTLVertexFormat get_metal_attribute_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return MTLVertexFormatFloat4;
        case DXGI_FORMAT_R32G32_FLOAT:
            return MTLVertexFormatFloat2;
        default:
            return MTLVertexFormatInvalid;
    }
}

static MTLPrimitiveType get_metal_primitive_type(D3D_PRIMITIVE_TOPOLOGY topology)
{
    switch (topology)
    {
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return MTLPrimitiveTypeTriangle;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            return MTLPrimitiveTypeTriangleStrip;

        default:
            fatal_error("Unhandled topology %#x.\n", topology);
    }
}

static MTLCompareFunction get_metal_compare_function(D3D12_COMPARISON_FUNC func)
{
    switch (func)
    {
        case VKD3D_SHADER_COMPARISON_FUNC_NEVER:
            return MTLCompareFunctionNever;
        case VKD3D_SHADER_COMPARISON_FUNC_LESS:
            return MTLCompareFunctionLess;
        case VKD3D_SHADER_COMPARISON_FUNC_EQUAL:
            return MTLCompareFunctionEqual;
        case VKD3D_SHADER_COMPARISON_FUNC_LESS_EQUAL:
            return MTLCompareFunctionLessEqual;
        case VKD3D_SHADER_COMPARISON_FUNC_GREATER:
            return MTLCompareFunctionGreater;
        case VKD3D_SHADER_COMPARISON_FUNC_NOT_EQUAL:
            return MTLCompareFunctionNotEqual;
        case VKD3D_SHADER_COMPARISON_FUNC_GREATER_EQUAL:
            return MTLCompareFunctionGreaterEqual;
        case VKD3D_SHADER_COMPARISON_FUNC_ALWAYS:
            return MTLCompareFunctionAlways;
    }
}

static void trace_messages(const char *messages)
{
    const char *p, *end, *line;

    if (!vkd3d_test_state.debug_level)
        return;

    p = messages;
    end = &p[strlen(p)];

    trace("Received messages:\n");
    while (p < end)
    {
        line = p;
        if ((p = memchr(line, '\n', end - line)))
            ++p;
        else
            p = end;
        trace("    %.*s", (int)(p - line), line);
    }
}

static struct metal_resource *metal_resource(struct resource *r)
{
    return CONTAINING_RECORD(r, struct metal_resource, r);
}

static struct metal_runner *metal_runner(struct shader_runner *r)
{
    return CONTAINING_RECORD(r, struct metal_runner, r);
}

static void init_resource_buffer(struct metal_runner *runner,
        struct metal_resource *resource, const struct resource_params *params)
{
    id<MTLDevice> device = runner->device;

    if (params->data)
        resource->buffer = [device newBufferWithBytes:params->data
                length:params->data_size
                options:DEFAULT_BUFFER_RESOURCE_OPTIONS];
    else
        resource->buffer = [device newBufferWithLength:params->data_size options:DEFAULT_BUFFER_RESOURCE_OPTIONS];
}

static void init_resource_texture(struct metal_runner *runner,
        struct metal_resource *resource, const struct resource_params *params)
{
    id<MTLDevice> device = runner->device;
    MTLTextureDescriptor *desc;

    if (params->desc.type != RESOURCE_TYPE_RENDER_TARGET && params->desc.type != RESOURCE_TYPE_DEPTH_STENCIL)
        return;

    if (params->desc.sample_count > 1)
    {
        if (params->desc.level_count > 1)
            fatal_error("Multisampled texture has multiple levels.\n");

        if (![device supportsTextureSampleCount:params->desc.sample_count])
        {
            skip("Format #%x with sample count %u is not supported; skipping.\n", params->desc.format,
                    params->desc.sample_count);
            return;
        }
    }

    if (params->data)
        fatal_error("Initial texture resource data not implemented.\n");

    desc = [[MTLTextureDescriptor alloc] init];
    if (params->desc.sample_count > 1)
        desc.textureType = MTLTextureType2DMultisample;
    desc.pixelFormat = get_metal_pixel_format(params->desc.format);
    ok(desc.pixelFormat != MTLPixelFormatInvalid, "Unhandled pixel format %#x.\n", params->desc.format);
    desc.width = params->desc.width;
    desc.height = params->desc.height;
    desc.mipmapLevelCount = params->desc.level_count;
    desc.sampleCount = max(params->desc.sample_count, 1);
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;

    resource->texture = [device newTextureWithDescriptor:desc];
    ok(resource->texture, "Failed to create texture.\n");
    [desc release];
}

static struct resource *metal_runner_create_resource(struct shader_runner *r, const struct resource_params *params)
{
    struct metal_runner *runner = metal_runner(r);
    struct metal_resource *resource;

    resource = calloc(1, sizeof(*resource));
    init_resource(&resource->r, params);

    if (params->desc.dimension == RESOURCE_DIMENSION_BUFFER)
        init_resource_buffer(runner, resource, params);
    else
        init_resource_texture(runner, resource, params);

    return &resource->r;
}

static void metal_runner_destroy_resource(struct shader_runner *r, struct resource *res)
{
    struct metal_resource *resource = metal_resource(res);

    [resource->texture release];
    [resource->buffer release];
    free(resource);
}

static bool compile_shader(struct metal_runner *runner, enum shader_type type, struct vkd3d_shader_code *out)
{
    struct vkd3d_shader_interface_info interface_info = {.type = VKD3D_SHADER_STRUCTURE_TYPE_INTERFACE_INFO};
    struct vkd3d_shader_compile_info info = {.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO};
    struct vkd3d_shader_resource_binding bindings[MAX_RESOURCES + MAX_SAMPLERS];
    struct vkd3d_shader_resource_binding *binding;
    unsigned int descriptor_binding = 0;
    char *messages;
    int ret;

    const struct vkd3d_shader_compile_option options[] =
    {
        {VKD3D_SHADER_COMPILE_OPTION_API_VERSION, VKD3D_SHADER_API_VERSION_1_13},
        {VKD3D_SHADER_COMPILE_OPTION_FEATURE, shader_runner_caps_get_feature_flags(&runner->caps)},
    };

    if (!(runner->d3d_blobs[type] = compile_hlsl(&runner->r, type)))
        return false;

    info.next = &interface_info;
    info.source.code = ID3D10Blob_GetBufferPointer(runner->d3d_blobs[type]);
    info.source.size = ID3D10Blob_GetBufferSize(runner->d3d_blobs[type]);
    info.source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;
    info.target_type = VKD3D_SHADER_TARGET_MSL;
    info.options = options;
    info.option_count = ARRAY_SIZE(options);
    info.log_level = VKD3D_SHADER_LOG_WARNING;

    if (runner->r.uniform_count)
    {
        binding = &bindings[interface_info.binding_count++];
        binding->type = VKD3D_SHADER_DESCRIPTOR_TYPE_CBV;
        binding->register_space = 0;
        binding->register_index = 0;
        binding->shader_visibility = VKD3D_SHADER_VISIBILITY_ALL;
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BUFFER;
        binding->binding.set = 0;
        binding->binding.binding = descriptor_binding++;
        binding->binding.count = 1;
    }

    interface_info.bindings = bindings;
    interface_info.next = &runner->signatures[type];
    runner->signatures[type].type = VKD3D_SHADER_STRUCTURE_TYPE_SCAN_SIGNATURE_INFO;
    runner->signatures[type].next = NULL;

    ret = vkd3d_shader_compile(&info, out, &messages);
    if (messages)
        trace_messages(messages);
    vkd3d_shader_free_messages(messages);

    return ret >= 0;
}

static id<MTLFunction> compile_stage(struct metal_runner *runner, enum shader_type type)
{
    struct vkd3d_shader_code out;
    id<MTLFunction> function;
    id<MTLLibrary> library;
    NSString *src;
    NSError *err;

    if (!compile_shader(runner, type, &out))
        return nil;
    src = [[[NSString alloc] initWithBytes:out.code length:out.size encoding:NSUTF8StringEncoding] autorelease];
    library = [[runner->device newLibraryWithSource:src options:nil error:&err] autorelease];
    ok(library, "Failed to create MTLLibrary.\n");
    if (err)
        trace_messages([err.localizedDescription UTF8String]);
    function = [library newFunctionWithName:@"shader_entry"];
    ok(function, "Failed to create MTLFunction.\n");
    vkd3d_shader_free_shader_code(&out);

    return [function autorelease];
}

static bool metal_runner_dispatch(struct shader_runner *r, unsigned int x, unsigned int y, unsigned int z)
{
    return false;
}

static void metal_runner_clear(struct shader_runner *r, struct resource *res, const struct vec4 *clear_value)
{
    struct metal_resource *resource = metal_resource(res);
    struct metal_runner *runner = metal_runner(r);
    id<MTLRenderCommandEncoder> encoder;
    id<MTLCommandBuffer> command_buffer;
    MTLRenderPassDescriptor *descriptor;

    @autoreleasepool
    {
        descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        command_buffer = [runner->queue commandBuffer];

        switch (resource->r.desc.type)
        {
            case RESOURCE_TYPE_RENDER_TARGET:
                descriptor.colorAttachments[0].texture = resource->texture;
                descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
                descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
                descriptor.colorAttachments[0].clearColor =
                        MTLClearColorMake(clear_value->x, clear_value->y, clear_value->z, clear_value->w);
                break;
            case RESOURCE_TYPE_DEPTH_STENCIL:
                descriptor.depthAttachment.texture = resource->texture;
                descriptor.depthAttachment.loadAction = MTLLoadActionClear;
                descriptor.depthAttachment.storeAction = MTLStoreActionStore;
                descriptor.depthAttachment.clearDepth = clear_value->x;
                break;
            default:
                fatal_error("Clears are not implemented for resource type %#x.\n", resource->r.desc.type);
        }

        encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
        [encoder endEncoding];

        [command_buffer commit];
        [command_buffer waitUntilCompleted];
    }
}

static bool metal_runner_draw(struct shader_runner *r, D3D_PRIMITIVE_TOPOLOGY topology,
        unsigned int vertex_count, unsigned int instance_count)
{
    MTLViewport viewport = {0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    MTLRenderPassColorAttachmentDescriptor *attachment;
    unsigned int fb_width, fb_height, vb_idx, i, j;
    struct metal_runner *runner = metal_runner(r);
    MTLRenderPipelineDescriptor *pipeline_desc;
    MTLVertexBufferLayoutDescriptor *binding;
    id<MTLDepthStencilState> ds_state = nil;
    id<MTLDevice> device = runner->device;
    size_t attribute_offsets[32], stride;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLCommandBuffer> command_buffer;
    MTLDepthStencilDescriptor *ds_desc;
    MTLRenderPassDescriptor *pass_desc;
    id<MTLArgumentEncoder> descriptors;
    MTLVertexDescriptor *vertex_desc;
    struct metal_resource *resource;
    MTLArgumentDescriptor *cbv_desc;
    id<MTLRenderPipelineState> pso;
    id<MTLBuffer> argument_buffer;
    id<MTLBuffer> cb;
    bool ret = false;
    NSError *err;

    struct
    {
        id<MTLBuffer> buffer;
        unsigned int idx;
    } vb_info[MAX_RESOURCES];

    @autoreleasepool
    {
        pass_desc = [MTLRenderPassDescriptor renderPassDescriptor];
        pipeline_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        vertex_desc = [MTLVertexDescriptor vertexDescriptor];

        if (!(pipeline_desc.vertexFunction = compile_stage(runner, SHADER_TYPE_VS)))
        {
            trace("Failed to compile vertex function.\n");
            goto done;
        }

        if (!(pipeline_desc.fragmentFunction = compile_stage(runner, SHADER_TYPE_PS)))
        {
            trace("Failed to compile fragment function.\n");
            goto done;
        }

        fb_width = ~0u;
        fb_height = ~0u;
        /* [[buffer(0)]] is used for the descriptor argument buffer. */
        vb_idx = 1;
        memset(vb_info, 0, sizeof(vb_info));
        for (i = 0; i < runner->r.resource_count; ++i)
        {
            resource = metal_resource(runner->r.resources[i]);
            switch (resource->r.desc.type)
            {
                case RESOURCE_TYPE_RENDER_TARGET:
                    pipeline_desc.colorAttachments[resource->r.desc.slot].pixelFormat = resource->texture.pixelFormat;
                    attachment = pass_desc.colorAttachments[resource->r.desc.slot];
                    attachment.loadAction = MTLLoadActionLoad;
                    attachment.storeAction = MTLStoreActionStore;
                    attachment.texture = resource->texture;
                    if (resource->r.desc.width < fb_width)
                        fb_width = resource->r.desc.width;
                    if (resource->r.desc.height < fb_height)
                        fb_height = resource->r.desc.height;
                    break;

                case RESOURCE_TYPE_DEPTH_STENCIL:
                    pipeline_desc.depthAttachmentPixelFormat = resource->texture.pixelFormat;
                    pass_desc.depthAttachment.loadAction = MTLLoadActionLoad;
                    pass_desc.depthAttachment.storeAction = MTLStoreActionStore;
                    pass_desc.depthAttachment.texture = resource->texture;
                    if (resource->r.desc.width < fb_width)
                        fb_width = resource->r.desc.width;
                    if (resource->r.desc.height < fb_height)
                        fb_height = resource->r.desc.height;

                    ds_desc = [[[MTLDepthStencilDescriptor alloc] init] autorelease];
                    ds_desc.depthCompareFunction = get_metal_compare_function(runner->r.depth_func);
                    ds_desc.depthWriteEnabled = true;
                    ds_state = [[device newDepthStencilStateWithDescriptor:ds_desc] autorelease];
                    break;

                case RESOURCE_TYPE_VERTEX_BUFFER:
                    assert(resource->r.desc.slot < ARRAY_SIZE(vb_info));
                    for (j = 0, stride = 0; j < runner->r.input_element_count; ++j)
                    {
                        if (runner->r.input_elements[j].slot != resource->r.desc.slot)
                            continue;
                        assert(j < ARRAY_SIZE(attribute_offsets));
                        attribute_offsets[j] = stride;
                        stride += runner->r.input_elements[j].texel_size;
                    }
                    if (!stride)
                        break;
                    vb_info[resource->r.desc.slot].buffer = resource->buffer;
                    vb_info[resource->r.desc.slot].idx = vb_idx;
                    binding = [vertex_desc.layouts objectAtIndexedSubscript:vb_idx];
                    binding.stepFunction = MTLVertexStepFunctionPerVertex;
                    binding.stride = stride;
                    ++vb_idx;
                    break;

                default:
                    break;
            }
        }
        viewport.width = fb_width;
        viewport.height = fb_height;

        command_buffer = [runner->queue commandBuffer];
        encoder = [command_buffer renderCommandEncoderWithDescriptor:pass_desc];

        if (r->uniform_count)
        {
            cb = [[device newBufferWithBytes:r->uniforms
                    length:runner->r.uniform_count * sizeof(*runner->r.uniforms)
                    options:DEFAULT_BUFFER_RESOURCE_OPTIONS] autorelease];

            cbv_desc = [MTLArgumentDescriptor argumentDescriptor];
            cbv_desc.dataType = MTLDataTypePointer;
            cbv_desc.index = 0;
            cbv_desc.access = MTLBindingAccessReadOnly;

            descriptors = [[device newArgumentEncoderWithArguments:@[cbv_desc]] autorelease];
            argument_buffer = [[device newBufferWithLength:descriptors.encodedLength
                    options:DEFAULT_BUFFER_RESOURCE_OPTIONS] autorelease];
            [descriptors setArgumentBuffer:argument_buffer offset:0];
            [descriptors setBuffer:cb offset:0 atIndex:0];

            [encoder setVertexBuffer:argument_buffer offset:0 atIndex:0];
            [encoder setFragmentBuffer:argument_buffer offset:0 atIndex:0];
            [encoder useResource:cb usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
        }

        if (runner->r.input_element_count > 32)
            fatal_error("Unsupported input element count %zu.\n", runner->r.input_element_count);

        for (i = 0; i < runner->r.input_element_count; ++i)
        {
            const struct input_element *element = &runner->r.input_elements[i];
            const struct vkd3d_shader_signature_element *signature_element;
            MTLVertexAttributeDescriptor *attribute;

            signature_element = vkd3d_shader_find_signature_element(&runner->signatures[SHADER_TYPE_VS].input,
                    element->name, element->index, 0);
            ok(signature_element, "Cannot find signature element %s%u.\n", element->name, element->index);

            attribute = [vertex_desc.attributes objectAtIndexedSubscript:signature_element->register_index];
            attribute.bufferIndex = vb_info[element->slot].idx;
            attribute.format = get_metal_attribute_format(element->format);
            ok(attribute.format != MTLVertexFormatInvalid, "Unhandled attribute format %#x.\n", element->format);
            attribute.offset = attribute_offsets[i];
        }
        for (i = 0; i < ARRAY_SIZE(vb_info); ++i)
        {
            if (!vb_info[i].buffer)
                continue;
            [encoder setVertexBuffer:vb_info[i].buffer offset:0 atIndex:vb_info[i].idx];
        }

        pipeline_desc.vertexDescriptor = vertex_desc;

        if (!(pso = [[device newRenderPipelineStateWithDescriptor:pipeline_desc error:&err] autorelease]))
        {
            trace("Failed to compile pipeline state.\n");
            if (err)
                trace_messages([err.localizedDescription UTF8String]);
            [encoder endEncoding];
            goto done;
        }

        if (ds_state)
            [encoder setDepthStencilState:ds_state];

        [encoder setRenderPipelineState:pso];
        [encoder setViewport:viewport];
        [encoder drawPrimitives:get_metal_primitive_type(topology)
                vertexStart:0
                vertexCount:vertex_count
                instanceCount:instance_count];
        [encoder endEncoding];

        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        ret = true;
    }

done:
    for (i = 0; i < SHADER_TYPE_COUNT; ++i)
    {
        if (!runner->d3d_blobs[i])
            continue;

        vkd3d_shader_free_scan_signature_info(&runner->signatures[i]);
        ID3D10Blob_Release(runner->d3d_blobs[i]);
        runner->d3d_blobs[i] = NULL;
    }

    return ret;
}

static bool metal_runner_copy(struct shader_runner *r, struct resource *src, struct resource *dst)
{
    return false;
}

static struct resource_readback *metal_runner_get_resource_readback(struct shader_runner *r, struct resource *res)
{
    struct metal_resource *resource = metal_resource(res);
    struct metal_runner *runner = metal_runner(r);
    id<MTLCommandBuffer> command_buffer;
    struct metal_resource_readback *rb;
    id<MTLBlitCommandEncoder> blit;

    if (resource->r.desc.dimension != RESOURCE_DIMENSION_2D)
        fatal_error("Unhandled resource dimension %#x.\n", resource->r.desc.dimension);
    if (resource->r.desc.sample_count > 1)
        fatal_error("Unhandled sample count %u.\n", resource->r.desc.sample_count);

    rb = malloc(sizeof(*rb));
    rb->rb.width = resource->r.desc.width;
    rb->rb.height = resource->r.desc.height;
    rb->rb.depth = 1;
    rb->rb.row_pitch = rb->rb.width * resource->r.desc.texel_size;
    rb->buffer = [runner->device newBufferWithLength:rb->rb.row_pitch * rb->rb.height
            options:DEFAULT_BUFFER_RESOURCE_OPTIONS];

    @autoreleasepool
    {
        command_buffer = [runner->queue commandBuffer];

        blit = [command_buffer blitCommandEncoder];
        [blit copyFromTexture:resource->texture
                sourceSlice:0
                sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(rb->rb.width, rb->rb.height, rb->rb.depth)
                toBuffer:rb->buffer
                destinationOffset:0
                destinationBytesPerRow:rb->rb.row_pitch
                destinationBytesPerImage:0];
        [blit endEncoding];

        [command_buffer commit];
        [command_buffer waitUntilCompleted];
    }
    rb->rb.data = rb->buffer.contents;

    return &rb->rb;
}

static void metal_runner_release_readback(struct shader_runner *r, struct resource_readback *rb)
{
    struct metal_resource_readback *metal_rb = CONTAINING_RECORD(rb, struct metal_resource_readback, rb);

    [metal_rb->buffer release];
    free(rb);
}

static const struct shader_runner_ops metal_runner_ops =
{
    .create_resource = metal_runner_create_resource,
    .destroy_resource = metal_runner_destroy_resource,
    .dispatch = metal_runner_dispatch,
    .clear = metal_runner_clear,
    .draw = metal_runner_draw,
    .copy = metal_runner_copy,
    .get_resource_readback = metal_runner_get_resource_readback,
    .release_readback = metal_runner_release_readback,
};

static bool check_msl_support(void)
{
    const enum vkd3d_shader_target_type *target_types;
    unsigned int count, i;

    target_types = vkd3d_shader_get_supported_target_types(VKD3D_SHADER_SOURCE_DXBC_TPF, &count);
    for (i = 0; i < count; ++i)
    {
        if (target_types[i] == VKD3D_SHADER_TARGET_MSL)
            return true;
    }

    return false;
}

static bool check_argument_buffer_support(id<MTLDevice> device)
{
    MTLArgumentDescriptor *d;

    d = [MTLArgumentDescriptor argumentDescriptor];
    d.dataType = MTLDataTypePointer;

    @try
    {
        [[device newArgumentEncoderWithArguments:@[d]] release];
        return true;
    }
    @catch (NSException *e)
    {
        return false;
    }
}

static bool metal_runner_init(struct metal_runner *runner)
{
    NSArray<id<MTLDevice>> *devices;
    id<MTLDevice> device;

    if (!check_msl_support())
    {
        skip("MSL support is not enabled. If this is unintentional, "
                "add -DVKD3D_SHADER_UNSUPPORTED_MSL to CPPFLAGS.\n");
        return false;
    }

    memset(runner, 0, sizeof(*runner));

    devices = MTLCopyAllDevices();
    if (![devices count])
    {
        skip("Failed to find a usable Metal device.\n");
        [devices release];
        return false;
    }
    device = [devices objectAtIndex:0];
    runner->device = [device retain];
    [devices release];

    trace("GPU: %s\n", [[device name] UTF8String]);

    if (!check_argument_buffer_support(device))
    {
        skip("Device does not have usable argument buffer support.\n");
        [device release];
        return false;
    }

    if (!(runner->queue = [device newCommandQueue]))
    {
        skip("Failed to create command queue.\n");
        [device release];
        return false;
    }

    runner->caps.runner = "Metal";
    runner->caps.tags[0] = "msl";
    runner->caps.tag_count = 1;
    runner->caps.minimum_shader_model = SHADER_MODEL_4_0;
    runner->caps.maximum_shader_model = SHADER_MODEL_5_0;

    return true;
}

static void metal_runner_cleanup(struct metal_runner *runner)
{
    [runner->queue release];
    [runner->device release];
}

void run_shader_tests_metal(void)
{
    struct metal_runner runner;

    if (!metal_runner_init(&runner))
        return;
    run_shader_tests(&runner.r, &runner.caps, &metal_runner_ops, NULL);
    metal_runner_cleanup(&runner);
}
