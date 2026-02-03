#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/AudioLegacy.cpp"
#endif

SFXInfo RSDK::sfxList[SFX_COUNT];
ChannelInfo RSDK::channels[CHANNEL_COUNT];

typedef struct {
    FileInfo fileInfo;
    uint32 dataStartPos;
    uint32 dataSize;
    uint32 currentReadPos;
    uint32 loopPoint;
    bool isActive;
    uint16 numChannels;
    uint32 sampleRate;
} StreamFileInfo;

static StreamFileInfo activeStream = {0};

char streamFilePath[0x40];
uint8 *streamBuffer    = NULL;
int32 streamBufferSize = 0;
uint32 streamStartPos  = 0;
int32 streamLoopPoint  = 0;

#define STREAM_BUFFER_SIZE (64 * 1024)
#define STREAM_CHUNK_SIZE (16 * 1024)

static uint8 *circularStreamBuffer = NULL;

// determines the 'resolution' of the lookup table
#define LINEAR_INTERPOLATION_LOOKUP_DIVISOR 0x40
#define LINEAR_INTERPOLATION_LOOKUP_LENGTH  (TO_FIXED(1) / LINEAR_INTERPOLATION_LOOKUP_DIVISOR)

float linearInterpolationLookup[LINEAR_INTERPOLATION_LOOKUP_LENGTH];

#if RETRO_AUDIODEVICE_XAUDIO
#include "XAudio/XAudioDevice.cpp"
#elif RETRO_AUDIODEVICE_SDL2
#include "SDL2/SDL2AudioDevice.cpp"
#elif RETRO_AUDIODEVICE_PORT
#include "PortAudio/PortAudioDevice.cpp"
#elif RETRO_AUDIODEVICE_MINI
#include "MiniAudio/MiniAudioDevice.cpp"
#elif RETRO_AUDIODEVICE_OBOE
#include "Oboe/OboeAudioDevice.cpp"
#elif RETRO_AUDIODEVICE_PS2
#include "PS2/PS2AudioDevice.cpp"
#endif

uint8 AudioDeviceBase::initializedAudioChannels = false;
uint8 AudioDeviceBase::audioState               = 0;
uint8 AudioDeviceBase::audioFocus               = 0;

typedef struct {
    char riff[4];
    uint32 fileSize;
    char wave[4];
} WAVHeader;

typedef struct {
    char chunkID[4];
    uint32 chunkSize;
} WAVChunk;

typedef struct {
    uint16 audioFormat;
    uint16 numChannels;
    uint32 sampleRate;
    uint32 byteRate;
    uint16 blockAlign;
    uint16 bitsPerSample;
} WAVFmt;

void AudioDeviceBase::Release()
{
}

