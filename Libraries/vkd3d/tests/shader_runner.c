/*
 * Copyright 2020-2024 Elizabeth Figura for CodeWeavers
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

/*
 * This application contains code derived from piglit, the license for which
 * follows:
 *
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef __MINGW32__
# define _HRESULT_DEFINED
typedef int HRESULT;
#else
# define WIDL_C_INLINE_WRAPPERS
#endif

#define COBJMACROS
#define CONST_VTABLE
#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "vkd3d_windows.h"
#include "vkd3d_d3dcommon.h"
#include "vkd3d_d3dcompiler.h"
#include "vkd3d_test.h"
#include "shader_runner.h"

struct test_options test_options = {0};

#ifdef VKD3D_CROSSTEST
static const char HLSL_COMPILER[] = "d3dcompiler47.dll";
#else
static const char HLSL_COMPILER[] = "vkd3d-shader";
#endif
static const char *const model_strings[] =
{
    [SHADER_MODEL_2_0] = "2.0",
    [SHADER_MODEL_3_0] = "3.0",
    [SHADER_MODEL_4_0] = "4.0",
    [SHADER_MODEL_4_1] = "4.1",
    [SHADER_MODEL_5_0] = "5.0",
    [SHADER_MODEL_5_1] = "5.1",
    [SHADER_MODEL_6_0] = "6.0",
};

void fatal_error(const char *format, ...)
{
    unsigned int i;
    va_list args;

    for (i = 0; i < vkd3d_test_state.context_count; ++i)
        fprintf(stderr, "%s: ", vkd3d_test_state.context[i]);

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}

enum parse_state
{
    STATE_NONE,
    STATE_INPUT_LAYOUT,
    STATE_PREPROC,
    STATE_PREPROC_INVALID,
    STATE_REQUIRE,
    STATE_RESOURCE,
    STATE_SAMPLER,
    STATE_SHADER,
    STATE_TEST,
};

static enum shader_model match_shader_model_string(const char *string, const char **rest)
{
    for (enum shader_model i = 0; i < ARRAY_SIZE(model_strings); ++i)
    {
        if (!strncmp(string, model_strings[i], strlen(model_strings[i])))
        {
            *rest = string + strlen(model_strings[i]);
            return i;
        }
        /* Allow e.g. "4" as a shorthand for "4.0". */
        if (string[0] == model_strings[i][0] && !strcmp(&model_strings[i][1], ".0")
                && string[1] != '.' && !isdigit(string[1]))
        {
            *rest = string + 1;
            return i;
        }
    }

    fatal_error("Unrecognized shader model '%s'.\n", string);
}

static bool match_tag(struct shader_runner *runner, const char *tag)
{
    for (size_t i = 0; i < runner->caps->tag_count; ++i)
    {
        if (!strcmp(tag, runner->caps->tags[i]))
            return true;
    }

    return false;
}

static bool check_qualifier_args_conjunction(struct shader_runner *runner,
        const char *line, const char **const rest, uint32_t *model_mask)
{
    static const char *const valid_tags[] = {"d3d12", "glsl", "llvmpipe", "msl", "mvk", "vulkan"};
    bool holds = true;

    *model_mask = ~0u;

    while (*line != ')' && *line != '|')
    {
        enum shader_model model;
        bool match = false;

        while (isspace(*line))
            ++line;

        if (!strncmp(line, "sm>=", 4))
        {
            match = true;
            line += 4;
            model = match_shader_model_string(line, &line);
            *model_mask &= ~((1u << model) - 1);
            if (runner->minimum_shader_model < model)
                holds = false;
        }
        else if (!strncmp(line, "sm<", 3))
        {
            match = true;
            line += 3;
            model = match_shader_model_string(line, &line);
            *model_mask &= ((1u << model) - 1);
            if (runner->minimum_shader_model >= model)
                holds = false;
        }
        else
        {
            for (unsigned int i = 0; i < ARRAY_SIZE(valid_tags); ++i)
            {
                const char *option_text = valid_tags[i];
                size_t option_len = strlen(option_text);

                if (strncmp(line, option_text, option_len))
                    continue;

                match = true;
                line += option_len;
                holds &= match_tag(runner, option_text);
                break;
            }
        }

        while (isspace(*line))
            ++line;

        if (match && *line == '&')
        {
            ++line;
        }
        else if (*line != ')' && *line != '|')
        {
            fatal_error("Invalid qualifier argument '%s'.\n", line);
        }
    }

    assert(*line == ')' || *line == '|');
    if (rest)
        *rest = line;

    return holds;
}

static bool check_qualifier_args(struct shader_runner *runner,
        const char *line, const char **const rest, uint32_t *model_mask)
{
    bool first = true;
    bool holds = false;

    if (*line != '(')
    {
        *model_mask = ~0u;
        return true;
    }
    ++line;

    *model_mask = 0;

    while (*line != ')')
    {
        uint32_t sub_mask;

        if (!first && *line == '|')
            ++line;
        first = false;

        holds = check_qualifier_args_conjunction(runner, line, &line, &sub_mask) || holds;
        *model_mask |= sub_mask;
    }

    assert(*line == ')');
    if (rest)
        *rest = line + 1;

    return holds;
}

static bool match_string_generic(struct shader_runner *runner, const char *line,
        const char *token, const char **const rest, bool allow_qualifier_args)
{
    size_t len = strlen(token);
    uint32_t model_mask;
    bool holds = true;

    while (isspace(*line))
        ++line;

    if (strncmp(line, token, len) || !(isspace(line[len]) || line[len] == '(' || line[len] == ']'))
        return false;
    line += len;

    if (allow_qualifier_args)
        holds = check_qualifier_args(runner, line, &line, &model_mask);

    if (rest)
    {
        *rest = line;
        while (isspace(**rest))
            ++*rest;
    }
    return holds;
}

static bool match_string_with_args(struct shader_runner *runner,
        const char *line, const char *token, const char **const rest)
{
    return match_string_generic(runner, line, token, rest, true);
}

static bool match_string(const char *line, const char *token, const char **const rest)
{
    return match_string_generic(NULL, line, token, rest, false);
}

static const char *close_parentheses(const char *line)
{
    while (isspace(*line))
        ++line;

    if (*line != ')')
        fatal_error("Malformed probe arguments '%s'.\n", line);

    return line;
}

static DXGI_FORMAT parse_format(const char *line, enum texture_data_type *data_type, unsigned int *texel_size,
        bool *is_shadow, const char **rest)
{
    static const struct
    {
        const char *string;
        enum texture_data_type data_type;
        unsigned int texel_size;
        DXGI_FORMAT format;
        bool is_shadow;
    }
    formats[] =
    {
        {"r32g32b32a32-float",  TEXTURE_DATA_FLOAT, 16, DXGI_FORMAT_R32G32B32A32_FLOAT},
        {"r32g32b32a32-sint",   TEXTURE_DATA_SINT,  16, DXGI_FORMAT_R32G32B32A32_SINT},
        {"r32g32b32a32-uint",   TEXTURE_DATA_UINT,  16, DXGI_FORMAT_R32G32B32A32_UINT},
        {"r32g32-float",        TEXTURE_DATA_FLOAT,  8, DXGI_FORMAT_R32G32_FLOAT},
        {"r32g32-sint",         TEXTURE_DATA_SINT,   8, DXGI_FORMAT_R32G32_SINT},
        {"r32g32-uint",         TEXTURE_DATA_UINT,   8, DXGI_FORMAT_R32G32_UINT},
        {"r32-float-shadow",    TEXTURE_DATA_FLOAT,  4, DXGI_FORMAT_R32_FLOAT, true},
        {"r32-float",           TEXTURE_DATA_FLOAT,  4, DXGI_FORMAT_R32_FLOAT},
        {"r32-sint",            TEXTURE_DATA_SINT,   4, DXGI_FORMAT_R32_SINT},
        {"r32-uint",            TEXTURE_DATA_UINT,   4, DXGI_FORMAT_R32_UINT},
        {"r32-typeless",        TEXTURE_DATA_UINT,   4, DXGI_FORMAT_R32_TYPELESS},
        {"unknown",             TEXTURE_DATA_UINT,   0, DXGI_FORMAT_UNKNOWN},
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(formats); ++i)
    {
        if (match_string(line, formats[i].string, rest))
        {
            if (data_type)
                *data_type = formats[i].data_type;
            if (texel_size)
                *texel_size = formats[i].texel_size;
            if (is_shadow)
                *is_shadow = formats[i].is_shadow;
            return formats[i].format;
        }
    }

    fatal_error("Unknown format '%s'.\n", line);
}

static const char *const shader_cap_strings[] =
{
    [SHADER_CAP_CLIP_PLANES]     = "clip-planes",
    [SHADER_CAP_DEPTH_BOUNDS]    = "depth-bounds",
    [SHADER_CAP_FLOAT64]         = "float64",
    [SHADER_CAP_FOG]             = "fog",
    [SHADER_CAP_GEOMETRY_SHADER] = "geometry-shader",
    [SHADER_CAP_INT64]           = "int64",
    [SHADER_CAP_POINT_SIZE]      = "point-size",
    [SHADER_CAP_ROV]             = "rov",
    [SHADER_CAP_WAVE_OPS]        = "wave-ops",
};

static bool match_shader_cap_string(const char *line, enum shader_cap *cap)
{
    for (enum shader_cap i = 0; i < SHADER_CAP_COUNT; ++i)
    {
        if (match_string(line, shader_cap_strings[i], &line))
        {
            *cap = i;
            return true;
        }
    }
    return false;
}

