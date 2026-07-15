"""CLI-Einstieg: canboss-monitor bzw. python -m canboss_monitor."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def _default_eds_dir() -> Path:
    """eds/ im Repo finden (Aufruf aus dem Monorepo), sonst cwd."""
    for base in (Path(__file__).resolve().parents[2].parent, Path.cwd()):
        cand = base / "eds"
        if (cand / "network.json").exists():
            return cand
    return Path.cwd() / "eds"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="canboss-monitor",
        description="CANboss Monitor - CANopen-Parameter-TUI (Textual + canopen)")
    parser.add_argument("--eds-dir", type=Path, default=_default_eds_dir(),
                        help="Verzeichnis mit network.json + EDS-Dateien (Default: eds/ im Repo)")
    parser.add_argument("--interface", default="socketcan",
                        help="python-can Interface (Default: socketcan)")
    parser.add_argument("--channel", default="vcan0",
                        help="CAN-Kanal, z.B. vcan0/can0 (Default: vcan0)")
    parser.add_argument("--bitrate", type=int, default=None,
                        help="Bitrate (nur fuer Interfaces, die sie brauchen, z.B. serial/slcan)")
    parser.add_argument("--offline", action="store_true",
                        help="ohne CAN-Verbindung starten (nur EDS ansehen)")
    args = parser.parse_args(argv)

    try:
        from .net import MonitorNet
        net = MonitorNet(args.eds_dir)
    except FileNotFoundError as exc:
        print(f"Fehler: {exc}", file=sys.stderr)
        print("Hinweis: --eds-dir auf das Verzeichnis mit network.json zeigen lassen.",
              file=sys.stderr)
        return 2

    if not args.offline:
        kwargs = {}
        if args.bitrate is not None:
            kwargs["bitrate"] = args.bitrate
        err = net.connect(interface=args.interface, channel=args.channel, **kwargs)
        if err is not None:
            print(f"Warnung: CAN-Verbindung fehlgeschlagen ({err})", file=sys.stderr)
            print("Der Monitor startet offline (nur EDS-Ansicht).", file=sys.stderr)

    from .app import MonitorApp
    try:
        MonitorApp(net).run()
    finally:
        net.disconnect()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
