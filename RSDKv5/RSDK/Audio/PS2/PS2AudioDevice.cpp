#include "PS2AudioDevice.hpp"
#include "../../Core/RetroEngine.hpp"
#include <audsrv.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

using namespace RSDK;

extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;

uint8 AudioDevice::contextInitialized = 0;
int AudioDevice::device = -1;
AudioDevice::PS2_AudioSpec AudioDevice::deviceSpec = {0};

static audsrv_adpcm_t adpcm_samples[SFX_COUNT];
static u8* adpcm_buffers[SFX_COUNT];
static int adpcm_channels[CHANNEL_COUNT];
static bool adpcm_loaded[SFX_COUNT];
static bool adpcm_system_initialized = false;

typedef struct {
    char magic[4];
    unsigned char version;
    unsigned char channels;
    unsigned char loop;
    unsigned char reserved;
    unsigned int pitch;
    unsigned int samples;
} adpcm_header_t;

struct ChannelTimer {
    uint32 startTime;
    uint32 duration;
    bool active;
};

static ChannelTimer channel_timers[CHANNEL_COUNT];
static uint32 current_frame = 0;

static uint32 GetADPCMDuration(uint8 slot)
{
    if (slot >= SFX_COUNT || !adpcm_loaded[slot])
        return 0;
    
    adpcm_header_t* header = (adpcm_header_t*)adpcm_buffers[slot];
    
    if (header->magic[0] == 'A' && header->magic[1] == 'P' && 
        header->magic[2] == 'C' && header->magic[3] == 'M') {
        
        uint32 sample_rate = (header->pitch * 22050) / 4096;
        uint32 duration_ms = (header->samples * 1000) / sample_rate;
        uint32 duration_frames = (duration_ms * 60) / 1000;
        
        return duration_frames + 10;
    }
    
    return 120;
}

bool32 AudioDevice::Init()
{
    if (contextInitialized) {
        return true;
    }

    sceSifInitRpc(0);

    memset(adpcm_samples, 0, sizeof(adpcm_samples));
    memset(adpcm_buffers, 0, sizeof(adpcm_buffers));
    memset(adpcm_loaded, 0, sizeof(adpcm_loaded));
    memset(channel_timers, 0, sizeof(channel_timers));
    current_frame = 0;
    
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        adpcm_channels[i] = -1;
        channel_timers[i].active = false;
    }

    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    
    int ret = SifLoadModule("rom0:LIBSD", 0, NULL);
    if (ret < 0) {
        return false;
    }
    
    ret = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, NULL);
    if (ret < 0) {
        ret = SifLoadModule("host:audsrv.irx", 0, NULL);
        if (ret < 0) {
            return false;
        }
    }
    
    ret = audsrv_init();
    if (ret != 0) {
        return false;
    }

    struct audsrv_fmt_t format;
    format.bits = 16;
    format.freq = AUDIO_FREQUENCY;
    format.channels = AUDIO_CHANNELS;
    
    ret = audsrv_set_format(&format);

    audsrv_set_volume(MAX_VOLUME);

    contextInitialized = true;
    InitAudioChannels();

    deviceSpec.freq = AUDIO_FREQUENCY;
    deviceSpec.channels = AUDIO_CHANNELS;
    deviceSpec.samples = MIX_BUFFER_SIZE / AUDIO_CHANNELS;

    audioState = true;

    return true;
}

bool32 AudioDevice::IsADPCMLoaded(uint8 slot)
{
    if (slot >= SFX_COUNT) {
        return false;
    }
    return adpcm_loaded[slot];
}

void AudioDevice::UnloadADPCM(uint8 slot)
{
    if (slot >= SFX_COUNT) {
        return;
    }
    
    if (!adpcm_loaded[slot]) {
        return;
    }
    
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (adpcm_channels[c] != -1 && channels[c].soundID == slot) {
            StopADPCM(c);
        }
    }
    
    audsrv_free_adpcm(&adpcm_samples[slot]);
    
    if (adpcm_buffers[slot]) {
        free(adpcm_buffers[slot]);
        adpcm_buffers[slot] = NULL;
    }
    
    adpcm_loaded[slot] = false;
}

void AudioDevice::Release()
{
    if (!contextInitialized)
        return;

    audioState = false;

    for (int i = 0; i < SFX_COUNT; i++) {
        if (adpcm_loaded[i]) {
            audsrv_free_adpcm(&adpcm_samples[i]);
            if (adpcm_buffers[i]) {
                free(adpcm_buffers[i]);
                adpcm_buffers[i] = NULL;
            }
            adpcm_loaded[i] = false;
        }
    }

    audsrv_quit();

    LockAudioDevice();
    AudioDeviceBase::Release();
    UnlockAudioDevice();

    contextInitialized = false;
}

void AudioDevice::InitAudioChannels()
{
    AudioDeviceBase::InitAudioChannels();
}

bool32 AudioDevice::LoadADPCM(const char *filename, uint8 slot)
{
    if (slot >= SFX_COUNT) {
        return false;
    }

    if (adpcm_loaded[slot]) {
        return true;
    }

    if (!adpcm_system_initialized) {
        int ret = audsrv_adpcm_init();
        if (ret != 0) {
            return false;
        }
        adpcm_system_initialized = true;
    }

    FileInfo info;
    InitFileInfo(&info);

    if (!LoadFile(&info, filename, FMODE_RB)) {
        return false;
    }

    int size = info.fileSize;

    adpcm_buffers[slot] = (u8*)memalign(64, size);
    if (adpcm_buffers[slot] == NULL) {
        CloseFile(&info);
        return false;
    }

    ReadBytes(&info, adpcm_buffers[slot], size);
    CloseFile(&info);

    int ret = audsrv_load_adpcm(&adpcm_samples[slot], adpcm_buffers[slot], size);

    if (ret < 0) {
        free(adpcm_buffers[slot]);
        adpcm_buffers[slot] = NULL;
        return false;
    }

    adpcm_loaded[slot] = true;
    return true;
}

