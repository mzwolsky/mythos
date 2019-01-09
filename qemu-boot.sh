#!/bin/bash
# qemu-boot.sh

if echo $@ | grep -qP '\B\-\w*h|\-\-help'; then #options '-*h' and '--help'
  #[ $1 = "-h" ] || [ $1 = "--help" ]; then
  echo "Usage: ./qemu-boot.sh [OPTIONS] [path/to/log]"
  echo -e "COMPILE OPTIONS:\n  -h, --help\tprint this guide\n  -k\t\tremake entire kernel (only)\n  -m\t\tremake (quiet)\n  -r\t\trebuild only (not quiet)"
  echo -e "FILTER OPTIONS: filter console output\n  -p\t\tfor playground\n  -a\t\tfor app messages\n  -e\t\tfor ec messages\n  -f\t\tfor FibonacciObj messages"
  echo -e "RUN OPTIONS:\n  -d\t\trun in DEBUG mode, creating a gdbserver at localhost:1234 and halt at boot"
  echo "Paths must start with either .|..|/ or a word character (alphanumeric + '_')"
  echo -e "WARNING:\n  Do not use more than two options with one '-' (like '-mrp'), this will be detected as a path!"
  exit 0
elif echo $@ | grep -qP '\B\-\w*k'; then #option '-*k' remake kernel
  echo "Remaking kernel..."
  make > /dev/null
  cd ./kernel-amd64
  make clean > /dev/null
  cd ..
fi
if echo $@ | grep -qP '\B\-\w*r'; then #option '-*r' rebuild only
  echo "Rebuild:"
  cd ./kernel-amd64
  make qemu
  exit 0
elif echo $@ | grep -qP '\B\-\w*m'; then #option '-*m' quiet remake with error failsafe
  echo "Remaking..."
  cd ./kernel-amd64
  err=$(make qemu 2> >(grep -oP 'app\/init.cc\:(.|\n)*') 1>-) #filter stderr of make for "app/init.cc:*" errors
  if [[ $err ]]; then
    echo "Compile errors occured, rebuilding!"
    cd ..
    $($0 "$@ -r")
    exit 1
  fi
  echo "All good!"
  cd ..
fi

path=$(echo $@ | grep -oP '(?<!-)[\.{1,2}\/\w]\S+') #extract path from params (absolute or relative to ./)
echo "Path: ${path:="/dev/null"}" #if none provided redirect to /dev/null
#define optional filter patterns
if echo $@ | grep -qP '\B\-\w*p'; then #option '-*p' filter output for "*playground:*"
  pattern1=".*playground\:.*"
fi
if echo $@ | grep -qP '\B\-\w*a'; then #option '-*a' filter output for "*app *"
  pattern2=".*app\s.*"
fi
if echo $@ | grep -qP '\B\-\w*e'; then #option '-*e' filter output for "*ec *"
  pattern3=".*ec\s.*"
fi
if echo $@ | grep -qP '\B\-\w*f'; then #option '-*f' filter output for "*FibonacciObj *"
  pattern4=".*FibonacciObj\s.*"
fi
if [[ $pattern1 || $pattern2 || $pattern3 || $pattern4 ]]; then
  pattern="${pattern1:=\0}|${pattern2:=\0}|${pattern3:=\0}|${pattern4:=\0}"
else
  pattern="(.|\n)*"
fi

#--debug call--
if echo $@ | grep -qP '\B\-\w*d'; then #option '-*d' start in debug mode (gdb server and halt on start)
  echo "Starting in DEBUG mode!"
  echo "gdbserver at localhost:1234 halted"
  # trap "kill 0" EXIT
  if ! pgrep -x "gdbgui" > /dev/null
  then
    cd ./kernel-amd64
    gdbgui &
    cd ..
  fi
  echo "gdbgui at localhost:5000 started"

  #call with ./kernel-amd64/boot32.elf
  qemu-system-x86_64 -S -s -m 1024 -cpu SandyBridge -smp 4 -D qemu-system.log -serial stdio -serial file:mythos.trace -d pcall,int,unimp,guest_errors -no-reboot -display none -kernel ./kernel-amd64/boot32.elf \
  | tee "$path" \
  | grep -P -e $pattern #filter output for the specified pattern
  exit 0
fi

#--standard call--
#call with ./kernel-amd64/boot32.elf
qemu-system-x86_64 -m 1024 -cpu SandyBridge -smp 4 -D qemu-system.log -serial stdio -serial file:mythos.trace -d pcall,int,unimp,guest_errors -no-reboot -display none -kernel ./kernel-amd64/boot32.elf \
| tee "$path" \
| grep -P -e $pattern #filter output for the specified pattern
