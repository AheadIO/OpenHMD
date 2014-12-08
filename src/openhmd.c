/*
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 * Copyright (C) 2013 Fredrik Hultin.
 * Copyright (C) 2013 Jakob Bornecrantz.
 * Distributed under the Boost 1.0 licence, see LICENSE for full text.
 */

/* Main Lib Implemenation */

#include "openhmdi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static OHMD_THREAD_LOCAL char thread_str[OHMD_STR_SIZE];
OHMD_THREAD_LOCAL char ohmd_error_msg[OHMD_STR_SIZE];

ohmd_context* OHMD_APIENTRY ohmd_ctx_create()
{
	ohmd_context* ctx = calloc(1, sizeof(ohmd_context));
	if(!ctx){
		LOGE("could not allocate RAM for context");
		return NULL;
	}

	ctx->mutex = ohmd_create_mutex(ctx);

	if(ctx->mutex == NULL){
		free(ctx);
		return NULL;
	}

	ctx->drivers[ctx->num_drivers++] = ohmd_create_oculus_rift_drv(ctx);

	// add dummy driver last to make it the lowest priority
	ctx->drivers[ctx->num_drivers++] = ohmd_create_dummy_drv(ctx);

	return ctx;
}

void OHMD_APIENTRY ohmd_ctx_destroy(ohmd_context* ctx)
{
	ohmd_lock_mutex(ctx->mutex);

	for(int i = 0; i < ctx->num_active_devices; i++){
		ctx->active_devices[i]->close(ctx->active_devices[i]);
	}

	for(int i = 0; i < ctx->num_drivers; i++){
		ctx->drivers[i]->destroy(ctx->drivers[i]);
	}

	ohmd_unlock_mutex(ctx->mutex);

	ohmd_destroy_mutex(ctx->mutex);

	free(ctx);
}

void OHMD_APIENTRY ohmd_ctx_update(ohmd_context* ctx)
{
	ohmd_lock_mutex(ctx->mutex);

	for(int i = 0; i < ctx->num_active_devices; i++)
		ctx->active_devices[i]->update(ctx->active_devices[i]);
	
	ohmd_unlock_mutex(ctx->mutex);
}

const char* OHMD_APIENTRY ohmd_ctx_get_error(ohmd_context* ctx)
{
	return ohmd_error_msg;
}

int OHMD_APIENTRY ohmd_ctx_probe(ohmd_context* ctx)
{
	ohmd_lock_mutex(ctx->mutex);

	memset(&ctx->list, 0, sizeof(ohmd_device_list));
	for(int i = 0; i < ctx->num_drivers; i++){
		ctx->drivers[i]->get_device_list(ctx->drivers[i], &ctx->list);
	}

	int num_devices = ctx->list.num_devices;

	ohmd_unlock_mutex(ctx->mutex);

	return num_devices;
}

const char* OHMD_APIENTRY ohmd_list_gets(ohmd_context* ctx, int index, ohmd_string_value type)
{
	ohmd_lock_mutex(ctx->mutex);

	if(index >= ctx->list.num_devices){
		ohmd_unlock_mutex(ctx->mutex);
		return NULL;
	}

	switch(type){
	case OHMD_VENDOR:
		strncpy(thread_str, ctx->list.devices[index].vendor, OHMD_STR_SIZE);
		break;
	case OHMD_PRODUCT:
		strncpy(thread_str, ctx->list.devices[index].product, OHMD_STR_SIZE);
		break;
	case OHMD_PATH:
		strncpy(thread_str, ctx->list.devices[index].path, OHMD_STR_SIZE);
		break;
	default:
		ohmd_unlock_mutex(ctx->mutex);
		return NULL;
	}
		
	ohmd_unlock_mutex(ctx->mutex);

	return thread_str;
}

