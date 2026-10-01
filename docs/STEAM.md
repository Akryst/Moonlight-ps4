# Steam library companion — Windows

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

Install the updated **01.35** PS4 PKG. The installer replaces **Moonlight PS4
Steam Sync** with a windowless listener started at sign-in. Sign-in does not
synchronize games, and there is no repeating synchronization task. Scripts and
logs live in `C:\ProgramData\MoonlightPS4`. The configured Python executable must
remain installed; rerun the installer to change it.

The PS4 sends one request when it first connects after opening Moonlight, before
loading Games. Returning from a stream or refreshing Games does not synchronize.
The companion listens on TLS port 47991, restricted by the firewall to the local
subnet. It requires the existing Sunshine pairing certificate of an enabled
device named **PS4**; unpaired clients cannot trigger synchronization. It reloads
the pairing state for each connection. Hosts without the companion still work.

## Usage

Install games through Steam. The companion reads manifests from configured
libraries and updates the catalog using Steam launch URIs. Sunshine reload is
deferred while its local server reports an active stream. Close the host session
with **Options** in Games, then close and reopen Moonlight to synchronize again.

Safe reload currently expects Sunshine's default HTTP port 47989 and the Windows
service `SunshineService`. To stop synchronization, disable or delete its scheduled task.

Only entries marked `moonlight-ps4-steam-id` are managed. Updates are atomic and
backed up; incomplete library scans preserve existing managed entries. Missing
artwork falls back to an icon. Portrait PNGs may carry an embedded `mlBg` panorama
for this client's horizontal layouts. The idle companion never launches sync
or PowerShell on a timer; any required service reload runs without a console.
