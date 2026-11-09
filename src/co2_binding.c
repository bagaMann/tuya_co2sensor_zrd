#include "app_main.h"

/*
 * CO2 threshold On/Off CLIENT binding.
 * Config persistence follows slacky1965/ts0201_tz3000_zed app_config.c:
 * packed NV record, additive checksum, save/restore and default values.
 * Cyclic repeat follows app_on_off.c (60 seconds, configurable on/off).
 */
#define CO2_PPM_MIN_VALID       350
#define CO2_PPM_MAX_VALID     10000
#define CO2_FAST_REPEATS_TOTAL  3
#define CO2_FAST_REPEAT_MS   1500
#define CO2_CYCLIC_REPEAT_MS (60 * 1000)

/*
 * Layout stays 9 bytes for compatibility with v1.0.05 NV records.
 * Old byte 5 was repeat count (1..5), bytes 6..7 repeat interval.
 * New byte 5 is cyclic repeat enable; bytes 6..7 are a format marker.
 */
#define CO2_CFG_FORMAT          0xC206

typedef struct __attribute__((packed)) {
    uint8_t enabled;
    uint16_t onThreshold;
    uint16_t offThreshold;
    uint8_t repeatEnabled;
    uint16_t format;
    uint8_t crc;
} co2_bind_nv_t;

static co2_bind_nv_t config = {0, 1000, 800, 0, CO2_CFG_FORMAT, 0};
static int8_t currentState = -1;  /* -1 unknown, 0 off, 1 on */
static int32_t lastPpm = 0;
static bool haveSample = FALSE;
static ev_timer_event_t *fastTimer = NULL;
static ev_timer_event_t *cyclicTimer = NULL;
static uint8_t fastLeft = 0;
static uint8_t fastCmd = ZCL_CMD_ONOFF_OFF;

static uint8_t co2_config_checksum(const co2_bind_nv_t *cfg)
{
    const uint8_t *data = (const uint8_t *)cfg;
    uint8_t crc8 = 0;
    for (uint8_t i = 0; i < sizeof(co2_bind_nv_t) - 1; i++) {
        crc8 += data[i];
    }
    return crc8;
}

static bool config_valid(const co2_bind_nv_t *cfg)
{
    return cfg->format == CO2_CFG_FORMAT &&
           cfg->enabled <= 1 &&
           cfg->repeatEnabled <= 1 &&
           cfg->offThreshold >= 400 &&
           cfg->onThreshold <= 5000 &&
           cfg->offThreshold < cfg->onThreshold;
}

static void config_to_zcl(void)
{
    g_zcl_co2Attrs.controlEnabled = config.enabled;
    g_zcl_co2Attrs.thresholdOn = config.onThreshold;
    g_zcl_co2Attrs.thresholdOff = config.offThreshold;
    g_zcl_co2Attrs.repeatEnabled = config.repeatEnabled;
}

/* Direct readback makes NV write failures diagnosable. */
static nv_sts_t config_save(void)
{
#if NV_ENABLE
    co2_bind_nv_t verify;
    config.crc = co2_config_checksum(&config);
    nv_sts_t st = nv_flashWriteNew(1, NV_MODULE_APP, NV_ITEM_APP_USER_CFG,
                                  sizeof(config), (uint8_t *)&config);
    if (st == NV_SUCC) {
        st = nv_flashReadNew(1, NV_MODULE_APP, NV_ITEM_APP_USER_CFG,
                             sizeof(verify), (uint8_t *)&verify);
        if (st == NV_SUCC && memcmp(&verify, &config, sizeof(config)) != 0) {
            st = NV_CHECK_SUM_ERROR;
        }
    }
#if UART_PRINTF_MODE && DEBUG_SAVE
    printf("CO2 config save: status=%d, on=%d, off=%d, enable=%d, repeat=%d\r\n",
           st, config.onThreshold, config.offThreshold,
           config.enabled, config.repeatEnabled);
#endif
    return st;
#else
    return NV_ENABLE_PROTECT_ERROR;
#endif
}

static void config_restore(void)
{
#if NV_ENABLE
    co2_bind_nv_t saved;
    nv_sts_t st = nv_flashReadNew(1, NV_MODULE_APP, NV_ITEM_APP_USER_CFG,
                                  sizeof(saved), (uint8_t *)&saved);
    if (st == NV_SUCC && saved.crc == co2_config_checksum(&saved)) {
        if (config_valid(&saved)) {
            config = saved;
        } else if (saved.format >= 500 && saved.format <= 10000 &&
                   saved.repeatEnabled >= 1 && saved.repeatEnabled <= 5 &&
                   saved.enabled <= 1 &&
                   saved.offThreshold >= 400 && saved.onThreshold <= 5000 &&
                   saved.offThreshold < saved.onThreshold) {
            /* Preserve thresholds/settings written by firmware 1.0.05. */
            config.enabled = saved.enabled;
            config.onThreshold = saved.onThreshold;
            config.offThreshold = saved.offThreshold;
            config.repeatEnabled = 0; /* new cyclic feature is opt-in */
            config.format = CO2_CFG_FORMAT;
            config_save();
        }
    }
#if UART_PRINTF_MODE && DEBUG_SAVE
    printf("CO2 config restore: on=%d, off=%d, enable=%d, repeat=%d\r\n",
           config.onThreshold, config.offThreshold,
           config.enabled, config.repeatEnabled);
#endif
#endif
    config_to_zcl();
}

