/**
 * @file RenderDevice_PS2_Video.cpp
 * @brief RenderDevice implementation for PS2 with multi-screen support (Competition Mode)
 */

#define MANIA_WIDTH (424)
#define MANIA_HEIGHT (240)

#include <gsKit.h>
#include <dmaKit.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>
#include <tamtypes.h>
#include <sifrpc.h>

#define SHADER_RGB_IMAGE 0
#define SHADER_YUV_420   1
#define SHADER_YUV_422   2
#define SHADER_YUV_424   3

using namespace RSDK;

static GSGLOBAL* gsGlobal = NULL;
static GSTEXTURE screenTexture[SCREEN_COUNT];
static GSTEXTURE imageTexture;

static uint16* screen_pixels[SCREEN_COUNT];
static ScanlineInfo* scanlines = NULL;

static bool initialized = false;

// texture format tracking
static uint8 lastTextureFormat = 0xFF;

// video settings tracking
static int32 lastScreenCount = -1;
static bool needsTextureRecreation = false;

// display info
static int32 displayWidth[16] = {640};
static int32 displayHeight[16] = {448};
static int32 displayCount = 1;
static uint32 displayModeIndex = 0;

bool InitScreenTextures() {
    // determine how many textures to create
    int texCount = videoSettings.screenCount > 0 ? videoSettings.screenCount : 1;
    
    // create/recreate necessary textures
    for (int s = 0; s < texCount; s++) {
        // free old texture if exists
        if (screenTexture[s].Vram != GSKIT_ALLOC_ERROR && screenTexture[s].Vram != 0) {
            memset(&screenTexture[s], 0, sizeof(GSTEXTURE));
        }
        
        screenTexture[s].Width = MANIA_WIDTH;
        screenTexture[s].Height = MANIA_HEIGHT;
        screenTexture[s].PSM = GS_PSM_CT16;
        screenTexture[s].ClutPSM = 0;
        screenTexture[s].Clut = NULL;
        screenTexture[s].Filter = GS_FILTER_LINEAR;
        screenTexture[s].Mem = NULL;
        
        int tex_size = gsKit_texture_size(MANIA_WIDTH, MANIA_HEIGHT, GS_PSM_CT16);
        screenTexture[s].Vram = gsKit_vram_alloc(gsGlobal, tex_size, GSKIT_ALLOC_USERBUFFER);
        
        if (screenTexture[s].Vram == GSKIT_ALLOC_ERROR) {
            return false;
        }
    }
    
    // clear unused textures
    for (int s = texCount; s < SCREEN_COUNT; s++) {
        memset(&screenTexture[s], 0, sizeof(GSTEXTURE));
        screenTexture[s].Vram = GSKIT_ALLOC_ERROR;
    }
    
    lastScreenCount = videoSettings.screenCount;
    needsTextureRecreation = false;
    return true;
}

bool InitImageTexture() {
    memset(&imageTexture, 0, sizeof(GSTEXTURE));
    imageTexture.Width = RETRO_VIDEO_TEXTURE_W;
    imageTexture.Height = RETRO_VIDEO_TEXTURE_H;
    imageTexture.PSM = GS_PSM_CT32;
    imageTexture.Filter = GS_FILTER_LINEAR;
    imageTexture.Mem = NULL;
    
    int tex_size = gsKit_texture_size(RETRO_VIDEO_TEXTURE_W, RETRO_VIDEO_TEXTURE_H, GS_PSM_CT32);
    imageTexture.Vram = gsKit_vram_alloc(gsGlobal, tex_size, GSKIT_ALLOC_USERBUFFER);
    
    if (imageTexture.Vram == GSKIT_ALLOC_ERROR) {
        return false;
    }
    
    lastTextureFormat = 0xFF;
    return true;
}

bool RenderDevice::InitGraphicsAPI() {
    // configure screen sizes
    videoSettings.pixWidth = MANIA_WIDTH;
    videoSettings.pixHeight = MANIA_HEIGHT;
    videoSettings.windowWidth = 640;
    videoSettings.windowHeight = 448;
    
    // configure screens
    for (int32 s = 0; s < SCREEN_COUNT; ++s) {
        screens[s].size.x = MANIA_WIDTH;
        screens[s].size.y = MANIA_HEIGHT;
        SetScreenSize(s, MANIA_WIDTH, MANIA_HEIGHT);
    }
    
    // initialize textures
    if (!InitScreenTextures()) {
        return false;
    }
    
    if (!InitImageTexture()) {
        return false;
    }
    
    videoSettings.viewportX = 0;
    videoSettings.viewportY = 0;
    videoSettings.viewportW = 1.0f / videoSettings.windowWidth;
    videoSettings.viewportH = 1.0f / videoSettings.windowHeight;
    
    return true;
}

