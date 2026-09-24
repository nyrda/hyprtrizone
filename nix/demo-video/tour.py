# Feature tour for the demo video. WIDTH, HEIGHT, OVERLAY and FFMPEG are defined by default.nix.
import json
import os
import shlex
import subprocess
import time

ENV = "XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1"
YDOTOOL = "YDOTOOL_SOCKET=/run/ydotoold/socket ydotool"
KEY_LEFTMETA = 125


def as_demo(command):
    return machine.succeed("su - demo -c " + shlex.quote(f"{ENV} {command}"))


def hyprctl(args):
    return as_demo(f"hyprctl -i 0 {args}")


def dispatch(lua):
    hyprctl("dispatch " + shlex.quote(lua))


def clients():
    return json.loads(hyprctl("-j clients"))


def client(title):
    return next((c for c in clients() if c["title"] == title), None)


def tour(function, *args):
    as_demo(f"quickshell ipc -p {OVERLAY} call tour {function} " + " ".join(shlex.quote(a) for a in args))


def caption(title, detail="", keys=""):
    tour("caption", title, detail, keys)


# Pointer and keyboard input goes through ydotool (a uinput device) and QEMU, like a real session.
def ydotool(*commands):
    machine.succeed("; ".join(f"{YDOTOOL} {c}" for c in commands))


def warp(x, y):
    dispatch(f"hl.dsp.cursor.move({{ x = {int(x)}, y = {int(y)} }})")


def glide(start, end, duration=1.0, fps=60):
    start = (round(start[0]), round(start[1]))
    end = (round(end[0]), round(end[1]))
    steps = max(1, int(duration * fps))
    moves = []
    px, py = start
    for i in range(1, steps + 1):
        t = i / steps
        eased = t * t * (3 - 2 * t)
        nx = round(start[0] + (end[0] - start[0]) * eased)
        ny = round(start[1] + (end[1] - start[1]) * eased)
        if nx != px or ny != py:
            moves.append(f"{YDOTOOL} mousemove -x {nx - px} -y {ny - py}")
        moves.append(f"sleep {1 / fps:.4f}")
        px, py = nx, ny
    machine.succeed("; ".join(moves))


def super_drag(start, end, button="left", duration=0.7):
    down, up = {"left": ("0x40", "0x80"), "right": ("0x41", "0x81")}[button]
    # The cursor hides when idle; a short approach brings it back into view before the drag.
    approach = (start[0] - 160, start[1] + 90)
    warp(*approach)
    glide(approach, start, duration=0.25)
    time.sleep(0.25)
    ydotool(f"key {KEY_LEFTMETA}:1", f"click {down}")
    time.sleep(0.15)
    glide(start, end, duration=duration)
    time.sleep(0.15)
    ydotool(f"click {up}", f"key {KEY_LEFTMETA}:0")
    park()


def park():
    # Over the left panel the pointer cannot pull focus away from the window the tour works on.
    warp(WIDTH * 0.2, 17)


def keys(combo, pause=1.6):
    machine.send_key(combo)
    time.sleep(pause)


def focus(title):
    dispatch(f'hl.dsp.focus({{ window = "title:^{title}$" }})')
    retry(lambda _: json.loads(hyprctl("-j activewindow")).get("title") == title)


def layout(message):
    dispatch(f'hl.dsp.layout("{message}")')


def point_in(title, fx=0.5, fy=0.5):
    c = client(title)
    return (c["at"][0] + c["size"][0] * fx, c["at"][1] + c["size"][1] * fy)


ACCENTS = {
    "center": "89b4fa",
    "left": "cba6f7",
    "right": "a6e3a1",
    "pair": "fab387",
    "follow": "f5c2e7",
    "plain": "94e2d5",
}


def spawn(title, app_id, accent, note, workspace=None, pause=1.4):
    command = (
        f"foot --app-id {app_id} --title {title} -o locked-title=yes "
        f"demo-window {shlex.quote(title)} {ACCENTS[accent]} {shlex.quote(note)}"
    )
    rules = f', {{ workspace = "{workspace} silent" }}' if workspace else ""
    dispatch(f"hl.dsp.exec_cmd([==[{command}]==]{rules})")
    retry(lambda _: client(title) is not None)
    time.sleep(pause)


def assert_layout(description, predicate):
    try:
        retry(lambda _: predicate(), timeout_seconds=20)
    except Exception:
        machine.screenshot("tour-failure")
        windows = [(c["title"], c["at"], c["size"]) for c in clients()]
        raise Exception(f"tour step failed: {description}; windows: {windows}")


