#include "RSDK/Core/RetroEngine.hpp"
#include "main.hpp"


#ifdef __ps2__
#include <reent.h>
#include <kernel.h>
#include <malloc.h>
#include <stdbool.h>
#include <time.h>
#include <tamtypes.h>
#include <stdio.h>

typedef struct {
    size_t binary_size;
    size_t allocs_size;
    size_t stack_size;
} AthenaMemory;

static AthenaMemory prog_mem;
static bool memory_monitor_enabled = true; 
static int frame_counter = 0;


void *malloc(size_t size) {
    void* raw_ptr = _malloc_r(_REENT, size);
    if (raw_ptr) {
        size_t* ptr = (size_t*)raw_ptr;
        prog_mem.allocs_size += ptr[-1];
    }
    return raw_ptr;
}

void *realloc(void *memblock, size_t size) {
    if (memblock) {
        size_t* ptr = (size_t*)memblock;
        prog_mem.allocs_size -= ptr[-1];
    }
    
    void* raw_ptr = _realloc_r(_REENT, memblock, size);
    
    if (raw_ptr) {
        size_t* ptr = (size_t*)raw_ptr;
        prog_mem.allocs_size += ptr[-1];
    }
    return raw_ptr;
}

void *calloc(size_t number, size_t size) {
    void* raw_ptr = _calloc_r(_REENT, number, size);
    if (raw_ptr) {
        size_t* ptr = (size_t*)raw_ptr;
        prog_mem.allocs_size += ptr[-1];
    }
    return raw_ptr;
}

void *memalign(size_t alignment, size_t size) {
    void* raw_ptr = _memalign_r(_REENT, alignment, size);
    if (raw_ptr) {
        size_t* ptr = (size_t*)raw_ptr;
        prog_mem.allocs_size += ptr[-1];
    }
    return raw_ptr;
}

void free(void* ptr) {
    if (ptr) {
        size_t* size_ptr = (size_t*)ptr;
        prog_mem.allocs_size -= size_ptr[-1];
    }
    _free_r(_REENT, ptr);
}

void init_memory_manager() {
    extern char _end[], __start[];
    prog_mem.binary_size = (size_t)&_end - (size_t)&__start;
    prog_mem.stack_size = 0x20000;
    prog_mem.allocs_size = 0;
    frame_counter = 0;
}

size_t get_binary_size() { return prog_mem.binary_size; }
size_t get_allocs_size() { return prog_mem.allocs_size; }
size_t get_stack_size() { return prog_mem.stack_size; }
size_t get_used_memory() { 
    return prog_mem.stack_size + prog_mem.allocs_size + prog_mem.binary_size; 
}


void format_memory_size(size_t bytes, char *buffer) {
    if (bytes < 1024) {
        sprintf(buffer, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        sprintf(buffer, "%.2f KB", bytes / 1024.0f);
    } else {
        sprintf(buffer, "%.2f MB", bytes / (1024.0f * 1024.0f));
    }
}


void print_memory_report() {
    char binary_str[32], allocs_str[32], stack_str[32], total_str[32];
    
    format_memory_size(prog_mem.binary_size, binary_str);
    format_memory_size(prog_mem.allocs_size, allocs_str);
    format_memory_size(prog_mem.stack_size, stack_str);
    format_memory_size(get_used_memory(), total_str);

}


extern "C" void update_memory_monitor_frame() {
    frame_counter++;
    
    if (!memory_monitor_enabled) return;
    
    
    if (frame_counter % 180 == 0) {
        print_memory_report();
    }
}


void set_memory_monitor(bool enabled) {
    memory_monitor_enabled = enabled;

}

bool is_memory_monitor_enabled() {
    return memory_monitor_enabled;
}


#endif

#if RETRO_STANDALONE
#define LinkGameLogic RSDK::LinkGameLogic
#else
#define EngineInfo RSDK::EngineInfo
#include <GameMain.h>
#define LinkGameLogic LinkGameLogicDLL
#endif

#if RETRO_PLATFORM == RETRO_WIN && !RETRO_RENDERDEVICE_SDL2

#if RETRO_RENDERDEVICE_DIRECTX9 || RETRO_RENDERDEVICE_DIRECTX11
INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nShowCmd)
{
    RSDK::RenderDevice::hInstance     = hInstance;
    RSDK::RenderDevice::hPrevInstance = hPrevInstance;
    RSDK::RenderDevice::nShowCmd      = nShowCmd;

    return RSDK_main(1, &lpCmdLine, LinkGameLogic);
}
#else
INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nShowCmd)
{
    return RSDK_main(1, &lpCmdLine, LinkGameLogic);
}
#endif

#elif RETRO_PLATFORM == RETRO_ANDROID
extern "C" {
void android_main(struct android_app *app);
}

void android_main(struct android_app *ap)
{
    app                                 = ap;
    app->onAppCmd                       = AndroidCommandCallback;
    app->activity->callbacks->onKeyDown = AndroidKeyDownCallback;
    app->activity->callbacks->onKeyUp   = AndroidKeyUpCallback;

    JNISetup *jni = GetJNISetup();
    Paddleboat_init(jni->env, jni->thiz);

    SwappyGL_init(jni->env, jni->thiz);
    SwappyGL_setAutoSwapInterval(false);
    SwappyGL_setSwapIntervalNS(SWAPPY_SWAP_60FPS);
    SwappyGL_setMaxAutoSwapIntervalNS(SWAPPY_SWAP_60FPS);

    getFD    = jni->env->GetMethodID(jni->clazz, "getFD", "([BB)I");
    writeLog = jni->env->GetMethodID(jni->clazz, "writeLog", "([BI)V");

    setLoading = jni->env->GetMethodID(jni->clazz, "setLoadingIcon", "([B)V");
    showLoading = jni->env->GetMethodID(jni->clazz, "showLoadingIcon", "()V");
    hideLoading = jni->env->GetMethodID(jni->clazz, "hideLoadingIcon", "()V");

    setPixSize = jni->env->GetMethodID(jni->clazz, "setPixSize", "(II)V");

#if RETRO_USE_MOD_LOADER
    fsExists      = jni->env->GetMethodID(jni->clazz, "fsExists", "([B)Z");
    fsIsDir       = jni->env->GetMethodID(jni->clazz, "fsIsDir", "([B)Z");
    fsDirIter     = jni->env->GetMethodID(jni->clazz, "fsDirIter", "([B)[Ljava/lang/String;");
    fsRecurseIter = jni->env->GetMethodID(jni->clazz, "fsRecurseIter", "([B)Ljava/lang/String;");
#endif

    GameActivity_setWindowFlags(app->activity,
                                AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_TURN_SCREEN_ON | AWINDOW_FLAG_LAYOUT_NO_LIMITS | AWINDOW_FLAG_FULLSCREEN
                                    | AWINDOW_FLAG_SHOW_WHEN_LOCKED,
                                0);

    RSDK_main(0, NULL, (void *)LinkGameLogic);

    Paddleboat_destroy(jni->env);
    SwappyGL_destroy();
}
#else

int32 main(int32 argc, char *argv[]) { 
#ifdef __ps2__
    init_memory_manager();

#endif
    
    return RSDK_main(argc, argv, (void *)LinkGameLogic); 
}
#endif

int32 RSDK_main(int32 argc, char **argv, void *linkLogicPtr)
{
    RSDK::linkGameLogic = (RSDK::LogicLinkHandle)linkLogicPtr;

    RSDK::InitCoreAPI();

    int32 exitCode = RSDK::RunRetroEngine(argc, argv);

    RSDK::ReleaseCoreAPI();

    return exitCode;
}