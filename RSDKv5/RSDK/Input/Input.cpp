#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

InputDevice *RSDK::inputDeviceList[INPUTDEVICE_COUNT];
int32 RSDK::inputDeviceCount = 0;

int32 RSDK::inputSlots[PLAYER_COUNT]              = { INPUT_NONE, INPUT_NONE, INPUT_NONE, INPUT_NONE };
InputDevice *RSDK::inputSlotDevices[PLAYER_COUNT] = { NULL, NULL, NULL, NULL };


ControllerState RSDK::controller[PLAYER_COUNT + 1];
AnalogState RSDK::stickL[PLAYER_COUNT + 1];
#if RETRO_REV02
AnalogState RSDK::stickR[PLAYER_COUNT + 1];
TriggerState RSDK::triggerL[PLAYER_COUNT + 1];
TriggerState RSDK::triggerR[PLAYER_COUNT + 1];
#endif
TouchInfo RSDK::touchInfo;


GamePadMappings *RSDK::gamePadMappings = NULL;
int32 RSDK::gamePadCount               = 0;

#if RETRO_INPUTDEVICE_KEYBOARD
#include "Keyboard/KBInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_XINPUT
#include "XInput/XInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_RAWINPUT
#include "RawInput/RawInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_STEAM
#include "Steam/SteamInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_NX
#include "NX/NXInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_SDL2
#include "SDL2/SDL2InputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_GLFW
#include "GLFW/GLFWInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_PDBOAT
#include "Paddleboat/PDBInputDevice.cpp"
#endif

#if RETRO_INPUTDEVICE_PS2
#include "PS2/PS2InputDevice.cpp"
#endif

void RSDK::RemoveInputDevice(InputDevice *targetDevice)
{
    if (targetDevice) {
        for (int32 d = 0; d < inputDeviceCount; ++d) {
            if (inputDeviceList[d] && inputDeviceList[d] == targetDevice) {
                uint32 deviceID = targetDevice->id;
                targetDevice->CloseDevice();
                inputDeviceCount--;

                delete inputDeviceList[d];
                inputDeviceList[d] = NULL;

                for (int32 id = d + 1; id <= inputDeviceCount && id < INPUTDEVICE_COUNT; ++id) inputDeviceList[id - 1] = inputDeviceList[id];
                
                if (inputDeviceCount < INPUTDEVICE_COUNT)
                    inputDeviceList[inputDeviceCount] = NULL;

                for (int32 id = 0; id < PLAYER_COUNT; ++id) {
                    if (inputSlots[id] == deviceID) {
#if !RETRO_REV02
                        inputSlots[id] = INPUT_NONE;
#endif
                        inputSlotDevices[id] = NULL;
                    }
                }

                for (int32 id = 0; id < PLAYER_COUNT; ++id) {
                    for (int32 c = 0; c < inputDeviceCount; ++c) {
                        if (inputDeviceList[c] && inputDeviceList[c]->id == inputSlots[id]) {
                            if (inputSlotDevices[id] != inputDeviceList[c])
                                inputSlotDevices[id] = inputDeviceList[c];
                        }
                    }
                }
            }
        }
    }
}

void RSDK::InitInputDevices()
{
#if !RETRO_USE_ORIGINAL_CODE
    
    
    
    for (int32 i = 0; i < PLAYER_COUNT; ++i) inputSlots[i] = INPUT_AUTOASSIGN;
#endif

#if RETRO_INPUTDEVICE_KEYBOARD
    SKU::InitKeyboardInputAPI();
#endif

#if RETRO_INPUTDEVICE_RAWINPUT
    SKU::InitHIDAPI();
#endif

#if RETRO_INPUTDEVICE_XINPUT
    SKU::InitXInputAPI();
#endif

#if RETRO_INPUTDEVICE_STEAM
    SKU::InitSteamInputAPI();
#endif

#if RETRO_INPUTDEVICE_NX
    SKU::InitNXInputAPI();
#endif

#if RETRO_INPUTDEVICE_SDL2
    SKU::InitSDL2InputAPI();
#endif

#if RETRO_INPUTDEVICE_GLFW
    SKU::InitGLFWInputAPI();
#endif

#if RETRO_INPUTDEVICE_PDBOAT
    SKU::InitPaddleboatInputAPI();
#endif

#if RETRO_INPUTDEVICE_PS2
    SKU::InitPS2InputAPI();
#endif
}