static void parse_require_directive(struct shader_runner *runner, const char *line)
{
    enum shader_cap shader_cap;
    bool less_than = false;
    unsigned int i;

    if (match_string(line, "shader model >=", &line)
            || (less_than = match_string(line, "shader model <", &line)))
    {
        for (i = 0; i < ARRAY_SIZE(model_strings); ++i)
        {
            if (match_string(line, model_strings[i], &line))
            {
                if (less_than)
                {
                    if (!i)
                        fatal_error("Shader model < '%s' is invalid.\n", line);
                    runner->maximum_shader_model = min(runner->maximum_shader_model, i - 1);
                }
                else
                {
                    runner->minimum_shader_model = max(runner->minimum_shader_model, i);
                }
                return;
            }
        }

        fatal_error("Unknown shader model '%s'.\n", line);
    }
    else if (match_string(line, "options:", &line))
    {
        static const struct option
        {
            unsigned int option;
            const char *name;
        }
        options[] =
        {
            { 0, "none" },
            { D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, "row-major" },
            { D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR, "column-major" },
            { D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, "backcompat" },
            { D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES, "unbounded-descriptor-arrays" },
        };

        runner->compile_options = 0;
        for (i = 0; i < ARRAY_SIZE(options); ++i)
        {
            if (match_string(line, options[i].name, &line))
                runner->compile_options |= options[i].option;
        }
    }
    else if (match_string(line, "format", &line))
    {
        DXGI_FORMAT format = parse_format(line, NULL, NULL, NULL, &line);

        while (line[0] != '\0')
        {
            if (match_string(line, "uav-load", &line))
                runner->require_format_caps[format] |= FORMAT_CAP_UAV_LOAD;
            else
                fatal_error("Unknown format cap '%s'.\n", line);
        }
    }
    else if (match_shader_cap_string(line, &shader_cap))
    {
        runner->require_shader_caps[shader_cap] = true;
    }
    else
    {
        fatal_error("Unknown require directive '%s'.\n", line);
    }
}

static D3D12_COMPARISON_FUNC parse_comparison_func(const char *line, const char **rest)
{
    static const struct
    {
        const char *string;
        D3D12_COMPARISON_FUNC func;
    }
    funcs[] =
    {
        {"less equal", D3D12_COMPARISON_FUNC_LESS_EQUAL},
        {"not equal", D3D12_COMPARISON_FUNC_NOT_EQUAL},
        {"greater equal", D3D12_COMPARISON_FUNC_GREATER_EQUAL},
        {"never", D3D12_COMPARISON_FUNC_NEVER},
        {"less", D3D12_COMPARISON_FUNC_LESS},
        {"equal", D3D12_COMPARISON_FUNC_EQUAL},
        {"greater", D3D12_COMPARISON_FUNC_GREATER},
        {"always", D3D12_COMPARISON_FUNC_ALWAYS},
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(funcs); ++i)
    {
        if (match_string(line, funcs[i].string, rest))
            return funcs[i].func;
    }

    fatal_error("Unknown comparison func '%s'.\n", line);
}

static D3D12_TEXTURE_ADDRESS_MODE parse_sampler_address_mode(const char *line, const char **rest)
{
    if (match_string(line, "border", rest))
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    if (match_string(line, "clamp", rest))
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    if (match_string(line, "mirror_once", rest))
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    if (match_string(line, "mirror", rest))
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    if (match_string(line, "wrap", rest))
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    fatal_error("Unknown sampler address mode '%s'.\n", line);
}

static void parse_sampler_directive(struct sampler *sampler, const char *line)
{
    if (match_string(line, "address", &line))
    {
        sampler->u_address = parse_sampler_address_mode(line, &line);
        sampler->v_address = parse_sampler_address_mode(line, &line);
        sampler->w_address = parse_sampler_address_mode(line, &line);
    }
    else if (match_string(line, "filter", &line))
    {
        static const struct
        {
            const char *string;
            D3D12_FILTER filter;
        }
        filters[] =
        {
            {"point point point",       D3D12_FILTER_MIN_MAG_MIP_POINT},
            {"point point linear",      D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR},
            {"point linear point",      D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT},
            {"point linear linear",     D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR},
            {"linear point point",      D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT},
            {"linear point linear",     D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR},
            {"linear linear point",     D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT},
            {"linear linear linear",    D3D12_FILTER_MIN_MAG_MIP_LINEAR},
        };
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(filters); ++i)
        {
            if (match_string(line, filters[i].string, &line))
            {
                sampler->filter = filters[i].filter;
                if (sampler->func)
                    sampler->filter |= D3D12_FILTER_REDUCTION_TYPE_COMPARISON << D3D12_FILTER_REDUCTION_TYPE_SHIFT;
                return;
            }
        }

        fatal_error("Unknown sampler filter '%s'.\n", line);
    }
    else if (match_string(line, "comparison", &line))
    {
        sampler->filter |= D3D12_FILTER_REDUCTION_TYPE_COMPARISON << D3D12_FILTER_REDUCTION_TYPE_SHIFT;
        sampler->func = parse_comparison_func(line, &line);
        return;
    }
    else
    {
        fatal_error("Unknown sampler directive '%s'.\n", line);
    }
}

static void parse_resource_directive(struct resource_params *resource, const char *line)
{
    if (match_string(line, "format", &line))
    {
        resource->desc.format = parse_format(line, &resource->data_type,
                &resource->desc.texel_size, &resource->is_shadow, &line);
        assert_that(!resource->explicit_format, "Resource format already specified.\n");
        resource->explicit_format = true;
    }
    else if (match_string(line, "stride", &line))
    {
        if (sscanf(line, "%u", &resource->stride) < 1)
            fatal_error("Malformed texture stride '%s'.\n", line);
        resource->desc.texel_size = resource->stride;
        resource->desc.format = DXGI_FORMAT_UNKNOWN;
        assert_that(!resource->explicit_format, "Resource format already specified.\n");
        resource->explicit_format = true;
    }
    else if (match_string(line, "size", &line))
    {
        if (sscanf(line, "( buffer , %u ) ", &resource->desc.width) == 1)
        {
            resource->desc.dimension = RESOURCE_DIMENSION_BUFFER;
            resource->desc.height = 1;
        }
        else if (sscanf(line, "( raw_buffer , %u ) ", &resource->desc.width) == 1)
        {
            resource->desc.dimension = RESOURCE_DIMENSION_BUFFER;
            resource->desc.height = 1;
            resource->is_raw = true;
        }
        else if (sscanf(line, "( counter_buffer , %u ) ", &resource->desc.width) == 1)
        {
            resource->desc.dimension = RESOURCE_DIMENSION_BUFFER;
            resource->desc.height = 1;
            resource->is_uav_counter = true;
            resource->stride = sizeof(uint32_t);
            resource->desc.texel_size = resource->stride;
            resource->desc.format = DXGI_FORMAT_UNKNOWN;
            assert_that(!resource->explicit_format, "Resource format already specified.\n");
            resource->explicit_format = true;
        }
        else if (sscanf(line, "( 2d , %u , %u ) ", &resource->desc.width, &resource->desc.height) == 2)
        {
            resource->desc.dimension = RESOURCE_DIMENSION_2D;
        }
        else if (sscanf(line, "( 2dms , %u , %u , %u ) ",
                &resource->desc.sample_count, &resource->desc.width, &resource->desc.height) == 3)
        {
            resource->desc.dimension = RESOURCE_DIMENSION_2D;
        }
        else
        {
            fatal_error("Malformed resource size '%s'.\n", line);
        }
    }
    else if (match_string(line, "levels", &line))
    {
        char *rest;

        resource->desc.level_count = strtoul(line, &rest, 10);
        if (rest == line)
            fatal_error("Malformed texture directive '%s'.\n", line);
    }
    else
    {
        union
        {
            float f;
            int32_t i;
            uint32_t u;
        } u;
        char *rest;

        u.u = 0;

        for (;;)
        {
            switch (resource->data_type)
            {
                case TEXTURE_DATA_FLOAT:
                    u.f = strtof(line, &rest);
                    break;

                case TEXTURE_DATA_SINT:
                    u.i = strtol(line, &rest, 0);
                    break;

                case TEXTURE_DATA_UINT:
                    u.u = strtoul(line, &rest, 0);
                    break;
            }

            if (rest == line)
                break;

            vkd3d_array_reserve((void **)&resource->data, &resource->data_capacity, resource->data_size + sizeof(u), 1);
            memcpy(resource->data + resource->data_size, &u, sizeof(u));
            resource->data_size += sizeof(u);
            line = rest;
        }
    }
}

static void parse_input_layout_directive(struct shader_runner *runner, const char *line)
{
    struct input_element *element;
    const char *rest;

    vkd3d_array_reserve((void **)&runner->input_elements, &runner->input_element_capacity,
            runner->input_element_count + 1, sizeof(*runner->input_elements));
    element = &runner->input_elements[runner->input_element_count++];

    element->slot = strtoul(line, (char **)&rest, 10);
    if (rest == line)
        fatal_error("Malformed input layout directive '%s'.\n", line);
    line = rest;

    element->format = parse_format(line, NULL, &element->texel_size, NULL, &line);

    if (!(rest = strpbrk(line, " \n")))
        rest = line + strlen(line);
    element->name = malloc(rest - line + 1);
    memcpy(element->name, line, rest - line);
    element->name[rest - line] = 0;
    line = rest;

    element->index = strtoul(line, (char **)&rest, 10);
    if (rest == line)
        element->index = 0;
}

