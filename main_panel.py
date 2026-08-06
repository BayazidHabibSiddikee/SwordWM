"""
main_panel.py — left+center panel with browser-style closeable tabs.

Tab bar lives just below the clock strip.  Graph tab is always pinned
(no × button).  Browser / SwordFM / Terminal tabs are opened on first
click and can be closed with ×, which kills the underlying process and
frees the RAM.

Theme colours are published via the module-level THEME dict and can be
swapped at runtime by right_panel calling apply_theme().
"""

import os, json, subprocess, shutil
from datetime import datetime

from PySide6.QtWidgets import (
    QWidget, QPushButton, QStackedWidget,
    QVBoxLayout, QHBoxLayout, QLabel,
    QLineEdit, QSizePolicy, QFrame,
)
from PySide6.QtCore  import Qt, QTimer, QRect, QFileSystemWatcher
from PySide6.QtGui   import (
    QPainter, QColor, QFont, QPixmap,
    QFontMetrics, QPen, QWindow,
)

from pipes_layer import PipesLayer

# ── paths ─────────────────────────────────────────────────────────────────────
CFG_DIR    = os.path.expanduser("~/.config/animated-wallpaper")
GRAPH_PNG  = os.path.join(CFG_DIR, "graph.png")
GRAPH_JSON = os.path.join(CFG_DIR, "graph.json")
HERE       = os.path.dirname(os.path.abspath(__file__))

# ── theme ─────────────────────────────────────────────────────────────────────
# Right panel calls apply_theme() to swap these at runtime.
THEME = {
    "bg":        QColor(40,  44,  52),
    "bg2":       QColor(33,  37,  43),
    "cyan":      QColor(97,  175, 239),
    "green":     QColor(152, 195, 121),
    "dim":       QColor(62,  68,  81),
    "white":     QColor(171, 178, 191),
    "tab_act":   QColor(97,  175, 239),
    "tab_inact": QColor(62,  68,  81),
}

PRESETS = {
    "Dark (default)": {
        "bg": QColor(40,44,52),   "bg2": QColor(33,37,43),
        "cyan": QColor(97,175,239), "green": QColor(152,195,121),
        "dim": QColor(62,68,81),    "white": QColor(171,178,191),
        "tab_act": QColor(97,175,239), "tab_inact": QColor(62,68,81),
    },
    "Nord": {
        "bg": QColor(46,52,64),   "bg2": QColor(39,44,55),
        "cyan": QColor(136,192,208), "green": QColor(163,190,140),
        "dim": QColor(76,86,106),    "white": QColor(216,222,233),
        "tab_act": QColor(136,192,208), "tab_inact": QColor(76,86,106),
    },
    "Gruvbox": {
        "bg": QColor(40,40,40),   "bg2": QColor(29,32,33),
        "cyan": QColor(131,165,152), "green": QColor(184,187,38),
        "dim": QColor(80,73,69),     "white": QColor(235,219,178),
        "tab_act": QColor(250,189,47), "tab_inact": QColor(80,73,69),
    },
    "Dracula": {
        "bg": QColor(40,42,54),   "bg2": QColor(33,34,44),
        "cyan": QColor(139,233,253), "green": QColor(80,250,123),
        "dim": QColor(68,71,90),     "white": QColor(248,248,242),
        "tab_act": QColor(189,147,249), "tab_inact": QColor(68,71,90),
    },
    "Monokai": {
        "bg": QColor(39,40,34),   "bg2": QColor(30,31,26),
        "cyan": QColor(102,217,239), "green": QColor(166,226,46),
        "dim": QColor(73,72,62),     "white": QColor(248,248,242),
        "tab_act": QColor(249,38,114), "tab_inact": QColor(73,72,62),
    },
    "Solarized": {
        "bg": QColor(0,43,54),    "bg2": QColor(7,54,66),
        "cyan": QColor(42,161,152), "green": QColor(133,153,0),
        "dim": QColor(88,110,117),   "white": QColor(131,148,150),
        "tab_act": QColor(38,139,210), "tab_inact": QColor(88,110,117),
    },
}

