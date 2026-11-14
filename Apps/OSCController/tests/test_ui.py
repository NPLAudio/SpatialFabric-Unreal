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
        self.assertIsNone(self.app.receiver.bound)
        self.app.vars['front'].set('0.75'); self.app.update()
        self.assertEqual(self.app.tx, 0)
        self.app.cue_send('play', 1.0)
        self.assertEqual(self.receive().address, '/demo/playback/mono/play')
        self.app.stop_all()
        messages = [self.receive() for _ in range(3)]
        self.assertTrue(all(m.address.endswith('/stop') and m.args == () for m in messages))
        self.assertFalse(self.app.live.get())

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
        self.app.receiver._put(('message', time.monotonic(), ('127.0.0.1',9000), Message('/demo/playback/mono/state', (('f',1.0),))))
        self.app.receiver._put(('message', time.monotonic(), ('127.0.0.1',9000), Message('/not-configured/state', (('f',1.0),))))
        until = time.monotonic() + 1
        while time.monotonic() < until and not self.app.cue_states:
            self.app.update(); time.sleep(0.01)
        self.assertIn('PLAYING', self.app.cues.set('0','state'))
        self.assertEqual(set(self.app.cue_states), {'/demo/playback/mono'})
        self.app.vars['compose_port'].set(self.app.vars['playback_port'].get())
        self.app.vars['compose_address'].set('/custom/test')
        self.app.arguments.insert('1.0', 's Hello world\ni 2')
        self.app.send_composed()
        self.assertEqual(self.receive().args, (('s','Hello world'),('i',2)))


if __name__ == '__main__': unittest.main()