bool RenderDevice::InitShaders() {
    videoSettings.shaderSupport = false;
    videoSettings.shaderID = 0;
    shaderCount = 1;
    
    if (shaderList) {
        strcpy(shaderList[0].name, "None");
        shaderList[0].linear = false;
    }
    
    return true;
}

void RenderDevice::GetDisplays() {
    displayCount = 1;
    displayModeIndex = 0;
    
    if (gsGlobal) {
        displayWidth[0] = gsGlobal->Width;
        displayHeight[0] = gsGlobal->Height;
    } else {
        displayWidth[0] = 640;
        displayHeight[0] = 448;
    }
}

bool RenderDevice::Init()
{
    gsGlobal = gsKit_init_global();
    if (!gsGlobal) {
        return false;
    }
    
    gsGlobal->PSM = GS_PSM_CT16;
    gsGlobal->PSMZ = GS_PSMZ_16S;
    gsGlobal->ZBuffering = GS_SETTING_OFF;
    gsGlobal->PrimAlpha = GS_BLEND_BACK2FRONT;
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    
    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    
    // initialize video settings
    videoSettings.windowed = false;
    videoSettings.bordered = false;
    videoSettings.exclusiveFS = true;
    videoSettings.vsync = true;
    videoSettings.tripleBuffered = false;
    videoSettings.windowWidth = 640;
    videoSettings.windowHeight = 448;
    videoSettings.fsWidth = 640;
    videoSettings.fsHeight = 448;
    videoSettings.refreshRate = 60;
    videoSettings.pixWidth = MANIA_WIDTH;
    videoSettings.pixHeight = MANIA_HEIGHT;
    videoSettings.screenCount = 1;
    videoSettings.dimMax = 1.0f;
    videoSettings.dimPercent = 1.0f;
    videoSettings.shaderSupport = false;
    videoSettings.shaderID = 0;
    videoSettings.windowState = WINDOWSTATE_UNINITIALIZED;
    
    // allocate buffers for each screen
    for (int s = 0; s < SCREEN_COUNT; s++) {
        size_t buffer_size = MANIA_WIDTH * MANIA_HEIGHT * sizeof(uint16);
        screen_pixels[s] = (uint16*)memalign(64, buffer_size);
        if (!screen_pixels[s]) {
            // free already allocated buffers
            for (int j = 0; j < s; j++) {
                free(screen_pixels[j]);
            }
            return false;
        }
        memset(screen_pixels[s], 0, buffer_size);
    }
    
    // allocate scanlines
    scanlines = (ScanlineInfo*)malloc(MANIA_HEIGHT * sizeof(ScanlineInfo));
    if (!scanlines) {
        for (int s = 0; s < SCREEN_COUNT; s++) {
            free(screen_pixels[s]);
        }
        return false;
    }
    memset(scanlines, 0, MANIA_HEIGHT * sizeof(ScanlineInfo));
    
    // get display information
    GetDisplays();
    
    // initialize graphics api
    if (!InitGraphicsAPI()) {
        Release(false);
        return false;
    }
    
    // initialize shaders
    if (!InitShaders()) {
        Release(false);
        return false;
    }
    
    // clear screen
    u64 Black = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00);
    gsKit_clear(gsGlobal, Black);
    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
    
    // initialize audio
    if (!AudioDevice::Init()) {
        // continue without audio
    }
    
    // initialize input
    InitInputDevices();
    
    // finalize initialization
    engine.inFocus = 1;
    videoSettings.windowState = WINDOWSTATE_ACTIVE;
    initialized = true;
    lastScreenCount = videoSettings.screenCount;
    
    return true;
}

void RenderDevice::CopyFrameBuffer()
{
    if (!initialized) return;
    
    // detect screencount change
    if (videoSettings.screenCount != lastScreenCount) {
        needsTextureRecreation = true;
        lastScreenCount = videoSettings.screenCount;
    }
    
    // copy each screen to its individual buffer (SAME AS SDL)
    for (int32 s = 0; s < videoSettings.screenCount; ++s) {
        if (!screen_pixels[s]) continue;
        
        uint16* frameBuffer = screens[s].frameBuffer;
        uint16* dest = screen_pixels[s];
        
        for (int32 y = 0; y < MANIA_HEIGHT; ++y) {
            memcpy(dest, frameBuffer, screens[s].size.x * sizeof(uint16));
            frameBuffer += screens[s].pitch;
            dest += MANIA_WIDTH;
        }
    }
}