int32 AudioDevice::PlayADPCM(uint8 slot, uint32 loopPoint, uint32 priority)
{
    if (slot >= SFX_COUNT || !adpcm_loaded[slot]) {
        return -1;
    }

    int32 channel = -1;
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (adpcm_channels[c] == -1) {
            channel = c;
            break;
        }
    }

    if (channel == -1) {
        return -1;
    }

    int audsrv_ch = audsrv_ch_play_adpcm(-1, &adpcm_samples[slot]);

    if (audsrv_ch < 0) {
        return -1;
    }

    audsrv_adpcm_set_volume_and_pan(audsrv_ch, MAX_VOLUME, 0);

    adpcm_channels[channel] = audsrv_ch;
    
    channel_timers[channel].startTime = current_frame;
    channel_timers[channel].duration = GetADPCMDuration(slot);
    channel_timers[channel].active = true;

    return channel;
}

void AudioDevice::StopADPCM(int32 channel)
{
    if (channel < 0 || channel >= CHANNEL_COUNT)
        return;

    if (adpcm_channels[channel] != -1) {
        audsrv_adpcm_set_volume_and_pan(adpcm_channels[channel], 0, 0);
        adpcm_channels[channel] = -1;
        channel_timers[channel].active = false;
    }
}

void AudioDevice::UpdateADPCMChannels()
{
    current_frame++;
    
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (channels[c].state == CHANNEL_STREAM || channels[c].state == CHANNEL_LOADING_STREAM)
            continue;
            
        if (!channel_timers[c].active)
            continue;
            
        uint32 elapsed = current_frame - channel_timers[c].startTime;
        
        if (elapsed >= channel_timers[c].duration) {
            adpcm_channels[c] = -1;
            channel_timers[c].active = false;
            
            if (channels[c].soundID != -1 && channels[c].state == CHANNEL_SFX) {
                channels[c].state = CHANNEL_IDLE;
                channels[c].soundID = -1;
            }
        }
    }
}

void AudioDevice::FrameInit()
{
    if (!audioState)
        return;
    
    UpdateADPCMChannels();
    
    int available = audsrv_available();
    const int bufferSizeBytes = MIX_BUFFER_SIZE * sizeof(int16_t);
    
    if (available < bufferSizeBytes) {
        return;
    }
    
    static int16_t outputBuffer[MIX_BUFFER_SIZE];
    memset(outputBuffer, 0, sizeof(outputBuffer));

    const int numSamples = MIX_BUFFER_SIZE / AUDIO_CHANNELS;

    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        ChannelInfo *channel = &channels[c];

        if (channel->state != CHANNEL_STREAM)
            continue;

        int16_t *streamBuffer = (int16_t *)&channel->samplePtr[channel->bufferPos];

        float volL = channel->volume, volR = channel->volume;
        if (channel->pan < 0.0f)
            volR = (1.0f + channel->pan) * channel->volume;
        else
            volL = (1.0f - channel->pan) * channel->volume;

        float panL = volL * engine.streamVolume;
        float panR = volR * engine.streamVolume;

        uint32 speedPercent = 0;
        int outPos = 0;
        
        while (outPos < numSamples) {
            speedPercent += channel->speed;
            int32 next = FROM_FIXED(speedPercent);
            speedPercent %= TO_FIXED(1);

            int32_t left = outputBuffer[outPos * 2] + (int32_t)(streamBuffer[0] * panL);
            int32_t right = outputBuffer[outPos * 2 + 1] + (int32_t)(streamBuffer[1] * panR);

            outputBuffer[outPos * 2] = (int16_t)CLAMP(left, -32768, 32767);
            outputBuffer[outPos * 2 + 1] = (int16_t)CLAMP(right, -32768, 32767);
            
            outPos++;

            streamBuffer += next * 2;
            channel->bufferPos += next * 2;

            if (channel->bufferPos >= channel->sampleLength) {
                channel->bufferPos -= (uint32)channel->sampleLength;
                streamBuffer = (int16_t *)&channel->samplePtr[channel->bufferPos];
                UpdateStreamBuffer(channel);
                
                if (channel->state == CHANNEL_IDLE)
                    break;
            }
        }
    }

    audsrv_play_audio((const char *)outputBuffer, bufferSizeBytes);
}

int32 AudioDevice::GetADPCMChannel(int32 channel)
{
    if (channel < 0 || channel >= CHANNEL_COUNT)
        return -1;
    return adpcm_channels[channel];
}

void AudioDevice::HandleStreamLoad(ChannelInfo *channel, bool32 async)
{
    LoadStream(channel);
}

void AudioDevice::SetMasterVolume(uint8 volume)
{
    if (volume > MAX_VOLUME)
        volume = MAX_VOLUME;
    audsrv_set_volume(volume);
}

uint8 AudioDevice::GetMasterVolume()
{
    return 100;
}

void AudioDevice::PauseAudio()
{
    audioState = false;
}

void AudioDevice::ResumeAudio()
{
    audioState = true;
}

bool32 AudioDevice::IsAudioPlaying()
{
    return audioState && contextInitialized;
}