ohmd_device* OHMD_APIENTRY ohmd_list_open_device(ohmd_context* ctx, int index)
{
	ohmd_lock_mutex(ctx->mutex);

	if(index >= 0 && index < ctx->list.num_devices){

		ohmd_device_desc* desc = &ctx->list.devices[index];
		ohmd_driver* driver = (ohmd_driver*)desc->driver_ptr;
		ohmd_device* device = driver->open_device(driver, desc);

		device->rotation_correction.w = 1;

		if (device == NULL){
			ohmd_unlock_mutex(ctx->mutex);
			return NULL;
		}

		device->ctx = ctx;
		device->active_device_idx = ctx->num_active_devices;
		ctx->active_devices[ctx->num_active_devices++] = device;
		
		ohmd_unlock_mutex(ctx->mutex);
		return device;
	}

	ohmd_set_error("no device with index: %d", index);
	
	ohmd_unlock_mutex(ctx->mutex);
	return NULL;
}

OHMD_APIENTRYDLL int OHMD_APIENTRY ohmd_close_device(ohmd_device* device)
{
	ohmd_context* ctx = device->ctx;
	int idx = device->active_device_idx;

	memmove(ctx->active_devices + idx, ctx->active_devices + idx + 1, 
		sizeof(ohmd_device*) * (ctx->num_active_devices - idx - 1));
	
	device->close(device);
	
	ctx->num_active_devices--;

	for(int i = idx; i < ctx->num_active_devices; i++)
		ctx->active_devices[i]->active_device_idx--;

	return 0;
}

int OHMD_APIENTRY ohmd_device_getf(ohmd_device* device, ohmd_float_value type, float* out)
{
	switch(type){
	case OHMD_LEFT_EYE_GL_MODELVIEW_MATRIX: {
			vec3f point = {{0, 0, 0}};
			quatf rot;
			device->getf(device, OHMD_ROTATION_QUAT, (float*)&rot);
			quatf tmp = device->rotation_correction;
			oquatf_mult_me(&tmp, &rot);
			rot = tmp;
			mat4x4f orient, world_shift, result;
			omat4x4f_init_look_at(&orient, &rot, &point);
			omat4x4f_init_translate(&world_shift, +(device->properties.ipd / 2.0f), 0, 0);
			omat4x4f_mult(&world_shift, &orient, &result);
			omat4x4f_transpose(&result, (mat4x4f*)out);
			return 0;
		}
	case OHMD_RIGHT_EYE_GL_MODELVIEW_MATRIX: {
			vec3f point = {{0, 0, 0}};
			quatf rot;
			device->getf(device, OHMD_ROTATION_QUAT, (float*)&rot);
			oquatf_mult_me(&rot, &device->rotation_correction);
			mat4x4f orient, world_shift, result;
			omat4x4f_init_look_at(&orient, &rot, &point);
			omat4x4f_init_translate(&world_shift, -(device->properties.ipd / 2.0f), 0, 0);
			omat4x4f_mult(&world_shift, &orient, &result);
			omat4x4f_transpose(&result, (mat4x4f*)out);
			return 0;
		}
	case OHMD_LEFT_EYE_GL_PROJECTION_MATRIX:
		omat4x4f_transpose(&device->properties.proj_left, (mat4x4f*)out);
		return 0;
	case OHMD_RIGHT_EYE_GL_PROJECTION_MATRIX:
		omat4x4f_transpose(&device->properties.proj_right, (mat4x4f*)out);
		return 0;

	case OHMD_SCREEN_HORIZONTAL_SIZE:
		*out = device->properties.hsize;
		return 0;
	case OHMD_SCREEN_VERTICAL_SIZE:
		*out = device->properties.vsize;
		return 0;

	case OHMD_LENS_HORIZONTAL_SEPARATION:
		*out = device->properties.lens_sep;
		return 0;
	case OHMD_LENS_VERTICAL_POSITION:
		*out = device->properties.lens_vpos;
		return 0;

	case OHMD_RIGHT_EYE_FOV:
	case OHMD_LEFT_EYE_FOV:
		*out = device->properties.fov;
		return 0;
	case OHMD_RIGHT_EYE_ASPECT_RATIO:
	case OHMD_LEFT_EYE_ASPECT_RATIO:
		*out = device->properties.ratio;
		return 0;

	case OHMD_EYE_IPD:
		*out = device->properties.ipd;
		return 0;

	case OHMD_PROJECTION_ZFAR:
		*out = device->properties.zfar;
		return 0;
	case OHMD_PROJECTION_ZNEAR:
		*out = device->properties.znear;
		return 0;

	case OHMD_ROTATION_QUAT:
	{
		int ret = device->getf(device, OHMD_ROTATION_QUAT, out);

		if(ret != 0)
			return ret;

		oquatf_mult_me((quatf*)out, &device->rotation_correction);
		quatf tmp = device->rotation_correction;
		oquatf_mult_me(&tmp, (quatf*)out);
		*(quatf*)out = tmp;
		return 0;
	}
	case OHMD_POSITION_VECTOR:
	{
		int ret = device->getf(device, OHMD_POSITION_VECTOR, out);

		if(ret != 0)
			return ret;

		for(int i = 0; i < 3; i++)
			out[i] += device->position_correction.arr[i];

		return 0;
	}
		
	default:
		return device->getf(device, type, out);
	}
}