void AudioDeviceBase::ProcessAudioMixing(void *stream, int32 length)
{
    SAMPLE_FORMAT *streamF    = (SAMPLE_FORMAT *)stream;
    SAMPLE_FORMAT *streamEndF = ((SAMPLE_FORMAT *)stream) + length;

    memset(stream, 0, length * sizeof(SAMPLE_FORMAT));

    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        ChannelInfo *channel = &channels[c];

        switch (channel->state) {
            default:
            case CHANNEL_IDLE: break;

            case CHANNEL_SFX: {
#if RETRO_PLATFORM != RETRO_PS2
                SAMPLE_FORMAT *sfxBuffer = &channel->samplePtr[channel->bufferPos];

                float volL = channel->volume, volR = channel->volume;
                if (channel->pan < 0.0f)
                    volR = (1.0f + channel->pan) * channel->volume;
                else
                    volL = (1.0f - channel->pan) * channel->volume;

                float panL = volL * engine.soundFXVolume;
                float panR = volR * engine.soundFXVolume;

                uint32 speedPercent       = 0;
                SAMPLE_FORMAT *curStreamF = streamF;
                while (curStreamF < streamEndF && streamF < streamEndF) {
                    // perform linear interpolation
                    SAMPLE_FORMAT sample;
#if !RETRO_USE_ORIGINAL_CODE
                    // protection for v5u (and other mysterious crashes)
                    if (!sfxBuffer)
                        sample = 0;
                    else
#endif
                        sample = (sfxBuffer[1] - sfxBuffer[0]) * linearInterpolationLookup[speedPercent / LINEAR_INTERPOLATION_LOOKUP_DIVISOR]
                                 + sfxBuffer[0];

                    speedPercent += channel->speed;
                    sfxBuffer += FROM_FIXED(speedPercent);
                    channel->bufferPos += FROM_FIXED(speedPercent);
                    speedPercent %= TO_FIXED(1);

                    curStreamF[0] += sample * panL;
                    curStreamF[1] += sample * panR;
                    curStreamF += 2;

                    if (channel->bufferPos >= channel->sampleLength) {
                        if (channel->loop == (uint32)-1) {
                            channel->state   = CHANNEL_IDLE;
                            channel->soundID = -1;
                            break;
                        }
                        else {
                            channel->bufferPos -= (uint32)channel->sampleLength;
                            channel->bufferPos += channel->loop;
                            sfxBuffer = &channel->samplePtr[channel->bufferPos];
                        }
                    }
                }
#endif
                break;
            }

            case CHANNEL_STREAM: {
                SAMPLE_FORMAT *streamBuffer = &channel->samplePtr[channel->bufferPos];

                float volL = channel->volume, volR = channel->volume;
                if (channel->pan < 0.0f)
                    volR = (1.0f + channel->pan) * channel->volume;
                else
                    volL = (1.0f - channel->pan) * channel->volume;

                float panL = volL * engine.streamVolume;
                float panR = volR * engine.streamVolume;

                uint32 speedPercent       = 0;
                SAMPLE_FORMAT *curStreamF = streamF;
                while (curStreamF < streamEndF && streamF < streamEndF) {
                    speedPercent += channel->speed;
                    int32 next = FROM_FIXED(speedPercent);
                    speedPercent %= TO_FIXED(1);

#if RETRO_PLATFORM == RETRO_PS2
                    int32 left = (int32)curStreamF[0] + (int32)(streamBuffer[0] * panL);
                    int32 right = (int32)curStreamF[1] + (int32)(streamBuffer[1] * panR);
                    curStreamF[0] = (int16_t)CLAMP(left, -32768, 32767);
                    curStreamF[1] = (int16_t)CLAMP(right, -32768, 32767);
#else
                    curStreamF[0] += streamBuffer[0] * panL;
                    curStreamF[1] += streamBuffer[1] * panR;
#endif
                    curStreamF += 2;

                    streamBuffer += next * 2;
                    channel->bufferPos += next * 2;

                    if (channel->bufferPos >= channel->sampleLength) {
                        channel->bufferPos -= (uint32)channel->sampleLength;
                        streamBuffer = &channel->samplePtr[channel->bufferPos];
                        UpdateStreamBuffer(channel);
                    }
                }
                break;
            }

            case CHANNEL_LOADING_STREAM: break;
        }
    }
}

void AudioDeviceBase::InitAudioChannels()
{
    for (int32 i = 0; i < CHANNEL_COUNT; ++i) {
        channels[i].soundID = -1;
        channels[i].state   = CHANNEL_IDLE;
    }

    // compute a lookup table of floating-point linear interpolation delta scales,
    // to speed-up the process of converting from fixed-point to floating-point
    for (int32 i = 0; i < LINEAR_INTERPOLATION_LOOKUP_LENGTH; ++i) 
        linearInterpolationLookup[i] = i / (float)LINEAR_INTERPOLATION_LOOKUP_LENGTH;

    #define STREAM_SLOT (SFX_COUNT - 2)
    
    GEN_HASH_MD5("Stream Channel 0", sfxList[STREAM_SLOT].hash);
    sfxList[STREAM_SLOT].scope              = SCOPE_NONE;
    sfxList[STREAM_SLOT].maxConcurrentPlays = 1;
    sfxList[STREAM_SLOT].length             = MIX_BUFFER_SIZE;
    
    // allocate only the mixing buffer (small, ~20KB)
    AllocateStorage((void **)&sfxList[STREAM_SLOT].buffer, MIX_BUFFER_SIZE * sizeof(SAMPLE_FORMAT), DATASET_MUS, false);
    
    // allocate fixed circular buffer of 64KB for streaming
    if (!circularStreamBuffer) {
        AllocateStorage((void **)&circularStreamBuffer, STREAM_BUFFER_SIZE, DATASET_MUS, false);
    }

    memset(&sfxList[SFX_COUNT - 1], 0, sizeof(SFXInfo));
    memset(&activeStream, 0, sizeof(StreamFileInfo));

    initializedAudioChannels = true;
}

