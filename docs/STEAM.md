# Steam and Playnite companion for Windows

The PS4 reads Sunshine's app list. This optional companion scans installed Steam
games on Windows and publishes them to Sunshine, including local artwork when
available. Desktop, Big Picture and manually configured applications are preserved.

## Install

Install Python and Pillow (`python -m pip install Pillow`). From the client root,
run the installer in an administrator PowerShell session, replacing the Python
path with your installation:

```powershell
.\scripts\install_steam_sync.ps1 `
  -PythonPath 'C:\Path\To\python.exe' `
  -SteamPath 'C:\Program Files (x86)\Steam'
```

Optional `-AppsPath` selects another Sunshine `apps.json`; the default is
`C:\Program Files\Sunshine\config\apps.json`.

Install the **01.40** PS4 PKG. The installer replaces **Moonlight PS4
Steam Sync** with a windowless listener started at sign-in. Sign-in does not
synchronize games, and there is no repeating synchronization task. Scripts and
logs live in `C:\ProgramData\MoonlightPS4`. The configured Python executable must
remain installed; rerun the installer to change it.

The PS4 sends one background request when it first connects after opening
Moonlight. The library refreshes when synchronization finishes. Returning from a stream or refreshing Games does not synchronize.
The companion listens on TLS port 47991, restricted by the firewall to the local
subnet. It requires the existing Sunshine pairing certificate of an enabled
device named **PS4**; unpaired clients cannot trigger synchronization. It reloads
the pairing state for each connection. Hosts without the companion still work.

## Usage

Install games through Steam. The companion reads manifests from configured
libraries and updates the catalog using Steam launch URIs. Sunshine reload is
deferred while its local server reports an active stream. Close the host session
with **Options** in Library, then close and reopen Moonlight to synchronize again.

Safe reload currently expects Sunshine's default HTTP port 47989 and the Windows
service `SunshineService`. To stop synchronization, disable or delete its scheduled task.

Only entries marked `moonlight-ps4-steam-id` or `moonlight-ps4-launcher` are managed. Updates are atomic and
backed up; incomplete library scans preserve existing managed entries. Missing
artwork falls back to an icon. Portrait PNGs may carry an embedded `mlBg` panorama
for this client's horizontal layouts. The idle companion never launches sync
or PowerShell on a timer; any required service reload runs without a console.

## Playnite

The 1.4.0 companion detects installed `Playnite.FullscreenApp.exe` in standard
locations and the Windows uninstall registry. It checks user profile locations
when running as SYSTEM. If found, it adds a **Playnite** launcher to Sunshine;
the existing PS4 application list then shows it alongside Steam and Desktop.
The launcher opens [Playnite's fullscreen interface](https://api.playnite.link/docs/manual/gettingStarted/configuringPlaynite.html).
It does not import or modify the Playnite database.

Manual Playnite entries are preserved without duplication. Uninstalling Playnite
removes only the companion-managed launcher on the next synchronization. Portable
locations can be supplied to the standalone synchronizer with `--playnite`;
automatic detection is limited to the standard and registered install locations.

To update an already installed companion, run `update-companion.bat` as
administrator. It verifies the replacement and makes a backup without changing
the listener, schedule, certificates or firewall rules. Close the active session
in Moonlight, then close and reopen the client to synchronize and reload the
updated catalog. Reload remains deferred while a host session is active.
