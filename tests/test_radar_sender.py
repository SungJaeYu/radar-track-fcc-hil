"""Run with: python -m unittest discover -s tests -p test_radar_sender.py"""
import contextlib
import io
import sys
import unittest
from unittest.mock import MagicMock, patch

from tools.hil import radar_sender as sender


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
            sender.build_radar_frame(0, 1000, 30),
            sender.build_radar_frame(100, 998, 30),
        ])
        sleep.assert_called_once_with(0.1)

    def test_big_endian_signed_azimuth_and_checksum(self):
        self.assertEqual(
            sender.build_radar_frame(100, 1000, -30),
            bytes.fromhex('7e 08 02 00 00 00 64 03 e8 ff e2 90'),
        )

    def test_invalid_settings(self):
        for option, value in [('--hz', '0'), ('--hz', 'nan'),
                              ('--count', '0'), ('--noise', '-1'),
                              ('--azimuth', '181')]:
            with self.subTest(option=option, value=value), \
                    patch.object(sys, 'argv', ['sender', option, value]), \
                    contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as error:
                sender.main()
            self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