void init_resource(struct resource *resource, const struct resource_params *params)
{
    resource->desc = params->desc;
}

struct resource *shader_runner_get_resource(struct shader_runner *runner, enum resource_type type, unsigned int slot)
{
    struct resource *resource;
    size_t i;

    for (i = 0; i < runner->resource_count; ++i)
    {
        resource = runner->resources[i];

        if (resource->desc.type == type && resource->desc.slot == slot)
            return resource;
    }

    return NULL;
}

static void set_resource(struct shader_runner *runner, const struct resource_params *params)
{
    struct resource *resource;
    size_t i;

    if (!(resource = runner->ops->create_resource(runner, params)))
    {
        if (!bitmap_is_set(runner->failed_resources[params->desc.type], params->desc.slot))
        {
            ++runner->failed_resource_count;
            bitmap_set(runner->failed_resources[params->desc.type], params->desc.slot);
        }
        return;
    }

    if (bitmap_is_set(runner->failed_resources[params->desc.type], params->desc.slot))
    {
        assert(runner->failed_resource_count);
        --runner->failed_resource_count;
        bitmap_clear(runner->failed_resources[params->desc.type], params->desc.slot);
    }

    for (i = 0; i < runner->resource_count; ++i)
    {
        if (runner->resources[i]->desc.slot == resource->desc.slot
                && runner->resources[i]->desc.type == resource->desc.type)
        {
            runner->ops->destroy_resource(runner, runner->resources[i]);
            runner->resources[i] = resource;
            return;
        }
    }

    if (runner->resource_count == MAX_RESOURCES)
        fatal_error("Too many resources declared.\n");

    runner->resources[runner->resource_count++] = resource;
}

static void set_default_target(struct shader_runner *runner)
{
    struct resource_params params = {0};

    if (shader_runner_get_resource(runner, RESOURCE_TYPE_RENDER_TARGET, 0)
            || shader_runner_get_resource(runner, RESOURCE_TYPE_DEPTH_STENCIL, 0))
        return;

    params.desc.slot = 0;
    params.desc.type = RESOURCE_TYPE_RENDER_TARGET;
    params.desc.dimension = RESOURCE_DIMENSION_2D;
    params.desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    params.data_type = TEXTURE_DATA_FLOAT;
    params.desc.texel_size = 16;
    params.desc.width = RENDER_TARGET_WIDTH;
    params.desc.height = RENDER_TARGET_HEIGHT;
    params.desc.level_count = 1;

    set_resource(runner, &params);
}

static void set_uniforms(struct shader_runner *runner, size_t offset, size_t count, const void *uniforms)
{
    size_t initial_count = runner->uniform_count;

    runner->uniform_count = align(max(runner->uniform_count, offset + count), 4);
    vkd3d_array_reserve((void **)&runner->uniforms, &runner->uniform_capacity,
            runner->uniform_count, sizeof(*runner->uniforms));
    memset(runner->uniforms + initial_count, 127,
            (runner->uniform_count - initial_count) * sizeof(*runner->uniforms));
    memcpy(runner->uniforms + offset, uniforms, count * sizeof(*runner->uniforms));
}

static void read_int(const char **line, int *i, bool is_uniform)
{
    char *rest;
    long val;

    errno = 0;
    val = strtol(*line, &rest, 0);

    if (errno != 0 || (is_uniform && *rest != '\0' && !isspace((unsigned char)*rest)))
        fatal_error("Malformed int constant '%s'.\n", *line);

    *i = val;
    if (*i != val)
        fatal_error("Out of range int constant '%.*s'.\n", (int)(rest - *line), *line);

    *line = rest + (!is_uniform && *rest == ',');
}

static void read_uint(const char **line, unsigned int *u, bool is_uniform)
{
    char *rest;
    unsigned long val;

    errno = 0;
    val = strtoul(*line, &rest, 0);

    if (errno != 0 || (is_uniform && *rest != '\0' && !isspace((unsigned char)*rest)))
        fatal_error("Malformed uint constant '%s'.\n", *line);

    *u = val;
    if (*u != val)
        fatal_error("Out of range uint constant '%.*s'.\n", (int)(rest - *line), *line);

    *line = rest + (!is_uniform && *rest == ',');
}

static void read_int4(const char **line, struct ivec4 *v, bool is_uniform)
{
    read_int(line, &v->x, is_uniform);
    read_int(line, &v->y, is_uniform);
    read_int(line, &v->z, is_uniform);
    read_int(line, &v->w, is_uniform);
}

static void read_uint4(const char **line, struct uvec4 *v, bool is_uniform)
{
    read_uint(line, &v->x, is_uniform);
    read_uint(line, &v->y, is_uniform);
    read_uint(line, &v->z, is_uniform);
    read_uint(line, &v->w, is_uniform);
}

static void read_int64(const char **line, int64_t *i, bool is_uniform)
{
    char *rest;
    int64_t val;

    errno = 0;
    val = strtoll(*line, &rest, 0);

    if (errno != 0 || (is_uniform && *rest != '\0' && !isspace((unsigned char)*rest)))
        fatal_error("Malformed int64 constant '%s'.\n", *line);

    *i = val;
    *line = rest + (!is_uniform && *rest == ',');
}

static void read_uint64(const char **line, uint64_t *u, bool is_uniform)
{
    char *rest;
    uint64_t val;

    errno = 0;
    val = strtoull(*line, &rest, 0);

    if (errno != 0 || (is_uniform && *rest != '\0' && !isspace((unsigned char)*rest)))
        fatal_error("Malformed uint64 constant '%s'.\n", *line);

    *u = val;
    *line = rest + (!is_uniform && *rest == ',');
}

static void read_int64_t2(const char **line, struct i64vec2 *v)
{
    read_int64(line, &v->x, true);
    read_int64(line, &v->y, true);
}

static void read_uint64_t2(const char **line, struct u64vec2 *v)
{
    read_uint64(line, &v->x, true);
    read_uint64(line, &v->y, true);
}

static struct resource *parse_resource_reference(struct shader_runner *runner, const char **const line)
{
    enum resource_type type;
    unsigned int slot = 0;

    if (match_string(*line, "dsv", line))
        type = RESOURCE_TYPE_DEPTH_STENCIL;
    else if (match_string(*line, "rtv", line))
        type = RESOURCE_TYPE_RENDER_TARGET;
    else if (match_string(*line, "srv", line))
        type = RESOURCE_TYPE_TEXTURE;
    else if (match_string(*line, "uav", line))
        type = RESOURCE_TYPE_UAV;
    else if (match_string(*line, "vb", line))
        type = RESOURCE_TYPE_VERTEX_BUFFER;
    else
        fatal_error("Malformed resource reference '%s'.\n", *line);

    if (type != RESOURCE_TYPE_DEPTH_STENCIL)
        read_uint(line, &slot, false);

    return shader_runner_get_resource(runner, type, slot);
}

