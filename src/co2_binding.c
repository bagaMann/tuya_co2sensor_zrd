#include "app_main.h"

/* Standard ZCL On/Off CLIENT commands over Zigbee binding. No server state. */
#define CO2_PPM_MIN_VALID 350
#define CO2_PPM_MAX_VALID 10000

typedef struct __attribute__((packed)) {
    uint8_t enabled;
    uint16_t onThreshold;
    uint16_t offThreshold;
    uint8_t repeatCount;
    uint16_t repeatIntervalMs;
    uint8_t crc;
} co2_bind_nv_t;

static co2_bind_nv_t accepted = {0, 1000, 800, 3, 1500, 0};
static int8_t currentState = -1;
static int32_t lastPpm = 0;
static bool haveSample = FALSE;
static ev_timer_event_t *repeatTimer = NULL;
static uint8_t repeatsLeft = 0;
static uint8_t repeatCmd = ZCL_CMD_ONOFF_OFF;

static uint8_t cfg_crc(const co2_bind_nv_t *cfg)
{
    uint8_t sum = 0;
    const uint8_t *bytes = (const uint8_t *)cfg;
    for (uint8_t i = 0; i < sizeof(co2_bind_nv_t) - 1; i++) {
        sum += bytes[i];
    }
    return sum;
}

static bool cfg_valid(const co2_bind_nv_t *cfg)
{
    return cfg->enabled <= 1 &&
        cfg->offThreshold >= 400 && cfg->onThreshold <= 5000 &&
        cfg->offThreshold < cfg->onThreshold &&
        cfg->repeatCount >= 1 && cfg->repeatCount <= 5 &&
        cfg->repeatIntervalMs >= 500 && cfg->repeatIntervalMs <= 10000;
}

static void cfg_to_attrs(const co2_bind_nv_t *cfg)
{
    g_zcl_co2Attrs.controlEnabled = cfg->enabled;
    g_zcl_co2Attrs.thresholdOn = cfg->onThreshold;
    g_zcl_co2Attrs.thresholdOff = cfg->offThreshold;
    g_zcl_co2Attrs.repeatCount = cfg->repeatCount;
    g_zcl_co2Attrs.repeatIntervalMs = cfg->repeatIntervalMs;
}

static co2_bind_nv_t cfg_from_attrs(void)
{
    co2_bind_nv_t cfg = {0};
    cfg.enabled = g_zcl_co2Attrs.controlEnabled;
    cfg.onThreshold = g_zcl_co2Attrs.thresholdOn;
    cfg.offThreshold = g_zcl_co2Attrs.thresholdOff;
    cfg.repeatCount = g_zcl_co2Attrs.repeatCount;
    cfg.repeatIntervalMs = g_zcl_co2Attrs.repeatIntervalMs;
    cfg.crc = cfg_crc(&cfg);
    return cfg;
}

static void stop_repeat(void)
{
    if (repeatTimer) {
        TL_ZB_TIMER_CANCEL(&repeatTimer);
        repeatTimer = NULL;
    }
    repeatsLeft = 0;
}

/* Addressing follows slacky1965/ts0201_tz3000_zed src/app_on_off.c. */
static void send_to_bindings(uint8_t command)
{
    if (!zb_isDeviceJoinedNwk()) return;
    aps_binding_entry_t *entry = bindTblEntryGet();
    for (uint8_t i = 0; i < APS_BINDING_TABLE_NUM; i++, entry++) {
        if (!entry->used || entry->clusterId != ZCL_CLUSTER_GEN_ON_OFF ||
                entry->srcEp != APP_ENDPOINT1) continue;

        epInfo_t dst;
        TL_SETSTRUCTCONTENT(dst, 0);
        dst.profileId = HA_PROFILE_ID;
        dst.dstAddrMode = entry->dstAddrMode;
        if (dst.dstAddrMode == APS_SHORT_GROUPADDR_NOEP) {
            dst.dstAddr.shortAddr = entry->groupAddr;
        } else {
            dst.dstAddrMode = APS_LONG_DSTADDR_WITHEP;
            dst.dstEp = entry->dstExtAddrInfo.dstEp;
            memcpy(dst.dstAddr.extAddr, entry->dstExtAddrInfo.extAddr, sizeof(extAddr_t));
        }
        if (command == ZCL_CMD_ONOFF_ON)
            zcl_onOff_onCmd(APP_ENDPOINT1, &dst, FALSE);
        else
            zcl_onOff_offCmd(APP_ENDPOINT1, &dst, FALSE);
    }
}

static int32_t repeat_cb(void *arg)
{
    (void)arg;
    if (!accepted.enabled || !zb_isDeviceJoinedNwk() || !repeatsLeft) {
        repeatTimer = NULL;
        repeatsLeft = 0;
        return -1;
    }
    send_to_bindings(repeatCmd);
    if (--repeatsLeft == 0) {
        repeatTimer = NULL;
        return -1;
    }
    return accepted.repeatIntervalMs;
}

static void set_state(uint8_t state)
{
    stop_repeat();
    currentState = (int8_t)state;
    repeatCmd = state ? ZCL_CMD_ONOFF_ON : ZCL_CMD_ONOFF_OFF;
    send_to_bindings(repeatCmd);
    if (accepted.repeatCount > 1) {
        repeatsLeft = accepted.repeatCount - 1;
        repeatTimer = TL_ZB_TIMER_SCHEDULE(repeat_cb, NULL, accepted.repeatIntervalMs);
    }
}

static void evaluate_sample(void)
{
    if (!accepted.enabled || !haveSample) return;
    if (!zb_isDeviceJoinedNwk()) {
        currentState = -1;
        stop_repeat();
        return;
    }
    if (currentState < 0) {
        if (lastPpm >= accepted.onThreshold) set_state(1);
        else if (lastPpm <= accepted.offThreshold) set_state(0);
    } else if (currentState == 0 && lastPpm >= accepted.onThreshold) {
        set_state(1);
    } else if (currentState == 1 && lastPpm <= accepted.offThreshold) {
        set_state(0);
    }
}

void co2_binding_init(void)
{
    co2_bind_nv_t saved;
    if (nv_flashReadNew(1, NV_MODULE_APP, NV_ITEM_APP_USER_CFG,
            sizeof(saved), (uint8_t *)&saved) == NV_SUCC &&
            saved.crc == cfg_crc(&saved) && cfg_valid(&saved)) {
        accepted = saved;
    }
    cfg_to_attrs(&accepted);
}

void co2_binding_on_sample(int32_t ppm)
{
    if (ppm < CO2_PPM_MIN_VALID || ppm > CO2_PPM_MAX_VALID) return;
    lastPpm = ppm;
    haveSample = TRUE;
    evaluate_sample();
}

/* Called from standard ZCL write callback after attributes were updated. */
void co2_binding_config_written(void)
{
    co2_bind_nv_t next = cfg_from_attrs();
    if (!cfg_valid(&next)) {
        cfg_to_attrs(&accepted);
        return;
    }
    if (memcmp(&next, &accepted, sizeof(next)) == 0) return;

    bool resetState = (next.enabled != accepted.enabled ||
                       next.onThreshold != accepted.onThreshold ||
                       next.offThreshold != accepted.offThreshold);
    accepted = next;
    nv_flashWriteNew(1, NV_MODULE_APP, NV_ITEM_APP_USER_CFG,
            sizeof(accepted), (uint8_t *)&accepted);
    stop_repeat();
    if (resetState) currentState = -1;
    evaluate_sample();
}
