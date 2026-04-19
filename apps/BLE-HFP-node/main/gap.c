#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "gap.h"
#include "audio.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_hf_client_api.h"

#define BT_HF_TAG "BT_HF"

static esp_bd_addr_t peer_addr;
static char peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
static uint8_t peer_bdname_len;
static bool target_found = false;
static bool discovery_active = false;
static bool hf_connecting = false;
static bool hf_connected = false;
static bool call_active = false;
static bool ring_active = false;
static bool audio_active = false;
static esp_hf_call_setup_status_t call_setup_state = ESP_HF_CALL_SETUP_STATUS_IDLE;
static char caller_number[ESP_BT_HF_CLIENT_NUMBER_LEN + 1];

static bool parse_ssp_passkey(const char *passkey_str, uint32_t *passkey_out)
{
    if (!passkey_str || !passkey_out || *passkey_str == '\0') {
        return false;
    }

    for (const char *p = passkey_str; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }

    char *end = NULL;
    unsigned long value = strtoul(passkey_str, &end, 10);
    if (*end != '\0' || value > 999999UL) {
        return false;
    }

    *passkey_out = (uint32_t)value;
    return true;
}

static bool get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len)
{
    if (!eir) {
        return false;
    }

    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }
        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static bool connect_target_phone(void)
{
    if (!target_found || hf_connected || hf_connecting) {
        return false;
    }

    esp_err_t err = esp_hf_client_connect(peer_addr);
    if (err != ESP_OK) {
        ESP_LOGW(BT_HF_TAG, "esp_hf_client_connect failed: %s", esp_err_to_name(err));
        return false;
    }

    hf_connecting = true;
    ESP_LOGI(BT_HF_TAG, "Attempting HF connection to target phone");
    return true;
}

