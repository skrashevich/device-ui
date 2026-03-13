#ifdef HAS_CUSTOM_APPS

#include "apps/builtin/HelloApp.h"
#include "apps/AppContext.h"
#include "util/ILog.h"
#include <cstdio>
#include <cstring>

bool HelloApp::init(AppContext *ctx)
{
    this->ctx = ctx;
    ILOG_INFO("HelloApp initialized");
    return true;
}

lv_obj_t *HelloApp::createUI(lv_obj_t *parent)
{
    panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(panel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(panel, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(panel, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(panel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Title
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, LV_SYMBOL_GPS " Hello Mesh");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Node info label
    nodeLabel = lv_label_create(panel);
    char buf[64];
    snprintf(buf, sizeof(buf), "Node: %08x", ctx ? ctx->getMyNodeNum() : 0);
    lv_label_set_text(nodeLabel, buf);

    // Status
    statusLabel = lv_label_create(panel);
    lv_label_set_text(statusLabel, "Messages: 0");

    // Message list
    msgList = lv_list_create(panel);
    lv_obj_set_size(msgList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(msgList, 1);
    lv_obj_set_style_pad_all(msgList, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    return panel;
}

void HelloApp::onShow()
{
    ILOG_DEBUG("HelloApp: onShow");
    if (nodeLabel && ctx) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Node: %08x", ctx->getMyNodeNum());
        lv_label_set_text(nodeLabel, buf);
    }
}

void HelloApp::onHide()
{
    ILOG_DEBUG("HelloApp: onHide");
}

void HelloApp::onTick(uint32_t now_ms)
{
    // Update status every 5 seconds
    if (now_ms - lastUpdate > 5000) {
        lastUpdate = now_ms;
        if (statusLabel) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Messages: %lu", (unsigned long)msgCount);
            lv_label_set_text(statusLabel, buf);
        }
    }
}

void HelloApp::onMeshPacket(const meshtastic_MeshPacket &p)
{
    // Show text messages in the list
    if (p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && p.decoded.payload.size > 0) {
        msgCount++;

        if (msgList) {
            char header[24];
            snprintf(header, sizeof(header), "%04x:", p.from & 0xffff);

            char msg[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
            size_t len = p.decoded.payload.size;
            if (len > meshtastic_Constants_DATA_PAYLOAD_LEN)
                len = meshtastic_Constants_DATA_PAYLOAD_LEN;
            memcpy(msg, p.decoded.payload.bytes, len);
            msg[len] = '\0';

            std::string display = std::string(header) + " " + msg;

            // Keep max 10 messages, remove oldest
            if (lv_obj_get_child_count(msgList) >= 10) {
                lv_obj_del(lv_obj_get_child(msgList, 0));
            }

            lv_list_add_text(msgList, display.c_str());
        }
    }
}

void HelloApp::destroy()
{
    if (panel) {
        lv_obj_del(panel);
        panel = nullptr;
    }
    nodeLabel = nullptr;
    msgList = nullptr;
    statusLabel = nullptr;
    ILOG_INFO("HelloApp destroyed");
}

#endif // HAS_CUSTOM_APPS