def apply_theme(name: str):
    """Called by right_panel to switch colour theme globally."""
    preset = PRESETS.get(name)
    if preset:
        THEME.update(preset)

# ── tab IDs ───────────────────────────────────────────────────────────────────
TAB_GRAPH   = "graph"
TAB_BROWSER = "browser"
TAB_FM      = "fm"
TAB_TERM    = "terminal"

CLOCK_H = 96   # clock + date + separator
TAB_H   = 32   # tab bar height
TELE_H  = 38   # telemetry strip (graph page only)


# ═════════════════════════════════════════════════════════════════════════════
#  GraphPage
# ═════════════════════════════════════════════════════════════════════════════
class GraphPage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self._pipes = PipesLayer(self)
        self._px    = None
        self._nodes = 0
        self._edges = 0
        self._load_graph()
        QTimer(self, timeout=self.update, interval=1000).start()
        self._watcher = QFileSystemWatcher([GRAPH_PNG, GRAPH_JSON], self)
        self._watcher.fileChanged.connect(self._reload)

    def _load_graph(self):
        try:
            self._px = QPixmap(GRAPH_PNG)
        except Exception:
            self._px = None
        try:
            d = json.load(open(GRAPH_JSON))
            self._nodes = len(d.get("nodes", []))
            self._edges = len(d.get("edges", []))
        except Exception:
            pass

    def _reload(self, _=None):
        self._load_graph(); self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setRenderHint(QPainter.SmoothPixmapTransform)
        w, h = self.width(), self.height()
        self._pipes.paint(p)

        gh = h - TELE_H
        if self._nodes and self._px and not self._px.isNull():
            scaled = self._px.size().scaled(w, gh, Qt.KeepAspectRatio)
            dst = QRect(0, 0, scaled.width(), scaled.height())
            dst.moveLeft((w - scaled.width()) // 2)
            dst.moveTop((gh - scaled.height()) // 2)
            p.drawPixmap(dst, self._px, self._px.rect())
        else:
            p.setPen(THEME["dim"])
            p.setFont(QFont("JetBrains Mono", 11))
            p.drawText(QRect(0, 0, w, gh), Qt.AlignCenter, "[ no graph ]")

        # telemetry
        sy = h - TELE_H + 4
        p.setPen(QPen(THEME["dim"], 1)); p.drawLine(16, sy, w-16, sy)
        p.setFont(QFont("JetBrains Mono", 9))
        try:
            secs = float(open("/proc/uptime").read().split()[0])
            upt  = f"{int(secs//3600)}h {int((secs%3600)//60)}m"
        except Exception:
            upt = "?"
        try:
            kern = os.uname().release.split("-")[0]
        except Exception:
            kern = "?"
        tx = 16
        p.setPen(THEME["green"])
        s = f"◈ Nodes: {self._nodes}   Links: {self._edges}"
        p.drawText(tx, sy+16, s); tx += QFontMetrics(p.font()).horizontalAdvance(s)+30
        p.setPen(THEME["white"])
        s = f"◈ Uptime: {upt}"
        p.drawText(tx, sy+16, s); tx += QFontMetrics(p.font()).horizontalAdvance(s)+30
        p.setPen(THEME["cyan"]); p.drawText(tx, sy+16, f"◈ Kernel: {kern}")
        p.end()


# ═════════════════════════════════════════════════════════════════════════════
#  BrowserPage
# ═════════════════════════════════════════════════════════════════════════════
class BrowserPage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._ready = False

    def ensure_init(self):
        if self._ready:
            return
        self._ready = True
        try:
            from PySide6.QtWebEngineWidgets import QWebEngineView
            from PySide6.QtWebEngineCore    import QWebEngineSettings
        except ImportError:
            lay = QVBoxLayout(self)
            lbl = QLabel("QtWebEngine not installed", self)
            lbl.setAlignment(Qt.AlignCenter)
            lbl.setStyleSheet("color:#e06c75; font:11pt 'JetBrains Mono';")
            lay.addWidget(lbl)
            return

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        # address bar
        bar = QWidget(self)
        bar.setFixedHeight(30)
        bar.setStyleSheet("background:rgba(30,34,42,230);")
        bl = QHBoxLayout(bar)
        bl.setContentsMargins(6, 0, 6, 0)
        bl.setSpacing(4)

        self._addr = QLineEdit(bar)
        self._addr.setPlaceholderText("Search or enter URL…")
        self._addr.setStyleSheet("""
            QLineEdit { color:#abb2bf; background:rgba(62,68,81,200);
                border:1px solid #3e4451; border-radius:3px;
                font:9pt 'JetBrains Mono'; padding:2px 8px; }
            QLineEdit:focus { border-color:#61afef; }""")
        self._addr.returnPressed.connect(self._navigate)
        # Override-redirect window needs explicit X11 focus grab on click
        self._addr.installEventFilter(self)
        bl.addWidget(self._addr, 1)

        self._view = QWebEngineView(self)
        for txt, slot in [("←", self._view.back),
                          ("→", self._view.forward),
                          ("↺", self._view.reload)]:
            b = QPushButton(txt, bar)
            b.setFixedWidth(26)
            b.setStyleSheet("""QPushButton{color:#abb2bf;background:rgba(62,68,81,150);
                border:1px solid #3e4451;border-radius:3px;font:bold 10pt 'JetBrains Mono';}
                QPushButton:hover{background:rgba(97,175,239,200);}""")
            b.clicked.connect(slot)
            bl.addWidget(b)

        lay.addWidget(bar)
        s = self._view.settings()
        s.setAttribute(QWebEngineSettings.JavascriptEnabled, True)
        s.setAttribute(QWebEngineSettings.LocalStorageEnabled, True)
        self._view.urlChanged.connect(lambda u: self._addr.setText(u.toString()))
        lay.addWidget(self._view, 1)
        self._view.load("https://duckduckgo.com")
        self._addr.setText("https://duckduckgo.com")

    def _navigate(self):
        text = self._addr.text().strip()
        if not text:
            return
        if "." in text and " " not in text and not text.startswith("http"):
            text = "https://" + text
        elif not text.startswith("http"):
            text = "https://duckduckgo.com/?q=" + text.replace(" ", "+")
        self._view.load(text)

    def _grab_x11_focus(self):
        """Grab X keyboard focus for the override-redirect cyberdeck window."""
        try:
            w = self.window()
            wid = int(w.winId()) if w else None
            if not wid:
                return
            import Xlib.display, Xlib.X
            d = Xlib.display.Display()
            xw = d.create_resource_object('window', wid)
            d.set_input_focus(xw, Xlib.X.RevertToParent, Xlib.X.CurrentTime)
            d.sync(); d.close()
        except Exception:
            pass

    def eventFilter(self, obj, event):
        if obj is self._addr:
            from PySide6.QtCore import QEvent
            if event.type() == QEvent.MouseButtonPress:
                self._grab_x11_focus()
                self._addr.setFocus(Qt.MouseFocusReason)
        return super().eventFilter(obj, event)

    def showEvent(self, e):
        super().showEvent(e)
        self.ensure_init()

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), THEME["bg"])
        p.end()


