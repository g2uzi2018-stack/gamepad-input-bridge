#!/bin/sh
# Build the input-only EasySMX module in a new directory on the Jetson.
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: sh build-module.sh NEW_BUILD_DIRECTORY [upstream-xpad.c]" >&2
    exit 2
fi
release=$(uname -r)
headers="/lib/modules/$release/build"
fail() { echo "$*" >&2; exit 1; }
test "$release" = "5.15.148-rt-tegra" || fail "Unsupported running kernel: $release"
test "$(cat "$headers/include/config/kernel.release")" = "$release" || fail "Install matching RT kernel headers"
test -s "$headers/Module.symvers" || fail "Missing symbol versions: $headers/Module.symvers"
mkdir -- "$1"
build_dir=$(cd -- "$1" && pwd)
if [ "$#" -eq 2 ]; then
    cp -- "$2" "$build_dir/xpad.c.orig"
else
    curl -fLsS -o "$build_dir/xpad.c.orig" \
        https://raw.githubusercontent.com/gregkh/linux/v5.15.148/drivers/input/joystick/xpad.c
fi
cd "$build_dir"
echo '34a522dc1a3bdb8434334a1b1a67bde678adcaa0eafa14b38ee1f9c02d2070e7  xpad.c.orig' | sha256sum -c -
# Preserve the receiver's small reports, including its exact return to center.
# xpad complements the two Y axes, so their neutral evdev value is -1.
sed -e '/^} xpad_device\[\] = {/a\
\	{ 0x413d, 0x2131, "EasySMX D15 Dune 8K", 0, XTYPE_XBOX360 },' \
    -e '/^static const struct usb_device_id xpad_table\[\] = {/,/^};$/c\
static const struct usb_device_id xpad_table[] = {\
\	{ USB_DEVICE_AND_INTERFACE_INFO(0x413d, 0x2131, 0xff, 0x5d, 0x01) },\
\	{ }\
};' -e 's/= "xpad",/= "xpad_easysmx",/' \
    -e 's/abs, -32768, 32767, 16, 128);/abs, -32768, 32767, 0, 128);\
		if (abs == ABS_Y || abs == ABS_RY)\
			input_abs_set_val(input_dev, abs, -1);/' xpad.c.orig > xpad_easysmx.c
printf '%s\n' 'obj-m := xpad_easysmx.o' > Makefile
make -C "$headers" M="$build_dir" modules
test "$(modinfo -F vermagic ./xpad_easysmx.ko)" = "$(modinfo -F vermagic joydev)" || fail "Module ABI differs from installed joydev"
test "$(modinfo -F alias ./xpad_easysmx.ko)" = 'usb:v413Dp2131d*dc*dsc*dp*icFFisc5Dip01in*' || fail "Unexpected USB module alias"
printf '%s\n' "$build_dir/xpad_easysmx.ko"