void RSDK::UpdateStreamBuffer(ChannelInfo *channel)
{
#if RETRO_PLATFORM == RETRO_PS2
    int16_t *buffer = (int16_t *)channel->samplePtr;
    
    if (!activeStream.isActive || !circularStreamBuffer) {
        memset(buffer, 0, MIX_BUFFER_SIZE * sizeof(int16_t));
        return;
    }

    int32 bytesToCopy = MIX_BUFFER_SIZE * sizeof(int16_t);
    uint32 filePos = activeStream.currentReadPos;
    
    if (filePos >= activeStream.dataSize) {
        if (channel->loop) {
            // align loop point to stereo sample boundary
            uint32 alignedLoopPoint = (activeStream.loopPoint / 4) * 4;
            activeStream.currentReadPos = alignedLoopPoint;
            filePos = alignedLoopPoint;
            
            // clear last samples to avoid clicks
            if (bytesToCopy >= 16) {
                int16_t *lastSamples = buffer + (MIX_BUFFER_SIZE - 4);
                for (int i = 0; i < 4; i++) {
                    lastSamples[i] = lastSamples[i] / 2; // quick fade
                }
            }
        } else {
            memset(buffer, 0, bytesToCopy);
            channel->state = CHANNEL_IDLE;
            activeStream.isActive = false;
            CloseFile(&activeStream.fileInfo);
            return;
        }
    }
    
    uint32 remaining = activeStream.dataSize - filePos;
    uint32 toRead = (remaining > bytesToCopy) ? bytesToCopy : remaining;
    
    // ensure aligned read
    toRead = (toRead / 4) * 4;
    
    Seek_Set(&activeStream.fileInfo, activeStream.dataStartPos + filePos);
    ReadBytes(&activeStream.fileInfo, buffer, toRead);
    
    if (toRead < bytesToCopy) {
        memset((uint8*)buffer + toRead, 0, bytesToCopy - toRead);
    }
    
    activeStream.currentReadPos += toRead;
#endif
}

void RSDK::LoadStream(ChannelInfo *channel)
{
    // clean up previous stream if exists
    if (activeStream.isActive) {
        CloseFile(&activeStream.fileInfo);
        activeStream.isActive = false;
    }

    InitFileInfo(&activeStream.fileInfo);

    if (LoadFile(&activeStream.fileInfo, streamFilePath, FMODE_RB)) {
        WAVHeader header;
        ReadBytes(&activeStream.fileInfo, &header, sizeof(WAVHeader));

        if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
            CloseFile(&activeStream.fileInfo);
            channel->state = CHANNEL_IDLE;
            return;
        }

        WAVFmt fmt = {0};
        uint32 dataSize = 0;
        uint32 dataOffset = 0;
        bool foundFmt = false;
        bool foundData = false;

        while (!foundData && activeStream.fileInfo.readPos < activeStream.fileInfo.fileSize) {
            WAVChunk chunk;
            if (ReadBytes(&activeStream.fileInfo, &chunk, sizeof(WAVChunk)) != sizeof(WAVChunk))
                break;

            if (strncmp(chunk.chunkID, "fmt ", 4) == 0) {
                ReadBytes(&activeStream.fileInfo, &fmt, sizeof(WAVFmt));
                foundFmt = true;
                
                uint32 remaining = chunk.chunkSize - sizeof(WAVFmt);
                if (remaining > 0) {
                    uint8 skipBuf[256];
                    while (remaining > 0) {
                        uint32 toSkip = remaining > 256 ? 256 : remaining;
                        ReadBytes(&activeStream.fileInfo, skipBuf, toSkip);
                        remaining -= toSkip;
                    }
                }
            }
            else if (strncmp(chunk.chunkID, "data", 4) == 0) {
                dataSize = chunk.chunkSize;
                dataOffset = activeStream.fileInfo.readPos;
                foundData = true;
            }
            else {
                uint8 skipBuf[256];
                uint32 remaining = chunk.chunkSize;
                while (remaining > 0) {
                    uint32 toSkip = remaining > 256 ? 256 : remaining;
                    ReadBytes(&activeStream.fileInfo, skipBuf, toSkip);
                    remaining -= toSkip;
                }
            }
        }

        if (!foundFmt || !foundData) {
            CloseFile(&activeStream.fileInfo);
            channel->state = CHANNEL_IDLE;
            return;
        }

        // configure stream information
        activeStream.dataStartPos = dataOffset;
        activeStream.dataSize = dataSize;
        activeStream.currentReadPos = (streamStartPos < dataSize) ? streamStartPos : 0;
        activeStream.loopPoint = (streamLoopPoint < dataSize) ? streamLoopPoint : 0;
        activeStream.numChannels = fmt.numChannels;
        activeStream.sampleRate = fmt.sampleRate;
        activeStream.isActive = true;

        // load first chunk into circular buffer
        activeStream.fileInfo.readPos = activeStream.dataStartPos + activeStream.currentReadPos;
        
        uint32 initialLoad = (dataSize > STREAM_BUFFER_SIZE) ? STREAM_BUFFER_SIZE : dataSize;
        memset(circularStreamBuffer, 0, STREAM_BUFFER_SIZE);
        ReadBytes(&activeStream.fileInfo, circularStreamBuffer, initialLoad);
        activeStream.currentReadPos += initialLoad;

        UpdateStreamBuffer(channel);
        channel->state = CHANNEL_STREAM;
        
    } else {
        channel->state = CHANNEL_IDLE;
    }
}

