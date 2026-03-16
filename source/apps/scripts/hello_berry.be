# hello_berry.be - Example Berry script for Meshtastic device-ui
#
# This script demonstrates the Berry scripting framework for custom apps.
# It displays a simple message counter and echoes received mesh messages.
#
# Lifecycle functions called by ScriptApp:
#   app_init()        - called once at startup
#   app_create_ui()   - create LVGL widgets
#   app_show()        - panel becomes visible
#   app_hide()        - panel becomes hidden
#   app_tick(now_ms)  - periodic update (~50ms)
#   app_on_message(from, text) - incoming mesh text message
#   app_destroy()     - cleanup

var msg_count = 0
var last_tick = 0
var status_label = -1
var msg_panel = -1
var msg_labels = []

def app_init()
    print("Hello Mesh app initialized!")
end

def app_create_ui()
    var root = ui.root()

    # Set column layout on root panel
    ui.set_flex_flow(root, 1)

    # Title label
    ui.label(root, "Hello from Berry!")

    # Status label (updated on tick)
    status_label = ui.label(root, "Messages: 0")

    # Panel for incoming messages
    msg_panel = ui.panel(root)
    ui.set_flex_flow(msg_panel, 1)
end

def app_show()
    print("Hello Mesh: shown")
end

def app_hide()
    print("Hello Mesh: hidden")
end

def app_tick(now_ms)
    if now_ms - last_tick > 5000
        last_tick = now_ms
        if status_label >= 0
            ui.set_text(status_label, "Messages: " + str(msg_count))
        end
    end
end

def app_on_message(from, text)
    msg_count += 1
    print("Hello Mesh: message from " + from + ": " + text)

    # Add message label to the panel
    if msg_panel >= 0
        # Keep max 10 messages by hiding oldest
        if size(msg_labels) >= 10
            ui.delete(msg_labels[0])
            msg_labels = msg_labels[1..]
        end
        var lbl = ui.label(msg_panel, from + ": " + text)
        msg_labels.push(lbl)
    end
end

def app_destroy()
    print("Hello Mesh: destroyed")
end