static void start_discovery_if_needed(void)
{
    if (discovery_active || hf_connected || hf_connecting) {
        return;
    }

    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    if (err != ESP_OK) {
        ESP_LOGW(BT_HF_TAG, "esp_bt_gap_start_discovery failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(BT_HF_TAG, "Starting discovery for: %s", REMOTE_DEVICE_NAME);
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (!param) {
        ESP_LOGE(BT_HF_TAG, "bt_gap_cb called with NULL param");
        return;
    }

    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        // Handle each discovery result and connect when the configured target name is found.
        if (hf_connected || hf_connecting) {
            break;
        }

        for (int i = 0; i < param->disc_res.num_prop; i++) {
            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR
                && get_name_from_eir(param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len)) {
                if (strcmp(peer_bdname, REMOTE_DEVICE_NAME) == 0) {
                    memcpy(peer_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
                    ESP_LOGI(BT_HF_TAG, "Found target device: %s", peer_bdname);
                    ESP_LOG_BUFFER_HEX(BT_HF_TAG, peer_addr, ESP_BD_ADDR_LEN);
                    target_found = true;
                    connect_target_phone();
                    esp_bt_gap_cancel_discovery();
                    break;
                }
            }
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        // Track inquiry lifecycle and keep discovery/reconnect loop running when not connected.
        discovery_active = (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED);
        ESP_LOGI(BT_HF_TAG, "Discovery state: %s",
                 discovery_active ? "started" : "stopped");
        if (!discovery_active && !hf_connected) {
            if (!connect_target_phone()) {
                start_discovery_if_needed();
            }
        }
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        // Pairing/authentication result from the remote device.
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_HF_TAG, "Authentication success: %s", param->auth_cmpl.device_name);
            ESP_LOG_BUFFER_HEX(BT_HF_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_HF_TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT: {
        // SSP numeric comparison prompt; this implementation auto-confirms.
        ESP_LOGI(BT_HF_TAG, "SSP confirm request, numeric value: %" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    }
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        // SSP passkey shown by peer device for user notification.
        ESP_LOGI(BT_HF_TAG, "SSP passkey notification: %" PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT: {
        // SSP passkey input request; respond with configured numeric PIN if valid.
        uint32_t passkey = 0;
        if (parse_ssp_passkey(BT_PIN, &passkey)) {
            ESP_LOGI(BT_HF_TAG, "SSP passkey requested; replying with configured passkey");
            esp_bt_gap_ssp_passkey_reply(param->key_req.bda, true, passkey);
        } else {
            ESP_LOGW(BT_HF_TAG, "Invalid BT_PIN for SSP passkey, expected 0-999999 numeric value");
            esp_bt_gap_ssp_passkey_reply(param->key_req.bda, false, 0);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT: {
        // Legacy PIN pairing request; reject because SSP flow is expected.
        ESP_LOGW(BT_HF_TAG, "Legacy PIN request received while SSP is enabled; rejecting");
        esp_bt_pin_code_t pin_code = {0};
        esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, pin_code);
        break;
    }
    case ESP_BT_GAP_MODE_CHG_EVT:
        // Controller power mode transition (active/sniff/park/hold).
        ESP_LOGI(BT_HF_TAG, "Power mode changed: %d", param->mode_chg.mode);
        break;
    default:
        // All other GAP events are intentionally ignored for now.
        ESP_LOGI(BT_HF_TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

static void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    // Some HFP indications (e.g. ring) may arrive without payload.
    if (!param && event == ESP_HF_CLIENT_RING_IND_EVT) {
        ring_active = true;
        ESP_LOGI(BT_HF_TAG, "Incoming ring indication");
        return;
    }

    if (!param) {
        ESP_LOGW(BT_HF_TAG, "bt_hf_client_cb NULL param for event=%d", event);
        return;
    }

    switch (event) {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        // HFP link state machine for reconnect and connection bookkeeping.
        switch (param->conn_stat.state) {
        case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTING:
            // RFCOMM/SLC connection attempt has started.
            hf_connecting = true;
            hf_connected = false;
            break;
        case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTED:
            // RFCOMM transport connected; service-level connection may follow.
        case ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED:
            // Service-level connection ready for call/audio control.
            hf_connecting = false;
            hf_connected = true;
            target_found = true;
            memcpy(peer_addr, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            ESP_LOGI(BT_HF_TAG, "HF connected");
            break;
        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTING:
            // Remote/local side is tearing down the HFP link.
            hf_connecting = false;
            hf_connected = false;
            break;
        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
            // Link is down; attempt reconnect directly, then via discovery fallback.
        default:
            // Unknown state falls back to disconnected behavior to recover safely.
            hf_connecting = false;
            hf_connected = false;
            ESP_LOGW(BT_HF_TAG, "HF disconnected; will keep trying to reconnect");
            if (!connect_target_phone()) {
                start_discovery_if_needed();
            }
            break;
        }
        break;
    case ESP_HF_CLIENT_RING_IND_EVT:
        // Incoming call is ringing on AG; trigger local ringtone/LED feedback here.
        ring_active = true;
        ESP_LOGI(BT_HF_TAG, "Incoming ring indication");
        break;
    case ESP_HF_CLIENT_CLIP_EVT:
        // CLIP carries caller ID; persist it so UI can display the current caller.
        caller_number[0] = '\0';
        if (param->clip.number) {
            strncpy(caller_number, param->clip.number, ESP_BT_HF_CLIENT_NUMBER_LEN);
            caller_number[ESP_BT_HF_CLIENT_NUMBER_LEN] = '\0';
            ESP_LOGI(BT_HF_TAG, "Incoming caller ID: %s", caller_number);
        } else {
            ESP_LOGI(BT_HF_TAG, "Incoming caller ID unavailable");
        }
        break;
    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        // Call setup stage drives UI state and ringtone behavior before call is active.
        call_setup_state = param->call_setup.status;
        switch (call_setup_state) {
        case ESP_HF_CALL_SETUP_STATUS_IDLE:
            // No setup in progress; clear ring only if there is no active call.
            if (!call_active) {
                ring_active = false;
            }
            ESP_LOGI(BT_HF_TAG, "Call setup: idle");
            break;
        case ESP_HF_CALL_SETUP_STATUS_INCOMING:
            // Incoming setup: ring locally and surface answer/reject affordances.
            ring_active = true;
            ESP_LOGI(BT_HF_TAG, "Call setup: incoming");
            break;
        case ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING:
            // Outgoing dialing state before remote side starts alerting.
            ring_active = false;
            ESP_LOGI(BT_HF_TAG, "Call setup: outgoing dialing");
            break;
        case ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING:
            // Outgoing call is alerting (remote side is ringing).
            ring_active = false;
            ESP_LOGI(BT_HF_TAG, "Call setup: outgoing alerting");
            break;
        default:
            // Unexpected setup value from AG; keep state machine alive and log.
            ESP_LOGW(BT_HF_TAG, "Call setup: unknown status=%d", call_setup_state);
            break;
        }
        break;
    case ESP_HF_CLIENT_CIND_CALL_EVT:
        // Active call indicator is the source of truth for in-call vs idle state.
        call_active = (param->call.status == ESP_HF_CALL_STATUS_CALL_IN_PROGRESS);
        if (call_active) {
            ring_active = false;
            ESP_LOGI(BT_HF_TAG, "Call state: active");
        } else {
            if (call_setup_state == ESP_HF_CALL_SETUP_STATUS_IDLE) {
                ring_active = false;
            }
            ESP_LOGI(BT_HF_TAG, "Call state: no active calls");
        }
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        // SCO/eSCO audio link state controls when to start/stop your audio pipeline.
        switch (param->audio_stat.state) {
        case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED:
            // SCO audio connected with CVSD codec.
        case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC:
            // SCO audio connected with mSBC codec.
            audio_active = true;
            // Local ringtone uses DAC too; stop it once SCO audio is active.
            ring_active = false;
            audio_start();
            ESP_LOGI(BT_HF_TAG, "Audio link connected (state=%d)", param->audio_stat.state);
            break;
        case ESP_HF_CLIENT_AUDIO_STATE_CONNECTING:
            // Audio link setup is in progress; keep pipeline idle until connected.
            audio_active = false;
            ESP_LOGI(BT_HF_TAG, "Audio link connecting");
            break;
        case ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED:
            // Audio path is down; release mic/speaker resources.
        default:
            // Unknown value handled as disconnected to avoid stale active audio state.
            audio_active = false;
            audio_stop();
            ESP_LOGI(BT_HF_TAG, "Audio link disconnected");
            break;
        }
        break;
    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        // AT response confirms whether call-control commands succeeded or failed.
        if (param->at_response.code == ESP_HF_AT_RESPONSE_CODE_OK) {
            ESP_LOGI(BT_HF_TAG, "AT response OK");
        } else {
            ESP_LOGW(BT_HF_TAG, "AT response failure code=%d cme=%d",
                     param->at_response.code, param->at_response.cme);
        }
        break;
    default:
        // Other HFP events are currently not consumed by this application.
        break;
    }
}

void gap_init(void)
{
    esp_bt_gap_set_device_name("ESP_HFP_HF");

    esp_bt_gap_register_callback(bt_gap_cb);
    esp_hf_client_register_callback(bt_hf_client_cb);
    esp_hf_client_init();
    esp_hf_client_register_data_callback(audio_receive, audio_send);

    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(iocap));

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code = {0};
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    start_discovery_if_needed();
}

bool gap_is_ring_active(void)
{
    return ring_active;
}

bool gap_is_audio_active(void)
{
    return audio_active;
}

void gap_answer_call(void)
{
    if (!hf_connected) {
        ESP_LOGW(BT_HF_TAG, "Cannot answer: HF not connected");
        return;
    }
    ESP_LOGI(BT_HF_TAG, "Answering call");
    esp_hf_client_answer_call();
}

void gap_reject_call(void)
{
    if (!hf_connected) {
        ESP_LOGW(BT_HF_TAG, "Cannot reject: HF not connected");
        return;
    }
    ESP_LOGI(BT_HF_TAG, "Rejecting call");
    esp_hf_client_reject_call();
}

void gap_hangup_call(void)
{
    if (!hf_connected) {
        ESP_LOGW(BT_HF_TAG, "Cannot hang up: HF not connected");
        return;
    }

    if (!call_active && call_setup_state == ESP_HF_CALL_SETUP_STATUS_IDLE) {
        ESP_LOGW(BT_HF_TAG, "Cannot hang up: no call in progress");
        return;
    }

    ESP_LOGI(BT_HF_TAG, "Hanging up call");
    // esp_hf_client_reject_call() maps to AT+CHUP on AG and hangs up/cancels.
    esp_hf_client_reject_call();
}
