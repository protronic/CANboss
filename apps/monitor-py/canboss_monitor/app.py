"""Textual-TUI des CANboss-Monitors.

Zwei Ebenen wie im C-Monitor: Knotenliste -> Datenpunkt-Tabelle.
Alle SDO-Transfers laufen in Worker-Threads (canopen blockiert),
die UI bleibt dabei bedienbar.
"""

from __future__ import annotations

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen, Screen
from textual.widgets import Button, DataTable, Footer, Header, Input, Label, Static

from .net import Datapoint, MonitorNet, NodeDef

#: Auto-Refresh: alle REFRESH_PERIOD_S werden READS_PER_TICK Datenpunkte
#: (rundum) neu gelesen - gleiche Mechanik wie der C-Monitor.
REFRESH_PERIOD_S = 0.25
READS_PER_TICK = 4


class WriteModal(ModalScreen[str | None]):
    """Werteingabe fuer einen SDO-Download (Enter = schreiben, Esc = abbrechen)."""

    BINDINGS = [Binding("escape", "cancel", "Abbrechen")]

    def __init__(self, dp: Datapoint) -> None:
        super().__init__()
        self.dp = dp

    def compose(self) -> ComposeResult:
        with Vertical(id="write-box"):
            yield Label(f"{self.dp.key}  {self.dp.name}", id="write-title")
            hint = self.dp.type_name
            if self.dp.limits:
                hint += f"  [{self.dp.limits}]"
            yield Label(hint, id="write-hint")
            yield Input(value=self.dp.value, placeholder="neuer Wert", id="write-input")
            with Horizontal(id="write-buttons"):
                yield Button("Schreiben", variant="primary", id="write-ok")
                yield Button("Abbrechen", id="write-cancel")

    def on_mount(self) -> None:
        self.query_one("#write-input", Input).focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        self.dismiss(event.value)

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "write-ok":
            self.dismiss(self.query_one("#write-input", Input).value)
        else:
            self.dismiss(None)

    def action_cancel(self) -> None:
        self.dismiss(None)


class NodeScreen(Screen):
    """Datenpunkt-Tabelle eines Knotens mit Filter, Lesen/Schreiben, Auto-Refresh."""

    BINDINGS = [
        Binding("escape,q", "back", "Zurueck"),
        Binding("r", "read_one", "Lesen"),
        Binding("R", "read_all", "Alles lesen"),
        Binding("w", "write", "Schreiben"),
        Binding("m", "toggle_refresh", "Auto-Refresh"),
        Binding("slash", "focus_filter", "Filter", key_display="/"),
    ]

    def __init__(self, net: MonitorNet, node: NodeDef) -> None:
        super().__init__()
        self.net = net
        self.node = node
        self.rows: list[Datapoint] = []       # aktuell angezeigte (gefilterte) Reihenfolge
        self.auto_refresh_on = True
        self._rr_pos = 0                      # Round-Robin-Position des Auto-Refresh
        self._busy: set[str] = set()          # keys mit laufendem Transfer

    # ------------------------------------------------------------- Aufbau

    def compose(self) -> ComposeResult:
        yield Header(show_clock=False)
        yield Input(placeholder="Filter (Name oder Index) ...", id="filter")
        table = DataTable(id="dps", cursor_type="row", zebra_stripes=True)
        yield table
        yield Static("", id="status")
        yield Footer()

    def on_mount(self) -> None:
        self.sub_title = f"Node {self.node.node_id}  {self.node.name}"
        table = self.query_one("#dps", DataTable)
        table.add_columns("Index", "Name", "Typ", "Zugriff", "Limits", "Wert")
        self._fill_table("")
        table.focus()
        self.set_interval(REFRESH_PERIOD_S, self._tick)

    def _fill_table(self, needle: str) -> None:
        table = self.query_one("#dps", DataTable)
        table.clear()
        needle = needle.strip().lower()
        self.rows = []
        for dp in self.node.datapoints:
            if needle and needle not in dp.name.lower() and needle not in dp.key.lower():
                continue
            self.rows.append(dp)
            table.add_row(dp.key, dp.name, dp.type_name, dp.access, dp.limits,
                          dp.value or "--", key=dp.key)
        self._status(f"{len(self.rows)} Datenpunkte")

    # ------------------------------------------------------------- Hilfen

    def _status(self, text: str) -> None:
        refresh = "AUTO" if self.auto_refresh_on else "man."
        online = self.net.node_online(self.node.node_id)
        hb = {True: "online", False: "OFFLINE", None: "kein Heartbeat"}[online]
        conn = self.net.channel if self.net.connected else "offline"
        self.query_one("#status", Static).update(
            f" {conn} | {hb} | Refresh: {refresh} | {text}")

    def _selected(self) -> Datapoint | None:
        table = self.query_one("#dps", DataTable)
        if not self.rows or table.cursor_row is None or table.cursor_row >= len(self.rows):
            return None
        return self.rows[table.cursor_row]

    def _set_value_cell(self, dp: Datapoint, text: str) -> None:
        table = self.query_one("#dps", DataTable)
        try:
            table.update_cell(dp.key, table.ordered_columns[5].key, text)
        except Exception:  # noqa: BLE001 - Zeile ist ggf. weggefiltert
            pass

    # ------------------------------------------------------- SDO-Transfers

    def _read_dp(self, dp: Datapoint) -> None:
        """Blockierender SDO-Read (laeuft im Worker-Thread)."""
        try:
            value = self.net.sdo_read(self.node.node_id, dp)
            self.app.call_from_thread(self._set_value_cell, dp, value)
        except Exception as exc:  # noqa: BLE001 - Abort/Timeout anzeigen
            self.app.call_from_thread(self._set_value_cell, dp, "--")
            self.app.call_from_thread(self._status, f"{dp.key}: {exc}")
        finally:
            self._busy.discard(dp.key)

    def _write_dp(self, dp: Datapoint, text: str) -> None:
        try:
            value = self.net.sdo_write(self.node.node_id, dp, text)
            self.app.call_from_thread(self._set_value_cell, dp, value)
            self.app.call_from_thread(self._status, f"{dp.key} geschrieben")
        except Exception as exc:  # noqa: BLE001
            self.app.call_from_thread(self._status, f"{dp.key}: {exc}")
        finally:
            self._busy.discard(dp.key)

    def _submit_read(self, dp: Datapoint) -> None:
        if not self.net.connected or not dp.readable or dp.key in self._busy:
            return
        self._busy.add(dp.key)
        self.app.run_worker(lambda: self._read_dp(dp), thread=True, group="sdo")

    def _tick(self) -> None:
        """Auto-Refresh: naechste READS_PER_TICK lesbare Datenpunkte anstossen."""
        self._status("")
        if not self.auto_refresh_on or not self.net.connected or not self.rows:
            return
        started = 0
        for _ in range(len(self.rows)):
            dp = self.rows[self._rr_pos % len(self.rows)]
            self._rr_pos += 1
            if dp.readable and dp.key not in self._busy:
                self._submit_read(dp)
                started += 1
                if started >= READS_PER_TICK:
                    break

    # ------------------------------------------------------------ Aktionen

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "filter":
            self._fill_table(event.value)

    def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id == "filter":
            self.query_one("#dps", DataTable).focus()

    def action_back(self) -> None:
        # Escape im Filterfeld: nur zur Tabelle zurueck, nicht den Screen verlassen
        if self.focused is self.query_one("#filter", Input):
            self.query_one("#dps", DataTable).focus()
            return
        self.app.pop_screen()

    def action_focus_filter(self) -> None:
        self.query_one("#filter", Input).focus()

    def action_read_one(self) -> None:
        dp = self._selected()
        if dp is not None:
            self._submit_read(dp)

    def action_read_all(self) -> None:
        for dp in self.rows:
            self._submit_read(dp)

    def action_toggle_refresh(self) -> None:
        self.auto_refresh_on = not self.auto_refresh_on
        self._status("")

    def action_write(self) -> None:
        dp = self._selected()
        if dp is None or not dp.writable:
            self._status("Datenpunkt ist nicht schreibbar")
            return
        if not self.net.connected:
            self._status("nicht verbunden")
            return

        def on_result(text: str | None) -> None:
            if text is None or dp.key in self._busy:
                return
            self._busy.add(dp.key)
            self.app.run_worker(lambda: self._write_dp(dp, text), thread=True, group="sdo")

        self.app.push_screen(WriteModal(dp), on_result)

    def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        self.action_write() if self._selected() and self._selected().writable else self.action_read_one()