static void stop_fast(void)
{
    if (fastTimer) {
        TL_ZB_TIMER_CANCEL(&fastTimer);
        fastTimer = NULL;
    }
    fastLeft = 0;
}

static void stop_cyclic(void)
{
    if (cyclicTimer) {
        TL_ZB_TIMER_CANCEL(&cyclicTimer);
        cyclicTimer = NULL;
    }
}

/* Same APS binding table addressing as Slacky src/app_on_off.c. */
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

static int32_t fast_repeat_cb(void *arg)
{
    (void)arg;
    if (!config.enabled || !zb_isDeviceJoinedNwk() || fastLeft == 0) {
        fastTimer = NULL;
        fastLeft = 0;
        return -1;
    }
    send_to_bindings(fastCmd);
    if (--fastLeft == 0) {
        fastTimer = NULL;
        return -1;
    }
    return CO2_FAST_REPEAT_MS;
}

/* Slacky style: repeat last absolute On or Off every 60 seconds. */
static int32_t cyclic_repeat_cb(void *arg)
{
    (void)arg;
    if (!config.enabled || !config.repeatEnabled ||
        !zb_isDeviceJoinedNwk() || currentState < 0) {
        cyclicTimer = NULL;
        return -1;
    }
    send_to_bindings(currentState ? ZCL_CMD_ONOFF_ON : ZCL_CMD_ONOFF_OFF);
    return 0; /* keep original 60-second SDK timer period */
}

static void start_cyclic(void)
{
    stop_cyclic();
    if (config.enabled && config.repeatEnabled && currentState >= 0 &&
        zb_isDeviceJoinedNwk()) {
        cyclicTimer = TL_ZB_TIMER_SCHEDULE(cyclic_repeat_cb, NULL, CO2_CYCLIC_REPEAT_MS);
    }
}

static void set_state(uint8_t state)
{
    stop_fast();
    currentState = (int8_t)state;
    fastCmd = state ? ZCL_CMD_ONOFF_ON : ZCL_CMD_ONOFF_OFF;
    send_to_bindings(fastCmd);

    /* Existing working 3-command burst remains fixed and unchanged. */
    fastLeft = CO2_FAST_REPEATS_TOTAL - 1;
    fastTimer = TL_ZB_TIMER_SCHEDULE(fast_repeat_cb, NULL, CO2_FAST_REPEAT_MS);
    start_cyclic();
}

static void evaluate_sample(void)
{
    if (!config.enabled || !haveSample) return;
    if (!zb_isDeviceJoinedNwk()) {
        currentState = -1;
        stop_fast();
        stop_cyclic();
        return;
    }
    if (currentState < 0) {
        if (lastPpm >= config.onThreshold) set_state(1);
        else if (lastPpm <= config.offThreshold) set_state(0);
    } else if (currentState == 0 && lastPpm >= config.onThreshold) {
        set_state(1);
    } else if (currentState == 1 && lastPpm <= config.offThreshold) {
        set_state(0);
    }
}

void co2_binding_init(void)
{
    config_restore();
}

void co2_binding_on_sample(int32_t ppm)
{
    if (ppm < CO2_PPM_MIN_VALID || ppm > CO2_PPM_MAX_VALID) return;
    lastPpm = ppm;
    haveSample = TRUE;
    evaluate_sample();
}

void co2_binding_config_written(void)
{
    co2_bind_nv_t next = config;
    next.enabled = g_zcl_co2Attrs.controlEnabled;
    next.onThreshold = g_zcl_co2Attrs.thresholdOn;
    next.offThreshold = g_zcl_co2Attrs.thresholdOff;
    next.repeatEnabled = g_zcl_co2Attrs.repeatEnabled;
    next.format = CO2_CFG_FORMAT;

    if (!config_valid(&next)) {
        config_to_zcl();
        return;
    }

    /* Compare only configuration fields (CRC is calculated in config_save). */
    if (next.enabled == config.enabled &&
        next.onThreshold == config.onThreshold &&
        next.offThreshold == config.offThreshold &&
        next.repeatEnabled == config.repeatEnabled) return;

    bool resetState = (next.enabled != config.enabled ||
                       next.onThreshold != config.onThreshold ||
                       next.offThreshold != config.offThreshold);
    bool repeatChanged = next.repeatEnabled != config.repeatEnabled;

    config = next;
    config_save();
    config_to_zcl();

    if (resetState) {
        stop_fast();
        stop_cyclic();
        currentState = -1;
        evaluate_sample();
    } else if (repeatChanged) {
        start_cyclic();
    }
}
