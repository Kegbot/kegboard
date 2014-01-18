#!/usr/bin/env python
#
# Copyright 2012 Mike Wakerly <opensource@hoho.com>
#
# This file is part of the Kegboard package of the Kegbot project.
# For more information on Kegboard or Kegbot, see http://kegbot.org/
#
# Kegboard is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# Kegboard is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Kegboard.  If not, see <http://www.gnu.org/licenses/>.

"""Kegboard tester program.

Currently just cycles relays on and off."""

import gflags
import serial
import time

from kegbot.util import app 
from kegbot.kegboard import kegboard

FLAGS = gflags.FLAGS

class KegboardMonitorApp(app.App):

  def _SetupSignalHandlers(self):
    # Don't install signal handlers, so we actually receive
    # KeyboardInterrupt.
    pass

  def _MainLoop(self):
    try:
      ping_message = kegboard.PingCommand()
      print 'Waiting for a kegboard ...'
      board = kegboard.wait_for_kegboard()
      print 'Found: %s' % board
      print 'Listening to board ...'

      board.open()
      try:
        while not self._do_quit:
          for relay in range(4):
            for mode in (1, 0):
              cmd = kegboard.SetOutputCommand()
              cmd.SetValue('output_id', relay)
              cmd.SetValue('output_mode', mode)
              self._logger.info('Sending relay command: %s' % cmd)
              board.write_message(cmd)
              for m in board.drain_messages():
                self._logger.info(m)
              time.sleep(1.0)

      except IOError, e:
        print 'Error, closing board: %s' % e
        print ''
      finally:
        board.close_quietly()
    except KeyboardInterrupt:
      return

if __name__ == '__main__':
  KegboardMonitorApp.BuildAndRun()
