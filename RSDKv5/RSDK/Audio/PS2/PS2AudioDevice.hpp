#ifndef PS2_AUDIO_DEVICE_H
#define PS2_AUDIO_DEVICE_H

#define AUDIO_FREQUENCY 48000
#define AUDIO_CHANNELS 2
#define AUDIO_BUFFER_SIZE 4096
#define MAX_VOLUME 100

#define LockAudioDevice()   ;
#define UnlockAudioDevice() ;

namespace RSDK
{

class AudioDevice : public AudioDeviceBase
{
public:
    static int device;

    // initialization and cleanup
    static bool32 Init();
    static void Release();

    // frame-based operations (called every game frame)
    static void FrameInit();

    // stream handling
    static void HandleStreamLoad(ChannelInfo *channel, bool32 async);

    // volume control
    static void SetMasterVolume(uint8 volume);
    static uint8 GetMasterVolume();

    // playback control
    static void PauseAudio();
    static void ResumeAudio();
    static bool32 IsAudioPlaying();

    // ADPCM support
    static bool32 LoadADPCM(const char *filename, uint8 slot);
    static bool32 IsADPCMLoaded(uint8 slot); 
    static int32 PlayADPCM(uint8 slot, uint32 loopPoint, uint32 priority);
    static void StopADPCM(int32 channel);
    static int32 GetADPCMChannel(int32 channel);
    static void UpdateADPCMChannels(); 
    static void UnloadADPCM(uint8 slot); // free loaded ADPCM

private:
    struct PS2_AudioSpec {
        int freq;
        int format;
        int samples;
        int channels;
        void (*callback)(void *data, uint8 *stream, int32 len);
    };

    static PS2_AudioSpec deviceSpec;
    static uint8 contextInitialized;

    // initialization helpers
    static void InitAudioChannels();
    static void AudioCallback(void *data, uint8 *stream, int32 len);
};

} // namespace RSDK

#endif // PS2_AUDIO_DEVICE_H