void RenderDevice::FlipScreen()
{
    if (!initialized || !gsGlobal)
        return;
    
    // recreate textures if necessary
    if (needsTextureRecreation) {
        InitScreenTextures();
    }
    
    // copy framebuffers
    CopyFrameBuffer();
    
    // clear screen
    u64 Black = GS_SETREG_RGBAQ(0, 0, 0, 0, 0);
    gsKit_clear(gsGlobal, Black);

    // calculate dimming
    float dimAmount = videoSettings.dimMax * videoSettings.dimPercent;
    u8 dimValue = (u8)(0x80 * dimAmount);

    // render based on screencount (SAME AS SDL)
    switch (videoSettings.screenCount) {
        case 0: {
            // fullscreen image/video mode
            if (imageTexture.Vram != GSKIT_ALLOC_ERROR && lastTextureFormat != 0xFF) {
                gsKit_prim_sprite_texture(
                    gsGlobal, &imageTexture,
                    0, 0,
                    0, 0,
                    gsGlobal->Width, gsGlobal->Height,
                    imageTexture.Width, imageTexture.Height,
                    2,
                    GS_SETREG_RGBAQ(dimValue, dimValue, dimValue, 0x80, 0)
                );
            }
            break;
        }
        
        case 1: {
            // single fullscreen screen
            if (screenTexture[0].Vram != GSKIT_ALLOC_ERROR) {
                screenTexture[0].Mem = (u32*)screen_pixels[0];
                gsKit_texture_upload(gsGlobal, &screenTexture[0]);
                
                gsKit_prim_sprite_texture(
                    gsGlobal, &screenTexture[0],
                    0, 0,
                    0, 0,
                    gsGlobal->Width, gsGlobal->Height,
                    MANIA_WIDTH, MANIA_HEIGHT,
                    2,
                    GS_SETREG_RGBAQ(dimValue, dimValue, dimValue, 0x80, 0)
                );
            }
            break;
        }
        
        case 2: {
            // two screens (horizontal split) - competition mode
            float halfHeight = gsGlobal->Height / 2.0f;
            
            // top screen (player 1)
            if (screenTexture[0].Vram != GSKIT_ALLOC_ERROR) {
                screenTexture[0].Mem = (u32*)screen_pixels[0];
                gsKit_texture_upload(gsGlobal, &screenTexture[0]);
                
                gsKit_prim_sprite_texture(
                    gsGlobal, &screenTexture[0],
                    0, 0,
                    0, 0,
                    gsGlobal->Width, halfHeight,
                    MANIA_WIDTH, MANIA_HEIGHT,
                    2,
                    GS_SETREG_RGBAQ(dimValue, dimValue, dimValue, 0x80, 0)
                );
            }
            
            // bottom screen (player 2)
            if (screenTexture[1].Vram != GSKIT_ALLOC_ERROR) {
                screenTexture[1].Mem = (u32*)screen_pixels[1];
                gsKit_texture_upload(gsGlobal, &screenTexture[1]);
                
                gsKit_prim_sprite_texture(
                    gsGlobal, &screenTexture[1],
                    0, halfHeight,
                    0, 0,
                    gsGlobal->Width, gsGlobal->Height,
                    MANIA_WIDTH, MANIA_HEIGHT,
                    2,
                    GS_SETREG_RGBAQ(dimValue, dimValue, dimValue, 0x80, 0)
                );
            }
            break;
        }
        
        case 3: {
            // three screens (horizontal split in thirds)
            float thirdHeight = gsGlobal->Height / 3.0f;
            
            for (int s = 0; s < 3; s++) {
                if (screenTexture[s].Vram != GSKIT_ALLOC_ERROR) {
                    screenTexture[s].Mem = (u32*)screen_pixels[s];
                    gsKit_texture_upload(gsGlobal, &screenTexture[s]);
                    
                    float y0 = s * thirdHeight;
                    float y1 = (s + 1) * thirdHeight;
                    
                    gsKit_prim_sprite_texture(
                        gsGlobal, &screenTexture[s],
                        0, y0,
                        0, 0,
                        gsGlobal->Width, y1,
                        MANIA_WIDTH, MANIA_HEIGHT,
                        2,
                        GS_SETREG_RGBAQ(dimValue, dimValue, dimValue, 0x80, 0)
                    );
                }
            }
            break;
        }
        
        case 4: {
            // four screens (quad split)
            float halfWidth = gsGlobal->Width / 2.0f;
            float halfHeight = gsGlobal->Height / 2.0f;
            
            for (int s = 0; s < 4; s++) {
                if (screenTexture[s].Vram != GSKIT_ALLOC_ERROR) {
                    screenTexture[s].Mem = (u32*)screen_pixels[s];
                    gsKit_texture_upload(gsGlobal, &screenTexture[s]);
                    
                    float x0 = (s % 2) * halfWidth;
                    float y0 = (s / 2) * halfHeight;
                    float x1 = x0 + halfWidth;
                    float y1 = y0 + halfHeight;
                    
                    gsKit_prim_sprite_texture(
                        gsGlobal, &screenTexture[s],
                        x0, y0,
                        0, 0,
                        x1, y1,
                        MANIA_WIDTH, MANIA_HEIGHT,
                        2,
                        GS_SETREG_RGBAQ(dimValue, dimValue, dimValue, 0x80, 0)
                    );
                }
            }
            break;
        }
    }
    
    // apply extra dimming if necessary
    if (dimAmount < 1.0f) {
        u8 dimAlpha = (u8)(0x80 * (1.0f - dimAmount));
        u64 DimColor = GS_SETREG_RGBAQ(0, 0, 0, dimAlpha, 0);
        
        gsKit_prim_sprite(gsGlobal, 
                         0, 0, 
                         gsGlobal->Width, gsGlobal->Height, 
                         0, DimColor);
    }
    
    // finalize graphics
    gsKit_queue_exec(gsGlobal);
    
    // present
    gsKit_sync_flip(gsGlobal);
}