def zone(title):
    c = client(title)
    x = c["at"][0] + c["size"][0] / 2
    return "left" if x < WIDTH / 4 else "right" if x > WIDTH * 3 / 4 else "center"


def paired(a, b):
    ca, cb = client(a), client(b)
    return ca["at"][1] == cb["at"][1] and ca["at"][0] < cb["at"][0] and zone(a) == zone(b)


def panels():
    return [
        layer
        for monitor in json.loads(hyprctl("-j layers")).values()
        for group in monitor["levels"].values()
        for layer in group
        if layer["namespace"].startswith("hyprtrizone-demo-")
    ]


machine.wait_for_unit("multi-user.target")
machine.wait_for_unit("ydotoold.service")
machine.wait_until_succeeds(f"su - demo -c {shlex.quote(ENV + ' hyprctl -i 0 version')}", timeout=120)
machine.wait_until_succeeds("pgrep -u demo -f 'quickshell -p .*overlay.qml'", timeout=60)
retry(lambda _: len(panels()) == 2)
assert not hyprctl("configerrors").strip(), hyprctl("configerrors")
park()

# Started by Hyprland, so it outlives the su session. A near-lossless intermediate; the host encodes the result.
# wf-recorder only captures damaged frames, so its first frame is the title card fading in.
dispatch("hl.dsp.exec_cmd([==[wf-recorder --no-dmabuf -c libx264 -p preset=ultrafast -p crf=12 -y -f /tmp/tour.mkv > /tmp/wf-recorder.log 2>&1]==])")
machine.wait_until_succeeds("pgrep -u demo wf-recorder", timeout=30)
time.sleep(1.5)
# Video time starts with this card's fade-in, the first change on screen.
recording_started = time.monotonic()
tour("card", "hyprtrizone", "A three-zone Hyprland layout for ultrawide monitors", f"Hyprland 0.56.2  ·  {WIDTH}×{HEIGHT}  ·  recorded in a NixOS VM")
time.sleep(4.5)

# 1. The center column.
tour("hideCard")
time.sleep(1.2)
caption("A main column in the middle", "The first window takes the center: half the width, full height.")
time.sleep(1.2)
spawn("editor", "tour-center", "center", "The main window. It keeps the full monitor height, even below the side panels.", pause=2.5)
spawn("browser", "tour-center", "center", "Workspace 2.", workspace=2, pause=0)

# 2. Side stacks and zone tags.
caption("Side stacks on either edge", "Zone tags choose a column. Untagged windows follow side_bias.")
time.sleep(1.0)
spawn("notes", "tour-plain", "plain", "No zone tag, so it opens on the default side (side_bias = right).")
spawn("chat", "tour-left", "left", "Tagged for the left column.")
spawn("music", "tour-left-follow", "follow", "Tagged for the left column and to follow workspace switches.", pause=2.0)
assert_layout("zone placement", lambda: zone("notes") == "right" and zone("chat") == "left" and zone("music") == "left")

# 3. Scrolling and overflow indicators.
caption("Stacks scroll instead of shrinking", "Two rows stay visible here. The badges count windows above and below.")
spawn("logs", "tour-right-pair", "pair", "Right column, pairing tag.")
spawn("build", "tour-right", "right", "Right column.")
spawn("tests", "tour-right", "right", "Right column. The stack now overflows.", pause=2.0)
caption("Stacks scroll instead of shrinking", "Scroll a side stack with the mouse wheel.", "Super+Wheel")
for _ in range(3):
    layout("scroll right up")
    time.sleep(0.9)
time.sleep(1.2)
for _ in range(3):
    layout("scroll right down")
    time.sleep(0.9)
time.sleep(1.0)

# 4. Pairing at the insertion point.
caption("Two windows can share a row", "Pairing happens where a tagged window opens. Other rows stay where they are.")
time.sleep(1.0)
spawn("shell", "tour-right-pair", "pair", "Pairing tag.")
spawn("shell-2", "tour-right-pair", "pair", "Pairing tag. Shares the row with shell.", pause=2.5)
assert_layout("pair on insertion", lambda: paired("shell", "shell-2"))
poster_at = time.monotonic() - recording_started

