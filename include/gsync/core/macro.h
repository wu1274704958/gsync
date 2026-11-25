#pragma once
#ifdef GSYNC_NO_EXPORT
# define GSYNC_EXTERN /* nothing */
#else

#ifdef _WIN32
/* Windows - set up dll import/export decorators. */
# if defined(BUILDING_GSYNC_SHARED)
    /* Building shared library. */
#   define GSYNC_EXTERN __declspec(dllexport)
# elif 1
    /* Using shared library. */
#   define GSYNC_EXTERN __declspec(dllimport)
# else
    /* Building static library. */
#   define GSYNC_EXTERN /* nothing */
# endif
#elif __GNUC__ >= 4
# define GSYNC_EXTERN __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x550) /* Sun Studio >= 8 */
# define GSYNC_EXTERN __global
#else
# define GSYNC_EXTERN /* nothing */
#endif

#endif
