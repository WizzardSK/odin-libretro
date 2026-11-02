/* GLEW is built as for OSMesa (GLEW_OSMESA) so that it asks this one
 * function for every OpenGL function, and links neither GLX nor EGL: the
 * frontend owns the context and gives the functions (libretro_odin.cpp
 * defines it with the frontend's get_proc_address). */
#ifndef ODIN_LIBRETRO_OSMESA_SHIM_H
#define ODIN_LIBRETRO_OSMESA_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*OSMESAproc)(void);
OSMESAproc OSMesaGetProcAddress(const char *name);

#ifdef __cplusplus
}
#endif

#endif