int32 RSDK::PlayStream(const char *filename, uint32 slot, uint32 startPos, uint32 loopPoint, bool32 loadASync)
{
    if (!engine.streamsEnabled) {
        return -1;
    }

    // find available channel
    if (slot >= CHANNEL_COUNT) {
        for (int32 c = 0; c < CHANNEL_COUNT && slot >= CHANNEL_COUNT; ++c) {
            if (channels[c].soundID == -1 && channels[c].state != CHANNEL_LOADING_STREAM) {
                slot = c;
            }
        }

        if (slot >= CHANNEL_COUNT) {
            uint32 len = 0xFFFFFFFF;
            for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
                if (channels[c].sampleLength < len && channels[c].state != CHANNEL_LOADING_STREAM) {
                    slot = c;
                    len  = (uint32)channels[c].sampleLength;
                }
            }
        }
    }

    if (slot >= CHANNEL_COUNT) {
        return -1;
    }

    ChannelInfo *channel = &channels[slot];

    LockAudioDevice();

    // stop previous stream if exists
    if (channel->state == CHANNEL_STREAM || channel->state == CHANNEL_LOADING_STREAM) {
        if (activeStream.isActive) {
            CloseFile(&activeStream.fileInfo);
            activeStream.isActive = false;
        }
        channel->state = CHANNEL_IDLE;
    }

    // clear buffer before starting new stream
    #define STREAM_SLOT (SFX_COUNT - 2)
    if (sfxList[STREAM_SLOT].buffer) {
        memset(sfxList[STREAM_SLOT].buffer, 0, MIX_BUFFER_SIZE * sizeof(SAMPLE_FORMAT));
    }

    // configure channel
    channel->soundID      = 0xFF;
    channel->loop         = loopPoint != 0;
    channel->priority     = 0xFF;
    channel->state        = CHANNEL_LOADING_STREAM;
    channel->pan          = 0.0f;
    channel->volume       = 1.0f;
    channel->sampleLength = sfxList[STREAM_SLOT].length;
    channel->samplePtr    = sfxList[STREAM_SLOT].buffer;
    channel->bufferPos    = 0;
    
#if RETRO_PLATFORM == RETRO_PS2
    channel->speed = (int32)(0.80f * 65536.0f);
#else
    channel->speed = TO_FIXED(1);
#endif

    // convert .ogg extension to .wav
    char tempPath[0x40];
    sprintf_s(tempPath, sizeof(tempPath), "%s", filename);
    
    char *ext = strrchr(tempPath, '.');
    if (ext && strcmp(ext, ".ogg") == 0) {
        strcpy(ext, ".wav");
    }

    sprintf_s(streamFilePath, sizeof(streamFilePath), "Data/Music/%s", tempPath);
    
    // align positions to stereo sample (4 bytes = L+R in 16-bit)
    // each stereo sample = 2 channels × 2 bytes = 4 bytes
    streamStartPos  = (startPos / 4) * 4;
    streamLoopPoint = (loopPoint / 4) * 4;

    AudioDevice::HandleStreamLoad(channel, loadASync);

    UnlockAudioDevice();

    return slot;
}