static void parse_test_directive(struct shader_runner *runner, const char *line)
{
    bool skip_directive = false;
    const char *line_ini;
    bool match = true;
    char *rest;
    int ret;

    runner->is_todo = false;
    runner->is_bug = false;

    while (match)
    {
        match = false;

        if (match_string_with_args(runner, line, "todo", &line))
        {
            runner->is_todo = true;
            match = true;
        }

        if (match_string_with_args(runner, line, "bug", &line))
        {
            runner->is_bug = true;
            match = true;
        }

        line_ini = line;
        if (match_string_with_args(runner, line, "if", &line))
        {
            match = true;
        }
        else if (line != line_ini)
        {
            /* Matched "if" but for other shader models. */
            skip_directive = true;
            match = true;
        }
    }

    if (skip_directive)
    {
        const char *new_line;

        if ((new_line = strchr(line, '\n')))
            line = new_line + 1;
        else
            line += strlen(line);
        return;
    }

    if (match_string(line, "dispatch", &line))
    {
        unsigned int x, y, z;

        ret = sscanf(line, "%u %u %u", &x, &y, &z);
        if (ret < 3)
            fatal_error("Malformed dispatch arguments '%s'.\n", line);

        runner->last_render_failed = !runner->ops->dispatch(runner, x, y, z);
        todo_if(runner->is_todo) bug_if(runner->is_bug)
        ok(!runner->last_render_failed, "Dispatch failed.\n");
    }
    else if (match_string(line, "clear rtv", &line))
    {
        struct resource *resource;
        unsigned int slot;
        struct vec4 v;

        if (sscanf(line, "%u %f %f %f %f", &slot, &v.x, &v.y, &v.z, &v.w) < 5)
            fatal_error("Malformed rtv clear arguments '%s'.\n", line);

        set_default_target(runner);

        if (!(resource = shader_runner_get_resource(runner, RESOURCE_TYPE_RENDER_TARGET, slot)))
            fatal_error("Resource not found.\n");
        runner->ops->clear(runner, resource, &v);
    }
    else if (match_string(line, "clear dsv", &line))
    {
        struct resource *resource;
        struct vec4 v = {0};

        if (!sscanf(line, "%f", &v.x))
            fatal_error("Malformed dsv clear arguments '%s'.\n", line);

        if (!(resource = shader_runner_get_resource(runner, RESOURCE_TYPE_DEPTH_STENCIL, 0)))
            fatal_error("Resource not found.\n");
        runner->ops->clear(runner, resource, &v);
    }
    else if (match_string(line, "depth-bounds", &line))
    {
        if (sscanf(line, "%f %f", &runner->depth_min, &runner->depth_max) != 2)
            fatal_error("Malformed depth-bounds arguments '%s'.\n", line);
        if (!runner->caps->shader_caps[SHADER_CAP_DEPTH_BOUNDS])
            fatal_error("depth-bounds set but runner does not support depth bounds testing.");
        runner->depth_bounds = true;
    }
    else if (match_string(line, "depth", &line))
    {
        runner->depth_func = parse_comparison_func(line, &line);
    }
    else if (match_string(line, "draw quad", &line))
    {
        struct resource_params params;
        struct input_element *element;
        unsigned int i;

        /* For simplicity, draw a large triangle instead. */
        static const struct vec2 quad[] =
        {
            {-2.0f, -2.0f},
            {-2.0f,  4.0f},
            { 4.0f, -2.0f},
        };

        static const char vs_source[] =
            "float4 main(float4 pos : position) : sv_position\n"
            "{\n"
            "    return pos;\n"
            "}";

        if (!runner->shader_source[SHADER_TYPE_HS] != !runner->shader_source[SHADER_TYPE_DS])
            fatal_error("Have a domain or hull shader but not both.\n");

        set_default_target(runner);

        for (i = 0; i < runner->input_element_count; ++i)
            free(runner->input_elements[i].name);

        vkd3d_array_reserve((void **)&runner->input_elements, &runner->input_element_capacity,
                1, sizeof(*runner->input_elements));
        element = &runner->input_elements[0];
        element->name = strdup("position");
        element->slot = 0;
        element->format = DXGI_FORMAT_R32G32_FLOAT;
        element->texel_size = sizeof(*quad);
        element->index = 0;
        runner->input_element_count = 1;

        memset(&params, 0, sizeof(params));
        params.desc.slot = 0;
        params.desc.type = RESOURCE_TYPE_VERTEX_BUFFER;
        params.desc.dimension = RESOURCE_DIMENSION_BUFFER;
        params.desc.width = sizeof(quad);
        params.data = malloc(sizeof(quad));
        memcpy(params.data, quad, sizeof(quad));
        params.data_size = sizeof(quad);
        set_resource(runner, &params);
        free(params.data);

        if (!runner->shader_source[SHADER_TYPE_VS])
            runner->shader_source[SHADER_TYPE_VS] = strdup(vs_source);

        runner->sample_count = 1;
        for (i = 0; i < runner->resource_count; ++i)
        {
            if (runner->resources[i]->desc.type == RESOURCE_TYPE_RENDER_TARGET
                    || runner->resources[i]->desc.type == RESOURCE_TYPE_DEPTH_STENCIL)
                runner->sample_count = max(runner->sample_count, runner->resources[i]->desc.sample_count);
        }

        runner->last_render_failed = !runner->ops->draw(runner, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 3, 1);
        todo_if(runner->is_todo) bug_if(runner->is_bug)
        ok(!runner->last_render_failed, "Draw failed.\n");
    }
    else if (match_string(line, "draw", &line))
    {
        unsigned int vertex_count, instance_count;
        D3D_PRIMITIVE_TOPOLOGY topology;

        if (!runner->shader_source[SHADER_TYPE_HS] != !runner->shader_source[SHADER_TYPE_DS])
            fatal_error("Have a domain or hull shader but not both.\n");

        set_default_target(runner);

        if (match_string(line, "triangle list", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        else if (match_string(line, "triangle strip", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        else if (match_string(line, "point list", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        else if (match_string(line, "1 control point patch list", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        else if (match_string(line, "2 control point patch list", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST;
        else if (match_string(line, "3 control point patch list", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
        else if (match_string(line, "4 control point patch list", &line))
            topology = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
        else
            fatal_error("Unknown primitive topology '%s'.\n", line);

        vertex_count = strtoul(line, &rest, 10);
        if (line == rest)
            fatal_error("Malformed vertex count '%s'.\n", line);
        instance_count = strtoul(line = rest, &rest, 10);
        if (line == rest)
            instance_count = 1;

        runner->sample_count = 1;
        for (unsigned int i = 0; i < runner->resource_count; ++i)
        {
            if (runner->resources[i]->desc.type == RESOURCE_TYPE_RENDER_TARGET
                    || runner->resources[i]->desc.type == RESOURCE_TYPE_DEPTH_STENCIL)
                runner->sample_count = max(runner->sample_count, runner->resources[i]->desc.sample_count);
        }

        runner->last_render_failed = !runner->ops->draw(runner, topology, vertex_count, instance_count);
        todo_if(runner->is_todo) bug_if(runner->is_bug)
        ok(!runner->last_render_failed, "Draw failed.\n");
    }
    else if (match_string(line, "copy", &line))
    {
        struct resource *src, *dst;

        if (!(src = parse_resource_reference(runner, &line)))
            fatal_error("Undefined source resource.\n");
        if (!(dst = parse_resource_reference(runner, &line)))
            fatal_error("Undefined destination resource.\n");

        if (src->desc.dimension != dst->desc.dimension
                || src->desc.texel_size != dst->desc.texel_size
                || src->desc.width != dst->desc.width
                || src->desc.height != dst->desc.height
                || src->desc.level_count != dst->desc.level_count
                || src->desc.sample_count != dst->desc.sample_count)
            fatal_error("Resource dimensions don't match.\n");

        ret = runner->ops->copy(runner, src, dst);
        todo_if(runner->is_todo) bug_if(runner->is_bug)
        ok(ret, "Failed to copy resource.\n");
    }
    else if (match_string(line, "probe", &line))
    {
        unsigned int left, top, right, bottom, ulps, slot;
        struct resource_readback *rb;
        struct resource *resource;
        bool is_signed = false;
        RECT rect;
        int len;

        if (runner->last_render_failed)
            return;

        if (match_string(line, "uav", &line))
        {
            slot = strtoul(line, &rest, 10);

            if (rest == line)
                fatal_error("Malformed UAV index '%s'.\n", line);
            line = rest;

            resource = shader_runner_get_resource(runner, RESOURCE_TYPE_UAV, slot);
        }
        else if (match_string(line, "rtv", &line))
        {
            slot = strtoul(line, &rest, 10);

            if (rest == line)
                fatal_error("Malformed render target index '%s'.\n", line);
            line = rest;

            resource = shader_runner_get_resource(runner, RESOURCE_TYPE_RENDER_TARGET, slot);
        }
        else if (match_string(line, "dsv", &line))
        {
            resource = shader_runner_get_resource(runner, RESOURCE_TYPE_DEPTH_STENCIL, 0);
        }
        else
        {
            resource = shader_runner_get_resource(runner, RESOURCE_TYPE_RENDER_TARGET, 0);
        }

        rb = runner->ops->get_resource_readback(runner, resource);

        if (sscanf(line, " ( %d , %d , %d , %d )%n", &left, &top, &right, &bottom, &len) == 4)
        {
            set_rect(&rect, left, top, right, bottom);
            line += len;
        }
        else if (sscanf(line, " ( %u , %u )%n", &left, &top, &len) == 2)
        {
            set_rect(&rect, left, top, left + 1, top + 1);
            line += len;
        }
        else if (sscanf(line, " ( %u )%n", &left, &len) == 1)
        {
            set_rect(&rect, left, 0, left + 1, 1);
            line += len;
        }
        else
        {
            fatal_error("Malformed probe arguments '%s'.\n", line);
        }

        if (match_string(line, "rgbaui", &line))
        {
            struct uvec4 v;

            if (*line != '(')
                fatal_error("Malformed probe arguments '%s'.\n", line);
            ++line;
            read_uint4(&line, &v, false);
            line = close_parentheses(line);
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_uvec4(rb, &rect, &v);
        }
        else if (match_string(line, "rgbai", &line))
        {
            struct ivec4 v;

            if (*line != '(')
                fatal_error("Malformed probe arguments '%s'.\n", line);
            ++line;
            read_int4(&line, &v, false);
            line = close_parentheses(line);
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_ivec4(rb, &rect, &v);
        }
        else if (match_string(line, "rgba", &line))
        {
            struct vec4 v;

            ret = sscanf(line, "( %f , %f , %f , %f ) %u", &v.x, &v.y, &v.z, &v.w, &ulps);
            if (ret < 4)
                fatal_error("Malformed probe arguments '%s'.\n", line);
            if (ret < 5)
                ulps = 0;
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_vec4(rb, &rect, &v, ulps);
        }
        else if (match_string(line, "rg", &line))
        {
            struct vec4 v;

            ret = sscanf(line, "( %f , %f ) %u", &v.x, &v.y, &ulps);
            if (ret < 2)
                fatal_error("Malformed probe arguments '%s'.\n", line);
            if (ret < 3)
                ulps = 0;
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_vec2(rb, &rect, &v, ulps);
        }
        else if (match_string(line, "rui", &line) || (is_signed = match_string(line, "ri", &line)))
        {
            unsigned int expect;
            D3D12_BOX box;

            box.left = rect.left;
            box.right = rect.right;
            box.top = rect.top;
            box.bottom = rect.bottom;
            box.front = 0;
            box.back = 1;
            if (*line != '(')
                fatal_error("Malformed probe arguments '%s'.\n", line);
            ++line;
            if (is_signed)
                read_int(&line, (int *)&expect, false);
            else
                read_uint(&line, &expect, false);
            line = close_parentheses(line);
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_uint(rb, &box, expect, 0);
        }
        else if (match_string(line, "rgui64", &line))
        {
            struct u64vec2 v;

            if (*line != '(')
                fatal_error("Malformed probe arguments '%s'.\n", line);
            ++line;
            read_uint64(&line, &v.x, false);
            read_uint64(&line, &v.y, false);
            line = close_parentheses(line);
            todo_if(runner->is_todo) check_readback_data_u64vec2(rb, &rect, &v);
        }
        else if (match_string(line, "rui64", &line) || (is_signed = match_string(line, "ri64", &line)))
        {
            uint64_t expect;
            D3D12_BOX box;

            box.left = rect.left;
            box.right = rect.right;
            box.top = rect.top;
            box.bottom = rect.bottom;
            box.front = 0;
            box.back = 1;
            if (*line != '(')
                fatal_error("Malformed probe arguments '%s'.\n", line);
            ++line;
            if (is_signed)
                read_int64(&line, (int64_t *)&expect, false);
            else
                read_uint64(&line, &expect, false);
            line = close_parentheses(line);
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_uint64(rb, &box, expect, 0);
        }
        else if (match_string(line, "rgd", &line))
        {
            struct dvec2 v;

            ret = sscanf(line, "( %lf , %lf ) %u", &v.x, &v.y, &ulps);
            if (ret < 2)
                fatal_error("Malformed probe arguments '%s'.\n", line);
            if (ret < 3)
                ulps = 0;
            todo_if(runner->is_todo) check_readback_data_dvec2(rb, &rect, &v, ulps);
        }
        else if (match_string(line, "rd", &line))
        {
            double expect;

            ret = sscanf(line, "( %lf ) %u", &expect, &ulps);
            if (ret < 1)
                fatal_error("Malformed probe arguments '%s'.\n", line);
            if (ret < 2)
                ulps = 0;
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_double(rb, &rect, expect, ulps);
        }
        else if (match_string(line, "r", &line))
        {
            float expect;

            ret = sscanf(line, "( %f ) %u", &expect, &ulps);
            if (ret < 1)
                fatal_error("Malformed probe arguments '%s'.\n", line);
            if (ret < 2)
                ulps = 0;
            todo_if(runner->is_todo) bug_if(runner->is_bug)
            check_readback_data_float(rb, &rect, expect, ulps);
        }
        else
        {
            fatal_error("Malformed probe arguments '%s'.\n", line);
        }

        runner->ops->release_readback(runner, rb);
    }
    else if (match_string(line, "uniform", &line))
    {
        unsigned int offset;

        if (!sscanf(line, "%u", &offset))
            fatal_error("Malformed uniform offset '%s'.\n", line);
        line = strchr(line, ' ') + 1;

        if (match_string(line, "float4", &line))
        {
            struct vec4 v;

            if (sscanf(line, "%f %f %f %f", &v.x, &v.y, &v.z, &v.w) < 4)
                fatal_error("Malformed float4 constant '%s'.\n", line);
            set_uniforms(runner, offset, 4, &v);
        }
        else if (match_string(line, "float", &line))
        {
            float f;

            if (sscanf(line, "%f", &f) < 1)
                fatal_error("Malformed float constant '%s'.\n", line);
            set_uniforms(runner, offset, 1, &f);
        }
        else if (match_string(line, "double2", &line))
        {
            struct dvec2 v;

            if (sscanf(line, "%lf %lf", &v.x, &v.y) < 2)
                fatal_error("Malformed double2 constant '%s'.\n", line);
            set_uniforms(runner, offset, 4, &v);
        }
        else if (match_string(line, "int4", &line))
        {
            struct ivec4 v;

            read_int4(&line, &v, true);
            set_uniforms(runner, offset, 4, &v);
        }
        else if (match_string(line, "uint4", &line))
        {
            struct uvec4 v;

            read_uint4(&line, &v, true);
            set_uniforms(runner, offset, 4, &v);
        }
        else if (match_string(line, "int", &line))
        {
            int i;

            read_int(&line, &i, true);
            set_uniforms(runner, offset, 1, &i);
        }
        else if (match_string(line, "uint", &line))
        {
            unsigned int u;

            read_uint(&line, &u, true);
            set_uniforms(runner, offset, 1, &u);
        }
        else if (match_string(line, "int64_t2", &line))
        {
            struct i64vec2 v;

            read_int64_t2(&line, &v);
            set_uniforms(runner, offset, 4, &v);
        }
        else if (match_string(line, "uint64_t2", &line))
        {
            struct u64vec2 v;

            read_uint64_t2(&line, &v);
            set_uniforms(runner, offset, 4, &v);
        }
        else
        {
            fatal_error("Unknown uniform type '%s'.\n", line);
        }
    }
    else if (match_string(line, "sample mask", &line))
    {
        unsigned int sample_mask;

        read_uint(&line, &sample_mask, false);
        runner->sample_mask = sample_mask;
    }
    else if (match_string(line, "alpha test", &line))
    {
        runner->alpha_test_func = (enum vkd3d_shader_comparison_func)parse_comparison_func(line, &line);
        runner->alpha_test_ref = strtof(line, &rest);
        line = rest;
    }
    else if (match_string(line, "shade mode", &line))
    {
        if (match_string(line, "flat", &line))
            runner->flat_shading = true;
        else
            runner->flat_shading = false;
    }
    else if (match_string(line, "clip-plane", &line))
    {
        unsigned int index;
        struct vec4 *v;

        index = strtoul(line, (char **)&rest, 10);
        if (rest == line || index >= 8)
            fatal_error("Malformed clip plane directive '%s'.\n", line);
        line = rest;

        v = &runner->clip_planes[index];

        if (match_string(line, "disable", &line))
            runner->clip_plane_mask &= ~(1u << index);
        else
        {
            if (sscanf(line, "%f %f %f %f", &v->x, &v->y, &v->z, &v->w) < 4)
                fatal_error("Malformed float4 constant '%s'.\n", line);
            runner->clip_plane_mask |= (1u << index);
        }
    }
    else if (match_string(line, "point-size", &line))
    {
        runner->point_size = strtof(line, &rest);
        line = rest;
        runner->point_size_min = strtof(line, &rest);
        line = rest;
        runner->point_size_max = strtof(line, &rest);
    }
    else if (match_string(line, "point-sprite", &line))
    {
        if (match_string(line, "on", &line))
            runner->point_sprite = true;
        else
            runner->point_sprite = false;
    }
    else if (match_string(line, "fog", &line))
    {
        if (match_string(line, "disable", &line))
            runner->fog_mode = FOG_MODE_DISABLE;
        else if (match_string(line, "none", &line))
            runner->fog_mode = FOG_MODE_NONE;
        else if (match_string(line, "linear", &line))
            runner->fog_mode = FOG_MODE_LINEAR;
        else if (match_string(line, "exp", &line))
            runner->fog_mode = FOG_MODE_EXP;
        else if (match_string(line, "exp2", &line))
            runner->fog_mode = FOG_MODE_EXP2;
        else
            fatal_error("Invalid fog mode '%s'.\n", line);

        if (match_string(line, "ortho", &line))
            runner->ortho_fog = true;
        if (match_string(line, "non-ortho", &line))
            runner->ortho_fog = false;

        if (runner->fog_mode == FOG_MODE_LINEAR)
        {
            if (sscanf(line, "%f %f", &runner->fog_start, &runner->fog_end) < 2)
                fatal_error("Malformed fog constants '%s'.\n", line);
        }
        else if (runner->fog_mode == FOG_MODE_EXP || runner->fog_mode == FOG_MODE_EXP2)
        {
            if (sscanf(line, "%f", &runner->fog_density) < 1)
                fatal_error("Malformed fog constants '%s'.\n", line);
        }
    }
    else if (match_string(line, "fog-colour", &line))
    {
        struct vec4 *v = &runner->fog_colour;

        if (sscanf(line, "%f %f %f %f", &v->x, &v->y, &v->z, &v->w) < 4)
            fatal_error("Malformed float4 constant '%s'.\n", line);
    }
    else
    {
        fatal_error("Unknown test directive '%s'.\n", line);
    }
}

struct sampler *shader_runner_get_sampler(struct shader_runner *runner, unsigned int slot)
{
    struct sampler *sampler;
    size_t i;

    for (i = 0; i < runner->sampler_count; ++i)
    {
        sampler = &runner->samplers[i];

        if (sampler->slot == slot)
            return sampler;
    }

    return NULL;
}

unsigned int get_vb_stride(const struct shader_runner *runner, unsigned int slot)
{
    unsigned int stride = 0;
    size_t i;

    /* We currently don't deal with vertex formats less than 32 bits, so don't
     * bother with alignment. */
    for (i = 0; i < runner->input_element_count; ++i)
    {
        const struct input_element *element = &runner->input_elements[i];

        if (element->slot == slot)
            stride += element->texel_size;
    }

    return stride;
}

static HRESULT map_special_hrs(HRESULT hr)
{
    if (hr == 0x88760b59)
    {
        trace("Mapping hr %#x (D3DXERR_INVALIDDATA) as %#x.\n", hr, E_FAIL);
        return E_FAIL;
    }
    if (hr == 0x80010064)
    {
        trace("Mapping unidentified hr %#x as %#x.\n", hr, E_FAIL);
        return E_FAIL;
    }
    return hr;
}

const char *shader_type_string(enum shader_type type)
{
    static const char *const shader_types[] =
    {
        [SHADER_TYPE_CS] = "cs",
        [SHADER_TYPE_PS] = "ps",
        [SHADER_TYPE_VS] = "vs",
        [SHADER_TYPE_HS] = "hs",
        [SHADER_TYPE_DS] = "ds",
        [SHADER_TYPE_GS] = "gs",
        [SHADER_TYPE_FX] = "fx",
    };
    assert(type < ARRAY_SIZE(shader_types));
    return shader_types[type];
}

ID3D10Blob *compile_hlsl(const struct shader_runner *runner, enum shader_type type)
{
    const char *source = runner->shader_source[type];
    ID3D10Blob *blob = NULL, *errors = NULL;
    char profile[7];
    HRESULT hr;

    static const char *const shader_models[] =
    {
        [SHADER_MODEL_2_0] = "2_0",
        [SHADER_MODEL_3_0] = "3_0",
        [SHADER_MODEL_4_0] = "4_0",
        [SHADER_MODEL_4_1] = "4_1",
        [SHADER_MODEL_5_0] = "5_0",
        [SHADER_MODEL_5_1] = "5_1",
        [SHADER_MODEL_6_0] = "6_0",
    };

    if (runner->minimum_shader_model >= SHADER_MODEL_6_0)
    {
        assert(runner->dxc_compiler);
        hr = dxc_compiler_compile_shader(runner->dxc_compiler, (enum d3d12_shader_type)type,
                runner->compile_options, false, source, &blob);
    }
    else
    {
        sprintf(profile, "%s_%s", shader_type_string(type), shader_models[runner->minimum_shader_model]);
        hr = D3DCompile(source, strlen(source), NULL, NULL, NULL, "main",
                profile, runner->compile_options, 0, &blob, &errors);
    }
    if (hr != S_OK)
    {
        todo_if (runner->is_todo)
            ok(false, "Failed to compile shader, hr %#x.\n", hr);
    }
    if (errors)
    {
        if (vkd3d_test_state.debug_level)
            trace("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
        ID3D10Blob_Release(errors);
    }
    return blob;
}

static void compile_shader(struct shader_runner *runner, const char *source,
        size_t len, enum shader_type type, enum shader_model model)
{
    bool use_dxcompiler = (model >= SHADER_MODEL_6_0);
    ID3D10Blob *blob = NULL, *errors = NULL;
    char profile[7];
    HRESULT hr;

    static const char *const shader_models[] =
    {
        [SHADER_MODEL_2_0] = "2_0",
        [SHADER_MODEL_3_0] = "3_0",
        [SHADER_MODEL_4_0] = "4_0",
        [SHADER_MODEL_4_1] = "4_1",
        [SHADER_MODEL_5_0] = "5_0",
        [SHADER_MODEL_5_1] = "5_1",
        [SHADER_MODEL_6_0] = "6_0",
    };

    static const char *const effect_models[] =
    {
        [SHADER_MODEL_2_0] = "2_0",
        [SHADER_MODEL_4_0] = "4_0",
        [SHADER_MODEL_4_1] = "4_1",
        [SHADER_MODEL_5_0] = "5_0",
    };

    /* We can let this go through D3DCompile() with the invalid shader model
     * string, but it returns a unique error code. Just skip it. */
    if (model < SHADER_MODEL_4_0 && type != SHADER_TYPE_VS && type != SHADER_TYPE_PS && type != SHADER_TYPE_FX)
        return;
    if (model < SHADER_MODEL_5_0 && (type == SHADER_TYPE_HS || type == SHADER_TYPE_DS))
        return;

    if (use_dxcompiler)
    {
        assert(runner->dxc_compiler);
        hr = dxc_compiler_compile_shader(runner->dxc_compiler, (enum d3d12_shader_type)type,
                runner->compile_options, false, source, &blob);
    }
    else
    {
        if (type == SHADER_TYPE_FX)
            sprintf(profile, "%s_%s", shader_type_string(type), effect_models[model]);
        else
            sprintf(profile, "%s_%s", shader_type_string(type), shader_models[model]);
        hr = D3DCompile(source, len, NULL, NULL, NULL, "main", profile, runner->compile_options, 0, &blob, &errors);
    }
    hr = map_special_hrs(hr);
    todo_if (runner->hlsl_todo[model])
        ok(hr == runner->hlsl_hrs[model], "Got unexpected hr %#x.\n", hr);
    if (hr == S_OK)
    {
        ID3D10Blob_Release(blob);
    }
    else
    {
        assert_that(!blob, "Expected no compiled shader blob.\n");
        if (!use_dxcompiler)
            assert_that(!!errors, "Expected non-NULL error blob.\n");
    }
    if (errors)
    {
        if (vkd3d_test_state.debug_level)
            trace("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
        ID3D10Blob_Release(errors);
    }
}

static void read_shader_directive(struct shader_runner *runner, const char *line, const char *src)
{
    for (unsigned int i = SHADER_MODEL_MIN; i <= SHADER_MODEL_MAX; ++i)
    {
        runner->hlsl_hrs[i] = S_OK;
        runner->hlsl_todo[i] = false;
    }

    while (*src && *src != ']')
    {
        uint32_t model_mask;

        if (match_string(src, "todo", &src))
        {
            check_qualifier_args(runner, src, &src, &model_mask);
            for (unsigned int i = SHADER_MODEL_MIN; i <= SHADER_MODEL_MAX; ++i)
            {
                /* 'todo' is not meaningful when dxcompiler is in use. */
                if (i < SHADER_MODEL_6_0 && (model_mask & (1u << i)))
                    runner->hlsl_todo[i] = true;
            }
        }
        else if (match_string(src, "fail", &src))
        {
            check_qualifier_args(runner, src, &src, &model_mask);
            for (unsigned int i = SHADER_MODEL_MIN; i <= SHADER_MODEL_MAX; ++i)
            {
                if (model_mask & (1u << i))
                    runner->hlsl_hrs[i] = E_FAIL;
            }
        }
        else if (match_string(src, "notimpl", &src))
        {
            check_qualifier_args(runner, src, &src, &model_mask);
            for (unsigned int i = SHADER_MODEL_MIN; i <= SHADER_MODEL_MAX; ++i)
            {
                if (model_mask & (1u << i))
                    runner->hlsl_hrs[i] = E_NOTIMPL;
            }
        }
        else
        {
            fatal_error("Malformed line '%s'.\n", line);
        }
    }

    if (strcmp(src, "]\n"))
        fatal_error("Malformed line '%s'.\n", line);
}

static bool check_capabilities(const struct shader_runner *runner, const struct shader_runner_caps *caps)
{
    unsigned int i;

    for (i = 0; i < SHADER_CAP_COUNT; ++i)
    {
        if (runner->require_shader_caps[i] && !caps->shader_caps[i])
            return false;
    }

    for (i = 0; i < ARRAY_SIZE(runner->require_format_caps); ++i)
    {
        if (runner->require_format_caps[i] & ~caps->format_caps[i])
            return false;
    }
    return true;
}

static void trace_tags(const struct shader_runner_caps *caps)
{
    char tags[80], *p;
    size_t rem;
    int rc;

    p = tags;
    rem = ARRAY_SIZE(tags);
    rc = snprintf(p, rem, "%8s:", "tags");
    p += rc;
    rem -= rc;

    for (size_t i = 0; i < caps->tag_count; ++i)
    {
        rc = snprintf(p, rem, " \"%s\"%s", caps->tags[i], i == caps->tag_count - 1 ? "" : ",");
        if (!(rc >= 0 && (size_t)rc < rem))
        {
            *p = 0;
            trace("%s\n", tags);

            p = tags;
            rem = ARRAY_SIZE(tags);
            rc = snprintf(p, rem, "%8s ", "");
            --i;
        }
        p += rc;
        rem -= rc;
    }
    trace("%s.\n", tags);
}

static void trace_shader_caps(const bool *caps)
{
    bool show_none = true;
    char buffer[80], *p;
    size_t rem;
    int rc;

    p = buffer;
    rem = ARRAY_SIZE(buffer);
    rc = snprintf(p, rem, "%8s:", "caps");
    p += rc;
    rem -= rc;

    for (size_t i = 0; i < SHADER_CAP_COUNT; ++i)
    {
        if (!caps[i])
            continue;

        rc = snprintf(p, rem, " %s", shader_cap_strings[i]);
        if (!(rc >= 0 && (size_t)rc < rem))
        {
            *p = 0;
            trace("%s\n", buffer);

            p = buffer;
            rem = ARRAY_SIZE(buffer);
            rc = snprintf(p, rem, "%8s ", "");
            --i;
        }
        p += rc;
        rem -= rc;
        show_none = false;
    }
    if (show_none)
        snprintf(p, rem, " (none)");
    trace("%s.\n", buffer);
}

static void trace_format_cap(const struct shader_runner_caps *caps, enum format_cap cap, const char *cap_name)
{
    bool show_none = true;
    char buffer[80], *p;
    size_t rem;
    int rc;

    p = buffer;
    rem = ARRAY_SIZE(buffer);
    rc = snprintf(p, rem, "%8s:", cap_name);
    p += rc;
    rem -= rc;

    for (unsigned int i = 0; i < ARRAY_SIZE(caps->format_caps); ++i)
    {
        if (caps->format_caps[i] & cap)
        {
            rc = snprintf(p, rem, " 0x%x", i);
            if (!(rc >= 0 && (size_t)rc < rem))
            {
                *p = 0;
                trace("%s\n", buffer);

                p = buffer;
                rem = ARRAY_SIZE(buffer);
                rc = snprintf(p, rem, "%8s ", "");
                --i;
            }
            p += rc;
            rem -= rc;
            show_none = false;
        }
    }
    if (show_none)
        snprintf(p, rem, " (none)");
    trace("%s.\n", buffer);
}

static void update_line_number_context(const char *testname, unsigned int line_number)
{
    vkd3d_test_pop_context();
    vkd3d_test_push_context("%s:%u", testname, line_number);
}

enum test_action
{
    TEST_ACTION_COMPILE,
    TEST_ACTION_RUN,
    TEST_ACTION_SKIP_EXECUTION,
};

void run_shader_tests(struct shader_runner *runner, const struct shader_runner_caps *caps,
        const struct shader_runner_ops *ops, void *dxc_compiler)
{
    enum test_action test_action = ops ? TEST_ACTION_RUN : TEST_ACTION_COMPILE;
    size_t shader_source_size = 0, shader_source_len = 0;
    enum shader_type shader_type = SHADER_TYPE_CS;
    struct resource_params current_resource;
    struct sampler *current_sampler = NULL;
    enum parse_state state = STATE_NONE;
    unsigned int i, line_number = 0, block_start_line_number = 0;
    char *shader_source = NULL;
    char line_buffer[256];
    const char *testname;
    FILE *f;

    trace("Compiling SM%s-SM%s shaders with %s and executing with %s.\n",
            model_strings[caps->minimum_shader_model], model_strings[caps->maximum_shader_model],
            dxc_compiler ? "dxcompiler" : HLSL_COMPILER, caps->runner);
    if (caps->tag_count)
        trace_tags(caps);
    trace_shader_caps(caps->shader_caps);
    trace_format_cap(caps, FORMAT_CAP_UAV_LOAD, "uav-load");

    if (!test_options.filename)
        fatal_error("No filename specified.\n");

    if (!(f = fopen(test_options.filename, "r")))
        fatal_error("Unable to open '%s' for reading: %s\n", test_options.filename, strerror(errno));

    memset(runner, 0, sizeof(*runner));
    runner->ops = ops;
    runner->caps = caps;
    runner->dxc_compiler = dxc_compiler;
    runner->minimum_shader_model = caps->minimum_shader_model;
    runner->maximum_shader_model = caps->maximum_shader_model;
    runner->alpha_test_func = VKD3D_SHADER_COMPARISON_FUNC_ALWAYS;
    runner->point_size = 1.0f;
    runner->point_size_min = 1.0f;
    runner->point_size_max = FLT_MAX;
    runner->fog_mode = FOG_MODE_DISABLE;
    runner->fog_start = 0.0f;
    runner->fog_end = 1.0f;
    runner->fog_density = 1.0f;

    runner->sample_mask = ~0u;
    runner->depth_bounds = false;
    runner->depth_min = 0.0f;
    runner->depth_max = 1.0f;

    if ((testname = strrchr(test_options.filename, '/')))
        ++testname;
    else
        testname = test_options.filename;

    vkd3d_test_push_context("%s:%u", testname, line_number);

    for (;;)
    {
        char *ret = fgets(line_buffer, sizeof(line_buffer), f);
        const char *line = line_buffer;

        line_number++;

        if (!ret || line[0] == '[')
        {
            update_line_number_context(testname, block_start_line_number);

            switch (state)
            {
                case STATE_INPUT_LAYOUT:
                case STATE_NONE:
                case STATE_SAMPLER:
                    break;

                case STATE_TEST:
                    if (test_action == TEST_ACTION_SKIP_EXECUTION)
                        skip("Missing capabilities.\n");
                    break;

                case STATE_REQUIRE:
                    if (runner->maximum_shader_model < runner->minimum_shader_model
                            || !check_capabilities(runner, caps))
                        test_action = TEST_ACTION_SKIP_EXECUTION;
                    break;

                case STATE_RESOURCE:
                    if (current_resource.desc.type == RESOURCE_TYPE_VERTEX_BUFFER)
                        current_resource.desc.width = current_resource.data_size;

                    if (current_resource.desc.type == RESOURCE_TYPE_UAV)
                        assert_that(current_resource.explicit_format, "Format must be specified for UAV resources.\n");

                    /* Not every backend supports every resource type
                     * (specifically, D3D9 doesn't support UAVs and
                     * textures with data type other than float). */
                    if (test_action == TEST_ACTION_RUN)
                        set_resource(runner, &current_resource);
                    free(current_resource.data);
                    break;

                case STATE_SHADER:
                    if (test_action == TEST_ACTION_COMPILE)
                    {
                        /* Most versions have relatively minor differences
                         * between them, so don't waste time testing every
                         * version possible.
                         * SM1-3, SM4-5, and SM6 are completely different
                         * formats, often with different HLSL semantics, so
                         * where possible try to test one version from each set. */

                        static const uint32_t sm1_mask = (1u << SHADER_MODEL_4_0) - 1;
                        static const uint32_t sm4_mask = ((1u << SHADER_MODEL_6_0) - 1) & ~sm1_mask;

                        uint32_t model_mask = 0;

                        if (runner->hlsl_hrs[runner->minimum_shader_model] == S_OK)
                            bitmap_set(&model_mask, runner->minimum_shader_model);
                        for (unsigned int j = SHADER_MODEL_MIN + 1; j <= SHADER_MODEL_MAX; ++j)
                        {
                            if (runner->hlsl_hrs[j - 1] != runner->hlsl_hrs[j])
                            {
                                /* Behaviour changes here; test both versions. */
                                bitmap_set(&model_mask, j - 1);
                                bitmap_set(&model_mask, j);
                            }
                        }

                        if (!(model_mask & sm1_mask) && runner->minimum_shader_model <= SHADER_MODEL_3_0)
                            bitmap_set(&model_mask, runner->minimum_shader_model);
                        if (!(model_mask & sm4_mask)
                                && runner->minimum_shader_model <= SHADER_MODEL_5_1
                                && runner->maximum_shader_model >= SHADER_MODEL_4_0)
                            bitmap_set(&model_mask, max(runner->minimum_shader_model, SHADER_MODEL_4_0));
                        if (runner->maximum_shader_model >= SHADER_MODEL_6_0)
                            bitmap_set(&model_mask, max(runner->minimum_shader_model, SHADER_MODEL_6_0));

                        for (enum shader_model model = runner->minimum_shader_model;
                                model <= runner->maximum_shader_model; ++model)
                        {
                            if (!bitmap_is_set(&model_mask, model))
                                continue;
                            vkd3d_test_push_context("Model %s", model_strings[model]);
                            compile_shader(runner, shader_source, shader_source_len, shader_type, model);
                            vkd3d_test_pop_context();
                        }
                    }
                    free(runner->shader_source[shader_type]);
                    runner->shader_source[shader_type] = shader_source;
                    shader_source = NULL;
                    shader_source_len = 0;
                    shader_source_size = 0;
                    break;

                case STATE_PREPROC_INVALID:
                {
                    ID3D10Blob *blob = NULL, *errors = NULL;
                    HRESULT hr;

                    if (test_action != TEST_ACTION_COMPILE)
                        break;

                    hr = D3DPreprocess(shader_source, strlen(shader_source), NULL, NULL, NULL, &blob, &errors);
                    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
                    ok(!blob, "Expected no compiled shader blob.\n");
                    ok(!!errors, "Expected non-NULL error blob.\n");

                    if (errors)
                    {
                        if (vkd3d_test_state.debug_level)
                            trace("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
                        ID3D10Blob_Release(errors);
                    }

                    shader_source_len = 0;
                    break;
                }

                case STATE_PREPROC:
                {
                    ID3D10Blob *blob = NULL, *errors = NULL;
                    SIZE_T size;
                    HRESULT hr;
                    char *text;

                    if (test_action != TEST_ACTION_COMPILE)
                        break;

                    hr = D3DPreprocess(shader_source, strlen(shader_source), NULL, NULL, NULL, &blob, &errors);
                    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
                    if (hr == S_OK)
                    {
                        if (errors)
                        {
                            if (vkd3d_test_state.debug_level)
                                trace("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors));
                            ID3D10Blob_Release(errors);
                        }

                        text = ID3D10Blob_GetBufferPointer(blob);
                        size = ID3D10Blob_GetBufferSize(blob);
                        ok(vkd3d_memmem(text, size, "pass", strlen("pass")),
                                "'pass' not found in preprocessed shader.\n");
                        ok(!vkd3d_memmem(text, size, "fail", strlen("fail")),
                                "'fail' found in preprocessed shader.\n");
                        ID3D10Blob_Release(blob);
                    }

                    shader_source_len = 0;
                    break;
                }
            }

            if (!ret)
                break;
        }

        if (line[0] == '[')
        {
            unsigned int index;

            block_start_line_number = line_number;
            update_line_number_context(testname, line_number);

            if (match_string(line, "[compute shader", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_CS;
            }
            else if (!strcmp(line, "[require]\n"))
            {
                state = STATE_REQUIRE;
                runner->minimum_shader_model = caps->minimum_shader_model;
                runner->maximum_shader_model = caps->maximum_shader_model;
                memset(runner->require_shader_caps, 0, sizeof(runner->require_shader_caps));
                memset(runner->require_format_caps, 0, sizeof(runner->require_format_caps));
                runner->compile_options = 0;
                test_action = ops ? TEST_ACTION_RUN : TEST_ACTION_COMPILE;
            }
            else if (match_string(line, "[pixel shader", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_PS;
            }
            else if (sscanf(line, "[sampler %u]\n", &index))
            {
                state = STATE_SAMPLER;

                if (!(current_sampler = shader_runner_get_sampler(runner, index)))
                {
                    if (runner->sampler_count == MAX_SAMPLERS)
                        fatal_error("Too many samplers declared.\n");

                    current_sampler = &runner->samplers[runner->sampler_count++];
                }
                memset(current_sampler, 0, sizeof(*current_sampler));
                current_sampler->slot = index;
                current_sampler->filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                current_sampler->u_address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                current_sampler->v_address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                current_sampler->w_address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            }
            else if (sscanf(line, "[rtv %u]\n", &index))
            {
                state = STATE_RESOURCE;

                memset(&current_resource, 0, sizeof(current_resource));

                current_resource.desc.slot = index;
                current_resource.desc.type = RESOURCE_TYPE_RENDER_TARGET;
                current_resource.desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                current_resource.data_type = TEXTURE_DATA_FLOAT;
                current_resource.desc.texel_size = 16;
                current_resource.desc.level_count = 1;
            }
            else if (!strcmp(line, "[dsv]\n"))
            {
                state = STATE_RESOURCE;

                memset(&current_resource, 0, sizeof(current_resource));

                current_resource.desc.slot = 0;
                current_resource.desc.type = RESOURCE_TYPE_DEPTH_STENCIL;
                current_resource.desc.format = DXGI_FORMAT_D32_FLOAT;
                current_resource.is_shadow = true;
                current_resource.data_type = TEXTURE_DATA_FLOAT;
                current_resource.desc.texel_size = 4;
                current_resource.desc.level_count = 1;
            }
            else if (sscanf(line, "[srv %u]\n", &index))
            {
                state = STATE_RESOURCE;

                memset(&current_resource, 0, sizeof(current_resource));

                current_resource.desc.slot = index;
                current_resource.desc.type = RESOURCE_TYPE_TEXTURE;
                current_resource.desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                current_resource.data_type = TEXTURE_DATA_FLOAT;
                current_resource.desc.texel_size = 16;
                current_resource.desc.level_count = 1;
            }
            else if (sscanf(line, "[uav %u]\n", &index))
            {
                state = STATE_RESOURCE;

                memset(&current_resource, 0, sizeof(current_resource));

                current_resource.desc.slot = index;
                current_resource.desc.type = RESOURCE_TYPE_UAV;
                current_resource.desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                current_resource.data_type = TEXTURE_DATA_FLOAT;
                current_resource.desc.texel_size = 16;
                current_resource.desc.level_count = 1;
            }
            else if (sscanf(line, "[vb %u]\n", &index))
            {
                state = STATE_RESOURCE;

                memset(&current_resource, 0, sizeof(current_resource));

                current_resource.desc.slot = index;
                current_resource.desc.type = RESOURCE_TYPE_VERTEX_BUFFER;
                current_resource.desc.dimension = RESOURCE_DIMENSION_BUFFER;
                current_resource.data_type = TEXTURE_DATA_FLOAT;
            }
            else if (!strcmp(line, "[test]\n"))
            {
                state = STATE_TEST;
            }
            else if (!strcmp(line, "[preproc]\n"))
            {
                state = STATE_PREPROC;
            }
            else if (!strcmp(line, "[preproc fail]\n"))
            {
                state = STATE_PREPROC_INVALID;
            }
            else if (match_string(line, "[vertex shader", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_VS;
            }
            else if (match_string(line, "[effect", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_FX;
            }
            else if (match_string(line, "[hull shader", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_HS;
            }
            else if (match_string(line, "[domain shader", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_DS;
            }
            else if (match_string(line, "[geometry shader", &line))
            {
                state = STATE_SHADER;
                shader_type = SHADER_TYPE_GS;
            }
            else if (!strcmp(line, "[input layout]\n"))
            {
                state = STATE_INPUT_LAYOUT;

                for (i = 0; i < runner->input_element_count; ++i)
                    free(runner->input_elements[i].name);
                runner->input_element_count = 0;
            }
            else
            {
                fatal_error("Unknown directive '%s'.\n", line);
            }

            if (state == STATE_SHADER)
                read_shader_directive(runner, line_buffer, line);
        }
        else if (line[0] != '%' && line[0] != '\n')
        {
            update_line_number_context(testname, line_number);

            switch (state)
            {
                case STATE_NONE:
                    fatal_error("Malformed line '%s'.\n", line);
                    break;

                case STATE_INPUT_LAYOUT:
                    parse_input_layout_directive(runner, line);
                    break;

                case STATE_PREPROC:
                case STATE_PREPROC_INVALID:
                case STATE_SHADER:
                {
                    size_t len = strlen(line);

                    vkd3d_array_reserve((void **)&shader_source, &shader_source_size, shader_source_len + len + 1, 1);
                    memcpy(shader_source + shader_source_len, line, len + 1);
                    shader_source_len += len;
                    break;
                }

                case STATE_REQUIRE:
                    parse_require_directive(runner, line);
                    break;

                case STATE_RESOURCE:
                    parse_resource_directive(&current_resource, line);
                    break;

                case STATE_SAMPLER:
                    parse_sampler_directive(current_sampler, line);
                    break;

                case STATE_TEST:
                    /* Compilation which fails with dxcompiler is not 'todo', therefore the tests are
                     * not 'todo' either. They cannot run, so skip them entirely. */
                    if (!runner->failed_resource_count && test_action == TEST_ACTION_RUN
                            && SUCCEEDED(runner->hlsl_hrs[runner->minimum_shader_model]))
                        parse_test_directive(runner, line);
                    break;
            }
        }
    }

    /* Pop line_number context. */
    vkd3d_test_pop_context();

    for (i = 0; i < runner->input_element_count; ++i)
        free(runner->input_elements[i].name);
    free(runner->input_elements);
    for (i = 0; i < SHADER_TYPE_COUNT; ++i)
        free(runner->shader_source[i]);
    free(runner->uniforms);
    for (i = 0; i < runner->resource_count; ++i)
    {
        if (runner->resources[i])
            runner->ops->destroy_resource(runner, runner->resources[i]);
    }

    fclose(f);
}

#ifdef _WIN32
static void print_dll_version(const char *file_name)
{
    BOOL (WINAPI *GetFileVersionInfoA)(const char *, DWORD, DWORD, void *);
    BOOL (WINAPI *VerQueryValueA)(void *, char *, void **, UINT*);
    DWORD (WINAPI *GetFileVersionInfoSizeA)(const char *, DWORD *);
    HMODULE version_module;
    DWORD size, handle;
    bool done = false;

    version_module = LoadLibraryA("version.dll");
    if (!version_module)
        goto out;

#define X(name) name = (void *)GetProcAddress(version_module, #name);
    X(GetFileVersionInfoSizeA);
    X(GetFileVersionInfoA);
    X(VerQueryValueA);
#undef X

    if (!GetFileVersionInfoSizeA || !GetFileVersionInfoA || !VerQueryValueA)
    {
        FreeLibrary(version_module);
        goto out;
    }

    size = GetFileVersionInfoSizeA(file_name, &handle);
    if (size)
    {
        char *data = malloc(size);

        if (GetFileVersionInfoA(file_name, handle, size, data))
        {
            VS_FIXEDFILEINFO *info;
            UINT len;

            if (VerQueryValueA(data, "\\", (void **)&info, &len))
            {
                trace("%s version: %lu.%lu.%lu.%lu\n", file_name,
                        info->dwFileVersionMS >> 16, info->dwFileVersionMS & 0xffff,
                        info->dwFileVersionLS >> 16, info->dwFileVersionLS & 0xffff);
                done = true;
            }
        }
        free(data);
    }

    FreeLibrary(version_module);

out:
    if (!done)
        trace("%s version: unknown\n", file_name);
}
#endif

static void run_compile_tests(void *dxc_compiler)
{
    struct shader_runner_caps caps = {0};
    struct shader_runner runner = {0};

    caps.runner = "hlsl";
    caps.minimum_shader_model = SHADER_MODEL_MIN;
    caps.maximum_shader_model = dxc_compiler ? SHADER_MODEL_MAX : SHADER_MODEL_5_1;
    for (unsigned int i = 0; i < SHADER_CAP_COUNT; ++i)
        caps.shader_caps[i] = true;
    for (unsigned int i = 0; i < DXGI_FORMAT_COUNT; ++i)
        caps.format_caps[i] = ~0u;

    run_shader_tests(&runner, &caps, NULL, dxc_compiler);
}

START_TEST(shader_runner)
{
    IDxcCompiler3 *dxc;

    parse_args(argc, argv);

    dxc = dxcompiler_create();

    trace("Running HLSL compile tests.\n");
    run_compile_tests(dxc);

#if defined(VKD3D_CROSSTEST)
    trace("Running tests from a Windows cross build\n");

    run_shader_tests_d3d9();
    run_shader_tests_d3d11();
    run_shader_tests_d3d12(dxc);

    if (dxc)
        print_dll_version("dxcompiler.dll");
    print_dll_version("d3dcompiler_47.dll");
    print_dll_version("dxgi.dll");
    print_dll_version("d3d9.dll");
    print_dll_version("d3d11.dll");
    print_dll_version("d3d12.dll");
    print_dll_version("d3d12core.dll");
    print_dll_version("d3d10warp.dll");
    if (test_options.enable_debug_layer)
        print_dll_version("d3d12sdklayers.dll");

#elif defined(_WIN32)
    trace("Running tests from a Windows non-cross build\n");

    run_shader_tests_d3d9();
    run_shader_tests_d3d11();
    run_shader_tests_d3d12(dxc);

    if (dxc)
        print_dll_version(SONAME_LIBDXCOMPILER);
    print_dll_version("d3d9.dll");
    print_dll_version("d3d11.dll");

#else
    trace("Running tests from a Unix build\n");

# ifdef HAVE_OPENGL
    run_shader_tests_gl();
# endif
# ifdef HAVE_METAL
    run_shader_tests_metal();
# endif
    run_shader_tests_vulkan();
    run_shader_tests_d3d12(dxc);
#endif

    if (dxc)
        IDxcCompiler3_Release(dxc);
}
