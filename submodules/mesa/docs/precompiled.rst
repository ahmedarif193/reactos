Precompiled Libraries
=====================

Mesa does not provide precompiled libraries; that job is left in the hands of
the Linux distributions.

Most distributions pre-install Mesa along with the rest of the graphical user
interface, so you probably already have it installed, and applying your
updates regularly is all you need to do.
If not, searching for ``mesa`` in your package manager should return the relevant
packages (most distributions split the project into multiple packages, you
probably don't need all of them).
Be aware that other projects might also include that name. As always, pay
attention to what you install.

Some distributions (eg. Debian & Ubuntu and their derivatives) usually package
very old versions of software, which are often not supported by their projects
anymore. The current release version is visible near the top of our `homepage
<https://mesa3d.org>`__. If you have any issues (unsupported hardware, bugs,
missing features, performance issues, etc.) the first thing you should try is
to install that version.

Some distributions also provide unofficial packages for unreleased Mesa, but note
that those might break your system. Before installing one of those, make sure
you know how to recover if you can't get a graphical session anymore.

- Debian/Ubuntu based distributions - PPA: xorg-edgers, oibaf and padoka
- Fedora - Copr: erp and che
- OpenSuse/SLES - OBS: X11:XOrg and pontostroy:X11


Debug packages
--------------

Most distributions provide debug packages that contain among other things the
symbols corresponding to the precompiled package they provide.

Installing them will allow you to see the symbol (functions, variables, etc.)
names in situations like a backtrace.
**If you want to report an issue, we recommend installing these packages first
as these symbols allow us to know what is going on.**

Please refer to the corresponding documentation:

- Arch Linux: https://wiki.archlinux.org/title/Debugging/Getting_traces
- Debian: https://wiki.debian.org/HowToGetABacktrace/
- Fedora: https://docs.fedoraproject.org/en-US/quick-docs/bugzilla-providing-a-stacktrace/
- Ubuntu: https://ubuntu.com/server/docs/how-to/debugging/debug-symbol-packages/