void RSDK::LoadSfxToSlot(char *filename, uint8 slot, uint8 plays, uint8 scope)
{
#if RETRO_PLATFORM == RETRO_PS2
    if (sfxList[slot].scope != SCOPE_NONE) {
        return;
    }

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filename, hash);

    HASH_COPY_MD5(sfxList[slot].hash, hash);
    sfxList[slot].scope = scope;
    sfxList[slot].maxConcurrentPlays = plays;
    sfxList[slot].length = 0;
    sfxList[slot].buffer = NULL;
    sfxList[slot].playCount = 0;
    
    // convert .wav to .adp on ps2
    char convertedFilename[0x100];
    strncpy(convertedFilename, filename, sizeof(convertedFilename) - 1);
    convertedFilename[sizeof(convertedFilename) - 1] = '\0';
    
    char *extPos = strrchr(convertedFilename, '.');
    if (extPos) {
        char extLower[8];
        strcpy(extLower, extPos);
        for (int i = 0; extLower[i]; i++) {
            extLower[i] = tolower(extLower[i]);
        }
        
        if (strcmp(extLower, ".wav") == 0) {
            strcpy(extPos, ".adp");
        }
    }
    
    strncpy(sfxList[slot].fileName, convertedFilename, sizeof(sfxList[slot].fileName) - 1);
    sfxList[slot].fileName[sizeof(sfxList[slot].fileName) - 1] = '\0';

#else
    FileInfo info;
    InitFileInfo(&info);

    char fullFilePath[0x80];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/SoundFX/%s", filename);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filename, hash);

    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        HASH_COPY_MD5(sfxList[slot].hash, hash);
        sfxList[slot].scope              = scope;
        sfxList[slot].maxConcurrentPlays = plays;
    }
    CloseFile(&info);
#endif
}

void RSDK::LoadSfx(char *filename, uint8 plays, uint8 scope)
{
#if RETRO_PLATFORM == RETRO_PS2
    RETRO_HASH_MD5(newHash);
    GEN_HASH_MD5(filename, newHash);
    
    for (uint32 i = 0; i < SFX_COUNT - 2; ++i) {
        if (sfxList[i].scope != SCOPE_NONE) {
            bool match = true;
            for (int h = 0; h < 16; h++) {
                if (sfxList[i].hash[h] != newHash[h]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return;
            }
        }
    }

    if (scope == SCOPE_STAGE) {
        for (uint32 i = 0; i < SFX_COUNT - 2; ++i) {
            if (sfxList[i].scope == SCOPE_STAGE) {
#if RETRO_PLATFORM == RETRO_PS2
                AudioDevice::UnloadADPCM(i);
#endif
                memset(&sfxList[i], 0, sizeof(SFXInfo));
                sfxList[i].scope = SCOPE_NONE;
            }
        }
    }

    uint16 id = (uint16)-1;
    for (uint32 i = 0; i < SFX_COUNT - 2; ++i) {
        if (sfxList[i].scope == SCOPE_NONE) {
            id = i;
            break;
        }
    }

    if (id != (uint16)-1) {
        for (int h = 0; h < 16; h++) {
            sfxList[id].hash[h] = newHash[h];
        }
        
        sfxList[id].scope = scope;
        sfxList[id].maxConcurrentPlays = plays;
        sfxList[id].length = 0;
        sfxList[id].buffer = NULL;
        sfxList[id].playCount = 0;
        
        // convert .wav to .adp on ps2
        char convertedFilename[0x100];
        strncpy(convertedFilename, filename, sizeof(convertedFilename) - 1);
        convertedFilename[sizeof(convertedFilename) - 1] = '\0';
        
        char *extPos = strrchr(convertedFilename, '.');
        if (extPos) {
            char extLower[8];
            strcpy(extLower, extPos);
            for (int i = 0; extLower[i]; i++) {
                extLower[i] = tolower(extLower[i]);
            }
            
            if (strcmp(extLower, ".wav") == 0) {
                strcpy(extPos, ".adp");
            }
        }
        
        strncpy(sfxList[id].fileName, convertedFilename, sizeof(sfxList[id].fileName) - 1);
        sfxList[id].fileName[sizeof(sfxList[id].fileName) - 1] = '\0';
    }
#else
    FileInfo info;
    InitFileInfo(&info);

    char fullFilePath[0x80];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/SoundFX/%s", filename);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filename, hash);

    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        HASH_COPY_MD5(sfxList[slot].hash, hash);
        sfxList[slot].scope              = scope;
        sfxList[slot].maxConcurrentPlays = plays;
    }
    CloseFile(&info);
#endif
}

