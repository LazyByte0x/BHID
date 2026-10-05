// Author: jocover @ https://github.com/jocover
// Repository: https://github.com/GoldHEN/GoldHEN_Plugins_Repository
// Twin USB adaptation: fixed DragonRise 0x0810:0x0001 input path.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "plugin_common.h"
#include "Common.h"
#include <Patcher.h>
#include "pad.h"
#include "usb_hid.h"

attr_public const char* g_pluginName = "gamepad_helper";
attr_public const char* g_pluginDesc = "DragonRise Twin USB HID helper";
attr_public const char* g_pluginAuth = "jocover + Twin USB mapping";
attr_public u32 g_pluginVersion = 0x00000210; // 2.10

HOOK_INIT(scePadOpen);
HOOK_INIT(scePadClose);
HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);
HOOK_INIT(scePadSetVibration);

static Patcher* scePadReadExtPatcher = NULL;
static Patcher* scePadReadStateExtPatcher = NULL;

static struct twin_usb_device g_twinUsb;
static volatile bool g_twinUsbReady = false;

static int32_t g_logged_handles[2] = {
    -1,
    -1
};

#define JOY_CENTER_POS 0x80
#define DEADZONE_LEFT  0x0D
#define DEADZONE_RIGHT 0x0D

/* =======================================================
 * Twin handle bookkeeping
 * ======================================================= */

static void remember_player_handle(
    int32_t handle,
    int32_t index)
{
    if (handle < 0 ||
        index < 0 ||
        index > 1) {
        return;
    }

    usb_hid_bind_handle(
        handle,
        index
    );

    if (g_logged_handles[index] != handle) {
        final_printf(
            "[TwinUSB] controller index %d -> handle %d\n",
            index,
            handle
        );

        g_logged_handles[index] = handle;
    }
}

/* =======================================================
 * scePadOpen
 * ======================================================= */

static int32_t scePadOpen_hook(
    int32_t userId,
    int32_t type,
    int32_t index,
    void* param)
{
    int32_t handle = HOOK_CONTINUE(
        scePadOpen,
        int (*)(int32_t, int32_t, int32_t, void*),
        userId,
        type,
        index,
        param
    );

    /*
     * Fixed Twin routing:
     *
     * pad index 0 -> Twin Player 1
     * pad index 1 -> Twin Player 2
     *
     * Only bind successful handles for these two slots.
     */
    if (g_twinUsbReady &&
        handle >= 0 &&
        index >= 0 &&
        index <= 1) {

        remember_player_handle(
            handle,
            index
        );
    }

    return handle;
}

/* =======================================================
 * scePadClose
 * ======================================================= */

static int32_t scePadClose_hook(
    int32_t handle)
{
    if (g_twinUsbReady &&
        usb_hid_is_twin_handle(handle)) {

        final_printf(
            "[TwinUSB] closing handle %d\n",
            handle
        );

        usb_hid_unbind_handle(handle);

        for (int i = 0; i < 2; ++i) {
            if (g_logged_handles[i] == handle)
                g_logged_handles[i] = -1;
        }
    }

    return HOOK_CONTINUE(
        scePadClose,
        int (*)(int32_t),
        handle
    );
}

/* =======================================================
 * Deadzone
 * ======================================================= */

static uint8_t check_deadzone(
    uint8_t input,
    uint8_t deadZone)
{
    if (abs(
            (int)input -
            JOY_CENTER_POS) <= (int)deadZone) {

        return JOY_CENTER_POS;
    }

    return input;
}

static void deadzone_apply(
    ScePadData* pData)
{
    if (!pData)
        return;

    pData->leftStick.x =
        check_deadzone(
            pData->leftStick.x,
            DEADZONE_LEFT
        );

    pData->leftStick.y =
        check_deadzone(
            pData->leftStick.y,
            DEADZONE_LEFT
        );

    pData->rightStick.x =
        check_deadzone(
            pData->rightStick.x,
            DEADZONE_RIGHT
        );

    pData->rightStick.y =
        check_deadzone(
            pData->rightStick.y,
            DEADZONE_RIGHT
        );
}

/* =======================================================
 * scePadRead
 * ======================================================= */

static int32_t scePadRead_hook(
    int32_t handle,
    ScePadData* pData,
    int32_t num)
{
    if (!pData || num <= 0)
        return 0;

    /*
     * Only explicitly bound Twin handles use the USB path.
     */
    if (g_twinUsbReady &&
        usb_hid_is_twin_handle(handle)) {

        for (int32_t i = 0;
             i < num;
             ++i) {

            int ret = usb_hid_get_pad(
                handle,
                &pData[i]
            );

            if (ret < 0)
                return ret;

            /*
             * Deadzone applies only to Twin USB data.
             */
            deadzone_apply(
                &pData[i]
            );
        }

        return num;
    }

    /*
     * Normal controller:
     * preserve the original PS4 path.
     */
    return scePadReadExt(
        handle,
        pData,
        num
    );
}

/* =======================================================
 * scePadReadState
 * ======================================================= */

static int32_t scePadReadState_hook(
    int32_t handle,
    ScePadData* pData)
{
    if (!pData)
        return -1;

    /*
     * Twin handle -> cached USB HID state.
     */
    if (g_twinUsbReady &&
        usb_hid_is_twin_handle(handle)) {

        int ret = usb_hid_get_pad(
            handle,
            pData
        );

        if (ret == 0)
            deadzone_apply(pData);

        return ret;
    }

    /*
     * Normal controller:
     * preserve the original PS4 behavior.
     */
    return scePadReadStateExt(
        handle,
        pData
    );
}

/* =======================================================
 * scePadSetVibration
 * ======================================================= */

