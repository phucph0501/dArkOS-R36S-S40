/* libs40gbm - fills in GBM entry points that the RK3326 libmali driver (which replaces
 * libgbm.so.1 on dArkOS) does not export, but Debian's WPE WebKit / SDL2 link against.
 * WebKit is linked with BIND_NOW, so without these it would not even load.
 *
 * Each function forwards to the real implementation when the loaded libgbm has it
 * (mesa on a desktop), otherwise it falls back to the single-plane, no-modifier
 * equivalent from the older GBM API that libmali provides. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>

struct gbm_device;
struct gbm_bo;
union gbm_bo_handle { void* ptr; int32_t s32; uint32_t u32; int64_t s64; uint64_t u64; };

#define DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffULL
#define NEXT(name) static void* real; static int looked; if (!looked) { real = dlsym(RTLD_NEXT, name); looked = 1; }
#define BASE(var, name) static void* var; if (!var) var = dlsym(RTLD_DEFAULT, name);

struct gbm_bo* gbm_bo_create_with_modifiers2(struct gbm_device* dev, uint32_t w, uint32_t h, uint32_t format,
                                             const uint64_t* mods, unsigned int count, uint32_t flags) {
  NEXT("gbm_bo_create_with_modifiers2");
  if (real) return ((struct gbm_bo * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, const uint64_t*, unsigned int, uint32_t)) real)(dev, w, h, format, mods, count, flags);
  BASE(create, "gbm_bo_create");
  return create ? ((struct gbm_bo * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t)) create)(dev, w, h, format, flags) : NULL;
}

/* mpv's DRM/GBM context (and Mesa-style EGL clients) create the scanout surface with modifiers */
struct gbm_surface;
struct gbm_surface* gbm_surface_create_with_modifiers2(struct gbm_device* dev, uint32_t w, uint32_t h, uint32_t format,
                                                       const uint64_t* mods, const unsigned int count, uint32_t flags) {
  NEXT("gbm_surface_create_with_modifiers2");
  if (real) return ((struct gbm_surface * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, const uint64_t*, unsigned int, uint32_t)) real)(dev, w, h, format, mods, count, flags);
  BASE(create, "gbm_surface_create");
  return create ? ((struct gbm_surface * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t)) create)(dev, w, h, format, flags) : NULL;
}

struct gbm_surface* gbm_surface_create_with_modifiers(struct gbm_device* dev, uint32_t w, uint32_t h, uint32_t format,
                                                      const uint64_t* mods, const unsigned int count) {
  NEXT("gbm_surface_create_with_modifiers");
  if (real) return ((struct gbm_surface * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, const uint64_t*, unsigned int)) real)(dev, w, h, format, mods, count);
  BASE(create, "gbm_surface_create");
  /* GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING */
  return create ? ((struct gbm_surface * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t)) create)(dev, w, h, format, 5) : NULL;
}

struct gbm_bo* gbm_bo_create_with_modifiers(struct gbm_device* dev, uint32_t w, uint32_t h, uint32_t format,
                                            const uint64_t* mods, const unsigned int count) {
  NEXT("gbm_bo_create_with_modifiers");
  if (real) return ((struct gbm_bo * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, const uint64_t*, unsigned int)) real)(dev, w, h, format, mods, count);
  BASE(create, "gbm_bo_create");
  return create ? ((struct gbm_bo * (*)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t)) create)(dev, w, h, format, 0) : NULL;
}

uint64_t gbm_bo_get_modifier(struct gbm_bo* bo) {
  NEXT("gbm_bo_get_modifier");
  return real ? ((uint64_t(*)(struct gbm_bo*))real)(bo) : DRM_FORMAT_MOD_INVALID;
}

int gbm_bo_get_plane_count(struct gbm_bo* bo) {
  NEXT("gbm_bo_get_plane_count");
  return real ? ((int (*)(struct gbm_bo*))real)(bo) : 1;
}

uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo* bo, int plane) {
  NEXT("gbm_bo_get_stride_for_plane");
  if (real) return ((uint32_t(*)(struct gbm_bo*, int))real)(bo, plane);
  BASE(stride, "gbm_bo_get_stride");
  return stride && plane == 0 ? ((uint32_t(*)(struct gbm_bo*))stride)(bo) : 0;
}

uint32_t gbm_bo_get_offset(struct gbm_bo* bo, int plane) {
  NEXT("gbm_bo_get_offset");
  return real ? ((uint32_t(*)(struct gbm_bo*, int))real)(bo, plane) : 0;
}

int gbm_bo_get_fd_for_plane(struct gbm_bo* bo, int plane) {
  NEXT("gbm_bo_get_fd_for_plane");
  if (real) return ((int (*)(struct gbm_bo*, int))real)(bo, plane);
  BASE(fd, "gbm_bo_get_fd");
  return fd && plane == 0 ? ((int (*)(struct gbm_bo*))fd)(bo) : -1;
}

union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo* bo, int plane) {
  NEXT("gbm_bo_get_handle_for_plane");
  if (real) return ((union gbm_bo_handle(*)(struct gbm_bo*, int))real)(bo, plane);
  BASE(handle, "gbm_bo_get_handle");
  union gbm_bo_handle none;
  none.s64 = -1;
  return handle && plane == 0 ? ((union gbm_bo_handle(*)(struct gbm_bo*))handle)(bo) : none;
}