void RSDK::ReleaseInputDevices()
{
#if RETRO_INPUTDEVICE_SDL2
    SKU::ReleaseSDL2InputAPI();
#endif
}



void RSDK::ProcessInput()
{
    
    bool32 anyPress = false;
    for (int32 i = 0; i < inputDeviceCount; ++i) {
        if (inputDeviceList[i]) {
            inputDeviceList[i]->UpdateInput();
            anyPress |= inputDeviceList[i]->anyPress;
        }
    }

#if RETRO_REV02
    if (anyPress || touchInfo.count)
        videoSettings.dimTimer = 0;
    else if (videoSettings.dimTimer < videoSettings.dimLimit)
        ++videoSettings.dimTimer;
#endif
    
    for (int32 i = 0; i < PLAYER_COUNT; ++i) {
        int32 assign = inputSlots[i];
        if (assign && assign != INPUT_UNASSIGNED) {
            if (assign == INPUT_AUTOASSIGN) {
                int32 id = GetAvaliableInputDevice();
                inputSlots[i] = id;
                if (id != INPUT_AUTOASSIGN)
                    AssignInputSlotToDevice(CONT_P1 + i, id);
            }
            else {
                InputDevice *device = inputSlotDevices[i];
                if (device && device->id == assign && device->active)
                    device->ProcessInput(CONT_P1 + i);
            }
        }
    }

#if !RETRO_REV02 && RETRO_INPUTDEVICE_KEYBOARD
    RSDK::SKU::HandleSpecialKeys();
#endif

    
    
}

void RSDK::ClearInput()
{
    
    for (int32 i = 0; i <= PLAYER_COUNT; ++i) {
        if (i != 0 && inputSlots[i - 1] == INPUT_UNASSIGNED)
            continue;

        controller[i].keyUp.press     = false;
        controller[i].keyDown.press   = false;
        controller[i].keyLeft.press   = false;
        controller[i].keyRight.press  = false;
        controller[i].keyA.press      = false;
        controller[i].keyB.press      = false;
        controller[i].keyC.press      = false;
        controller[i].keyX.press      = false;
        controller[i].keyY.press      = false;
        controller[i].keyZ.press      = false;
        controller[i].keyStart.press  = false;
        controller[i].keySelect.press = false;

        stickL[i].keyUp.press    = false;
        stickL[i].keyDown.press  = false;
        stickL[i].keyLeft.press  = false;
        stickL[i].keyRight.press = false;

#if RETRO_REV02
        stickL[i].keyStick.press = false;

        stickR[i].keyUp.press    = false;
        stickR[i].keyDown.press  = false;
        stickR[i].keyLeft.press  = false;
        stickR[i].keyRight.press = false;
        stickR[i].keyStick.press = false;

        triggerL[i].keyBumper.press  = false;
        triggerL[i].keyTrigger.press = false;

        triggerR[i].keyBumper.press  = false;
        triggerR[i].keyTrigger.press = false;
#else
        controller[i].keyStickL.press = false;
        controller[i].keyStickR.press = false;

        controller[i].keyBumperL.press  = false;
        controller[i].keyTriggerL.press = false;

        controller[i].keyBumperR.press  = false;
        controller[i].keyTriggerR.press = false;
#endif
    }
}



void RSDK::ProcessInputDevices()
{
#if RETRO_INPUTDEVICE_NX
    SKU::ProcessNXInputDevices();
#endif
#if RETRO_INPUTDEVICE_PDBOAT
    SKU::ProcessPaddleboatInputDevices();
#endif
}

int32 RSDK::GetInputDeviceType(uint32 deviceID)
{
    for (int32 i = 0; i < inputDeviceCount; ++i) {
        if (inputDeviceList[i] && inputDeviceList[i]->id == deviceID)
            return inputDeviceList[i]->gamepadType;
    }

#if RETRO_REV02
    return SKU::userCore->GetDefaultGamepadType();
#else
    int32 platform = gameVerInfo.platform;

    switch (platform) {

#if RETRO_INPUTDEVICE_NX
        return currentNXControllerType;
#else
        return (DEVICE_API_NONE << 16) | (DEVICE_TYPE_CONTROLLER << 8) | (DEVICE_SWITCH_HANDHELD << 0);
#endif

        default:
        case PLATFORM_PS4:
        case PLATFORM_XB1:
        case PLATFORM_PC:
        case PLATFORM_DEV: return (DEVICE_API_NONE << 16) | (DEVICE_TYPE_CONTROLLER << 8) | (0 << 0); break;
    }
#endif
}

