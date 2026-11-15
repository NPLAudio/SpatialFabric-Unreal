"""Opt-in interactive desktop tests; all transmitted packets go to ephemeral loopback sockets."""
import os
import socket
import time
import unittest
from osc_controller.protocol import decode


@unittest.skipUnless(os.environ.get('OSC_CONTROLLER_GUI_TESTS') == '1', 'Set OSC_CONTROLLER_GUI_TESTS=1 on a desktop to test Tk')
class GuiTests(unittest.TestCase):
    def setUp(self):
        from osc_controller.ui import Controller
        self.app = Controller(); self.app.update()
        self.capture = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.capture.bind(('127.0.0.1', 0)); self.capture.settimeout(1)
        port = str(self.capture.getsockname()[1])
        self.app.vars['playback_port'].set(port); self.app.vars['spatial_port'].set(port)

    def tearDown(self):
        self.app.close(); self.capture.close()

    def receive(self):
        return decode(self.capture.recvfrom(65507)[0])[0]

    def test_no_network_on_open_or_edit_then_explicit_transport(self):
        self.assertEqual(self.app.tx, 0); self.assertIsNone(self.app.sender.socket)
        self.assertEqual(self.app.receivers.endpoints(), [])
        self.app.vars['front'].set('0.75'); self.app.update()
        self.assertEqual(self.app.tx, 0)
        self.app.cue_send('play', 1.0)
        self.assertEqual(self.receive().address, '/demo/playback/mono/play')
        self.app.stop_all()
        messages = [self.receive() for _ in range(3)]
        self.assertTrue(all(m.address.endswith('/stop') and m.args == () for m in messages))
        self.assertFalse(self.app.live.get())

    def test_scene_query_and_confirmed_cue_import_never_trigger_audio(self):
        import json
        from unittest.mock import patch
        from osc_controller.protocol import Message, encode
        from osc_controller.scene_query import SCHEMA, RESPONSE, QUERY
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', 0)); reply_port = probe.getsockname()[1]
        self.app.tracker.query_port.set(str(self.capture.getsockname()[1]))
        self.app.tracker.reply_port.set(str(reply_port)); self.app.query_scene()
        request = self.receive(); self.assertEqual(request.address, QUERY)
        rid = request.args[0][1]
        record = {'kind':'playback','key':'/actor/cue','actor_label':'Discovered cue','name':'AudioLoom','osc_base':'/new/discovered'}
        data = {'schema':SCHEMA,'schema_version':1,'world':'/Game/Test','offset':0,'limit':64,'returned':1,'total':1,'next_offset':-1,'objects':[record],'playback_network':{'bound_port':self.capture.getsockname()[1]}}
        text = json.dumps(data)
        self.capture.sendto(encode(Message(RESPONSE,(('s',rid),('i',0),('i',1),('s',text)))),('127.0.0.1',reply_port))
        until = time.monotonic() + 2
        while self.app.tracker.scene is None and time.monotonic() < until:
            self.app.update(); time.sleep(.01)
        self.assertIsNotNone(self.app.tracker.scene)
        self.assertEqual(len(self.app.profile.cues), 3)  # Query itself never replaces configuration.
        count = self.app.tx
        with patch('osc_controller.ui.messagebox.askyesno', return_value=True): self.app.import_discovered_cues()
        self.assertEqual(self.app.profile.cues[0].base, '/new/discovered')
        self.assertEqual(self.app.tx, count)
        self.assertFalse(self.app.live.get())
        self.assertEqual(self.app.receivers.endpoints('scene'), [])

    def test_simultaneous_state_and_two_spatial_receivers(self):
        from osc_controller.protocol import Message, encode
        held = []
        try:
            for _ in range(3):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.bind(('127.0.0.1', 0)); held.append(sock)
            ports = [s.getsockname()[1] for s in held]
        finally:
            for sock in held: sock.close()
        self.app.vars['monitor_port'].set(str(ports[0]))
        self.app.tracker.ports.set(','.join(str(p) for p in ports[1:]))
        self.app.start_all_receivers()
        self.assertEqual(len(self.app.receivers.endpoints()), 3)
        messages = [Message('/demo/playback/mono/state', (('f', 1.0),)),
                    Message('/adm/obj/1/xyz', (('f', .2), ('f', .3), ('f', .4))),
                    Message('/demo/main-stage/101/xyz', (('f', .3), ('f', .2), ('f', .4)))]
        for port, message in zip(ports, messages): self.capture.sendto(encode(message), ('127.0.0.1', port))
        until = time.monotonic() + 2
        while time.monotonic() < until and (len(self.app.tracker.model.objects) < 2 or not self.app.cue_states):
            self.app.update(); time.sleep(.01)
        self.assertEqual(len(self.app.tracker.model.objects), 2)
        self.assertIn('/demo/playback/mono', self.app.cue_states)
        self.assertEqual(self.app.tx, 0)
        self.app.tracker.master.select(self.app.tracker)
        self.app.update()
        self.app.tracker.refresh(time.monotonic() + 1)
        self.app.update()
        self.assertGreater(self.app.footer.winfo_height(), 25)
        self.assertLessEqual(self.app.footer.winfo_y() + self.app.footer.winfo_height(), self.app.winfo_height())
        views = self.app.tracker.canvases['Polar'].master
        for mode, canvas in self.app.tracker.canvases.items():
            views.select(canvas if canvas.master == views else canvas.master)
            self.app.update(); self.app.tracker.last_draw = 0
            self.app.tracker.refresh(time.monotonic())
            self.assertTrue(canvas.find_all(), mode + ' rendered')
        self.app.stop_monitor()
        self.assertEqual(len(self.app.receivers.endpoints()), 2)

    def test_spatial_send_has_consistent_wire_point_and_center_disarms(self):
        self.app.vars['front'].set('0.8'); self.app.vars['right'].set('0.6')
        self.app.send_spatial()
        messages = [self.receive() for _ in range(5)]
        xyz = [v for _,v in messages[0].args]
        self.assertAlmostEqual(xyz[0], 0.6, places=5); self.assertAlmostEqual(xyz[1], 0.8, places=5)
        self.assertEqual(messages[1].address, '/adm/obj/1/aed')
        self.app.live.set(True); self.app.center_pad()
        self.assertFalse(self.app.live.get()); self.assertEqual(self.app.tx, 5)

    def test_monitor_state_and_composer_literal(self):
        from osc_controller.protocol import Message
        self.app._incoming(time.monotonic(), ('127.0.0.1',9000), Message('/demo/playback/mono/state', (('f',1.0),)), 9001)
        self.app._incoming(time.monotonic(), ('127.0.0.1',9000), Message('/not-configured/state', (('f',1.0),)), 9001)
        until = time.monotonic() + 1
        while time.monotonic() < until and 'PLAYING' not in self.app.cues.set('0', 'state'):
            self.app.update(); time.sleep(0.01)
        self.assertIn('PLAYING', self.app.cues.set('0','state'))
        self.assertEqual(set(self.app.cue_states), {'/demo/playback/mono'})
        self.app.vars['compose_port'].set(self.app.vars['playback_port'].get())
        self.app.vars['compose_address'].set('/custom/test')
        self.app.arguments.insert('1.0', 's Hello world\ni 2')
        self.app.send_composed()
        self.assertEqual(self.receive().args, (('s','Hello world'),('i',2)))


if __name__ == '__main__': unittest.main()
