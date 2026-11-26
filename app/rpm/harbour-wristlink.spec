Name:       harbour-wristlink
Summary:    Bluetooth watch companion for Sailfish OS
Version:    0.7.1
Release:    1
License:    BSD-3-Clause
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   qt5-qtcore
Requires:   qt5-qtdeclarative
Requires:   qt5-qtdbus
Requires:   PackageKit
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  desktop-file-utils

%description
WristLink discovers, pairs, and reads health stats from Bluetooth
watches through BlueZ with a Sailfish-native user interface.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
make %{?_smp_mflags}
mkdir -p daemon
(cd daemon && qmake harbour-wristlinkd.pro && make %{?_smp_mflags})
mkdir -p plugin
(cd plugin && qmake wristlinkplugin.pro && make %{?_smp_mflags})
mkdir -p contactlookup-build
(cd contactlookup-build && qmake ../daemon/contactlookup.pro && make %{?_smp_mflags})

%install
rm -rf %{buildroot}
%qmake5_install
make -C daemon install INSTALL_ROOT=%{buildroot}
make -C plugin install INSTALL_ROOT=%{buildroot}
make -C contactlookup-build install INSTALL_ROOT=%{buildroot}

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
  %{buildroot}%{_datadir}/applications/*.desktop

mkdir -p %{buildroot}/usr/lib/systemd/user/user-session.target.wants
ln -sf ../%{name}.service %{buildroot}/usr/lib/systemd/user/user-session.target.wants/%{name}.service

%post
pkill -x harbour-wristlink >/dev/null 2>&1 || true
if [ -S /run/user/100000/dbus/user_bus_socket ]; then
    su -s /bin/sh defaultuser -c 'export XDG_RUNTIME_DIR=/run/user/100000; export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/100000/dbus/user_bus_socket; /usr/bin/systemctl --user daemon-reload; /usr/bin/systemctl --user restart harbour-wristlink.service' || true
fi

%preun
if [ "$1" = "0" ] && [ -S /run/user/100000/dbus/user_bus_socket ]; then
    su -s /bin/sh defaultuser -c 'export XDG_RUNTIME_DIR=/run/user/100000; export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/100000/dbus/user_bus_socket; /usr/bin/systemctl --user disable --now harbour-wristlink.service' || true
fi

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/%{name}d
%attr(2755,root,privileged) /usr/libexec/harbour-wristlink-contact
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_datadir}/jolla-settings/entries/%{name}.json
/usr/lib/systemd/user/%{name}.service
/usr/lib/systemd/user/user-session.target.wants/%{name}.service
%{_libdir}/qt5/qml/org/wristlink

%changelog
* Thu Nov 26 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.7.1-1
- Recognize Pixel Watch, OnePlus Watch, and other Wear OS devices as watches.

* Tue Nov 17 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.7.0-1
- Add service enable, uninstall, grouped notifications, and skip-while-unlocked settings.

* Tue Nov 17 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.9-1
- Keep the paired Bluetooth connection and send notifications, calls, and health only when needed.

* Tue Oct 06 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.8-1
- Leave the watch connected when health data pauses, instead of dropping the link.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.7-1
- Send the phone battery level to the watch.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.6-1
- Show the contact name on an incoming call when the number is in the phone book.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.5-1
- Ring the watch from the phone call itself. The missed-call notice arrives only after the call has ended.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.4-1
- Send a notification's own color to the watch when the phone provides one.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.3-1
- Restart the watch link from Settings when Bluetooth stops answering.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.2-1
- Open the WristLink pages inside Settings instead of a separate window.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.1-1
- Open WristLink from Settings instead of the application grid.
- Keep reading the notification list after the background service starts.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.6.0-1
- Run the watch link, notification forwarding, and call ringing as a background service.
- Leave the phone app as the window that controls that service.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.9-1
- Read notifications from the apps that actually post them, including mail and Android apps.
- Show the watch's daily step total instead of only the steps taken since WristLink opened.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.8-1
- Open the window while Bluetooth is still answering. The previous build waited on that and never appeared.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.7-1
- Advertise from the phone so the watch can connect. The phone never sees the watch advertisement.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.6-1
- Write the Bluetooth log where the sandbox can actually create it.
- Connect only when the watch app is advertising, not when the paired watch is already on Bluetooth.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.5-1
- Write a Bluetooth log on the phone and connect only after the scan has stopped.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.4-1
- Keep the Bluetooth error on screen and connect to the watch app advertiser.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.3-1
- Attach the watch app link without dropping the normal Bluetooth connection.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.2-1
- Keep the normal Bluetooth link while looking for the watch app.
- Connect Bluetooth LE only after the watch actually advertises WristLink.

* Mon Oct 05 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.1-1
- Start again after the permission prompt by dropping the Phone permission the sandbox rejects.
- Ring the watch from the incoming-call notification instead.

* Sun Oct 04 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.5.0-1
- Connect from the phone to the watch so health data actually flows.
- Ring the watch, with sound and vibration, while the phone is ringing.

* Sat Sep 26 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.4.0-1
- Host WristLink GATT on the phone so the watch can connect over Bluetooth LE.
- Mirror phone notifications to the watch with per-app allow/deny.
- Shrink health payload to fit the default BLE MTU.

* Fri Sep 04 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.3.0-1
- Fall back to the Sailfish pairing dialog when BlueZ refuses a custom agent.
- Ignore in-progress Bluetooth connects instead of failing the watch sync.
- Keep today's step count updating instead of freezing at zero or a stale total.
- Forward phone notifications to the WristLink watch app.

* Tue Sep 01 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.2.1-1
- Connect to GATT with Connect() instead of ConnectProfile().
- Report BlueZ and pairing agent errors instead of failing silently.

* Tue Sep 01 2026 Pauli Kettunen <pauligrinder@gmail.com> - 0.2.0-1
- Read steps, heart rate, calories, distance, floors, and battery from a paired watch.
