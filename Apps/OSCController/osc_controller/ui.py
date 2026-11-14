"""Standalone Tk desktop controller. UI thread owns all widgets; receiver uses a bounded queue."""
from collections import deque
import csv
import json
import math
from pathlib import Path
import re
import sys
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog
from .model import Cue, Profile, endpoint, load_profile, save_profile
from .protocol import Message, parse_arguments, spatial_messages
from .transport import Sender, Receiver

BG, PANEL, TEXT, MUTED, CYAN, PURPLE, ORANGE = '#0b1426', '#14243c', '#eef5ff', '#a9c1d9', '#54d8f5', '#bf9aff', '#ffbf69'


class Controller(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('SpatialFabric OSC Controller — standalone')
        width, height = min(1240, self.winfo_screenwidth()-60), min(820, self.winfo_screenheight()-90)
        self.geometry(f'{width}x{height}'); self.minsize(980, 640); self.configure(bg=BG)
        self.sender, self.receiver = Sender(), Receiver()
        self.profile = Profile(); self.profile_path = None
        self.records = deque(maxlen=1000); self.cue_states = {}; self.ghosts = {}
        self.vars = {}; self.live = tk.BooleanVar(value=False); self.mute = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value='Ready. No packets sent. Monitor is stopped.')
        self.monitor_status = tk.StringVar(value='Stopped — no socket bound')
        self.spatial_dirty = False; self.last_spatial = 0.0; self.last_pad_redraw = 0.0
        self.tx = self.rx = self.rejected = 0
        self._style(); self._build(); self._apply_profile(self.profile)
        self.protocol('WM_DELETE_WINDOW', self.close)
        self.bind('<Control-Shift-S>', lambda event: self.stop_all())
        self.pump_token = self.after(50, self._pump)

    def _style(self):
        style = ttk.Style(self); style.theme_use('clam')
        self.option_add('*Font', ('Segoe UI', 10))
        style.configure('.', background=BG, foreground=TEXT, font=('Segoe UI', 10))
        style.configure('TFrame', background=BG)
        style.configure('TLabelframe', background=BG, bordercolor='#304b69')
        style.configure('TLabelframe.Label', foreground=CYAN, font=('Segoe UI', 10, 'bold'))
        style.configure('TButton', background=PANEL, foreground=TEXT, padding=(12, 7), borderwidth=1)
        style.map('TButton', background=[('active', '#24405f')])
        style.configure('Stop.TButton', foreground=ORANGE, font=('Segoe UI', 10, 'bold'))
        style.configure('TEntry', fieldbackground=PANEL, foreground=TEXT, insertcolor=TEXT)
        style.configure('TCombobox', fieldbackground=PANEL, foreground=TEXT)
        style.configure('TNotebook', background=BG, borderwidth=0)
        style.configure('TNotebook.Tab', background=PANEL, padding=(20, 10))
        style.map('TNotebook.Tab', background=[('selected', '#24405f')], foreground=[('selected', CYAN)])
        style.configure('Treeview', background=PANEL, fieldbackground=PANEL, foreground=TEXT, rowheight=29, borderwidth=0)
        style.configure('Treeview.Heading', background='#24405f', foreground=TEXT, padding=6)
        style.map('Treeview', background=[('selected', '#305778')])
        style.configure('Muted.TLabel', foreground=MUTED)

    def _label(self, parent, text, **kw):
        return ttk.Label(parent, text=text, **kw)

    def _field(self, parent, label, name, row, width=18):
        self._label(parent, label).grid(row=row, column=0, sticky='w', padx=8, pady=5)
        var = self.vars.setdefault(name, tk.StringVar())
        ttk.Entry(parent, textvariable=var, width=width).grid(row=row, column=1, sticky='ew', padx=8, pady=5)
        return var

    def _build(self):
        top = ttk.Frame(self, padding=(18, 14)); top.pack(fill='x')
        try:
            asset = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1])) / 'assets/mark.png'
            image = tk.PhotoImage(file=str(asset)); self.logo = image.subsample(max(1, image.width() // 48))
            self.iconphoto(True, self.logo)
            ttk.Label(top, image=self.logo).pack(side='left', padx=(0, 12))
        except (tk.TclError, OSError): pass
        title = ttk.Frame(top); title.pack(side='left')
        ttk.Label(title, text='SPATIALFABRIC  /  OSC CONTROLLER', font=('Segoe UI', 17, 'bold'), foreground=CYAN).pack(anchor='w')
        self._label(title, 'Independent control app · control data, not an audio player', style='Muted.TLabel').pack(anchor='w')
        ttk.Button(top, text='STOP ALL CONFIGURED CUES', style='Stop.TButton', command=self.stop_all).pack(side='right', padx=8)
        ttk.Button(top, text='Save profile…', command=self.save).pack(side='right', padx=4)
        ttk.Button(top, text='Load profile…', command=self.load).pack(side='right', padx=4)
        endpoints = ttk.Frame(self, padding=(18, 0)); endpoints.pack(fill='x')
        self._label(endpoints, 'Playback destination').pack(side='left')
        for name, width in [('playback_ip', 16), ('playback_port', 7)]:
            self.vars[name] = tk.StringVar(); ttk.Entry(endpoints, textvariable=self.vars[name], width=width).pack(side='left', padx=5)
        self._label(endpoints, 'Unreal: Start OSC in Playback first. UDP send ≠ confirmed delivery.', style='Muted.TLabel').pack(side='left', padx=12)
        notebook = ttk.Notebook(self); notebook.pack(fill='both', expand=True, padx=18, pady=12)
        cue_tab, spatial_tab, composer_tab, monitor_tab = [ttk.Frame(notebook, padding=14) for _ in range(4)]
        for frame, label in [(cue_tab, 'Cue transport'), (spatial_tab, 'Spatial / renderer'), (composer_tab, 'Message composer'), (monitor_tab, 'Monitor / log')]: notebook.add(frame, text=label)
        self._cue_tab(cue_tab); self._spatial_tab(spatial_tab); self._composer_tab(composer_tab); self._monitor_tab(monitor_tab)
        footer = ttk.Frame(self); footer.pack(fill='x')
        ttk.Label(footer, textvariable=self.status, foreground=MUTED, padding=(18, 7), anchor='w').pack(side='left', fill='x', expand=True)
        ttk.Button(footer, text='About / licenses', command=self.about).pack(side='right', padx=18, pady=(0, 6))

    def _cue_tab(self, frame):
        self._label(frame, 'Select a cue, then send a transport command. No automatic playback on launch or profile load.', style='Muted.TLabel').pack(anchor='w', pady=(0, 12))
        self.cues = ttk.Treeview(frame, columns=('name', 'base', 'state'), show='headings', selectmode='browse', height=9)
        for key, label, width in [('name', 'Cue', 310), ('base', 'OSC base', 320), ('state', 'Last reported worker state', 260)]:
            self.cues.heading(key, text=label); self.cues.column(key, width=width)
        self.cues.pack(fill='both', expand=True)
        buttons = ttk.Frame(frame); buttons.pack(fill='x', pady=12)
        for label, op, value, style in [('▶  Play', 'play', 1.0, 'TButton'), ('■  Stop', 'stop', None, 'Stop.TButton'), ('Loop ON', 'loop', 1.0, 'TButton'), ('Loop OFF / drain', 'loop', 0.0, 'TButton')]:
            ttk.Button(buttons, text=label, style=style, command=lambda o=op, v=value: self.cue_send(o, v)).pack(side='left', padx=(0, 8))
        for label, action in [('Add cue…', self.add_cue), ('Edit…', self.edit_cue), ('Remove', self.remove_cue)]:
            ttk.Button(buttons, text=label, command=action).pack(side='right', padx=4)
        box = ttk.LabelFrame(frame, text='State is evidence, not an audio-level meter', padding=12); box.pack(fill='x')
        self._label(box, 'Start the monitor on port 9001 to receive /state. “Playing” is the last reported worker event,\nnot proof of sound at a loudspeaker. There is no heartbeat; elapsed age is shown, not a fake connection light.', style='Muted.TLabel').pack(anchor='w')
        self._label(frame, 'Stop All sends /stop once to each base in this list. It is not a safety-rated or acknowledged emergency mute.\nKeyboard: Ctrl+Shift+S. Closing this app does not stop remote audio. Keep an independent mute.', foreground=ORANGE).pack(anchor='w', pady=12)

    def _spatial_tab(self, frame):
        self._label(frame, 'DIRECT RENDERER CONTROL — these messages do not move Unreal actors or spatialize AudioLoom PCM.', foreground=ORANGE).pack(anchor='w', pady=(0, 10))
        body = ttk.Frame(frame); body.pack(fill='both', expand=True)
        left = ttk.Frame(body); left.pack(side='left', fill='both', expand=True)
        self.pad = tk.Canvas(left, background=PANEL, highlightthickness=1, highlightbackground='#304b69', width=560, height=390)
        self.pad.pack(fill='both', expand=True)
        self.pad.bind('<Configure>', lambda event: self._draw_pad())
        self.pad.bind('<Button-1>', self._drag_pad); self.pad.bind('<B1-Motion>', self._drag_pad)
        self._label(left, 'Drag: front/right. Purple outlines: received ADM XYZ objects (if monitoring their port).\nUnit-sphere limiting is applied coherently to outgoing XYZ and AED.', style='Muted.TLabel').pack(anchor='w', pady=8)
        controls = ttk.LabelFrame(body, text='ADM-OSC patch', padding=10); controls.pack(side='right', fill='y', padx=(16, 0))
        for row, (label, name) in enumerate([('Destination IP', 'spatial_ip'), ('Port', 'spatial_port'), ('Object ID', 'object_id'), ('Front / +X', 'front'), ('Right / +Y', 'right'), ('Up / +Z', 'up'), ('Gain (linear)', 'gain'), ('Width (0..1)', 'width'), ('Maximum Hz', 'send_hz')]): self._field(controls, label, name, row)
        for name in ('front', 'right', 'up', 'gain', 'width'):
            self.vars[name].trace_add('write', lambda *args: self._changed())
        ttk.Checkbutton(controls, text='Renderer mute metadata', variable=self.mute, command=self._changed).grid(row=9, column=0, columnspan=2, sticky='w', padx=8, pady=5)
        ttk.Button(controls, text='Send once', command=self.send_spatial).grid(row=10, column=0, columnspan=2, sticky='ew', padx=8, pady=6)
        ttk.Checkbutton(controls, text='Enable live changes', variable=self.live, command=self._changed).grid(row=11, column=0, columnspan=2, sticky='w', padx=8, pady=5)
        ttk.Button(controls, text='Center pad (no auto-send)', command=self.center_pad).grid(row=12, column=0, columnspan=2, sticky='ew', padx=8, pady=6)

    def _composer_tab(self, frame):
        self._label(frame, 'Send one custom OSC datagram. Destination is independent of the playback and spatial patches.', style='Muted.TLabel').pack(anchor='w', pady=(0, 14))
        patch = ttk.Frame(frame); patch.pack(anchor='nw')
        for row, label, name, value in [(0, 'IP', 'compose_ip', '127.0.0.1'), (1, 'Port', 'compose_port', '9000'), (2, 'OSC address', 'compose_address', '/demo/playback/mono/stop')]:
            self._field(patch, label, name, row, 50).set(value)
        self._label(frame, 'Arguments: one per line. Leave empty for an address-only message.').pack(anchor='w', pady=(16, 5))
        self.arguments = tk.Text(frame, height=8, bg=PANEL, fg=TEXT, insertbackground=TEXT, font=('Consolas', 12), relief='flat')
        self.arguments.pack(fill='both', expand=True)
        row = ttk.Frame(frame); row.pack(fill='x', pady=12)
        ttk.Button(row, text='Send message', command=self.send_composed).pack(side='left')
        self._label(row, 'Examples:  f 0.5    i 1    s Hello world    T    F    N\nStrings are literal; no evaluation, templating or shell commands.', style='Muted.TLabel').pack(side='left', padx=18)

    def _monitor_tab(self, frame):
        controls = ttk.Frame(frame); controls.pack(fill='x')
        self._label(controls, 'Listen IP').pack(side='left')
        for name, width in [('monitor_ip', 17), ('monitor_port', 8)]:
            self.vars[name] = tk.StringVar(); ttk.Entry(controls, textvariable=self.vars[name], width=width).pack(side='left', padx=6)
        ttk.Button(controls, text='Start monitor', command=self.start_monitor).pack(side='left', padx=6)
        ttk.Button(controls, text='Stop monitor', command=self.stop_monitor).pack(side='left', padx=6)
        ttk.Button(controls, text='Export log…', command=self.export_log).pack(side='right')
        ttk.Button(controls, text='Clear', command=self.clear_log).pack(side='right', padx=6)
        ttk.Label(frame, textvariable=self.monitor_status, foreground=CYAN).pack(anchor='w', pady=10)
        self._label(frame, '9001: playback state · 50018: ADM positions · 9002: example Custom output.\nOne listener per endpoint. Stop competing receivers first; this app never uses address reuse.', style='Muted.TLabel').pack(anchor='w', pady=(0, 10))
        self.log = ttk.Treeview(frame, columns=('time', 'dir', 'peer', 'address', 'args'), show='headings', height=12)
        for key, label, width in [('time', 'Time', 85), ('dir', 'Type', 55), ('peer', 'Endpoint', 150), ('address', 'OSC address / message', 300), ('args', 'Typed arguments', 360)]:
            self.log.heading(key, text=label); self.log.column(key, width=width)
        self.log.tag_configure('TX', foreground=CYAN); self.log.tag_configure('RX', foreground=PURPLE); self.log.tag_configure('ERR', foreground=ORANGE)
        scroll = ttk.Scrollbar(frame, orient='vertical', command=self.log.yview); self.log.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y'); self.log.pack(fill='both', expand=True)

    def _current_profile(self):
        data = {'schema_version': 1, 'cues': [{'name': c.name, 'base': c.base} for c in self.profile.cues]}
        for name in ('playback_ip', 'monitor_ip', 'spatial_ip'): data[name] = self.vars[name].get().strip()
        for name in ('playback_port', 'monitor_port', 'spatial_port', 'object_id', 'send_hz'):
            try: data[name] = int(self.vars[name].get())
            except ValueError: raise ValueError(name + ' must be an integer') from None
        for name in ('front', 'right', 'up', 'gain', 'width'):
            try: data[name] = float(self.vars[name].get())
            except ValueError: raise ValueError(name + ' must be numeric') from None
        return Profile.from_dict(data)

    def _apply_profile(self, profile):
        profile.validate(); self.live.set(False); self.mute.set(False)
        self.profile = profile; self.cue_states.clear(); self.ghosts.clear()
        for name in profile.__dataclass_fields__:
            if name in self.vars: self.vars[name].set(str(getattr(profile, name)))
        self._refresh_cues(); self.spatial_dirty = False; self._draw_pad()

    def _refresh_cues(self):
        self.cues.delete(*self.cues.get_children())
        for i, cue in enumerate(self.profile.cues): self.cues.insert('', 'end', iid=str(i), values=(cue.name, cue.base, 'Unknown — no state received'))
        if self.profile.cues: self.cues.selection_set('0')

    def _selected(self):
        selected = self.cues.selection()
        if not selected: raise ValueError('Select a cue first')
        return int(selected[0])

    def _record(self, kind, peer, address, args=''):
        record = (time.strftime('%H:%M:%S'), kind, peer, address, str(args)[:600])
        self.records.append(record); self.log.insert('', 'end', values=record, tags=(kind,))
        rows = self.log.get_children()
        if len(rows) > 1000: self.log.delete(*rows[:len(rows)-1000])
        self.log.yview_moveto(1)

    def _error(self, error):
        self.status.set(str(error)); self._record('ERR', '', str(error))

    def _send(self, ip, port, message, quiet=False):
        self.sender.send(ip, port, message); self.tx += 1
        if not quiet: self._record('TX', f'{ip}:{port}', message.address, self._args(message))
        self.status.set(f'UDP send accepted by socket · TX {self.tx} / RX {self.rx} · Delivery/audio not acknowledged')

    @staticmethod
    def _args(message):
        return '  '.join(tag + ':' + (f'<{len(v)} bytes>' if isinstance(v, bytes) else f'{v:.5g}' if isinstance(v, float) else str(v)) for tag, v in message.args)

    def cue_send(self, operation, value):
        try:
            cue = self.profile.cues[self._selected()]
            ip, port = endpoint(self.vars['playback_ip'].get().strip(), int(self.vars['playback_port'].get()))
            args = () if value is None else (('f', value),)
            self._send(ip, port, Message(cue.base + '/' + operation, args))
        except (ValueError, OSError) as error: self._error(error)

    def stop_all(self):
        self.live.set(False)
        try:
            ip, port = endpoint(self.vars['playback_ip'].get().strip(), int(self.vars['playback_port'].get()))
            for cue in self.profile.cues: self._send(ip, port, Message(cue.base + '/stop'))
            self.status.set('Stop sent to configured cues; live spatial disabled. UDP is not acknowledged.')
        except (ValueError, OSError) as error: self._error(error)

    def _cue_editor(self, cue=None):
        name = simpledialog.askstring('Cue name', 'Display name:', initialvalue=cue.name if cue else '', parent=self)
        if name is None: return None
        base = simpledialog.askstring('OSC base', 'Base only (no /play suffix):', initialvalue=cue.base if cue else '/demo/playback/new', parent=self)
        if base is None: return None
        result = Cue(name, base); result.validate(); return result

    def add_cue(self):
        try:
            cue = self._cue_editor()
            if cue:
                data = self._current_profile().to_dict(); data['cues'].append({'name': cue.name, 'base': cue.base})
                self._apply_profile(Profile.from_dict(data))
        except ValueError as error: self._error(error)

    def edit_cue(self):
        try:
            index = self._selected(); cue = self._cue_editor(self.profile.cues[index])
            if cue:
                data = self._current_profile().to_dict(); data['cues'][index] = {'name': cue.name, 'base': cue.base}
                self._apply_profile(Profile.from_dict(data))
        except ValueError as error: self._error(error)

    def remove_cue(self):
        try:
            if len(self.profile.cues) == 1: raise ValueError('Keep at least one configured cue')
            index = self._selected()
            if not messagebox.askyesno('Remove cue', 'Remove this entry? This does not stop remote playback; Stop first.', parent=self): return
            data = self._current_profile().to_dict(); del data['cues'][index]
            self._apply_profile(Profile.from_dict(data))
        except ValueError as error: self._error(error)

    def _changed(self):
        self.spatial_dirty = True
        if hasattr(self, 'pad'): self._draw_pad()

    def _drag_pad(self, event):
        width, height = self.pad.winfo_width(), self.pad.winfo_height()
        span = max(1, min(width, height) - 70); cx, cy = width/2, height/2
        right = max(-1, min(1, (event.x-cx) / (span/2)))
        front = max(-1, min(1, (cy-event.y) / (span/2)))
        self.vars['front'].set(f'{front:.4f}'); self.vars['right'].set(f'{right:.4f}')

    def center_pad(self):
        self.live.set(False)  # Centering must never silently fire at a renderer.
        self.vars['front'].set('0.0'); self.vars['right'].set('0.0'); self.vars['up'].set('0.0')
        self.status.set('Pad centered locally; live is disabled. Press Send once to transmit.')

    def _draw_pad(self):
        self.pad.delete('all'); w, h = max(100, self.pad.winfo_width()), max(100, self.pad.winfo_height())
        span = max(30, min(w, h)-70); cx, cy = w/2, h/2; r = span/2
        self.pad.create_rectangle(cx-r, cy-r, cx+r, cy+r, outline='#365678')
        self.pad.create_oval(cx-r, cy-r, cx+r, cy+r, outline='#365678')
        self.pad.create_line(cx-r, cy, cx+r, cy, fill='#365678'); self.pad.create_line(cx, cy-r, cx, cy+r, fill='#365678')
        self.pad.create_text(cx, cy-r-17, text='FRONT  +X', fill=CYAN)
        self.pad.create_text(cx+r-30, cy+17, text='+Y RIGHT', fill=CYAN)
        for object_id, (front, right, received) in list(self.ghosts.items()):
            if time.monotonic()-received > 10: continue
            x, y = cx+max(-1,min(1,right))*r, cy-max(-1,min(1,front))*r
            self.pad.create_oval(x-8,y-8,x+8,y+8,outline=PURPLE,width=2); self.pad.create_text(x,y-16,text=str(object_id),fill=PURPLE)
        try:
            front, right = float(self.vars['front'].get()), float(self.vars['right'].get())
            up = float(self.vars['up'].get())
            if not all(math.isfinite(v) for v in (front, right, up)): return
            radius = max(1.0, math.sqrt(front*front + right*right + up*up))
            front, right = front/radius, right/radius
        except (ValueError, KeyError): return
        x,y = cx+max(-1,min(1,right))*r, cy-max(-1,min(1,front))*r
        self.pad.create_oval(x-7,y-7,x+7,y+7,fill=CYAN,outline=CYAN)
        self.pad.create_text(14,h-15,anchor='w',text='LIVE CHANGES ENABLED' if self.live.get() else 'LOCAL PREVIEW — not transmitting',fill=ORANGE if self.live.get() else MUTED)

    def send_spatial(self, quiet=False):
        try:
            p = self._current_profile()
            for message in spatial_messages(p.object_id,p.front,p.right,p.up,p.gain,p.width,self.mute.get()): self._send(p.spatial_ip,p.spatial_port,message,quiet)
            self.last_spatial = time.monotonic(); self.spatial_dirty = False
        except (ValueError, OSError) as error:
            self.live.set(False); self._error(error)

    def send_composed(self):
        try:
            ip, port = endpoint(self.vars['compose_ip'].get().strip(), int(self.vars['compose_port'].get()))
            message = Message(self.vars['compose_address'].get().strip(), parse_arguments(self.arguments.get('1.0', 'end')))
            self._send(ip, port, message)
        except (ValueError, OSError) as error: self._error(error)

    def start_monitor(self):
        try:
            ip, port = endpoint(self.vars['monitor_ip'].get().strip(), int(self.vars['monitor_port'].get()), listen=True)
            if ip == '0.0.0.0' and not messagebox.askyesno('All-interface listener', 'Expose this unauthenticated monitor on all IPv4 interfaces? Use only a trusted network.', parent=self): return
            bound = self.receiver.start(ip, port); self.cue_states.clear(); self.ghosts.clear()
            self.monitor_status.set(f'Listening on {bound[0]}:{bound[1]} — exact bound endpoint')
            self._record('INFO', f'{bound[0]}:{bound[1]}', 'Monitor started')
        except (ValueError, OSError) as error:
            self.monitor_status.set('Stopped — bind failed'); self._error(error)

    def stop_monitor(self):
        self.receiver.stop(); self.monitor_status.set('Stopped — no socket bound')
        self._record('INFO', '', 'Monitor stopped; displayed states are historical')

    def _pump(self):
        now = time.monotonic()
        configured_bases = {cue.base for cue in self.profile.cues}
        for kind, received, peer, payload in self.receiver.poll():
            if kind == 'error': self.rejected += 1; self._record('ERR', f'{peer[0]}:{peer[1]}', payload); continue
            self.rx += 1; self._record('RX', f'{peer[0]}:{peer[1]}', payload.address, self._args(payload))
            if payload.address.endswith('/state') and len(payload.args) == 1 and payload.args[0][0] in ('f','i'):
                value = payload.args[0][1]
                base = payload.address[:-6]
                if base in configured_bases and value in (0,1): self.cue_states[base] = (bool(value), received)
            match = re.fullmatch(r'/adm/obj/([1-9]\d{0,9})/xyz', payload.address)
            if match and len(payload.args) == 3 and all(tag in ('f','i','d') for tag,_ in payload.args):
                right, front, _ = [v for _,v in payload.args]; self.ghosts[match[1]] = (front,right,received)
                if len(self.ghosts) > 256: self.ghosts.pop(next(iter(self.ghosts)))
                self._draw_pad()
        if self.receiver.bound:
            ip, port = self.receiver.bound
            self.monitor_status.set(f'Listening on {ip}:{port} · RX {self.rx} · rejected {self.rejected} · queue drops {self.receiver.dropped}')
        if now - self.last_pad_redraw > 1:
            self._draw_pad(); self.last_pad_redraw = now
        for i,cue in enumerate(self.profile.cues):
            state = self.cue_states.get(cue.base)
            label = ('Last: ' + ('PLAYING' if state[0] else 'stopped') + f' · {now-state[1]:.0f}s ago') if state else 'Unknown — no state received'
            self.cues.set(str(i), 'state', label)
        if self.live.get() and self.spatial_dirty:
            try: hz = int(self.vars['send_hz'].get())
            except ValueError: hz = 20
            if now-self.last_spatial >= 1/max(1,min(60,hz)): self.send_spatial(quiet=True)
        self.pump_token = self.after(10, self._pump)  # GUI timers are best-effort; configured Hz is an upper bound.

    def load(self):
        path = filedialog.askopenfilename(parent=self, title='Load OSC profile', filetypes=[('JSON profiles','*.json')])
        if not path: return
        try:
            profile = load_profile(path)  # Validate before disturbing current state.
            self.stop_monitor(); self._apply_profile(profile); self.profile_path = path
            self.status.set('Profile loaded. Live disabled; monitor stopped; no packets sent.')
        except (ValueError, OSError) as error: self._error(error)

    def save(self):
        try: profile = self._current_profile()
        except ValueError as error: self._error(error); return
        path = filedialog.asksaveasfilename(parent=self, title='Save OSC profile', defaultextension='.json', initialfile='osc-profile.json', filetypes=[('JSON profiles','*.json')])
        if not path: return
        try: save_profile(path, profile); self.profile_path = path; self.status.set('Profile saved; no packets sent.')
        except (ValueError, OSError) as error: self._error(error)

    def clear_log(self):
        self.records.clear(); self.log.delete(*self.log.get_children())

    def export_log(self):
        path = filedialog.asksaveasfilename(parent=self, title='Export bounded log', defaultextension='.csv', initialfile='osc-log.csv', filetypes=[('CSV','*.csv')])
        if not path: return
        try:
            with open(path,'w',newline='',encoding='utf-8') as stream:
                writer=csv.writer(stream); writer.writerow(('time','type','endpoint','address','arguments')); writer.writerows(self.records)
            self.status.set('Exported last <=1000 log records.')
        except OSError as error: self._error(error)

    def about(self):
        window = tk.Toplevel(self); window.title('About SpatialFabric OSC Controller'); window.geometry('820x560')
        text = tk.Text(window, wrap='word', bg=PANEL, fg=TEXT, font=('Consolas', 10), padx=16, pady=16)
        scroll = ttk.Scrollbar(window, command=text.yview); text.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y'); text.pack(fill='both', expand=True)
        text.insert('end', 'SpatialFabric OSC Controller 1.0.0\nIndependent OSC control; not an audio player.\n\nUDP is unauthenticated and unacknowledged. Use trusted networks.\nClosing the controller does not stop remote audio.\n\n')
        root = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))
        paths = [root / 'LICENSE', root / 'THIRD_PARTY_NOTICES.md'] + sorted((root / 'runtime-licenses').glob('*'))
        for path in paths:
            if path.is_file():
                text.insert('end', '\n--- ' + path.name + ' ---\n\n' + path.read_text(encoding='utf-8', errors='replace') + '\n')
        text.configure(state='disabled')

    def close(self):
        # Exiting is not a remote stop: do not send hidden transport requests.
        self.live.set(False)
        self.after_cancel(self.pump_token)
        self.receiver.stop(); self.sender.close(); self.destroy()


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Independent SpatialFabric OSC controller; does not play audio itself.')
    parser.add_argument('--profile', type=Path, help='Load settings only; never auto-send or auto-listen')
    parser.add_argument('--smoke-test', type=Path, help='Write a no-network GUI startup report, then close (developer use)')
    args = parser.parse_args()
    app = Controller()
    if args.profile:
        try: app._apply_profile(load_profile(args.profile)); app.profile_path = str(args.profile)
        except (ValueError, OSError) as error: app._error(error)
    if args.smoke_test:
        def smoke():
            report = {'status':'passed','mapped':bool(app.winfo_ismapped()),'cue_count':len(app.profile.cues),
                      'live_enabled':app.live.get(),'listener_bound':app.receiver.bound,'tx':app.tx,'sender_open':app.sender.socket is not None}
            if not report['mapped'] or not 1 <= report['cue_count'] <= 64 or report['live_enabled'] or report['listener_bound'] or report['tx'] or report['sender_open']: report['status']='failed'
            args.smoke_test.write_text(json.dumps(report,indent=2),encoding='utf-8'); app.close()
        app.after(1000, smoke)
    app.mainloop()
