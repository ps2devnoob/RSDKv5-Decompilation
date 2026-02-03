using ShaderEntry = ShaderEntryBase;

class RenderDevice : public RenderDeviceBase
{
public:
  struct WindowInfo {
    union {
      uint32 width;
      uint32 height;
      uint32 refresh_rate;
    } * displays;
  };

  static WindowInfo displayInfo;

  static bool Init();
  static void CopyFrameBuffer();
  static void FlipScreen();
  static void Release(bool32 isRefresh);

  static void RefreshWindow();

  static void SetupImageTexture(int32 width, int32 height, uint8* imagePixels);
  static void SetupVideoTexture_YUV420(int32 width, int32 height, uint8* y_plane, uint8* u_plane, uint8* v_plane, int32 y_stride, int32 u_stride, int32 v_stride);
  static void SetupVideoTexture_YUV422(int32 width, int32 height, uint8* y_plane, uint8* u_plane, uint8* v_plane, int32 y_stride, int32 u_stride, int32 v_stride);
  static void SetupVideoTexture_YUV424(int32 width, int32 height, uint8* y_plane, uint8* u_plane, uint8* v_plane, int32 y_stride, int32 u_stride, int32 v_stride);
  static void SetupVideoTexture_Direct(int32 width, int32 height, uint32 *pixels, int texAddr);
  static void RenderVideoFrameDirect(void* rgba32_data, int width, int height);
  static void MarkVideoFrameReady();

  static bool ProcessEvents();

  static void InitFPSCap();
  static bool CheckFPSCap();
  static void UpdateFPSCap();

  static void GetWindowSize(int32* width, int32* height);

  static void LoadShader(const char* fileName, bool32 linear);
  
  static void ToggleVSync();
  static void ChangeVideoMode(int32 width, int32 height, int32 refreshRate);
  static void CycleVideoMode(int direction);
  
public:
  static bool InitShaders();
  static bool SetupRendering();
  static void InitVertexBuffer();
  static bool InitGraphicsAPI();

  static void GetDisplays();
};