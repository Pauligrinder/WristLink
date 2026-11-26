# WristLink

WristLink is a Sailfish OS Bluetooth companion for Wear OS watches. It uses the
normal paired Bluetooth connection: Sailfish stays connected to the watch, and
WristLink sends a notification or a call only when one happens. Today's steps,
heart rate, calories, distance, floors, and battery are read at the same time.
If nothing else is going on, those numbers sync about every half hour.

Galaxy Watch and similar Wear OS watches do not publish Samsung Health over
standard Bluetooth, and they will not show Sailfish notifications by themselves.
Install the WristLink app on the watch so it can answer those reads and show
what the phone sends. The watch does not scan. Sailfish connects to the paired
watch and keeps that connection. The watch only advertises while that
connection is down, so the phone can reach it again.

WristLink has been tested on **Galaxy Watch 7**. Detection also recognizes other
Wear OS watches (Pixel Watch, OnePlus Watch, TicWatch, Fossil, and similar) via
Bluetooth name, BlueZ watch icon/appearance/class, or the WristLink GATT
service. Dual-mode watches that stay on classic Bluetooth are switched to LE so
the WristLink GATT service can be reached. Those models should work if they can
sideload the APK and expose Health Services / step sensors, but they have not
been tested yet.

## Jolla app

```sh
./build.sh
```

That builds the Sailfish RPM in Docker with `mb2` (target
`SailfishOS-5.2.0.15-aarch64`) and copies it to `app/RPMS/`. First run pulls
`coderus/sailfishos-platform-sdk-aarch64` if needed.

Or from the Sailfish SDK:

```sh
mb2 --target SailfishOS-5.2.0.15-aarch64 build
```

Install the phone and watch packages from the same release. Pair the watch
and connect it. On the watch, open WristLink and allow sensors and Bluetooth.
Leave that app running. The phone shows "Connected" while the Bluetooth link
is up. An incoming call rings and vibrates the watch until the call is
answered or missed. Steps update when a notification or call is delivered,
and on their own about every half hour.

Use **Mirrored notifications** in the pulley menu to choose which phone apps
are forwarded to the watch. New apps are mirrored until you turn them off.

Settings can turn the background service off, group watch notifications by
app, skip forwarding while the phone is unlocked, or uninstall WristLink
(configuration under `~/.config` is kept).

## Watch app

```sh
./build.sh watch
```

Or:

```sh
cd watch
./gradlew assembleDebug
```

The APK lands in `watch/app/build/outputs/apk/debug/app-debug.apk`.

Enable Developer options on the watch, turn on ADB debugging over Wi-Fi, then
sideload it:

```sh
adb connect WATCH_IP:5555
adb install -r watch/app/build/outputs/apk/debug/app-debug.apk
```

## Continuous integration

GitHub Actions builds the Sailfish RPM and the Wear OS APK on every push and
pull request to `main`. Pushing a new phone version on `main` (or tagging
`v*`) publishes a GitHub Release with RPMs for aarch64, armv7hl, and i486 plus
the watch APK.

WristLink is independent software and is not affiliated with Samsung, Google,
or Jolla.
