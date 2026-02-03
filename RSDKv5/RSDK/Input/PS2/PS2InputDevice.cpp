#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <stdio.h>

using namespace RSDK;

// buffer for pad data, aligned to 64 bytes as required by ps2 pad library
static char padBuffer[4][256] __attribute__((aligned(64)));
static int padInitialized = 0;
// tracks which pads are ready for input
static int padReady[4] = {0, 0, 0, 0};

// stores button states for each player
static uint32 lastButtons[PLAYER_COUNT] = {0};
static uint32 currentButtons[PLAYER_COUNT] = {0};

// maps generic button indices to ps2 pad button constants
static const int32 buttonMap[12] = {
    PAD_UP,        // 0: directional up
    PAD_DOWN,      // 1: directional down
    PAD_LEFT,      // 2: directional left
    PAD_RIGHT,     // 3: directional right
    PAD_CROSS,     // 4: action button a (cross)
    PAD_CIRCLE,    // 5: action button b (circle)
    0,             // 6: action button c (unmapped)
    PAD_SQUARE,    // 7: action button x (square)
    PAD_TRIANGLE,  // 8: action button y (triangle)
    0,             // 9: action button z (unmapped)
    PAD_START,     // 10: start button
    PAD_SELECT     // 11: select button
};

void RSDK::SKU::InputDevicePS2::UpdateInput()
{
    // get port number from controller id
    int32 port = this->controllerID - CONT_P1;
    if (port < 0 || port >= 4) {
        return;
    }
    
    int ret;
    struct padButtonStatus buttons;
    
    // check if pad is ready, if not try to initialize it
    if (!padReady[port]) {
        int state = padGetState(port, 0);
        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
            padReady[port] = 1;
        } else {
            return;
        }
    }
    
    // read button state from controller
    ret = padRead(port, 0, &buttons);
    
    if (ret != 0) {
        // invert bits (ps2 pad uses 0 for pressed, 1 for released)
        currentButtons[port] = 0xffff ^ buttons.btns;
        
        // indicate that some input was detected
        this->anyPress = 1;
    } else {
        // no valid data, clear buttons
        currentButtons[port] = 0;
        this->anyPress = 1;
    }
}

void RSDK::SKU::InputDevicePS2::ProcessInput(int32 controllerID)
{
    // validate controller id range
    if (controllerID < CONT_P1 || controllerID > CONT_P4) {
        return;
    }
    
    // convert controller id to array index
    int32 playerIndex = controllerID - CONT_P1;
    if (playerIndex < 0 || playerIndex >= PLAYER_COUNT) {
        return;
    }
    
    // get current and previous button states
    uint32 current = currentButtons[playerIndex];
    uint32 last = lastButtons[playerIndex];
    uint32 pressed = current & ~last; // newly pressed buttons this frame
    
    // setup indices for controller mapping
    int indices[2];
    int indexCount;
    
    if (playerIndex == 0) {
        // player 1 also updates CONT_ANY for backwards compatibility
        indices[0] = CONT_ANY;
        indices[1] = controllerID;
        indexCount = 2;
    } else {
        // other players only update their own controller
        indices[0] = controllerID;
        indexCount = 1;
    }
    
    // update all relevant controller indices
    for (int i = 0; i < indexCount; i++) {
        int idx = indices[i];
        
        // directional up
        controller[idx].keyUp.down = (current & buttonMap[0]) ? true : false;
        controller[idx].keyUp.press = (pressed & buttonMap[0]) ? true : false;
        
        // directional down
        controller[idx].keyDown.down = (current & buttonMap[1]) ? true : false;
        controller[idx].keyDown.press = (pressed & buttonMap[1]) ? true : false;
        
        // directional left
        controller[idx].keyLeft.down = (current & buttonMap[2]) ? true : false;
        controller[idx].keyLeft.press = (pressed & buttonMap[2]) ? true : false;
        
        // directional right
        controller[idx].keyRight.down = (current & buttonMap[3]) ? true : false;
        controller[idx].keyRight.press = (pressed & buttonMap[3]) ? true : false;
        
        // action button a (cross)
        controller[idx].keyA.down = (current & buttonMap[4]) ? true : false;
        controller[idx].keyA.press = (pressed & buttonMap[4]) ? true : false;
        
        // action button b (circle)
        controller[idx].keyB.down = (current & buttonMap[5]) ? true : false;
        controller[idx].keyB.press = (pressed & buttonMap[5]) ? true : false;
        
        // action button c (unmapped)
        controller[idx].keyC.down = (current & buttonMap[6]) ? true : false;
        controller[idx].keyC.press = (pressed & buttonMap[6]) ? true : false;
        
        // action button x (square)
        controller[idx].keyX.down = (current & buttonMap[7]) ? true : false;
        controller[idx].keyX.press = (pressed & buttonMap[7]) ? true : false;
        
        // action button y (triangle)
        controller[idx].keyY.down = (current & buttonMap[8]) ? true : false;
        controller[idx].keyY.press = (pressed & buttonMap[8]) ? true : false;
        
        // action button z (unmapped)
        controller[idx].keyZ.down = (current & buttonMap[9]) ? true : false;
        controller[idx].keyZ.press = (pressed & buttonMap[9]) ? true : false;
        
        // start button
        controller[idx].keyStart.down = (current & buttonMap[10]) ? true : false;
        controller[idx].keyStart.press = (pressed & buttonMap[10]) ? true : false;
        
        // select button
        controller[idx].keySelect.down = (current & buttonMap[11]) ? true : false;
        controller[idx].keySelect.press = (pressed & buttonMap[11]) ? true : false;
    }
    
    // save current state for next frame
    lastButtons[playerIndex] = current;
    
    // debug output (currently disabled)
    if (playerIndex == 0) {
        static int debugCount = 0;
        debugCount++;
        if (debugCount % 60 == 0) {
            // debug code would go here
        }
    }
}