int OHMD_APIENTRY ohmd_device_setf(ohmd_device* device, ohmd_float_value type, float* in)
{
	switch(type){
	case OHMD_EYE_IPD:
		device->properties.ipd = *in;
		return 0;
	case OHMD_PROJECTION_ZFAR:
		device->properties.zfar = *in;
		return 0;
	case OHMD_PROJECTION_ZNEAR:
		device->properties.znear = *in;
		return 0;
	case OHMD_ROTATION_QUAT:
		{
			// adjust rotation correction
			quatf q;
			int ret = device->getf(device, OHMD_ROTATION_QUAT, (float*)&q);

			if(ret != 0){
				return ret;
			}

			oquatf_diff(&q, (quatf*)in, &device->rotation_correction);
			return 0;
		}
	case OHMD_POSITION_VECTOR:
		{
			// adjust position correction
			vec3f v;
			int ret = device->getf(device, OHMD_POSITION_VECTOR, (float*)&v);

			if(ret != 0){
				return ret;
			}

			for(int i = 0; i < 3; i++)
				device->position_correction.arr[i] = in[i] - v.arr[i];

			return 0;
		}
	default:
		return -1;
	}
}

int OHMD_APIENTRY ohmd_device_geti(ohmd_device* device, ohmd_int_value type, int* out)
{
	ohmd_lock_mutex(device->ctx->mutex);

	switch(type){
		case OHMD_SCREEN_HORIZONTAL_RESOLUTION:
			*out = device->properties.hres;
			break;
		case OHMD_SCREEN_VERTICAL_RESOLUTION:
			*out = device->properties.vres;
			break;
		default:
			ohmd_unlock_mutex(device->ctx->mutex);
			ohmd_set_error("no such integer value type: %d", type);
			return -1;
	}

	ohmd_unlock_mutex(device->ctx->mutex);
	return 0;
}

void* ohmd_allocfn(char* e_msg, size_t size)
{
	void* ret = calloc(1, size);
	if(!ret)
		ohmd_set_error("%s", e_msg);
	return ret;
}

void ohmd_set_default_device_properties(ohmd_device_properties* props)
{
	props->ipd = 0.061f;
	props->znear = 0.1f;
	props->zfar = 1000.0f;
}

void ohmd_calc_default_proj_matrices(ohmd_device_properties* props)
{
	mat4x4f proj_base; // base projection matrix

	// Calculate where the lens is on each screen,
	// and with the given value offset the projection matrix.
	float screen_center = props->hsize / 4.0f;
	float lens_shift = screen_center - props->lens_sep / 2.0f;
	float proj_offset = 4.0f * lens_shift / props->hsize;

	// Setup the base projection matrix. Each eye mostly have the
	// same projection matrix with the exception of the offset.
	omat4x4f_init_perspective(&proj_base, props->fov, props->ratio, props->znear, props->zfar);

	// Setup the two adjusted projection matricies. Each is setup to deal
	// with the fact that the lens is not in the center of the screen.
	// These matrices only change of the hardware changes, so static.
	mat4x4f translate;

	omat4x4f_init_translate(&translate, proj_offset, 0, 0);
	omat4x4f_mult(&translate, &proj_base, &props->proj_left);

	omat4x4f_init_translate(&translate, -proj_offset, 0, 0);
	omat4x4f_mult(&translate, &proj_base, &props->proj_right);
}
