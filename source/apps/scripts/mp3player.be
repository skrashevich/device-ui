# mp3player.be - MP3 Player UI for ESP32 device (320x240 display)
#
# Lifecycle functions called by ScriptApp:
#   app_init()        - called once at startup
#   app_create_ui()   - create LVGL widgets
#   app_tick(now_ms)  - periodic update (~50ms)
#   app_destroy()     - cleanup
#
# Widget handle budget (max 64):
#   root panel:        1  (from ui.root())
#   header label:      1
#   file list panel:   1
#   file labels:      20  (max)
#   track info panel:  1
#   track name label:  1
#   track status lbl:  1
#   controls panel:    1
#   btn_prev:          1
#   btn_play:          1
#   btn_next:          1
#   btn_stop:          1
#   TOTAL:            ~31 (well under 64)

# ── State ──────────────────────────────────────────────────────────────
var files = []          # list of mp3 filenames
var file_labels = []    # widget handles for file list items
var current_idx = -1    # index of selected/playing track
var last_tick = 0       # last UI update timestamp

# Widget handles
var lbl_track_name = -1
var lbl_track_status = -1
var btn_play = -1
var panel_files = -1
var was_playing = false   # tracks if audio was playing last tick (for auto-advance)

# ── Helpers ─────────────────────────────────────────────────────────────

# Load mp3 file list from SD card (max 20 to stay under handle budget)
# fs.list_dir() returns a Berry list of filenames, with optional ext filter
def load_files()
    files = []
    var all = fs.list_dir("/mp3", ".mp3")
    var i = 0
    while i < size(all) && size(files) < 20
        files.push(all[i])
        i += 1
    end
end

# Return display-friendly name: strip path prefix, truncate at 28 chars
def display_name(path)
    # Strip leading "/mp3/" prefix if present
    var name = path
    if size(name) > 5 && name[0..4] == "/mp3/"
        name = name[5..]
    end
    if size(name) > 28
        name = name[0..24] + "..."
    end
    return name
end

# Rebuild file label text to show selection marker
def refresh_file_list()
    var i = 0
    while i < size(file_labels)
        var prefix = (i == current_idx) ? "> " : "  "
        ui.set_text(file_labels[i], prefix + display_name(files[i]))
        # Highlight selected track
        if i == current_idx
            ui.set_style_text_color(file_labels[i], 255, 220, 0)
        else
            ui.set_style_text_color(file_labels[i], 220, 220, 220)
        end
        i += 1
    end
end

# Update track info labels and play button text
def refresh_track_info()
    if lbl_track_name < 0 || lbl_track_status < 0 || btn_play < 0
        return
    end

    # Track name
    if current_idx >= 0 && current_idx < size(files)
        ui.set_text(lbl_track_name, display_name(files[current_idx]))
    else
        ui.set_text(lbl_track_name, "No track selected")
    end

    # Status + play button label
    var st = audio.status()
    if st == "playing"
        ui.set_text(lbl_track_status, "Playing")
        ui.set_text(btn_play, "Pause")
    elif st == "paused"
        ui.set_text(lbl_track_status, "Paused")
        ui.set_text(btn_play, "Resume")
    else
        ui.set_text(lbl_track_status, "Stopped")
        ui.set_text(btn_play, "Play")
    end
end

# Play the track at current_idx
def play_current()
    if current_idx >= 0 && current_idx < size(files)
        audio.play("/mp3/" + files[current_idx])
        refresh_file_list()
        refresh_track_info()
    end
end

# ── Button callbacks ─────────────────────────────────────────────────────

def on_prev()
    if size(files) == 0
        return
    end
    if current_idx <= 0
        current_idx = size(files) - 1
    else
        current_idx -= 1
    end
    play_current()
end

def on_play_pause()
    var st = audio.status()
    if st == "playing"
        audio.pause()
        refresh_track_info()
    elif st == "paused"
        audio.resume()
        refresh_track_info()
    else
        # Stopped — start from current or first track
        if current_idx < 0 && size(files) > 0
            current_idx = 0
        end
        play_current()
    end
end

def on_next()
    if size(files) == 0
        return
    end
    current_idx = (current_idx + 1) % size(files)
    play_current()
end

def on_stop()
    was_playing = false   # prevent auto-advance on explicit stop
    audio.stop()
    refresh_track_info()
end

def on_file_click(idx)
    current_idx = idx
    play_current()
end