RSDK::SKU::InputDevicePS2 *RSDK::SKU::InitPS2Device(uint32 id, int32 port) {
    // check if we can add more devices
    if (inputDeviceCount >= INPUTDEVICE_COUNT) {
        return NULL;
    }

    // clean up existing device if present
    if (inputDeviceList[inputDeviceCount])
        delete inputDeviceList[inputDeviceCount];

    // initialize pad library on first call
    if (!padInitialized) {
        sceSifInitRpc(0);
        
        // load sio2man module (handles controller i/o)
        int ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
        if (ret < 0) {
            return NULL;
        }
        
        // load padman module (controller manager)
        ret = SifLoadModule("rom0:PADMAN", 0, NULL);
        if (ret < 0) {
            return NULL;
        }
        
        // initialize pad library
        padInit(0);
        padInitialized = 1;
    }
    
    // open the controller port
    int ret = padPortOpen(port, 0, padBuffer[port]);
    if (ret == 0) {
        return NULL;
    }

    // create new input device
    inputDeviceList[inputDeviceCount] = new RSDK::SKU::InputDevicePS2();

    RSDK::SKU::InputDevicePS2 *device = (RSDK::SKU::InputDevicePS2*)inputDeviceList[inputDeviceCount];
    // set device type (using ps4 constant for compatibility)
    device->gamepadType = (DEVICE_API_NONE << 16) | (DEVICE_TYPE_CONTROLLER << 8) | (DEVICE_PS4 << 0);
    device->disabled = false;
    device->id = id;
    device->active = true;
    device->anyPress = 1;
    device->isAssigned = false;
    device->controllerID = CONT_P1 + port; // assign controller id based on port

    inputDeviceCount++;
    
    return device;
}

void RSDK::SKU::InitPS2InputAPI() {
    // reset device count and pad state
    inputDeviceCount = 0;
    padInitialized = 0;
    
    // clear all button states
    for (int i = 0; i < PLAYER_COUNT; i++) {
        lastButtons[i] = 0;
        currentButtons[i] = 0;
        padReady[i] = 0;
    }

    // initialize all 4 controller ports
    for (int port = 0; port < 4; port++) {
        // generate unique id for each controller
        uint32 id = 1 + port;
        char deviceName[32];
        sprintf(deviceName, "PS2Device%d", port);
        GenerateHashCRC(&id, deviceName);
        
        // attempt to initialize device for this port
        InputDevicePS2* device = InitPS2Device(id, port);
        if (device) {
            // device initialized successfully
        } else {
            // device initialization failed
        }
    }
}