# ═════════════════════════════════════════════════════════════════════════════
#  EmbedPage — X11-reparented process (SwordFM / Terminal)
# ═════════════════════════════════════════════════════════════════════════════
class EmbedPage(QWidget):
    def __init__(self, cmd: list, search_args: list | None = None, parent=None):
        super().__init__(parent)
        self._cmd         = cmd
        self._search_args = search_args or []
        self._proc        = None
        self._container   = None
        self._wid         = None
        self._embedded    = False
        self._attempts    = 0

        self._lay = QVBoxLayout(self)
        self._lay.setContentsMargins(0, 0, 0, 0)

        self._placeholder = QLabel("⏳ launching…", self)
        self._placeholder.setAlignment(Qt.AlignCenter)
        self._placeholder.setStyleSheet(
            "color:#61afef; font:bold 12pt 'JetBrains Mono';")
        self._lay.addWidget(self._placeholder)

        self._poll = QTimer(self, interval=100)  # fast poll — grab window before i3 tiles it
        self._poll.timeout.connect(self._try_embed)

    def launch(self):
        if self._proc and self._proc.poll() is None:
            return
        self._embedded = False
        self._attempts = 0
        self._wid      = None
        self._placeholder.setText("⏳ launching…")
        self._placeholder.show()
        if self._container:
            self._container.hide()
        self._proc = subprocess.Popen(
            self._cmd, env=os.environ.copy(),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self._poll.start()

    def terminate(self):
        self._poll.stop()
        if self._proc:
            try: self._proc.terminate()
            except Exception: pass
            self._proc = None
        self._embedded = False
        self._wid = None
        if self._container:
            self._container.deleteLater()
            self._container = None
        self._placeholder.setText("⏳ launching…")
        self._placeholder.show()

    def _try_embed(self):
        if self._embedded:
            self._poll.stop(); return
        self._attempts += 1
        if self._attempts > 60:   # 6s at 100ms
            self._poll.stop()
            self._placeholder.setText(
                f"❌ Could not embed\n{' '.join(self._cmd)}")
            return
        wid = self._find_wid()
        if wid:
            # Unmap immediately so the window never appears full-screen
            try:
                subprocess.call(["xdotool","windowunmap",str(wid)],timeout=1)
            except Exception:
                pass
            self._do_embed(wid)

    def _find_wid(self):
        if self._proc:
            try:
                out = subprocess.check_output(
                    ["xdotool","search","--pid",str(self._proc.pid)],
                    timeout=1, stderr=subprocess.DEVNULL, text=True).strip()
                if out: return int(out.splitlines()[-1])
            except Exception: pass
        if self._search_args:
            try:
                out = subprocess.check_output(
                    ["xdotool","search"]+self._search_args,
                    timeout=1, stderr=subprocess.DEVNULL, text=True).strip()
                if out: return int(out.splitlines()[-1])
            except Exception: pass
        return None

    def _do_embed(self, wid):
        try:
            # Unmap (hide) the window before reparenting so it never flashes
            # full-screen while we're setting things up
            subprocess.call(["xdotool","windowunmap",str(wid)],timeout=2)

            subprocess.call(["xdotool","windowreparent",str(wid),str(int(self.winId()))],timeout=2)
            subprocess.call(["xdotool","set_window","--overrideredirect","1",str(wid)],timeout=2)
            w, h = max(1,self.width()), max(1,self.height())
            subprocess.call(["xdotool","windowsize",str(wid),str(w),str(h)],timeout=2)
            subprocess.call(["xdotool","windowmove",str(wid),"0","0"],timeout=2)

            # Now re-map it — it appears clipped inside our container
            subprocess.call(["xdotool","windowmap",str(wid)],timeout=2)

            qwin = QWindow.fromWinId(wid)
            if qwin is None:
                self._placeholder.setText("❌ QWindow.fromWinId failed"); return
            qwin.setFlags(Qt.FramelessWindowHint)
            c = QWidget.createWindowContainer(qwin, self)
            c.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
            c.setMinimumSize(1,1)
            self._lay.removeWidget(self._placeholder)
            self._placeholder.hide()
            self._lay.addWidget(c, 1)
            self._container = c
            self._wid       = wid
            self._embedded  = True
            self._poll.stop()
            QTimer.singleShot(200, c.setFocus)
        except Exception as e:
            self._placeholder.setText(f"❌ {e}")

    def resizeEvent(self, e):
        super().resizeEvent(e)
        if self._embedded and self._wid:
            w, h = max(1,self.width()), max(1,self.height())
            subprocess.call(["xdotool","windowsize",str(self._wid),str(w),str(h)],timeout=1)

    def showEvent(self, e):
        super().showEvent(e)
        if self._embedded and self._container:
            self._container.show()
        elif not self._embedded:
            self.launch()

    def hideEvent(self, e):
        super().hideEvent(e)
        if self._container: self._container.hide()

    def paintEvent(self, _):
        p = QPainter(self); p.fillRect(self.rect(), THEME["bg"]); p.end()


# ═════════════════════════════════════════════════════════════════════════════
#  TabBar — browser-style tab strip with × close buttons
# ═════════════════════════════════════════════════════════════════════════════
class TabBar(QWidget):
    """
    Paints a row of tabs.  Graph tab is always pinned (no ×).
    Browser / FM / Terminal are created on first open and have a × button.
    Signals are routed via callbacks passed from MainPanel.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedHeight(TAB_H)
        # tab_id → {"label": str, "closeable": bool, "btn": QPushButton, "close_btn": QPushButton|None}
        self._tabs: dict[str, dict] = {}
        self._active = TAB_GRAPH
        self._on_switch = None   # callback(tab_id)
        self._on_close  = None   # callback(tab_id)

        self._lay = QHBoxLayout(self)
        self._lay.setContentsMargins(8, 2, 8, 2)
        self._lay.setSpacing(4)
        self._lay.addStretch(1)

        # Graph tab is always present
        self._add_tab(TAB_GRAPH, "⬡ Graph", closeable=False)

    def set_callbacks(self, on_switch, on_close):
        self._on_switch = on_switch
        self._on_close  = on_close

    def has_tab(self, tid): return tid in self._tabs
    def active_tab(self):   return self._active

    def open_tab(self, tid, label):
        if tid not in self._tabs:
            self._add_tab(tid, label, closeable=True)
        self._set_active(tid)

    def close_tab(self, tid):
        if tid == TAB_GRAPH or tid not in self._tabs:
            return
        info = self._tabs.pop(tid)
        info["btn"].deleteLater()
        if info["close_btn"]:
            info["close_btn"].deleteLater()
        if self._active == tid:
            self._set_active(TAB_GRAPH)
        if self._on_close:
            self._on_close(tid)

    def _add_tab(self, tid, label, closeable):
        # wrapper so tab + close button sit together
        wrap = QWidget(self)
        wrap.setAttribute(Qt.WA_TranslucentBackground)
        wl = QHBoxLayout(wrap)
        wl.setContentsMargins(0, 0, 0, 0)
        wl.setSpacing(0)

        btn = QPushButton(label, wrap)
        btn.setFixedHeight(TAB_H - 4)
        btn.setCursor(Qt.PointingHandCursor)
        btn.clicked.connect(lambda _, t=tid: self._set_active(t))
        wl.addWidget(btn)

        close_btn = None
        if closeable:
            close_btn = QPushButton("×", wrap)
            close_btn.setFixedSize(18, TAB_H - 4)
            close_btn.setCursor(Qt.PointingHandCursor)
            close_btn.setStyleSheet("""
                QPushButton{color:#e06c75;background:transparent;
                    border:none;font:bold 11pt 'JetBrains Mono';}
                QPushButton:hover{color:#fff;background:rgba(224,108,117,180);
                    border-radius:3px;}""")
            close_btn.clicked.connect(lambda _, t=tid: self.close_tab(t))
            wl.addWidget(close_btn)

        # insert before the trailing stretch
        self._lay.insertWidget(self._lay.count() - 1, wrap)
        self._tabs[tid] = {"label": label, "btn": btn,
                           "close_btn": close_btn, "wrap": wrap}
        self._refresh_styles()

    def _set_active(self, tid):
        if tid not in self._tabs:
            return
        self._active = tid
        self._refresh_styles()
        if self._on_switch:
            self._on_switch(tid)

    def _refresh_styles(self):
        for tid, info in self._tabs.items():
            active = (tid == self._active)
            ac = THEME["tab_act"]
            ic = THEME["tab_inact"]
            if active:
                info["btn"].setStyleSheet(f"""
                    QPushButton{{color:#1e2228;
                        background:rgb({ac.red()},{ac.green()},{ac.blue()});
                        border:none;border-radius:4px;
                        font:bold 9pt 'JetBrains Mono';padding:2px 10px;}}""")
            else:
                info["btn"].setStyleSheet(f"""
                    QPushButton{{color:#abb2bf;
                        background:rgba({ic.red()},{ic.green()},{ic.blue()},160);
                        border:1px solid rgba({ic.red()},{ic.green()},{ic.blue()},120);
                        border-radius:4px;font:9pt 'JetBrains Mono';padding:2px 10px;}}
                    QPushButton:hover{{background:rgba({ac.red()},{ac.green()},{ac.blue()},140);
                        color:#1e2228;}}""")

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), THEME["bg2"])
        p.setPen(QPen(THEME["dim"], 1))
        p.drawLine(0, self.height()-1, self.width(), self.height()-1)
        p.end()


# ═════════════════════════════════════════════════════════════════════════════
#  _ClockWidget
# ═════════════════════════════════════════════════════════════════════════════
class _ClockWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)
        QTimer(self, timeout=self.update, interval=1000).start()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        now = datetime.now()
        cf = QFont("JetBrains Mono", 34, QFont.Bold)
        p.setFont(cf); p.setPen(THEME["cyan"])
        ts = now.strftime("%H:%M:%S")
        p.drawText((w - QFontMetrics(cf).horizontalAdvance(ts))//2, 56, ts)
        df = QFont("JetBrains Mono", 11)
        p.setFont(df); p.setPen(THEME["green"])
        ds = now.strftime("%a  %d %b %Y")
        p.drawText((w - QFontMetrics(df).horizontalAdvance(ds))//2, 76, ds)
        p.setPen(QPen(THEME["dim"], 1))
        p.drawLine(16, 88, w-16, 88)
        p.end()


# ═════════════════════════════════════════════════════════════════════════════
#  MainPanel
# ═════════════════════════════════════════════════════════════════════════════
class MainPanel(QWidget):
    """Left + center columns: clock → tab bar → stacked content."""

    # Maps tab_id → (stack_index, page_widget)
    _TAB_LABELS = {
        TAB_BROWSER: "🌐 Browser",
        TAB_FM:      "📁 SwordFM",
        TAB_TERM:    "⬛ Terminal",
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self._deck_wid = None

        vlay = QVBoxLayout(self)
        vlay.setContentsMargins(0, 0, 0, 0)
        vlay.setSpacing(0)

        # clock
        self._clock = _ClockWidget(self)
        self._clock.setFixedHeight(CLOCK_H)
        vlay.addWidget(self._clock)

        # tab bar
        self._tabbar = TabBar(self)
        self._tabbar.set_callbacks(self._on_tab_switch, self._on_tab_close)
        vlay.addWidget(self._tabbar)

        # separator line
        sep = QFrame(self)
        sep.setFrameShape(QFrame.HLine)
        sep.setStyleSheet(f"color: rgba(62,68,81,180);")
        sep.setFixedHeight(1)
        vlay.addWidget(sep)

        # stacked area
        self._stack = QStackedWidget(self)
        vlay.addWidget(self._stack, 1)

        # Page 0 — always present: Graph
        self._page_graph = GraphPage(self._stack)
        self._stack.addWidget(self._page_graph)

        # Pages for closeable tabs are created lazily
        self._pages: dict[str, tuple[int, QWidget]] = {
            TAB_GRAPH: (0, self._page_graph),
        }

        self._stack.setCurrentIndex(0)

    # ── tab open button row (Edit Graph shown only on graph tab) ─────────────
    # Edit-graph is wired separately as a right-click / context on the tab bar
    # but we expose it as a method for cyberdesk.sh binding too.
    def edit_graph(self):
        script = os.path.join(HERE, "graph-edit.sh")
        subprocess.Popen([script], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)

    # ── external open-tab API (called from right panel or keyboard) ───────────
    def open_tab(self, tid: str):
        label = self._TAB_LABELS.get(tid, tid)
        if tid not in self._pages:
            page = self._make_page(tid)
            idx  = self._stack.addWidget(page)
            self._pages[tid] = (idx, page)
        self._tabbar.open_tab(tid, label)
        # switching is handled by _on_tab_switch callback

    def _make_page(self, tid) -> QWidget:
        if tid == TAB_BROWSER:
            return BrowserPage(self._stack)
        if tid == TAB_FM:
            cmd = self._resolve_swordfm()
            return EmbedPage(cmd, search_args=["--classname","swordfm"],
                             parent=self._stack)
        if tid == TAB_TERM:
            cmd, sa = self._resolve_terminal()
            return EmbedPage(cmd, search_args=sa, parent=self._stack)
        return QWidget(self._stack)

    # ── tab callbacks ─────────────────────────────────────────────────────────
    def _on_tab_switch(self, tid: str):
        if tid not in self._pages:
            return
        idx, _ = self._pages[tid]
        self._stack.setCurrentIndex(idx)
        if tid != TAB_GRAPH:
            QTimer.singleShot(150, self._grab_x11_focus)

    def _on_tab_close(self, tid: str):
        """Kill the process and free the page."""
        if tid not in self._pages:
            return
        _, page = self._pages.pop(tid)
        if isinstance(page, EmbedPage):
            page.terminate()
        page.deleteLater()
        # stack index of remaining pages must be rebuilt
        self._rebuild_stack_indices()

    def _rebuild_stack_indices(self):
        """Re-sync self._pages indices after a page is removed."""
        for tid, (_, page) in list(self._pages.items()):
            new_idx = self._stack.indexOf(page)
            if new_idx >= 0:
                self._pages[tid] = (new_idx, page)

    # ── X11 focus ─────────────────────────────────────────────────────────────
    def set_deck_wid(self, wid: int):
        self._deck_wid = wid

    def _grab_x11_focus(self):
        if not self._deck_wid:
            return
        try:
            import Xlib.display, Xlib.X
            d = Xlib.display.Display()
            w = d.create_resource_object('window', self._deck_wid)
            d.set_input_focus(w, Xlib.X.RevertToParent, Xlib.X.CurrentTime)
            d.sync(); d.close()
        except Exception:
            pass

    # ── static helpers ────────────────────────────────────────────────────────
    @staticmethod
    def _resolve_swordfm():
        for c in [os.path.join(HERE,"swordfm"),
                  os.path.expanduser("~/.local/bin/swordfm"), "swordfm"]:
            if os.path.isfile(c) or shutil.which(c):
                return [c]
        return ["xterm","-T","swordfm"]

    @staticmethod
    def _resolve_terminal():
        if shutil.which("ghostty"):
            return ["ghostty","--class=deck-terminal"], ["--classname","deck-terminal"]
        if shutil.which("alacritty"):
            return ["alacritty","--title","deck-terminal"], ["--name","deck-terminal"]
        if shutil.which("xfce4-terminal"):
            return ["xfce4-terminal","--title=deck-terminal"], ["--name","deck-terminal"]
        return ["xterm","-T","deck-terminal"], ["--name","deck-terminal"]

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), THEME["bg"])
        p.end()
