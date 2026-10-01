# Moonlight PS4 01.35 — Experimental

Steam library synchronization now runs once when the PS4 opens Moonlight and
connects to the PC, before the Games list loads. The companion no longer scans
at sign-in or every five minutes, preventing recurring PowerShell flashes while
playing. Returning from streaming or refreshing Games does not trigger a scan.

## Upgrade

Install `Moonlight-PS4-01.35-test.pkg` with GoldHEN Package Installer and rerun
`install_steam_sync.ps1` from `Moonlight-PS4-Steam-Companion-01.35.zip` in an
administrator PowerShell session. Both parts must be updated; earlier PS4 clients
do not send the opening request. The title ID remains **MLNT00001** and saved
settings remain in `/data/moonlight`.

The installer replaces the old repeating task with an idle, windowless listener.
Windows sign-in starts only this listener. TLS port **47991** is restricted to the
local subnet by the firewall and requires the Sunshine pairing certificate of an
enabled device named **PS4**. Pairing changes are checked on each connection.

The companion preserves manual applications and synchronizes Steam manifests
and local artwork. Sunshine service reload is deferred while a session is active;
service restarts run without a console. To retry a deferred reload, close the host
session and reopen Moonlight. Hosts without the companion still support normal
Sunshine streaming.

## Assets

- `Moonlight-PS4-01.35-test.pkg`: updated PS4 client.
- `Moonlight-PS4-Steam-Companion-01.35.zip`: Windows installer, scripts and setup guide; requires Python with Pillow.
- `SHA256SUMS`: SHA-256 checksums for both assets.

## Validation and limitations

OpenOrbis compilation and packaging, host checks, companion request/deduplication
checks and TLS authentication checks passed. Paired PS4 certificates are accepted;
anonymous clients and revoked pairings are rejected. The Windows listener was
installed and verified running without a repetition trigger on the development PC.

The opening flow still requires end-to-end testing on a real PS4. The 01.34
presentation changes remain included and require console comparison. No new
streaming performance gain is claimed. Compatibility beyond the original PS4 on
firmware 9.00 with GoldHEN is unverified. Local IPv4 discovery, keyboard/mouse and
native YCbCr limitations remain unchanged.

This is an unofficial client, independent of the official Moonlight project.
Existing dependency licenses and notices apply.