static int32_t scePadSetVibration_hook(
    int32_t handle,
    const ScePadVibrationParam* pParam)
{
    if (!pParam)
        return -1;

    /*
     * The Twin adapter has no usable PS4 rumble output path.
     * Suppress vibration only for Twin-backed handles.
     */
    if (g_twinUsbReady &&
        usb_hid_is_twin_handle(handle)) {

        return 0;
    }

    /*
     * All other controllers use the original path.
     */
    return HOOK_CONTINUE(
        scePadSetVibration,
        int (*)(int32_t,
                const ScePadVibrationParam*),
        handle,
        pParam
    );
}

/* =======================================================
 * module_start
 * ======================================================= */

s32 attr_module_hidden module_start(
    s64 argc,
    const void* args)
{
    (void)argc;
    (void)args;

    final_printf(
        "[GoldHEN] <%s\\Ver.0x%08x> %s\n",
        g_pluginName,
        g_pluginVersion,
        __func__
    );

    final_printf(
        "[GoldHEN] Plugin Author(s): %s\n",
        g_pluginAuth
    );

    /*
     * Ensure libScePad is loaded before resolving
     * and hooking scePad functions.
     */
    char module[256];

    snprintf(
        module,
        sizeof(module),
        "/%s/common/lib/%s",
        sceKernelGetFsSandboxRandomWord(),
        "libScePad.sprx"
    );

    int h = 0;

    if (sys_dynlib_load_prx(
            module,
            &h) < 0) {

        final_printf(
            "[TwinUSB] failed to load libScePad.sprx\n"
        );

        return -1;
    }

    /* ---------------------------------------------------
     * Initial state
     * --------------------------------------------------- */

    g_twinUsbReady = false;

    g_logged_handles[0] = -1;
    g_logged_handles[1] = -1;

    memset(
        &g_twinUsb,
        0,
        sizeof(g_twinUsb)
    );

    /* ---------------------------------------------------
     * Allocate patchers
     * --------------------------------------------------- */

    scePadReadExtPatcher =
        (Patcher*)malloc(
            sizeof(Patcher)
        );

    scePadReadStateExtPatcher =
        (Patcher*)malloc(
            sizeof(Patcher)
        );

    if (!scePadReadExtPatcher ||
        !scePadReadStateExtPatcher) {

        free(scePadReadExtPatcher);
        free(scePadReadStateExtPatcher);

        scePadReadExtPatcher = NULL;
        scePadReadStateExtPatcher = NULL;

        final_printf(
            "[TwinUSB] failed to allocate patchers\n"
        );

        return -1;
    }

    Patcher_Construct(
        scePadReadExtPatcher
    );

    Patcher_Construct(
        scePadReadStateExtPatcher
    );

    /* ---------------------------------------------------
     * Original jocover patches
     * --------------------------------------------------- */

    uint8_t xor_ecx_ecx[5] = {
        0x31, 0xC9, 0x90, 0x90, 0x90
    };

    Patcher_Install_Patch(
        scePadReadExtPatcher,
        (uint64_t)scePadReadExt,
        xor_ecx_ecx,
        sizeof(xor_ecx_ecx)
    );

    uint8_t xor_edx_edx[5] = {
        0x31, 0xD2, 0x90, 0x90, 0x90
    };

    Patcher_Install_Patch(
        scePadReadStateExtPatcher,
        (uint64_t)scePadReadStateExt,
        xor_edx_edx,
        sizeof(xor_edx_edx)
    );

    /* ---------------------------------------------------
     * Required hooks
     * --------------------------------------------------- */

    HOOK32(scePadOpen);
    HOOK32(scePadClose);
    HOOK32(scePadRead);
    HOOK32(scePadReadState);
    HOOK32(scePadSetVibration);

    /* ---------------------------------------------------
     * Initialize direct Twin USB HID
     * --------------------------------------------------- */

    if (usb_hid_init(
            &g_twinUsb) == 0) {

        g_twinUsbReady = true;

        final_printf(
            "[TwinUSB] plugin initialized successfully\n"
        );
    } else {
        /*
         * USB initialization failure must not break
         * normal PS4 controller handling.
         */
        g_twinUsbReady = false;

        final_printf(
            "[TwinUSB] device not initialized; "
            "using normal scePad path\n"
        );
    }

    return 0;
}

/* =======================================================
 * module_stop
 * ======================================================= */

s32 attr_module_hidden module_stop(
    s64 argc,
    const void* args)
{
    (void)argc;
    (void)args;

    final_printf(
        "[GoldHEN] <%s\\Ver.0x%08x> %s\n",
        g_pluginName,
        g_pluginVersion,
        __func__
    );

    /*
     * Stop Twin USB processing before removing hooks.
     */
    if (g_twinUsbReady) {
        g_twinUsbReady = false;

        usb_hid_exit(
            &g_twinUsb
        );
    }

    /*
     * Reset local logging state.
     */
    g_logged_handles[0] = -1;
    g_logged_handles[1] = -1;

    /* ---------------------------------------------------
     * Remove hooks
     * --------------------------------------------------- */

    UNHOOK(scePadOpen);
    UNHOOK(scePadClose);
    UNHOOK(scePadRead);
    UNHOOK(scePadReadState);
    UNHOOK(scePadSetVibration);

    /* ---------------------------------------------------
     * Restore / destroy patches
     * --------------------------------------------------- */

    if (scePadReadExtPatcher) {
        Patcher_Destroy(
            scePadReadExtPatcher
        );

        free(
            scePadReadExtPatcher
        );

        scePadReadExtPatcher = NULL;
    }

    if (scePadReadStateExtPatcher) {
        Patcher_Destroy(
            scePadReadStateExtPatcher
        );

        free(
            scePadReadStateExtPatcher
        );

        scePadReadStateExtPatcher = NULL;
    }

    return 0;
}