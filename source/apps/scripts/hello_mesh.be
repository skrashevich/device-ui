# hello_mesh.be - Example Berry script for Meshtastic device-ui
#
# This script demonstrates the custom apps scripting framework.
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

def app_init()
    print("Hello Mesh app initialized!")
end

def app_create_ui()
    # UI creation will use the 'ui' module bindings
    # ui.label(nil, "Hello from Berry!")
    # ui.label(nil, "Waiting for messages...")
    print("Hello Mesh: UI created")
end

def app_show()
    print("Hello Mesh: shown")
end

def app_hide()
    print("Hello Mesh: hidden")
end

def app_tick(now_ms)
    # Update every 10 seconds
    if now_ms - last_tick > 10000
        last_tick = now_ms
        # ui.set_text(status_label, "Messages: " + str(msg_count))
    end
end

def app_on_message(from, text)
    msg_count += 1
    print("Hello Mesh: message from " + from + ": " + text)
    # Echo back: mesh.send_text(0xFFFFFFFF, 0, "[Echo] " + text)
end

def app_destroy()
    print("Hello Mesh: destroyed")
end