class MonitorApp(App):
    """CANboss Monitor - Knotenuebersicht als Startscreen."""

    TITLE = "CANboss Monitor"
    CSS = """
    #filter { margin: 0 1; }
    #status { height: 1; background: $surface; color: $text-muted; }
    #nodes, #dps { height: 1fr; margin: 0 1; }
    WriteModal { align: center middle; }
    #write-box {
        width: 60; height: auto; padding: 1 2;
        background: $panel; border: thick $primary;
    }
    #write-title { text-style: bold; }
    #write-hint { color: $text-muted; margin-bottom: 1; }
    #write-buttons { height: auto; align-horizontal: right; }
    #write-buttons Button { margin-left: 2; }
    """

    BINDINGS = [
        Binding("q", "quit", "Beenden"),
        Binding("enter", "open_node", "Oeffnen", show=False),
    ]

    def __init__(self, net: MonitorNet) -> None:
        super().__init__()
        self.net = net

    def compose(self) -> ComposeResult:
        yield Header(show_clock=False)
        table = DataTable(id="nodes", cursor_type="row", zebra_stripes=True)
        yield table
        yield Static("", id="status")
        yield Footer()

    def on_mount(self) -> None:
        self.sub_title = self.net.channel if self.net.connected else "offline (nur EDS-Ansicht)"
        table = self.query_one("#nodes", DataTable)
        table.add_columns("Node-ID", "Name", "EDS", "Datenpunkte", "Status")
        for node in self.net.nodes:
            table.add_row(str(node.node_id), node.name, node.eds_file,
                          str(len(node.datapoints)), "?", key=str(node.node_id))
        table.focus()
        self.set_interval(1.0, self._update_status)

    def _update_status(self) -> None:
        table = self.query_one("#nodes", DataTable)
        status_key = table.ordered_columns[4].key
        for node in self.net.nodes:
            online = self.net.node_online(node.node_id)
            text = {True: "online", False: "OFFLINE", None: "?"}[online]
            try:
                table.update_cell(str(node.node_id), status_key, text)
            except Exception:  # noqa: BLE001
                pass

    def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        if event.data_table.id != "nodes":
            return
        node_id = int(event.row_key.value)
        for node in self.net.nodes:
            if node.node_id == node_id:
                self.push_screen(NodeScreen(self.net, node))
                return
