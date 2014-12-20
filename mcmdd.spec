Name:           mcmdd
Version:        1.0.0
Release:        1%{?dist}
Summary:        Game server manager

Group:          System Environment/Daemons
License:        GPLv2+
URL:            https://cmastudios.me/mcmdd
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake,gcc-c++,systemd

%description
mcmdd monitors, controls, and restarts other application servers. It has
been designed specifically for control of game servers, specifically
Minecraft servers.

%prep
%setup -q


%build
cmake -DWITH_SYSTEMD=true -DSYSTEMD_LIB_INSTALL_DIR=%{_unitdir} -DCMAKE_INSTALL_PREFIX:PATH=%{_prefix} .
make %{?_smp_mflags}


%install
make install DESTDIR=%{buildroot}

%post
if ! id mcmdd > /dev/null 2>&1 ; then
        useradd -M -r -d /var/lib/mcmdd -U mcmdd
fi
chown -R mcmdd:mcmdd /var/lib/mcmdd
systemctl enable mcmdd

%files
%doc
/usr/bin/mcmdd
/var/lib/mcmdd/mcmdd.conf
%{_unitdir}/mcmdd.service



%changelog
* Sun Dec 14 2014 Connor Monahan <admin@cmastudios.me> 1.0.0-1
- Initial release