int32 RSDK::PlaySfx(uint16 sfx, uint32 loopPoint, uint32 priority)
{
#if RETRO_PLATFORM == RETRO_PS2
    if (sfx == (uint16)-1 || sfx >= SFX_COUNT || sfxList[sfx].scope == SCOPE_NONE) {
        return -1;
    }

    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (channels[c].soundID == sfx) {
            AudioDevice::StopADPCM(c);
            channels[c].state = CHANNEL_IDLE;
            channels[c].soundID = -1;
        }
    }

    if (!AudioDevice::IsADPCMLoaded(sfx)) {
        char fullFilePath[0x80];
        sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/SoundFX/%s", sfxList[sfx].fileName);
        
        if (!AudioDevice::LoadADPCM(fullFilePath, sfx)) {
            return -1;
        }
    }

    int32 channel = -1;
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (channels[c].state == CHANNEL_IDLE) {
            channel = c;
            break;
        }
    }

    if (channel == -1) {
        return -1;
    }

    int32 audsrvChannel = AudioDevice::PlayADPCM(sfx, loopPoint, priority);
    
    if (audsrvChannel >= 0) {
        channels[channel].soundID = sfx;
        channels[channel].state = CHANNEL_SFX;
        channels[channel].priority = priority;
        channels[channel].playIndex = sfxList[sfx].playCount++;
        channels[channel].loop = loopPoint;
        channels[channel].volume = 1.0f;
        channels[channel].pan = 0.0f;
    }

    return channel;
#else
    return -1;
#endif
}

void RSDK::SetChannelAttributes(uint8 channel, float volume, float panning, float speed)
{
    if (channel < CHANNEL_COUNT) {
        volume                   = fminf(4.0f, volume);
        volume                   = fmaxf(0.0f, volume);
        channels[channel].volume = volume;

        panning               = fminf(1.0f, panning);
        panning               = fmaxf(-1.0f, panning);
        channels[channel].pan = panning;

        if (speed > 0.0f)
            channels[channel].speed = (int32)(speed * TO_FIXED(1));
        else if (speed == 1.0f)
            channels[channel].speed = TO_FIXED(1);

#if RETRO_PLATFORM == RETRO_PS2
        if (channels[channel].state == CHANNEL_SFX) {
            int audVolume = (int)(volume * 25.0f);
            if (audVolume > MAX_VOLUME) audVolume = MAX_VOLUME;
            
            int audPan = (int)((panning + 1.0f) * 50.0f);
        }
#endif
    }
}

uint32 RSDK::GetChannelPos(uint32 channel)
{
    if (channel >= CHANNEL_COUNT)
        return 0;

    if (channels[channel].state == CHANNEL_SFX)
        return channels[channel].bufferPos;

    if (channels[channel].state == CHANNEL_STREAM) {
        return activeStream.currentReadPos;
    }

    return 0;
}

double RSDK::GetVideoStreamPos()
{
    if (channels[0].state == CHANNEL_STREAM && AudioDevice::audioState && AudioDevice::initializedAudioChannels) {
        return activeStream.currentReadPos / (double)(AUDIO_FREQUENCY * 2 * sizeof(int16_t));
    }

    return -1.0;
}

void RSDK::ClearStageSfx()
{
    LockAudioDevice();

    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (channels[c].state == CHANNEL_SFX || channels[c].state == (CHANNEL_SFX | CHANNEL_PAUSED)) {
#if RETRO_PLATFORM == RETRO_PS2
            AudioDevice::StopADPCM(c);
#endif
            channels[c].soundID = -1;
            channels[c].state   = CHANNEL_IDLE;
        }
    }

    // unload stage sfx
    for (int32 s = 0; s < SFX_COUNT - 2; ++s) {
        if (sfxList[s].scope >= SCOPE_STAGE) {
#if RETRO_PLATFORM == RETRO_PS2
            AudioDevice::UnloadADPCM(s);
#endif
            MEM_ZERO(sfxList[s]);
            sfxList[s].scope = SCOPE_NONE;
        }
    }

    UnlockAudioDevice();
}

#if RETRO_USE_MOD_LOADER
void RSDK::ClearGlobalSfx()
{
    LockAudioDevice();

    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        if (channels[c].state == CHANNEL_SFX || channels[c].state == (CHANNEL_SFX | CHANNEL_PAUSED)) {
#if RETRO_PLATFORM == RETRO_PS2
            AudioDevice::StopADPCM(c);
#endif
            channels[c].soundID = -1;
            channels[c].state   = CHANNEL_IDLE;
        }
    }

    // unload global sfx (do not clear the stream channel 0 slot)
    for (int32 s = 0; s < SFX_COUNT - 2; ++s) {
        if (sfxList[s].scope == SCOPE_GLOBAL) {
            MEM_ZERO(sfxList[s]);
            sfxList[s].scope = SCOPE_NONE;
        }
    }

    UnlockAudioDevice();
}
#endif