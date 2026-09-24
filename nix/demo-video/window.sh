# Terminal content for the demo video: a large name in the window's accent color above a short note.
# Usage: demo-window <name> <accent hex> <note>
name="$1"
accent="$2"
note="$3"

r=$((16#${accent:0:2}))
g=$((16#${accent:2:2}))
b=$((16#${accent:4:2}))

printf '\n\e[1;38;2;%d;%d;%dm' "$r" "$g" "$b"
toilet -f future -w "$(tput cols)" "$name" | sed 's/^/  /'
printf '\e[0m\n\e[38;5;250m%s\e[0m\n\n' "$note" | fold -s -w "$(($(tput cols) - 4))" | sed 's/^/  /'
exec bash
