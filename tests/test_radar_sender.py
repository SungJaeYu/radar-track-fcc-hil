"""Run with: python -m unittest discover -s tests -p test_radar_sender.py"""
import contextlib
import io
import sys
import unittest
from unittest.mock import MagicMock, patch

from pc_sim import radar_sender as sender


class RadarSenderTest(unittest.TestCase):
    def test_serial_stream_and_interval(self):
        serial = MagicMock()
        argv = ['sender', '--port', 'test', '--count', '2', '--noise', '0']
        with patch.dict(sys.modules, serial=serial), \
                patch('time.sleep') as sleep, \
                patch.object(sys, 'argv', argv), \
                contextlib.redirect_stdout(io.StringIO()):
            sender.main()
        writes = serial.Serial.return_value.__enter__.return_value.write.call_args_list
        self.assertEqual([call.args[0] for call in writes], [
            bytes.fromhex("7e 01 01 00 c8 9d"),
            sender.build_radar_frame(0, 1000, 178956971),
            sender.build_radar_frame(100, 998, 178956971),
        ])
        sleep.assert_called_once_with(0.1)

    def test_big_endian_signed_azimuth_and_checksum(self):
        self.assertEqual(
            sender.build_radar_frame(100, 1000, -1073741824),
            bytes.fromhex('7e 0a 02 00 00 00 64 03 e8 c0 00 00 00 0f b1'),
        )

    def test_semicircle_boundaries(self):
        for value, encoded in [(-2147483648, '80000000'),
                               (0, '00000000'),
                               (2147483647, '7fffffff')]:
            with self.subTest(value=value):
                frame = sender.build_radar_frame(0, 1000, value)
                self.assertEqual(frame[1], 10)
                self.assertEqual(frame[9:13], bytes.fromhex(encoded))

    def test_cli_accepts_semicircle(self):
        with patch.object(sys, 'argv', ['sender', '--count', '1',
                                       '--noise', '0', '--azimuth', '-1073741824']), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            sender.main()
        self.assertEqual(output.getvalue().splitlines()[0], 'INIT frame=7e 01 01 00 c8 9d')
        self.assertIn('c0 00 00 00', output.getvalue())

    def test_invalid_settings(self):
        for option, value in [('--hz', '0'), ('--hz', 'nan'),
                              ('--count', '0'), ('--noise', '-1'),
                              ('--azimuth', '2147483648'),
                              ('--azimuth', '-2147483649')]:
            with self.subTest(option=option, value=value), \
                    patch.object(sys, 'argv', ['sender', option, value]), \
                    contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as error:
                sender.main()
            self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