# 5. Drag and drop within a stack. Both drags lift a pair member, so no row disappears while dragging.
caption("Rearrange a stack by drag and drop", "Drop onto a single window to pair with it. The pointer picks the half.", "Super+Left drag")
spawn("todo", "tour-right-pair", "pair", "Pairing tag. Opens below the full pair, so it starts a new row.", pause=1.4)
super_drag(point_in("shell", 0.5, 0.4), point_in("todo", 0.75, 0.45))
assert_layout("shell pairs with todo", lambda: paired("todo", "shell") and client("shell-2")["at"][1] < client("todo")["at"][1])
time.sleep(1.6)
caption("Rearrange a stack by drag and drop", "Drop onto the partner to swap the two.", "Super+Left drag")
time.sleep(0.8)
super_drag(point_in("todo", 0.5, 0.4), point_in("shell", 0.6, 0.45))
assert_layout("the pair swaps", lambda: paired("shell", "todo"))
time.sleep(1.8)

caption("Swap with the main window", "Exchange the focused side window with the center, and back.", "Super+Space")
focus("shell-2")
park()
time.sleep(0.8)
keys("meta_l-spc", pause=2.0)
assert_layout("shell-2 takes the center", lambda: zone("shell-2") == "center" and zone("editor") == "right")
focus("editor")
keys("meta_l-spc", pause=2.0)
assert_layout("editor returns to the center", lambda: zone("editor") == "center" and zone("shell-2") == "right")

# 6. Keyboard movement between columns and rows.
caption("Move windows with the keyboard", "Toward a partner swaps the pair; further travels through the columns.", "Super+Shift+Arrows")
focus("todo")
time.sleep(1.0)
keys("meta_l-shift-left")
assert_layout("todo swaps with its partner", lambda: paired("todo", "shell"))
keys("meta_l-shift-left")
assert_layout("todo joins the center", lambda: zone("todo") == "center")
keys("meta_l-shift-left")
assert_layout("todo joins the left stack", lambda: zone("todo") == "left")
caption("Move windows with the keyboard", "Up and down trade rows within a column.", "Super+Shift+Up")
keys("meta_l-shift-up", pause=2.2)

# 7. Resizing the center.
caption("Resize the main column", "Both side columns follow symmetrically.", "Super+Right drag")
time.sleep(1.0)
edge = point_in("editor", 0.93, 0.5)
super_drag(edge, (edge[0] + 420, edge[1]), button="right", duration=0.9)
time.sleep(1.6)
edge = point_in("editor", 0.93, 0.5)
super_drag(edge, (edge[0] - 420, edge[1]), button="right", duration=0.9)
time.sleep(1.4)

# 8. Workspace following.
caption("Windows that follow you", "A tagged window moves to each workspace you open and keeps its zone.", "Super+2")
focus("editor")
park()
time.sleep(1.2)
keys("meta_l-2", pause=2.6)
assert_layout("music follows to workspace 2", lambda: client("music")["workspace"]["id"] == 2)
caption("Windows that follow you", "Scroll offsets and rows belong to each workspace.", "Super+1")
keys("meta_l-1", pause=2.4)

# 9. Fit instead of scroll.
caption("Or fit every row", "Scrolling and pairing can be toggled at runtime.", "Super+Shift+F10")
time.sleep(1.2)
keys("meta_l-shift-f10", pause=2.8)
keys("meta_l-shift-f10", pause=1.8)

tour("hideCaption")
time.sleep(0.8)
tour("card", "hyprtrizone", "Try it:  ./dev.sh vm-demo", "github.com/nyrda/hyprtrizone")
# The screen stops changing once the card is in; the encoder holds the last frame for OUTRO_HOLD seconds.
OUTRO_HOLD = 4.0
time.sleep(1.5)

machine.succeed("pkill -INT -u demo wf-recorder")
machine.wait_until_fails("pgrep -u demo wf-recorder", timeout=60)
machine.succeed("cat /tmp/wf-recorder.log >&2")
machine.copy_from_machine("/tmp/tour.mkv")

out = str(machine.out_dir)
raw = os.path.join(out, "tour.mkv")
subprocess.run(
    [
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y", "-i", raw,
        "-vf", f"fps=30,tpad=stop_mode=clone:stop_duration={OUTRO_HOLD},format=yuv420p",
        "-c:v", "libx264", "-preset", "slow", "-crf", "24", "-tune", "animation",
        "-movflags", "+faststart", "-an",
        os.path.join(out, "hyprtrizone-demo.mp4"),
    ],
    check=True,
)
subprocess.run(
    [
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{poster_at:.2f}", "-i", raw,
        "-frames:v", "1", os.path.join(out, "hyprtrizone-demo-poster.png"),
    ],
    check=True,
)
os.remove(raw)
