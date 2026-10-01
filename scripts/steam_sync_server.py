"""Wait for an authenticated PS4 opening Moonlight; never synchronize on a timer."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import ssl
import subprocess
import sys
import time


def ps4_certificates(state):
    devices = json.loads(state.read_text(encoding="utf-8-sig"))["root"]["named_devices"]
    return [d["cert"] for d in devices
            if d.get("enabled", True) and d.get("name", "").casefold() == "ps4"]


class SyncServer(HTTPServer):
    # Load pairing state for each connection, so new/revoked PS4 pairings apply
    # without restarting the companion. Idle connections cannot block forever.
    def get_request(self):
        connection, address = super().get_request()
        try:
            connection.settimeout(10)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(self.config / "credentials/cacert.pem",
                                    self.config / "credentials/cakey.pem")
            certs = ps4_certificates(self.config / "sunshine_state.json")
            if not certs:
                raise ssl.SSLError("No enabled device named PS4 is paired")
            context.load_verify_locations(cadata="\n".join(certs))
            context.verify_mode = ssl.CERT_REQUIRED
            context.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
            return context.wrap_socket(connection, server_side=True), address
        except Exception:
            connection.close()
            raise OSError("PS4 authentication failed") from None


class SyncHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/moonlight-ps4/open" or not self.headers.get("User-Agent", "").startswith("moonlight-ps4/"):
            self.send_error(404)
            return
        server = self.server
        # A failed/retried connection at startup must not repeatedly reload.
        if time.monotonic() - server.last_request < 10:
            self.reply(200, "Already synchronized for this opening")
            return
        try:
            if server.log.exists() and server.log.stat().st_size > 1048576:
                server.log.replace(server.log.with_suffix(".log.1"))
            with server.log.open("a", encoding="utf-8") as log:
                log.write(time.strftime("\n%Y-%m-%d %H:%M:%S PS4 opened Moonlight\n"))
                log.flush()
                result = subprocess.run(server.command, stdout=log, stderr=log,
                                        timeout=45, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if result.returncode:
                self.reply(500, "Synchronization failed; see steam-sync.log")
                return
            server.last_request = time.monotonic()
            self.reply(200, "Steam catalog synchronized")
        except (OSError, subprocess.TimeoutExpired):
            self.reply(500, "Synchronization failed; see steam-sync.log")

    def reply(self, status, message):
        body = message.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steam", required=True, type=Path)
    parser.add_argument("--apps", required=True, type=Path)
    parser.add_argument("--port", type=int, default=47991)
    args = parser.parse_args()
    server = SyncServer(("0.0.0.0", args.port), SyncHandler)
    server.config = args.apps.parent
    server.log = Path(__file__).with_name("steam-sync.log")
    server.last_request = float("-inf")
    python = Path(sys.executable).with_name("python.exe")
    server.command = [str(python), str(Path(__file__).with_name("steam_sync.py")),
                      "--steam", str(args.steam), "--apps", str(args.apps),
                      "--apply", "--safe-reload"]
    server.serve_forever()


if __name__ == "__main__":
    main()