# Per-file click callbacks (Berry closures via named functions)
# We generate on_file_0 .. on_file_19 to avoid dynamic callback names
def on_file_0()  on_file_click(0)  end
def on_file_1()  on_file_click(1)  end
def on_file_2()  on_file_click(2)  end
def on_file_3()  on_file_click(3)  end
def on_file_4()  on_file_click(4)  end
def on_file_5()  on_file_click(5)  end
def on_file_6()  on_file_click(6)  end
def on_file_7()  on_file_click(7)  end
def on_file_8()  on_file_click(8)  end
def on_file_9()  on_file_click(9)  end
def on_file_10() on_file_click(10) end
def on_file_11() on_file_click(11) end
def on_file_12() on_file_click(12) end
def on_file_13() on_file_click(13) end
def on_file_14() on_file_click(14) end
def on_file_15() on_file_click(15) end
def on_file_16() on_file_click(16) end
def on_file_17() on_file_click(17) end
def on_file_18() on_file_click(18) end
def on_file_19() on_file_click(19) end

var file_callbacks = [
    "on_file_0",  "on_file_1",  "on_file_2",  "on_file_3",  "on_file_4",
    "on_file_5",  "on_file_6",  "on_file_7",  "on_file_8",  "on_file_9",
    "on_file_10", "on_file_11", "on_file_12", "on_file_13", "on_file_14",
    "on_file_15", "on_file_16", "on_file_17", "on_file_18", "on_file_19"
]

# ── Lifecycle ────────────────────────────────────────────────────────────

def app_init()
    print("MP3 Player: initializing")
    load_files()
    print("MP3 Player: found " + str(size(files)) + " files")
end

def app_create_ui()
    var root = ui.root()
    ui.set_flex_flow(root, 1)   # column layout
    ui.set_style_bg_color(root, 20, 20, 30)

    # ── Header ──
    var lbl_header = ui.label(root, "MP3 Player")
    ui.set_style_text_color(lbl_header, 100, 200, 255)

    # ── File list panel ──
    panel_files = ui.panel(root)
    ui.set_flex_flow(panel_files, 1)
    ui.set_size(panel_files, 320, 110)
    ui.set_style_bg_color(panel_files, 30, 30, 40)

    # Populate file labels
    file_labels = []
    var i = 0
    while i < size(files)
        var lbl = ui.label(panel_files, "  " + display_name(files[i]))
        ui.set_style_text_color(lbl, 220, 220, 220)
        ui.on_click(lbl, file_callbacks[i])
        file_labels.push(lbl)
        i += 1
    end

    if size(files) == 0
        ui.label(panel_files, "No .mp3 files on /mp3")
    end

    # ── Track info panel ──
    var panel_info = ui.panel(root)
    ui.set_flex_flow(panel_info, 1)
    ui.set_size(panel_info, 320, 50)
    ui.set_style_bg_color(panel_info, 25, 25, 35)

    lbl_track_name   = ui.label(panel_info, "No track selected")
    lbl_track_status = ui.label(panel_info, "Stopped")
    ui.set_style_text_color(lbl_track_name,   200, 200, 200)
    ui.set_style_text_color(lbl_track_status, 150, 150, 150)

    # ── Controls panel ──
    var panel_ctrl = ui.panel(root)
    ui.set_flex_flow(panel_ctrl, 2)   # row layout
    ui.set_size(panel_ctrl, 320, 50)
    ui.set_style_bg_color(panel_ctrl, 20, 20, 30)

    var btn_prev = ui.button(panel_ctrl, "Prev")
    btn_play     = ui.button(panel_ctrl, "Play")
    var btn_next = ui.button(panel_ctrl, "Next")
    var btn_stop = ui.button(panel_ctrl, "Stop")

    ui.on_click(btn_prev, "on_prev")
    ui.on_click(btn_play, "on_play_pause")
    ui.on_click(btn_next, "on_next")
    ui.on_click(btn_stop, "on_stop")
end

def app_tick(now_ms)
    # Update UI at most once per second to reduce render load
    if now_ms - last_tick < 1000
        return
    end
    last_tick = now_ms

    var st = audio.status()

    # Auto-advance: if we were playing and now stopped, track ended naturally
    if st == "stopped" && was_playing && current_idx >= 0
        was_playing = false
        current_idx = (current_idx + 1) % size(files)
        play_current()
        return
    end

    was_playing = (st == "playing")
    refresh_track_info()
end

def app_destroy()
    audio.stop()
    print("MP3 Player: destroyed")
end