// ===== VIDEO/IMAGE TEXTURES =====

void RenderDevice::SetupImageTexture(int32 width, int32 height, uint8* imagePixels) 
{
    if (!gsGlobal || !imagePixels) return;
    
    if (lastTextureFormat != SHADER_RGB_IMAGE || 
        imageTexture.Width != width || 
        imageTexture.Height != height) {
        
        memset(&imageTexture, 0, sizeof(GSTEXTURE));
        imageTexture.Width = width;
        imageTexture.Height = height;
        imageTexture.PSM = GS_PSM_CT32;
        imageTexture.Filter = GS_FILTER_LINEAR;
        
        int tex_size = gsKit_texture_size(width, height, GS_PSM_CT32);
        imageTexture.Vram = gsKit_vram_alloc(gsGlobal, tex_size, GSKIT_ALLOC_USERBUFFER);
        
        if (imageTexture.Vram == GSKIT_ALLOC_ERROR) {
            return;
        }
        
        lastTextureFormat = SHADER_RGB_IMAGE;
    }
    
    imageTexture.Mem = (u32*)imagePixels;
    gsKit_texture_upload(gsGlobal, &imageTexture);
}

void RenderDevice::SetupVideoTexture_YUV420(int32 width, int32 height, 
                                            uint8* y_plane, uint8* u_plane, uint8* v_plane,
                                            int32 y_stride, int32 u_stride, int32 v_stride)
{
    lastTextureFormat = SHADER_YUV_420;
}

void RenderDevice::SetupVideoTexture_YUV422(int32 width, int32 height,
                                            uint8* y_plane, uint8* u_plane, uint8* v_plane,
                                            int32 y_stride, int32 u_stride, int32 v_stride)
{
    lastTextureFormat = SHADER_YUV_422;
}

void RenderDevice::SetupVideoTexture_YUV424(int32 width, int32 height,
                                            uint8* y_plane, uint8* u_plane, uint8* v_plane,
                                            int32 y_stride, int32 u_stride, int32 v_stride)
{
    lastTextureFormat = SHADER_YUV_424;
}

void RenderDevice::SetupVideoTexture_Direct(int32 width, int32 height, uint32* pixels, int texAddr)
{

}


void RenderDevice::RefreshWindow() {
    needsTextureRecreation = true;
}

void RenderDevice::GetWindowSize(int32* width, int32* height)
{
    if (gsGlobal) {
        if (width) *width = gsGlobal->Width;
        if (height) *height = gsGlobal->Height;
    } else {
        if (width) *width = 640;
        if (height) *height = 448;
    }
}

void RenderDevice::Release(bool32 isRefresh)
{
    if (scanlines) {
        free(scanlines);
        scanlines = NULL;
    }
    
    for (int s = 0; s < SCREEN_COUNT; s++) {
        if (screen_pixels[s]) {
            free(screen_pixels[s]);
            screen_pixels[s] = NULL;
        }
    }
    
    if (!isRefresh && gsGlobal) {
        gsKit_deinit_global(gsGlobal);
        gsGlobal = NULL;
    }
    
    initialized = false;
    lastTextureFormat = 0xFF;
    lastScreenCount = -1;
}

bool RenderDevice::ProcessEvents() { return true; }
void RenderDevice::InitFPSCap() {}
bool RenderDevice::CheckFPSCap() { return true; }
void RenderDevice::UpdateFPSCap() {}
void RenderDevice::LoadShader(const char* fileName, bool32 linear) {}
void RenderDevice::InitVertexBuffer() {}