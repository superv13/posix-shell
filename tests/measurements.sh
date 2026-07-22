SHELLS=("./posixsh_release" "dash" "bash")
CMDS=("exit" "echo hello" "ls /dev/null" "true" "echo hello | cat" "echo hello > /dev/null")
LABELS=("exit" "echo" "ls" "true" "pipe" "redir")

for shell in "${SHELLS[@]}"; do
    for i in "${!CMDS[@]}"; do
        cmd="${CMDS[$i]}"
        label="${LABELS[$i]}"
        sname=$(basename "$shell")
        outfile="/tmp/strace_${sname}_${label}.txt"

        strace -c -o "$outfile" \
            $shell -c "$cmd" >/dev/null 2>/dev/null

        # Print: shell | command | calls | errors
        awk -v s="$sname" -v c="$label" '
            /total$/ {
                calls = (NF==5) ? $3 : $3
                errors = (NF==5) ? $4 : "0"
                printf "%s | %-8s | calls=%-4s | errors=%s\n", s, c, calls, errors
            }
        ' "$outfile"
    done
done